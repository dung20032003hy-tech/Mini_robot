#include "voice_assistant.h"
#include "voice_dsp.h"
#include "voice_message.h"
#include "audio_input.h"
#include "audio_output.h"
#include "safety_manager.h"
#include "wifi_manager.h"
#include "robot_eyes.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RECORD_SAMPLES (16000 * 12)
#define REPLY_SAMPLES (24000 * 45)
#define MESSAGE_LIMIT (128 * 1024)
#define PRE_ROLL 3200

static voice_config_t active, pending;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static bool initialized, manual_busy, paused;
static unsigned generation, turns;
static const char *state = "DISABLED", *error_code = "";
static char answer[1537];
static int64_t retry_at;

typedef struct {
    esp_transport_handle_t tls, ws;
    char *message;
    voice_message_t incoming;
} connection_t;

static void set_state(const char *s, const char *error) {
    portENTER_CRITICAL(&mux);
    state = s;
    error_code = error ? error : "";
    portEXIT_CRITICAL(&mux);
}
void voice_assistant_control_busy(bool busy) {
    portENTER_CRITICAL(&mux);
    if (busy != manual_busy)
        ++generation;
    manual_busy = busy;
    portEXIT_CRITICAL(&mux);
    if (busy)
        audio_output_stop();
}
void voice_assistant_pause(bool value) {
    portENTER_CRITICAL(&mux);
    paused = value;
    ++generation;
    portEXIT_CRITICAL(&mux);
    if (value)
        audio_output_stop();
}
bool voice_assistant_owns_audio(void) {
    portENTER_CRITICAL(&mux);
    bool owns = active.enabled && !manual_busy && !paused;
    portEXIT_CRITICAL(&mux);
    return owns;
}
static bool permitted(unsigned epoch) {
    portENTER_CRITICAL(&mux);
    bool ok = active.enabled && !manual_busy && !paused && epoch == generation;
    portEXIT_CRITICAL(&mux);
    safety_status_t safe;
    safety_snapshot(&safe);
    wifi_status_t wifi;
    wifi_manager_snapshot(&wifi);
    return ok && wifi.station_connected && safe.state == CORE_DISARMED &&
           !safe.servo_enabled && !safe.critical_battery && !safe.sleeping;
}
static unsigned current_epoch(void) {
    portENTER_CRITICAL(&mux);
    unsigned value = generation;
    portEXIT_CRITICAL(&mux);
    return value;
}
esp_err_t voice_assistant_configure(const cJSON *patch) {
    if (!initialized)
        return ESP_ERR_INVALID_STATE;
    voice_config_t next = pending;
    if (!voice_config_patch(&next, patch))
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open("walle", NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, "voice_v1", &next, sizeof(next));
        if (e == ESP_OK)
            e = nvs_commit(h);
        nvs_close(h);
    }
    if (e == ESP_OK) {
        pending = next;
        voice_assistant_pause(true);
    }
    memset(&next, 0, sizeof(next));
    return e;
}
cJSON *voice_assistant_settings(void) { return voice_config_public(&pending); }
cJSON *voice_assistant_status(void) {
    cJSON *j = voice_config_public(&active);
    if (!j)
        return NULL;
    char text[sizeof(answer)];
    const char *s, *error;
    bool stopped;
    unsigned count;
    int64_t retry;
    portENTER_CRITICAL(&mux);
    s = state;
    error = error_code;
    stopped = paused || manual_busy;
    count = turns;
    retry = retry_at;
    memcpy(text, answer, sizeof(text));
    portEXIT_CRITICAL(&mux);
    cJSON_AddStringToObject(j, "state", s);
    cJSON_AddStringToObject(j, "error", error);
    cJSON_AddStringToObject(j, "answer", text);
    cJSON_AddBoolToObject(j, "paused", stopped);
    cJSON_AddNumberToObject(j, "turns_this_hour", count);
    cJSON_AddNumberToObject(j, "retry_after_s",
                           retry > esp_timer_get_time() ? (retry - esp_timer_get_time()) / 1000000 : 0);
    return j;
}
static void close_connection(connection_t *c) {
    if (c->ws) {
        esp_transport_close(c->ws);
        esp_transport_destroy(c->ws);
    }
    if (c->tls)
        esp_transport_destroy(c->tls);
    free(c->message);
    memset(c, 0, sizeof(*c));
}
static bool send_text(connection_t *c, const char *text) {
    size_t n = strlen(text);
    /* esp_transport_ws masks its const-qualified payload IN PLACE. Never pass
     * a flash string literal to it, including commit/clear/response.create. */
    char *writable = c->ws ? strdup(text) : NULL;
    bool ok = writable && esp_transport_ws_send_raw(c->ws,
        WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN, writable, n, 1500) == (int)n;
    free(writable);
    return ok;
}
static bool send_json(connection_t *c, cJSON *j) {
    if (!j)
        return false;
    char *text = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    bool ok = text && send_text(c, text);
    cJSON_free(text);
    return ok;
}
/* The transport handles Ping/Pong internally. Assemble both partial reads and
 * fragmented WS messages; a FIN bit alone does not mean a whole payload was read. */
