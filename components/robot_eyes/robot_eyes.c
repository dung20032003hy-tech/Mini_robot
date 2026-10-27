#include "robot_eyes.h"
#include "lcd_display.h"
#include "safety_manager.h"
#include "diagnostics.h"
#include "control_core.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *names[] = {"normal",    "happy",    "sad",   "angry",
                              "surprised", "blink",    "sleep", "listening",
                              "thinking",  "speaking", "error", "low_battery"};
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static int expression, brightness = 80;
static float look_x, look_y;
static bool blink = true, test_pattern, ready;
static char notice[9] = "BOOT";
static uint32_t notice_until = 3000;
static uint16_t *frame;
const char *eyes_name(void) {
    portENTER_CRITICAL(&mux);
    int e = expression;
    portEXIT_CRITICAL(&mux);
    return names[e];
}
esp_err_t eyes_expression(const char *name) {
    for (int i = 0; i < 12; ++i)
        if (!strcmp(name, names[i])) {
            portENTER_CRITICAL(&mux);
            expression = i;
            portEXIT_CRITICAL(&mux);
            return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
        }
    return ESP_ERR_INVALID_ARG;
}
void eyes_look(float x, float y) {
    portENTER_CRITICAL(&mux);
    look_x = core_clamp(x, -1, 1);
    look_y = core_clamp(y, -1, 1);
    portEXIT_CRITICAL(&mux);
}
void eyes_options(bool b, unsigned light) {
    portENTER_CRITICAL(&mux);
    blink = b;
    brightness = light > 100 ? 100 : light;
    portEXIT_CRITICAL(&mux);
}
void eyes_test(void) {
    portENTER_CRITICAL(&mux);
    test_pattern = true;
    portEXIT_CRITICAL(&mux);
}
void eyes_notice(const char *message) {
    portENTER_CRITICAL(&mux);
    strncpy(notice, message, sizeof(notice) - 1);
    notice[sizeof(notice) - 1] = 0;
    notice_until = esp_timer_get_time() / 1000 + 3000;
    portEXIT_CRITICAL(&mux);
}
static uint8_t glyph(char c, int column) {
    const char letters[] = "ABEFIOPRTWY ";
    const uint8_t pixels[][5] = {{0x7e, 0x09, 0x09, 0x09, 0x7e},
                                 {0x7f, 0x49, 0x49, 0x49, 0x36},
                                 {0x7f, 0x49, 0x49, 0x49, 0x41},
                                 {0x7f, 0x09, 0x09, 0x09, 0x01},
                                 {0, 0x41, 0x7f, 0x41, 0},
                                 {0x3e, 0x41, 0x41, 0x41, 0x3e},
                                 {0x7f, 0x09, 0x09, 0x09, 0x06},
                                 {0x7f, 0x09, 0x19, 0x29, 0x46},
                                 {1, 1, 0x7f, 1, 1},
                                 {0x3f, 0x40, 0x38, 0x40, 0x3f},
                                 {7, 8, 0x70, 8, 7},
                                 {0, 0, 0, 0, 0}};
    const char *p = strchr(letters, c);
    return p ? pixels[p - letters][column] : 0;
}
static void display(void *arg) {
    int w = lcd_width(), h = lcd_height();
    float opening = 0, x = 0, y = 0;
    uint32_t next_blink = 2000, close_until = 0, test_until = 0;
    while (true) {
        uint32_t now = esp_timer_get_time() / 1000;
        int e, light;
        float tx, ty;
        bool automatic, test;
        char label[9];
        uint32_t label_until;
        portENTER_CRITICAL(&mux);
        e = expression;
        light = brightness;
        tx = look_x;
        ty = look_y;
        automatic = blink;
        test = test_pattern;
        test_pattern = false;
        memcpy(label, notice, sizeof(label));
        label_until = notice_until;
        portEXIT_CRITICAL(&mux);
        safety_status_t s;
        safety_snapshot(&s);
        if (s.state == CORE_ESTOP || s.state == CORE_FAULT)
            e = 10;
        else if (s.low_battery)
            e = 11;
        else if (s.sleeping)
            e = 6;
        if (automatic && now >= next_blink) {
            close_until = now + 130;
            next_blink = now + 2500 + esp_random() % 3500;
        }
        if (test)
            test_until = now + 3000;
        float target = (e == 5 || e == 6 || now < close_until) ? 0.06f
                                                               : (e == 1   ? 0.45f
                                                                  : e == 2 ? 0.65f
                                                                  : e == 4 ? 1.25f
                                                                           : 1);
        opening += (target - opening) * 0.35f;
        x += (tx - x) * 0.2f;
        y += (ty - y) * 0.2f;
        memset(frame, 0, w * h * 2);
        int intensity = e == 6 ? light / 5 : light;
        uint16_t color =
            e == 10   ? (uint16_t)((31 * intensity / 100) << 11)
            : e == 11 ? (uint16_t)(((31 * intensity / 100) << 11) | ((40 * intensity / 100) << 5))
                      : (uint16_t)(((45 * intensity / 100) << 5) | (31 * intensity / 100));
        float ew = w * 0.22f, eh = h * 0.65f * opening;
        if (e == 7) {
            ew *= 1.1f;
            eh *= 0.88f + 0.12f * sinf(now / 180.0f);
        }
        if (e == 8)
            x = sinf(now / 500.0f) * 0.7f;
        if (e == 9)
            eh *= 0.85f + 0.15f * sinf(now / 80.0f);
        for (int eye = 0; eye < 2; ++eye) {
            float cx = w * (eye ? 0.73f : 0.27f) + x * w * 0.045f, cy = h * 0.5f + y * h * 0.1f;
            for (int py = 0; py < h; ++py)
                for (int px = 0; px < w; ++px) {
                    float dx = fabsf(px - cx), dy = fabsf(py - cy);
                    float radius = fmaxf(2, fminf(ew, eh) * 0.2f),
                          qx = fmaxf(0, dx - (ew / 2 - radius)),
                          qy = fmaxf(0, dy - (eh / 2 - radius));
                    bool inside =
                        dx <= ew / 2 && dy <= eh / 2 && (qx * qx + qy * qy <= radius * radius);
                    if (e == 3 && py < cy - eh * 0.25f + (eye ? -1 : 1) * (px - cx) * 0.35f)
                        inside = false;
                    if (e == 2 && py < cy - eh * 0.25f + (eye ? 1 : -1) * (px - cx) * 0.25f)
                        inside = false;
                    if (e == 1 && py > cy + eh * 0.1f - sinf((px - cx) / ew * 3.14f) * eh * 0.2f)
                        inside = false;
                    if (inside)
                        frame[py * w + px] = color;
                }
        }
        /* Compact boot/network status indicator and calibration border. */
        if (now < 3000)
            for (int px = 0; px < w * (int)now / 3000; ++px)
                frame[(h - 2) * w + px] = color;
        if (now < label_until)
            for (int i = 0; label[i]; ++i)
                for (int col = 0; col < 5; ++col)
                    for (int row = 0; row < 7; ++row) {
                        int px = 2 + i * 6 + col, py = 2 + row;
                        if (px < w && py < h && (glyph(label[i], col) & (1 << row)))
                            frame[py * w + px] = 0xffff;
                    }
        if (now < test_until) {
            const uint16_t bars[] = {0xf800, 0x07e0, 0x001f, 0xffff};
            for (int py = 0; py < h; ++py)
                for (int px = 0; px < w; ++px)
                    frame[py * w + px] = bars[px * 4 / w];
            for (int px = 0; px < w; ++px)
                frame[px] = frame[(h - 1) * w + px] = 0xffff;
            for (int py = 0; py < h; ++py)
                frame[py * w] = frame[py * w + w - 1] = 0xffff;
        }
        esp_err_t err = lcd_present(frame);
        if (err != ESP_OK) {
            diagnostics_record("lcd_runtime", err);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
esp_err_t eyes_init(const settings_t *cfg) {
    esp_err_t e = lcd_init(cfg);
    if (e != ESP_OK)
        return e;
    frame = heap_caps_malloc(lcd_width() * lcd_height() * 2, MALLOC_CAP_8BIT);
    if (!frame)
        return ESP_ERR_NO_MEM;
    brightness = cfg->brightness;
    blink = cfg->auto_blink;
    ready = xTaskCreate(display, "display", 4096, NULL, 2, NULL) == pdPASS;
    if (!ready) {
        heap_caps_free(frame);
        frame = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
