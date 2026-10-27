#include "audio_input.h"
#include "board_config.h"
#include "safety_manager.h"
#include "diagnostics.h"
#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>
#include <string.h>

#define BLOCK 128
#define RING 8192
typedef struct {
    uint16_t count;
    int16_t pcm[BLOCK];
} pcm_block_t;
static i2s_chan_handle_t rx, tx;
static QueueHandle_t playback;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static int16_t capture[RING];
static unsigned head, tail, count, volume, tone_left;
static bool right_channel, flush_output;
static audio_status_t status;
static bool IRAM_ATTR receive_overflow(i2s_chan_handle_t channel, i2s_event_data_t *event,
                                       void *context) {
    portENTER_CRITICAL_ISR(&mux);
    ++status.io_errors;
    if (status.capturing)
        status.dropped_samples += event->size / (2 * sizeof(int32_t));
    portEXIT_CRITICAL_ISR(&mux);
    return false;
}
void audio_snapshot(audio_status_t *out) {
    portENTER_CRITICAL(&mux);
    *out = status;
    portEXIT_CRITICAL(&mux);
}
esp_err_t audio_capture(bool on) {
    portENTER_CRITICAL(&mux);
    bool ready = status.ready;
    if (ready) {
        status.capturing = on;
        if (on) {
            head = tail = count = 0;
            status.peak = 0;
        }
    }
    portEXIT_CRITICAL(&mux);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}
