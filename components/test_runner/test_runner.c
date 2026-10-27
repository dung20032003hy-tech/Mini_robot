#include "test_runner.h"
#include "safety_manager.h"
#include "robot_eyes.h"
#include "audio_input.h"
#include "audio_output.h"
#include "wifi_manager.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
esp_err_t test_run(const char *name, bool confirmed, char *message, size_t size) {
    esp_err_t e = ESP_ERR_NOT_SUPPORTED;
    snprintf(message, size, "Unknown diagnostic command");
    const char *motors[] = {"test_motor_lf", "test_motor_lr", "test_motor_rf", "test_motor_rr"};
    for (unsigned i = 0; i < 4; ++i)
        if (!strcmp(name, motors[i])) {
            e = safety_motor_test(i, confirmed);
            snprintf(message, size,
                     "200 ms low-duty pulse requested; observe rotation manually (no encoder)");
            return e;
        }
    if (!strcmp(name, "test_servo_left") || !strcmp(name, "test_servo_right")) {
        safety_status_t s;
        safety_snapshot(&s);
        unsigned i = !strcmp(name, "test_servo_left") ? 0 : 1;
        e = safety_servo_enable(true, confirmed);
        float angle[2] = {s.servo_commanded[0], s.servo_commanded[1]};
        angle[i] += 2;
        if (e == ESP_OK)
            e = safety_arms(angle[0], angle[1]);
        if (e != ESP_OK) {
            esp_err_t off = safety_servo_enable(false, false);
            if (off != ESP_OK)
                return off;
        }
        snprintf(message, size, "2 degree command requested; physical angle unmeasured");
        return e;
    }
    if (!strcmp(name, "test_lcd")) {
        eyes_test();
        e = eyes_expression("normal");
        snprintf(message, size, "Inspect RGB bars, border, offsets and orientation");
    }
    if (!strcmp(name, "test_microphone")) {
        e = audio_capture(true);
        snprintf(message, size,
                 "Capture started; inspect peak/clipping and listen to captured PCM");
    }
    if (!strcmp(name, "test_speaker")) {
        e = audio_output_tone();
        snprintf(message, size, "Tone requested; confirm sound manually");
    }
    if (!strcmp(name, "test_wifi")) {
        wifi_status_t s;
        wifi_manager_snapshot(&s);
        e = s.ap_ip[0] ? ESP_OK : ESP_ERR_INVALID_STATE;
        snprintf(message, size, "AP %s; STA connected=%d (connectivity only)", s.ap_ip,
                 s.station_connected);
    }
    if (!strcmp(name, "test_memory")) {
        bool good = heap_caps_check_integrity_all(true);
        e = good ? ESP_OK : ESP_FAIL;
        snprintf(message, size, "Heap integrity %s; PSRAM free=%u bytes", good ? "OK" : "ERROR",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    if (!strcmp(name, "test_safety")) {
        safety_estop();
        e = ESP_OK;
        snprintf(message, size,
                 "E-STOP latched; verify motor stops, then explicit reset and re-arm");
    }
    return e;
}
