#include "web_server.h"
#include "command_protocol.h"
#include "safety_manager.h"
#include "diagnostics.h"
#include "wifi_manager.h"
#include "robot_eyes.h"
#include "audio_input.h"
#include "audio_output.h"
#include "voice_assistant.h"
#include "battery_monitor.h"
#include "test_runner.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

extern const char html_start[] asm("_binary_index_html_start");
extern const char html_end[] asm("_binary_index_html_end");
extern const char css_start[] asm("_binary_style_css_start");
extern const char css_end[] asm("_binary_style_css_end");
extern const char js_start[] asm("_binary_app_js_start");
extern const char js_end[] asm("_binary_app_js_end");
typedef struct {
    bool authenticated;
    uint32_t last_seq;
    int fd;
    int64_t heartbeat;
} session_t;
static httpd_handle_t server;
static session_t *owner;
static session_t usb_session;
static portMUX_TYPE usb_queue_mux = portMUX_INITIALIZER_UNLOCKED;
static unsigned usb_queued;
static settings_t active, pending;
static bool settings_dirty;
static int64_t last_settings_save;
static int64_t last_ai_save;
static bool equal_secret(const char *a, const char *b) {
    size_t n = strlen(a);
    if (n != strlen(b))
        return false;
    unsigned difference = 0;
    for (size_t i = 0; i < n; ++i)
        difference |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return difference == 0;
}
static bool authorized(httpd_req_t *r) {
    char header[80];
    if (httpd_req_get_hdr_value_str(r, "Authorization", header, sizeof(header)) != ESP_OK)
        return false;
    return !strncmp(header, "Bearer ", 7) && equal_secret(header + 7, active.token);
}
static esp_err_t deny(httpd_req_t *r) {
    httpd_resp_set_status(r, "401 Unauthorized");
    return httpd_resp_sendstr(r, "Authentication required");
}
static esp_err_t json_reply(httpd_req_t *r, cJSON *j) {
    if (!j)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory exhausted");
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body)
        return ESP_ERR_NO_MEM;
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_sendstr(r, body);
    cJSON_free(body);
    return e;
}
static cJSON *telemetry(void) {
    cJSON *j = diagnostics_json();
    if (!j)
        return NULL;
    audio_status_t a;
    audio_snapshot(&a);
    battery_status_t b;
    battery_snapshot(&b);
    wifi_status_t w;
    wifi_manager_snapshot(&w);
    cJSON_AddStringToObject(j, "type", "telemetry");
    cJSON_AddNumberToObject(j, "v", 1);
    cJSON_AddBoolToObject(j, "controller_claimed", owner != NULL);
    cJSON_AddBoolToObject(j, "restart_required", settings_dirty);
    cJSON_AddBoolToObject(j, "hardware_confirmed", active.hardware_confirmed);
    cJSON_AddStringToObject(j, "controller_transport",
                           owner == &usb_session ? "usb" : owner ? "wifi" : "none");
    cJSON_AddNumberToObject(j, "motor_limit", active.motor_limit);
    cJSON_AddBoolToObject(j, "lcd_enabled", active.lcd_enabled);
    cJSON_AddBoolToObject(j, "audio_enabled", active.audio_enabled);
    cJSON *limits = cJSON_AddArrayToObject(j, "servo_limits");
    for (unsigned i = 0; i < 2; ++i) {
        cJSON *arm = cJSON_CreateObject();
        cJSON_AddNumberToObject(arm, "min_deg", active.servo[i].min_deg);
        cJSON_AddNumberToObject(arm, "max_deg", active.servo[i].max_deg);
        cJSON_AddNumberToObject(arm, "home_deg", active.servo[i].home_deg);
        cJSON_AddItemToArray(limits, arm);
    }
    cJSON_AddStringToObject(j, "expression", eyes_name());
    cJSON *audio = cJSON_AddObjectToObject(j, "audio");
    cJSON_AddBoolToObject(audio, "ready", a.ready);
    cJSON_AddBoolToObject(audio, "capturing", a.capturing);
    cJSON_AddBoolToObject(audio, "playing", a.playing);
    cJSON_AddNumberToObject(audio, "peak", a.peak);
    cJSON_AddNumberToObject(audio, "clipped", a.clipped);
    cJSON_AddNumberToObject(audio, "dropped_samples", a.dropped_samples);
    cJSON_AddNumberToObject(audio, "io_errors", a.io_errors);
    cJSON_AddNumberToObject(audio, "captured_samples", a.captured_samples);
    cJSON *battery = cJSON_AddObjectToObject(j, "battery");
    cJSON_AddBoolToObject(battery, "available", b.available);
    if (b.available)
        cJSON_AddNumberToObject(battery, "voltage", b.voltage);
    else
        cJSON_AddStringToObject(battery, "message", "Battery sensor unavailable");
    cJSON *wifi = cJSON_AddObjectToObject(j, "wifi");
    cJSON_AddBoolToObject(wifi, "station_connected", w.station_connected);
    cJSON_AddStringToObject(wifi, "station_ip", w.station_ip);
    cJSON_AddStringToObject(wifi, "ap_ip", w.ap_ip);
    cJSON_AddStringToObject(wifi, "ap_ssid", w.ap_ssid);
    if (a.playing)
        cJSON_AddStringToObject(j, "audio_state", "SPEAKING");
    else if (a.capturing)
        cJSON_AddStringToObject(j, "audio_state", "LISTENING");
    else
        cJSON_AddStringToObject(j, "audio_state", "IDLE");
    cJSON_AddItemToObject(j, "ai", voice_assistant_status());
    return j;
}
static void expire_owner(void) {
    if (owner && esp_timer_get_time() - owner->heartbeat > 1000000) {
        owner = NULL;
        safety_controller_lease(false);
        safety_disarm("Controller heartbeat expired");
        audio_output_stop();
        voice_assistant_control_busy(false);
    }
}
static void session_free(void *ctx) {
    if (owner == ctx) {
        owner = NULL;
        safety_controller_lease(false);
        safety_disarm("Control WebSocket disconnected");
        audio_output_stop();
        voice_assistant_control_busy(false);
    }
    free(ctx);
}
static esp_err_t ws_ack(httpd_req_t *r, uint32_t seq, esp_err_t result, const char *message) {
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(j, "v", 1);
    cJSON_AddStringToObject(j, "type", "ack");
    cJSON_AddNumberToObject(j, "seq", seq);
    cJSON_AddBoolToObject(j, "ok", result == ESP_OK);
    cJSON_AddStringToObject(j, "result", esp_err_to_name(result));
    if (message)
        cJSON_AddStringToObject(j, "message", message);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body)
        return ESP_ERR_NO_MEM;
    if (!r) {
        printf("GUI %s\n", body);
        cJSON_free(body);
        return ESP_OK;
    }
    httpd_ws_frame_t f = {
        .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)body, .len = strlen(body)};
    esp_err_t e = httpd_ws_send_frame(r, &f);
    cJSON_free(body);
    return e;
}
static esp_err_t dispatch_command(httpd_req_t *r, session_t *s, const command_t *command);
static esp_err_t websocket(httpd_req_t *r) {
    if (r->method == HTTP_GET) {
        session_t *s = calloc(1, sizeof(*s));
        if (!s)
            return ESP_ERR_NO_MEM;
        s->fd = httpd_req_to_sockfd(r);
        s->heartbeat = esp_timer_get_time();
        r->sess_ctx = s;
        r->free_ctx = session_free;
        return ESP_OK;
    }
    session_t *s = r->sess_ctx;
    if (!s)
        return ESP_FAIL;
    expire_owner();
    httpd_ws_frame_t frame = {0};
    esp_err_t e = httpd_ws_recv_frame(r, &frame, 0);
    if (e != ESP_OK)
        return e;
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        if (owner == s) {
            owner = NULL;
            safety_controller_lease(false);
            safety_disarm("WebSocket closed");
            audio_output_stop();
            voice_assistant_control_busy(false);
        }
        return ESP_OK;
    }
    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0 || frame.len > 1024 || !frame.final)
        return ESP_FAIL;
    char payload[1025];
    frame.payload = (uint8_t *)payload;
    if ((e = httpd_ws_recv_frame(r, &frame, 1024)) != ESP_OK)
        return e;
    payload[frame.len] = 0;
    command_t c;
    if (!protocol_parse(payload, frame.len, &c))
        return ws_ack(r, 0, ESP_ERR_INVALID_ARG, "Invalid versioned command");
    return dispatch_command(r, s, &c);
}
static esp_err_t dispatch_command(httpd_req_t *r, session_t *s, const command_t *command) {
    const command_t c = *command;
    esp_err_t e;
    if (!core_accept_sequence(s->last_seq, c.seq))
        return ws_ack(r, c.seq, ESP_ERR_INVALID_STATE, "Stale sequence rejected");
    s->last_seq = c.seq;
    if (c.kind == CMD_AUTH) {
        s->authenticated = equal_secret(c.value, active.token);
        return ws_ack(r, c.seq, s->authenticated ? ESP_OK : ESP_ERR_INVALID_ARG,
                      "Authentication result");
    }
    if (!s->authenticated)
        return ws_ack(r, c.seq, ESP_ERR_INVALID_STATE, "Authenticate first");
    if (c.kind == CMD_ESTOP) {
        safety_estop();
        voice_assistant_pause(true);
        audio_output_stop();
        return ws_ack(r, c.seq, ESP_OK, "E-STOP latched");
    }
    if (c.kind == CMD_CLAIM) {
        if (owner && owner != s)
            return ws_ack(r, c.seq, ESP_ERR_INVALID_STATE, "Another controller holds the lease");
        if (!owner)
            safety_disarm("Controller claimed; explicit arm required");
        voice_assistant_control_busy(true);
        owner = s;
        s->heartbeat = esp_timer_get_time();
        safety_controller_lease(true);
        return ws_ack(r, c.seq, ESP_OK, "Control lease acquired");
    }
    if (owner != s)
        return ws_ack(r, c.seq, ESP_ERR_INVALID_STATE, "Claim control first");
    if (c.kind == CMD_HEARTBEAT) {
        s->heartbeat = esp_timer_get_time();
        safety_controller_heartbeat();
        return ws_ack(r, c.seq, ESP_OK, NULL);
    }
    e = ESP_OK;
    char message[160] = {0};
    float motors[4];
    switch (c.kind) {
    case CMD_RELEASE:
        owner = NULL;
        safety_controller_lease(false);
        safety_disarm("Control released");
        audio_output_stop();
        voice_assistant_control_busy(false);
        break;
    case CMD_ARM:
        e = settings_dirty ? ESP_ERR_INVALID_STATE : safety_arm(c.confirmed);
        break;
    case CMD_DISARM:
        safety_disarm("User stop");
        break;
    case CMD_RESET_ESTOP:
        e = safety_reset_estop(c.confirmed);
        break;
    case CMD_DRIVE:
        if (!core_mix(c.throttle, c.steering, active.motor_limit * c.speed, motors))
            e = ESP_ERR_INVALID_ARG;
        else
            e = safety_drive(motors);
        break;
    case CMD_MOTORS:
        for (int i = 0; i < 4; ++i)
            motors[i] = c.motors[i] * active.motor_limit;
        e = safety_drive(motors);
        break;
    case CMD_ARMS:
        e = safety_arms(c.left, c.right);
        break;
    case CMD_SERVO_ENABLE:
        e = settings_dirty ? ESP_ERR_INVALID_STATE : safety_servo_enable(c.enabled, c.confirmed);
        break;
    case CMD_EXPRESSION:
        e = eyes_expression(c.value);
        break;
    case CMD_LOOK:
        eyes_look(c.x, c.y);
        break;
    case CMD_EYES_OPTIONS:
        eyes_options(c.auto_blink, c.brightness);
        break;
    case CMD_SLEEP:
        safety_sleep(c.enabled);
        if (c.enabled)
            audio_output_stop();
        break;
    case CMD_TEST:
        e = settings_dirty ? ESP_ERR_INVALID_STATE
                           : test_run(c.value, c.confirmed, message, sizeof(message));
        break;
    case CMD_AUDIO: {
        safety_status_t safe;
        safety_snapshot(&safe);
        if (safe.state == CORE_ESTOP || safe.state == CORE_FAULT || safe.critical_battery) {
            e = ESP_ERR_INVALID_STATE;
            break;
        }
        audio_output_volume(c.volume);
        if (!strcmp(c.value, "capture_start"))
            e = audio_capture(true);
        else if (!strcmp(c.value, "capture_stop"))
            e = audio_capture(false);
        else if (!strcmp(c.value, "tone"))
            e = audio_output_tone();
        else if (!strcmp(c.value, "stop"))
            audio_output_stop();
        else
            e = ESP_ERR_INVALID_ARG;
        break;
    }
    default:
        e = ESP_ERR_INVALID_ARG;
        break;
    }
    return ws_ack(r, c.seq, e, message[0] ? message : NULL);
}
static esp_err_t asset(httpd_req_t *r) {
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "Content-Security-Policy",
                       "default-src 'self'; connect-src 'self' ws:; style-src 'self'; script-src "
                       "'self'; media-src 'self' blob:; frame-ancestors 'none'");
    if (!strcmp(r->uri, "/style.css")) {
        httpd_resp_set_type(r, "text/css");
        return httpd_resp_send(r, css_start, css_end - css_start);
    }
    if (!strcmp(r->uri, "/app.js")) {
        httpd_resp_set_type(r, "application/javascript");
        return httpd_resp_send(r, js_start, js_end - js_start);
    }
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, html_start, html_end - html_start);
}
static esp_err_t status_get(httpd_req_t *r) {
    if (!authorized(r))
        return deny(r);
    expire_owner();
    return json_reply(r, telemetry());
}
static esp_err_t settings_get(httpd_req_t *r) {
    if (!authorized(r))
        return deny(r);
    return json_reply(r, settings_public_json(&pending));
}
static esp_err_t read_exact(httpd_req_t *r, void *buffer, size_t n) {
    size_t got = 0;
    while (got < n) {
        int result = httpd_req_recv(r, (char *)buffer + got, n - got);
        if (result <= 0)
            return ESP_FAIL;
        got += result;
    }
    return ESP_OK;
}
static esp_err_t save_pending_settings(const cJSON *patch) {
    if (!safety_is_disarmed() || owner ||
        esp_timer_get_time() - last_settings_save < 3000000)
        return ESP_ERR_INVALID_STATE;
    settings_t next = pending;
    if (!protocol_json_unique(patch) || !settings_patch(&next, patch))
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = settings_save(&next);
    if (e != ESP_OK)
        return e;
    pending = next;
    settings_dirty = true;
    voice_assistant_pause(true);
    safety_maintenance();
    last_settings_save = esp_timer_get_time();
    return ESP_OK;
}
static esp_err_t settings_post(httpd_req_t *r) {
    if (!authorized(r))
        return deny(r);
    if (!safety_is_disarmed() || owner)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST,
                                   "Release control before saving settings");
    if (esp_timer_get_time() - last_settings_save < 3000000)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Wait 3 seconds between saves");
    if (r->content_len == 0 || r->content_len > 4096)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Settings body too large");
    char body[4097];
    if (read_exact(r, body, r->content_len) != ESP_OK)
        return ESP_FAIL;
    body[r->content_len] = 0;
    if (!protocol_json_frame_valid(body, r->content_len))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Malformed/deep settings JSON");
    const char *end;
    cJSON *j = cJSON_ParseWithLengthOpts(body, r->content_len + 1, &end, true);
    if (!j || end != body + r->content_len) {
        cJSON_Delete(j);
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid settings; nothing saved");
    }
    esp_err_t e = save_pending_settings(j);
    cJSON_Delete(j);
    if (e != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, esp_err_to_name(e));
    return httpd_resp_sendstr(r, "Saved. Reboot to apply; actuators remain locked.");
}
static void restart_task(void *arg) {
    safety_disarm("Reboot requested");
    audio_output_stop();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}