size_t audio_read_pcm(int16_t *out, size_t capacity) {
    portENTER_CRITICAL(&mux);
    size_t n = count < capacity ? count : capacity;
    for (size_t i = 0; i < n; ++i) {
        out[i] = capture[tail];
        tail = (tail + 1) % RING;
    }
    count -= n;
    portEXIT_CRITICAL(&mux);
    return n;
}
esp_err_t audio_engine_enqueue(const int16_t *pcm, size_t n) {
    if (!playback || n > BLOCK || n == 0)
        return ESP_ERR_INVALID_ARG;
    audio_status_t s;
    audio_snapshot(&s);
    if (!s.ready)
        return ESP_ERR_INVALID_STATE;
    pcm_block_t b = {.count = n};
    memcpy(b.pcm, pcm, n * 2);
    return xQueueSend(playback, &b, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t audio_engine_tone(void) {
    portENTER_CRITICAL(&mux);
    bool ready = status.ready;
    if (ready)
        tone_left = 4000;
    portEXIT_CRITICAL(&mux);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}
void audio_engine_stop(void) {
    portENTER_CRITICAL(&mux);
    tone_left = 0;
    flush_output = true;
    status.capturing = false;
    portEXIT_CRITICAL(&mux);
}
void audio_engine_volume(unsigned v) {
    portENTER_CRITICAL(&mux);
    volume = v > 100 ? 100 : v;
    portEXIT_CRITICAL(&mux);
}
static void engine(void *arg) {
    int32_t input[BLOCK * 2], output[BLOCK * 2];
    pcm_block_t b;
    unsigned phase = 0;
    while (true) {
        size_t bytes = 0;
        esp_err_t read = i2s_channel_read(rx, input, sizeof(input), &bytes, 20);
        if (read == ESP_OK) {
            uint16_t peak = 0;
            uint32_t clips = 0;
            size_t samples = bytes / (2 * sizeof(int32_t));
            portENTER_CRITICAL(&mux);
            if (status.capturing)
                for (size_t i = 0; i < samples; ++i) {
                    /* INMP441: signed 24-bit sample in the MSBs of its 32-bit
                     * Philips slot. Keep its upper 16 bits; select L/R slot. */
                    int16_t pcm = (int16_t)(input[i * 2 + (right_channel ? 1 : 0)] >> 16);
                    uint16_t magnitude = pcm < 0 ? -(int32_t)pcm : pcm;
                    if (magnitude > peak)
                        peak = magnitude;
                    if (magnitude >= 32760)
                        ++clips;
                    if (count == RING) {
                        tail = (tail + 1) % RING;
                        --count;
                        ++status.dropped_samples;
                    }
                    capture[head] = pcm;
                    head = (head + 1) % RING;
                    ++count;
                }
            if (status.capturing) {
                status.captured_samples += samples;
                status.clipped += clips;
                status.peak = peak;
            }
            portEXIT_CRITICAL(&mux);
        } else if (read != ESP_ERR_TIMEOUT) {
            portENTER_CRITICAL(&mux);
            ++status.io_errors;
            portEXIT_CRITICAL(&mux);
        }
        safety_status_t safe;
        safety_snapshot(&safe);
        bool stop;
        unsigned gain, remaining;
        portENTER_CRITICAL(&mux);
        stop = flush_output || safe.state == CORE_ESTOP || safe.state == CORE_FAULT ||
               safe.critical_battery;
        flush_output = false;
        gain = volume;
        remaining = tone_left;
        if (stop) {
            tone_left = 0;
            status.capturing = false;
            remaining = 0;
        } else
            tone_left = remaining > BLOCK ? remaining - BLOCK : 0;
        portEXIT_CRITICAL(&mux);
        if (stop)
            xQueueReset(playback); /* Single task owns dequeue/reset. */
        bool have = !stop && xQueueReceive(playback, &b, 0) == pdTRUE;
        for (int i = 0; i < BLOCK; ++i) {
            int32_t pcm = have && i < b.count ? b.pcm[i] : 0;
            if (!stop && remaining > (unsigned)i)
                pcm = (((phase++ / 18) & 1) ? 3000 : -3000);
            pcm = pcm * (int)gain / 100;
            output[i * 2] = output[i * 2 + 1] = pcm * 65536;
        }
        size_t written = 0;
        esp_err_t write = i2s_channel_write(tx, output, sizeof(output), &written, 20);
        portENTER_CRITICAL(&mux);
        status.playing = have || remaining > 0;
        if (write != ESP_OK || written != sizeof(output))
            ++status.io_errors;
        portEXIT_CRITICAL(&mux);
        if (read != ESP_OK && write != ESP_OK)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
}
esp_err_t audio_init(const settings_t *cfg) {
    if (!cfg->audio_enabled)
        return ESP_ERR_NOT_SUPPORTED;
    right_channel = cfg->mic_right;
    volume = cfg->volume;
    playback = xQueueCreate(12, sizeof(pcm_block_t));
    if (!playback)
        return ESP_ERR_NO_MEM;
    bool tx_on = false, rx_on = false;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 6;
    channel.dma_frame_num = BLOCK;
    esp_err_t e = i2s_new_channel(&channel, &tx, &rx);
    if (e != ESP_OK)
        goto fail;
    i2s_std_config_t std = {.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
                            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                                I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
                            .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                                         .bclk = BOARD.i2s_bclk,
                                         .ws = BOARD.i2s_ws,
                                         .dout = BOARD.speaker_din,
                                         .din = BOARD.mic_sd}};
    if ((e = i2s_channel_init_std_mode(tx, &std)) != ESP_OK)
        goto fail;
    if ((e = i2s_channel_init_std_mode(rx, &std)) != ESP_OK)
        goto fail;
    i2s_event_callbacks_t callbacks = {.on_recv_q_ovf = receive_overflow};
    if ((e = i2s_channel_register_event_callback(rx, &callbacks, NULL)) != ESP_OK)
        goto fail;
    if ((e = i2s_channel_enable(tx)) != ESP_OK)
        goto fail;
    tx_on = true;
    if ((e = i2s_channel_enable(rx)) != ESP_OK)
        goto fail;
    rx_on = true;
    status.ready = true;
    if (xTaskCreate(engine, "audio_duplex", 6144, NULL, 4, NULL) != pdPASS) {
        e = ESP_ERR_NO_MEM;
        goto fail;
    }
    return ESP_OK;
fail:
    status.ready = false;
    if (rx_on) {
        esp_err_t cleanup = i2s_channel_disable(rx);
        if (cleanup != ESP_OK)
            ESP_LOGW("audio", "RX disable: %s", esp_err_to_name(cleanup));
    }
    if (tx_on) {
        esp_err_t cleanup = i2s_channel_disable(tx);
        if (cleanup != ESP_OK)
            ESP_LOGW("audio", "TX disable: %s", esp_err_to_name(cleanup));
    }
    if (rx) {
        esp_err_t cleanup = i2s_del_channel(rx);
        if (cleanup != ESP_OK)
            ESP_LOGW("audio", "RX release: %s", esp_err_to_name(cleanup));
        rx = NULL;
    }
    if (tx) {
        esp_err_t cleanup = i2s_del_channel(tx);
        if (cleanup != ESP_OK)
            ESP_LOGW("audio", "TX release: %s", esp_err_to_name(cleanup));
        tx = NULL;
    }
    vQueueDelete(playback);
    playback = NULL;
    return e;
}