static int receive_message(connection_t *c, int timeout_ms) {
    char block[2048];
    int n = esp_transport_read(c->ws, block, sizeof(block), timeout_ms);
    if (n <= 0)
        return n;
    int opcode = esp_transport_ws_get_read_opcode(c->ws);
    int frame_length = esp_transport_ws_get_read_payload_len(c->ws);
    return frame_length < 0 ? -1 : voice_message_feed(&c->incoming, c->message, MESSAGE_LIMIT + 1,
        opcode, esp_transport_ws_get_fin_flag(c->ws), frame_length, block, n);
}
static const char *string_field(const cJSON *j, const char *name) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, name);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static const char *api_error(const cJSON *j) {
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(j, "error");
    const char *code = string_field(e, "code");
    if (strstr(code, "api_key") || strstr(code, "authentication"))
        return "API_AUTH";
    if (strstr(code, "quota"))
        return "API_QUOTA";
    if (strstr(code, "rate_limit"))
        return "API_RATE_LIMIT";
    if (strstr(code, "model") || strstr(code, "permission"))
        return "API_MODEL_ACCESS";
    return "API_ERROR"; /* Never echo a remote message or credential. */
}
static cJSON *wait_event(connection_t *c, const char *wanted, unsigned epoch, int seconds) {
    int64_t deadline = esp_timer_get_time() + (int64_t)seconds * 1000000;
    while (permitted(epoch) && esp_timer_get_time() < deadline) {
        int n = receive_message(c, 200);
        if (n < 0)
            break;
        if (!n)
            continue;
        cJSON *j = cJSON_Parse(c->message);
        if (!j)
            break;
        const char *type = string_field(j, "type");
        if (!strcmp(type, wanted))
            return j;
        if (!strcmp(type, "error")) {
            set_state("ERROR", api_error(j));
            cJSON_Delete(j);
            return NULL;
        }
        cJSON_Delete(j);
    }
    return NULL;
}
static bool open_connection(connection_t *c, unsigned epoch) {
    set_state("CONNECTING", NULL);
    c->message = heap_caps_malloc(MESSAGE_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    c->tls = esp_transport_ssl_init();
    if (!c->message || !c->tls)
        return false;
    esp_transport_ssl_crt_bundle_attach(c->tls, esp_crt_bundle_attach);
    c->ws = esp_transport_ws_init(c->tls);
    if (!c->ws)
        return false;
    char path[128], auth[sizeof(active.api_key) + 8];
    snprintf(path, sizeof(path), "/v1/realtime?model=%s", active.model);
    snprintf(auth, sizeof(auth), "Bearer %s", active.api_key);
    esp_transport_ws_config_t cfg = {.ws_path = path, .auth = auth,
                                      .user_agent = "WALL-E/2.2", .propagate_control_frames = false};
    esp_err_t e = esp_transport_ws_set_config(c->ws, &cfg);
    memset(auth, 0, sizeof(auth));
    if (e != ESP_OK || esp_transport_connect(c->ws, "api.openai.com", 443, 8000) < 0) {
        int http = esp_transport_ws_get_upgrade_request_status(c->ws);
        set_state("ERROR", http == 401 ? "API_AUTH" : http == 403 ? "API_MODEL_ACCESS" :
                           http == 429 ? "API_RATE_LIMIT" : "TLS_OR_NETWORK");
        return false;
    }
    /* Send the configuration immediately after the WS upgrade, as in the GA
     * API guide. session.created may already be buffered with the handshake. */
    cJSON *root = cJSON_CreateObject(), *session = cJSON_AddObjectToObject(root, "session");
    cJSON_AddStringToObject(root, "type", "session.update");
    cJSON_AddStringToObject(session, "type", "realtime");
    cJSON_AddStringToObject(session, "instructions",
        "Ban la WALL-E, tro ly giong noi than thien. Tra loi bang tieng Viet co dau, "
        "ngan gon trong 1 den 4 cau, tru khi nguoi dung can giai thich. "
        "Neu khong nghe ro, hay hoi lai. Khong gia vo biet thong tin thoi gian thuc. "
        "Ban chi hoi dap, khong co cong cu dieu khien motor hoac servo.");
    cJSON *modalities = cJSON_AddArrayToObject(session, "output_modalities");
    cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
    cJSON_AddNumberToObject(session, "max_output_tokens", 1024);
    cJSON *audio = cJSON_AddObjectToObject(session, "audio");
    cJSON *input = cJSON_AddObjectToObject(audio, "input");
    cJSON_AddNullToObject(input, "turn_detection");
    cJSON *format = cJSON_AddObjectToObject(input, "format");
    cJSON_AddStringToObject(format, "type", "audio/pcm");
    cJSON_AddNumberToObject(format, "rate", 24000);
    cJSON *output = cJSON_AddObjectToObject(audio, "output");
    cJSON_AddStringToObject(output, "voice", active.voice);
    format = cJSON_AddObjectToObject(output, "format");
    cJSON_AddStringToObject(format, "type", "audio/pcm");
    cJSON_AddNumberToObject(format, "rate", 24000);
    if (!send_json(c, root))
        return false;
    cJSON *event = wait_event(c, "session.updated", epoch, 10);
    if (!event)
        return false;
    cJSON_Delete(event);
    return true;
}
/* Local energy VAD uses a 200 ms pre-roll, so the start of a word is retained.
 * The mic is never captured while receiving or playing an assistant response. */
static size_t record_question(connection_t *c, int16_t *recording, unsigned epoch) {
    int16_t block[512], pre[PRE_ROLL];
    size_t used = 0, pre_count = 0, pre_head = 0;
    voice_vad_t vad = {0};
    audio_status_t before, after;
    vTaskDelay(pdMS_TO_TICKS(20)); /* Let a preceding audio stop flush reach the I2S engine. */
    if (!permitted(epoch))
        return 0;
    audio_snapshot(&before);
    if (audio_capture(true) != ESP_OK)
        return 0;
    set_state("LISTENING", NULL);
    eyes_expression("listening");
    while (permitted(epoch)) {
        size_t n = audio_read_pcm(block, 512);
        if (!n) {
            int got = receive_message(c, 50); /* Keep Ping/Pong and session expiry serviced. */
            if (got < 0)
                break;
            if (got > 0) {
                cJSON *j = cJSON_Parse(c->message);
                bool failed = j && !strcmp(string_field(j, "type"), "error");
                if (failed)
                    set_state("ERROR", api_error(j));
                cJSON_Delete(j);
                if (failed)
                    break;
            }
            vTaskDelay(pdMS_TO_TICKS(4));
            continue;
        }
        voice_vad_result_t result = voice_vad_feed(&vad, block, n, active.vad_threshold);
        if (result == VOICE_WAIT || result == VOICE_START) {
            for (size_t i = 0; i < n; ++i) {
                pre[pre_head] = block[i];
                pre_head = (pre_head + 1) % PRE_ROLL;
                if (pre_count < PRE_ROLL)
                    ++pre_count;
            }
            if (result == VOICE_START) {
                for (size_t i = 0; i < pre_count; ++i)
                    recording[used++] = pre[(pre_head + PRE_ROLL - pre_count + i) % PRE_ROLL];
                set_state("RECORDING", NULL);
            }
        } else {
            size_t take = n < RECORD_SAMPLES - used ? n : RECORD_SAMPLES - used;
            memcpy(recording + used, block, take * sizeof(*block));
            used += take;
            if (result == VOICE_END || used == RECORD_SAMPLES) {
                audio_capture(false);
                audio_snapshot(&after);
                if (after.dropped_samples != before.dropped_samples || after.io_errors != before.io_errors) {
                    set_state("ERROR", "MIC_OVERRUN");
                    return 0;
                }
                return used;
            }
        }
    }
    if (current_epoch() == epoch)
        audio_capture(false);
    return 0;
}
static bool submit_question(connection_t *c, const int16_t *pcm, size_t n, unsigned epoch) {
    if (!send_text(c, "{\"type\":\"input_audio_buffer.clear\"}"))
        return false;
    int16_t converted[960];
    unsigned char encoded[2564];
    char packet[2650];
    size_t offset = 0, amount;
    while ((amount = voice_resample(pcm, n, 16000, 24000, offset, converted, 960))) {
        if (!permitted(epoch))
            return false;
        size_t bytes;
        if (mbedtls_base64_encode(encoded, sizeof(encoded), &bytes,
                                 (const unsigned char *)converted, amount * 2))
            return false;
        encoded[bytes] = 0;
        snprintf(packet, sizeof(packet), "{\"type\":\"input_audio_buffer.append\",\"audio\":\"%s\"}", encoded);
        if (!send_text(c, packet))
            return false;
        offset += amount;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return send_text(c, "{\"type\":\"input_audio_buffer.commit\"}") &&
           send_text(c, "{\"type\":\"response.create\"}");
}
static bool receive_reply(connection_t *c, int16_t *pcm, size_t *samples, unsigned epoch) {
    size_t bytes_used = 0;
    int64_t deadline = esp_timer_get_time() + 60000000;
    while (permitted(epoch) && esp_timer_get_time() < deadline) {
        int got = receive_message(c, 200);
        if (got < 0)
            return false;
        if (!got)
            continue;
        cJSON *j = cJSON_Parse(c->message);
        if (!j)
            return false;
        const char *type = string_field(j, "type");
        bool ok = true, done = false;
        if (!strcmp(type, "response.output_audio.delta")) {
            const char *delta = string_field(j, "delta");
            size_t length = strlen(delta), decoded = 0;
            /* Reject overlong/odd PCM before it can overwrite the PSRAM buffer. */
            if (!length || length % 4 || (length / 4 * 3) > REPLY_SAMPLES * 2 - bytes_used ||
                mbedtls_base64_decode((unsigned char *)pcm + bytes_used,
                    REPLY_SAMPLES * 2 - bytes_used, &decoded, (const unsigned char *)delta, length) ||
                decoded % 2) {
                set_state("ERROR", "AUDIO_REPLY_LIMIT");
                ok = false;
            } else
                bytes_used += decoded;
        } else if (!strcmp(type, "response.output_audio_transcript.done")) {
            const char *text = string_field(j, "transcript");
            size_t n = strlen(text);
            if (n >= sizeof(answer)) {
                n = sizeof(answer) - 1;
                while (n && ((unsigned char)text[n] & 0xc0) == 0x80)
                    --n;
            }
            portENTER_CRITICAL(&mux);
            memcpy(answer, text, n);
            answer[n] = 0;
            portEXIT_CRITICAL(&mux);
        } else if (!strcmp(type, "error")) {
            set_state("ERROR", api_error(j));
            ok = false;
        } else if (!strcmp(type, "response.done")) {
            const cJSON *response = cJSON_GetObjectItemCaseSensitive(j, "response");
            done = !strcmp(string_field(response, "status"), "completed");
            if (!done) {
                const cJSON *details = cJSON_GetObjectItemCaseSensitive(response, "status_details");
                set_state("ERROR", details ? api_error(details) : "API_RESPONSE_INCOMPLETE");
                ok = false;
            }
        }
        cJSON_Delete(j);
        if (!ok)
            return false;
        if (done) {
            *samples = bytes_used / 2;
            return *samples > 0;
        }
    }
    return false;
}
static bool play_reply(connection_t *c, const int16_t *pcm, size_t count, unsigned epoch) {
    int16_t block[128];
    size_t offset = 0, n;
    set_state("SPEAKING", NULL);
    eyes_expression("speaking");
    while ((n = voice_resample(pcm, count, 24000, 16000, offset, block, 128))) {
        if (offset % (128 * 32) == 0) {
            int got = receive_message(c, 10);
            if (got < 0)
                return false;
            if (got > 0) {
                cJSON *event = cJSON_Parse(c->message);
                bool failed = event && !strcmp(string_field(event, "type"), "error");
                if (failed)
                    set_state("ERROR", api_error(event));
                cJSON_Delete(event);
                if (failed)
                    return false;
            }
        }
        int64_t deadline = esp_timer_get_time() + 500000;
        esp_err_t e;
        do {
            if (!permitted(epoch))
                return false;
            e = audio_output_pcm(block, n);
            if (e == ESP_ERR_TIMEOUT)
                vTaskDelay(pdMS_TO_TICKS(4));
        } while (e == ESP_ERR_TIMEOUT && esp_timer_get_time() < deadline);
        if (e != ESP_OK)
            return false;
        offset += n;
    }
    /* Drain the bounded 96 ms playback queue, then suppress acoustic tail. */
    for (unsigned i = 0; i < 70; ++i) {
        if (!permitted(epoch))
            return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}
static void assistant_task(void *arg) {
    int16_t *recording = heap_caps_malloc(RECORD_SAMPLES * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *reply = heap_caps_malloc(REPLY_SAMPLES * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    connection_t connection = {0};
    if (!recording || !reply) {
        free(recording);
        free(reply);
        set_state("ERROR", "NO_PSRAM");
        vTaskDelete(NULL);
        return;
    }
    bool sntp_started = false;
    int64_t hour = esp_timer_get_time();
    while (true) {
        unsigned epoch = current_epoch();
        audio_status_t audio;
        audio_snapshot(&audio);
        if (!permitted(epoch) || !audio.ready) {
            close_connection(&connection);
            bool busy, stopped;
            portENTER_CRITICAL(&mux);
            busy = manual_busy;
            stopped = paused;
            const char *previous_error = error_code;
            portEXIT_CRITICAL(&mux);
            set_state(busy || stopped ? "PAUSED" : !audio.ready ? "AUDIO_NOT_READY" : "WAIT_WIFI", previous_error);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!sntp_started) {
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            sntp_started = true;
        }
        if (time(NULL) < 1735689600) { /* Never bypass TLS validation to work around an unset clock. */
            set_state("WAIT_TIME", NULL);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&mux);
        if (now - hour >= 3600000000LL) {
            turns = 0;
            hour = now;
        }
        bool limited = turns >= 30;
        bool waiting = now < retry_at;
        portEXIT_CRITICAL(&mux);
        if (limited || waiting) {
            close_connection(&connection);
            if (limited)
                set_state("RATE_LIMITED", "LOCAL_30_TURNS_PER_HOUR");
            else {
                portENTER_CRITICAL(&mux);
                state = "ERROR";
                portEXIT_CRITICAL(&mux);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        bool ok = connection.ws || open_connection(&connection, epoch);
        size_t count = ok ? record_question(&connection, recording, epoch) : 0;
        if (ok && count && permitted(epoch)) {
            portENTER_CRITICAL(&mux);
            ++turns;
            answer[0] = 0;
            portEXIT_CRITICAL(&mux);
            set_state("THINKING", NULL);
            eyes_expression("thinking");
            size_t reply_count = 0;
            ok = submit_question(&connection, recording, count, epoch) &&
                 receive_reply(&connection, reply, &reply_count, epoch) &&
                 play_reply(&connection, reply, reply_count, epoch);
        } else
            ok = false;
        if (!ok) {
            close_connection(&connection);
            /* A manual controller owns audio after a takeover; do not flush its output. */
            if (permitted(epoch)) {
                audio_output_stop();
                portENTER_CRITICAL(&mux);
                if (!error_code[0])
                    error_code = "NETWORK_OR_AUDIO";
                state = "ERROR";
                bool permanent = !strcmp(error_code, "API_AUTH") || !strcmp(error_code, "API_QUOTA") ||
                                 !strcmp(error_code, "API_MODEL_ACCESS") || !strcmp(error_code, "API_ERROR");
                retry_at = permanent ? INT64_MAX : esp_timer_get_time() + 60000000;
                portEXIT_CRITICAL(&mux);
                eyes_expression("error");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t voice_assistant_init(const settings_t *settings) {
    voice_config_defaults(&active);
    nvs_handle_t h;
    esp_err_t e = nvs_open("walle", NVS_READONLY, &h);
    if (e == ESP_OK) {
        voice_config_t loaded;
        size_t bytes = sizeof(loaded);
        e = nvs_get_blob(h, "voice_v1", &loaded, &bytes);
        if (e == ESP_OK && bytes == sizeof(loaded) && voice_config_validate(&loaded))
            active = loaded;
        memset(&loaded, 0, sizeof(loaded));
        nvs_close(h);
    }
    pending = active;
    initialized = true;
    /* ESP-IDF's WS transport logs the upgrade request on some failures.
     * Suppress this tag entirely because Authorization is part of that request. */
    esp_log_level_set("transport_ws", ESP_LOG_NONE);
    if (!active.enabled) {
        set_state("DISABLED", NULL);
        return ESP_OK;
    }
    if (!settings->audio_enabled) {
        set_state("AUDIO_NOT_READY", NULL);
        return ESP_OK;
    }
    set_state("WAIT_WIFI", NULL);
    return xTaskCreate(assistant_task, "voice_ai", 18432, NULL, 2, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