static esp_err_t restart_post(httpd_req_t *r) {
    if (!authorized(r))
        return deny(r);
    voice_assistant_pause(true);
    safety_maintenance();
    owner = NULL;
    safety_controller_lease(false);
    if (xTaskCreate(restart_task, "restart", 2048, NULL, 2, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return httpd_resp_sendstr(r, "Restarting safely");
}
static esp_err_t reset_post(httpd_req_t *r) {
    if (!authorized(r))
        return deny(r);
    char confirmation[32];
    if (httpd_req_get_hdr_value_str(r, "X-Confirm", confirmation, sizeof(confirmation)) != ESP_OK ||
        strcmp(confirmation, "FACTORY_RESET"))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST,
                                   "Explicit FACTORY_RESET confirmation required");
    safety_disarm("Factory reset");
    owner = NULL;
    if (settings_factory_reset() != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS reset failed");
    return restart_post(r);
}
static void usb_reply(cJSON *j, uint32_t seq) {
    if (!j) {
        ws_ack(NULL, seq, ESP_ERR_NO_MEM, NULL);
        return;
    }
    cJSON_AddNumberToObject(j, "seq", seq);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (body) {
        printf("GUI %s\n", body);
        cJSON_free(body);
    }
}
/* Runs only on the HTTP task, including all access to owner/active/pending.
 * No second controller or independent lease is introduced for USB. */
static void usb_work(void *arg) {
    portENTER_CRITICAL(&usb_queue_mux);
    --usb_queued;
    portEXIT_CRITICAL(&usb_queue_mux);
    char *body = arg;
    size_t length = strlen(body);
    expire_owner();
    command_t command;
    if (protocol_parse(body, length, &command)) {
        dispatch_command(NULL, &usb_session, &command);
        free(body);
        return;
    }
    uint32_t seq = 0;
    cJSON *j = protocol_usb_parse(body, length, &seq);
    free(body);
    if (!j) {
        ws_ack(NULL, 0, ESP_ERR_INVALID_ARG, "Invalid USB command");
        return;
    }
    const char *type = cJSON_GetObjectItemCaseSensitive(j, "type")->valuestring;
    if (!strcmp(type, "usb_hello")) {
        if (owner == &usb_session) {
            owner = NULL;
            safety_controller_lease(false);
            safety_disarm("USB session restarted; re-arm required");
            audio_output_stop();
            voice_assistant_control_busy(false);
        }
        /* Like the existing physical console, USB needs physical access.
         * This does not authenticate any WebSocket or reveal pairing secrets. */
        usb_session = (session_t){.authenticated = true, .last_seq = seq, .fd = -1};
        cJSON *hello = cJSON_CreateObject();
        cJSON_AddStringToObject(hello, "type", "hello");
        cJSON_AddNumberToObject(hello, "v", 1);
        cJSON_AddStringToObject(hello, "transport", "usb");
        usb_reply(hello, seq);
    } else if (!usb_session.authenticated || !core_accept_sequence(usb_session.last_seq, seq)) {
        ws_ack(NULL, seq, ESP_ERR_INVALID_STATE, "Open a fresh USB session first");
    } else {
        usb_session.last_seq = seq;
        if (!strcmp(type, "usb_status")) {
            cJSON *status = telemetry();
            if (status)
                cJSON_AddBoolToObject(status, "usb_controller", owner == &usb_session);
            usb_reply(status, seq);
        } else if (!strcmp(type, "usb_settings_get")) {
            cJSON *reply = cJSON_CreateObject();
            cJSON_AddStringToObject(reply, "type", "settings");
            cJSON_AddNumberToObject(reply, "v", 1);
            cJSON_AddItemToObject(reply, "settings", settings_public_json(&pending));
            cJSON_AddItemToObject(reply, "ai_settings", voice_assistant_settings());
            usb_reply(reply, seq);
        } else if (!strcmp(type, "usb_settings_patch")) {
            esp_err_t e = save_pending_settings(cJSON_GetObjectItemCaseSensitive(j, "patch"));
            ws_ack(NULL, seq, e, e == ESP_OK ? "Saved; reboot required" :
                   "Release control, wait 3s and check settings");
        } else if (!strcmp(type, "usb_ai_config")) {
            esp_err_t e = ESP_ERR_INVALID_STATE;
            if (!owner && safety_is_disarmed() && esp_timer_get_time() - last_ai_save >= 3000000) {
                e = voice_assistant_configure(cJSON_GetObjectItemCaseSensitive(j, "config"));
                if (e == ESP_OK) {
                    last_ai_save = esp_timer_get_time();
                    settings_dirty = true;
                    safety_maintenance();
                }
            }
            ws_ack(NULL, seq, e, e == ESP_OK ? "AI saved; reboot required" :
                   "Release control, wait 3s and check AI configuration");
        } else if (!strcmp(type, "usb_ai_pause")) {
            bool paused = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "paused"));
            esp_err_t e = !owner && safety_is_disarmed() && (!settings_dirty || paused)
                              ? ESP_OK : ESP_ERR_INVALID_STATE;
            if (e == ESP_OK)
                voice_assistant_pause(paused);
            ws_ack(NULL, seq, e, "Release control before changing AI listening state");
        } else if (!strcmp(type, "usb_reboot")) {
            voice_assistant_pause(true);
            safety_maintenance();
            owner = NULL;
            safety_controller_lease(false);
            esp_err_t e = xTaskCreate(restart_task, "usb_restart", 2048, NULL, 2, NULL) == pdPASS
                              ? ESP_OK : ESP_ERR_NO_MEM;
            ws_ack(NULL, seq, e, "Restarting safely");
        }
    }
    cJSON_Delete(j);
}
esp_err_t web_server_usb_command(const char *json, size_t length) {
    if (!server || !json || length == 0 || length > 1024 || memchr(json, 0, length))
        return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&usb_queue_mux);
    bool full = usb_queued >= 8;
    if (!full)
        ++usb_queued;
    portEXIT_CRITICAL(&usb_queue_mux);
    if (full)
        return ESP_ERR_TIMEOUT;
    char *copy = malloc(length + 1);
    if (!copy) {
        portENTER_CRITICAL(&usb_queue_mux);
        --usb_queued;
        portEXIT_CRITICAL(&usb_queue_mux);
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, json, length);
    copy[length] = 0;
    esp_err_t e = httpd_queue_work(server, usb_work, copy);
    if (e != ESP_OK) {
        free(copy);
        portENTER_CRITICAL(&usb_queue_mux);
        --usb_queued;
        portEXIT_CRITICAL(&usb_queue_mux);
    }
    return e;
}
static esp_err_t audio_get(httpd_req_t *r) {
    if (!authorized(r))
        return deny(r);
    if (voice_assistant_owns_audio())
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Pause AI before manual microphone access");
    int16_t pcm[512];
    size_t n = audio_read_pcm(pcm, 512);
    httpd_resp_set_type(r, "application/octet-stream");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, (const char *)pcm, n * 2);
}
static esp_err_t audio_post(httpd_req_t *r) {
    if (!authorized(r))
        return deny(r);
    audio_status_t a;
    audio_snapshot(&a);
    safety_status_t s;
    safety_snapshot(&s);
    if (voice_assistant_owns_audio() || !a.ready || s.state == CORE_ESTOP || s.state == CORE_FAULT || s.critical_battery ||
        !safety_is_disarmed() || owner)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST,
                                   "Release controller and stop actuators before upload");
    if (r->content_len == 0 || r->content_len > 1024 * 1024)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Audio limit 1 MiB");
    unsigned remaining = r->content_len;
    if (!strcmp(r->uri, "/api/wav")) {
        uint8_t header[44];
        uint32_t bytes;
        if (remaining < 44 || read_exact(r, header, 44) != ESP_OK ||
            !audio_output_wav_header(header, remaining, &bytes))
            return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST,
                                       "Use canonical PCM WAV mono/16kHz/16bit header");
        remaining = bytes;
    }
    if (remaining % 2)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "PCM must have even byte count");
    while (remaining) {
        int16_t pcm[128];
        unsigned bytes = remaining > sizeof(pcm) ? sizeof(pcm) : remaining;
        if (read_exact(r, pcm, bytes) != ESP_OK) {
            audio_output_stop();
            return ESP_FAIL;
        }
        esp_err_t e = ESP_ERR_TIMEOUT;
        int64_t deadline = esp_timer_get_time() + 250000;
        do {
            e = audio_output_pcm(pcm, bytes / 2);
            if (e == ESP_ERR_TIMEOUT)
                vTaskDelay(pdMS_TO_TICKS(4));
        } while (e == ESP_ERR_TIMEOUT && esp_timer_get_time() < deadline);
        if (e != ESP_OK) {
            audio_output_stop();
            return ESP_FAIL;
        }
        remaining -= bytes;
    }
    return httpd_resp_sendstr(r, "Audio queued");
}
static void lease_check(void *arg) {
    expire_owner();
}
static void lease_task(void *arg) {
    while (true) {
        esp_err_t e = httpd_queue_work(server, lease_check, NULL);
        if (e != ESP_OK)
            safety_disarm("Web server unavailable");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
esp_err_t web_server_init(const settings_t *s) {
    active = pending = *s;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 16;
    cfg.max_open_sockets = 5;
    cfg.stack_size = 12288;
    cfg.recv_wait_timeout = 1;
    cfg.send_wait_timeout = 1;
    cfg.lru_purge_enable = true;
    esp_err_t e = httpd_start(&server, &cfg);
    if (e != ESP_OK)
        return e;
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = asset},
        {.uri = "/style.css", .method = HTTP_GET, .handler = asset},
        {.uri = "/app.js", .method = HTTP_GET, .handler = asset},
        {.uri = "/ws", .method = HTTP_GET, .handler = websocket, .is_websocket = true},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/settings", .method = HTTP_GET, .handler = settings_get},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = settings_post},
        {.uri = "/api/reboot", .method = HTTP_POST, .handler = restart_post},
        {.uri = "/api/factory-reset", .method = HTTP_POST, .handler = reset_post},
        {.uri = "/api/audio", .method = HTTP_GET, .handler = audio_get},
        {.uri = "/api/pcm", .method = HTTP_POST, .handler = audio_post},
        {.uri = "/api/wav", .method = HTTP_POST, .handler = audio_post},
    };
    for (unsigned i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i)
        if ((e = httpd_register_uri_handler(server, &handlers[i])) != ESP_OK)
            return e;
    return xTaskCreate(lease_task, "web_lease", 2048, NULL, 3, NULL) == pdPASS ? ESP_OK
                                                                               : ESP_ERR_NO_MEM;
}
