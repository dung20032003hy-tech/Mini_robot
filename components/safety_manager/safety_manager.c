#include "safety_manager.h"
#include "motor_control.h"
#include "servo_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static settings_t cfg;
static safety_status_t status;
static float target[4], arms[2];
static uint32_t last_command;
static uint32_t test_started;
static bool testing;
static bool hardware_ready;
static bool lease_active, maintenance;
static uint32_t last_heartbeat;
static TaskHandle_t control_task;
static void reason(const char *s) {
    snprintf(status.reason, sizeof(status.reason), "%s", s);
}
static uint32_t millis(void) {
    return esp_timer_get_time() / 1000;
}
void safety_snapshot(safety_status_t *out) {
    portENTER_CRITICAL(&mux);
    *out = status;
    portEXIT_CRITICAL(&mux);
}
bool safety_is_disarmed(void) {
    safety_status_t s;
    safety_snapshot(&s);
    return s.state != CORE_ARMED && !s.servo_enabled;
}
void safety_controller_lease(bool active) {
    portENTER_CRITICAL(&mux);
    lease_active = active;
    last_heartbeat = millis();
    portEXIT_CRITICAL(&mux);
}
void safety_controller_heartbeat(void) {
    portENTER_CRITICAL(&mux);
    last_heartbeat = millis();
    portEXIT_CRITICAL(&mux);
}
void safety_maintenance(void) {
    portENTER_CRITICAL(&mux);
    maintenance = true;
    portEXIT_CRITICAL(&mux);
    safety_disarm("Maintenance; reboot required");
}
void safety_disarm(const char *why) {
    portENTER_CRITICAL(&mux);
    if (status.state == CORE_ARMED)
        status.state = CORE_DISARMED;
    memset(target, 0, sizeof(target));
    status.servo_enabled = false;
    testing = false;
    reason(why);
    portEXIT_CRITICAL(&mux);
    if (control_task)
        xTaskNotifyGive(control_task);
}
void safety_estop(void) {
    portENTER_CRITICAL(&mux);
    status.state = CORE_ESTOP;
    status.servo_enabled = false;
    memset(target, 0, sizeof(target));
    testing = false;
    reason("Emergency stop latched");
    portEXIT_CRITICAL(&mux);
    if (control_task)
        xTaskNotifyGive(control_task);
}
esp_err_t safety_reset_estop(bool confirmed) {
    esp_err_t e = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&mux);
    if (confirmed && status.state == CORE_ESTOP && !status.critical_battery && hardware_ready) {
        status.state = core_transition(status.state, false, false, true, false, true);
        memset(target, 0, sizeof(target));
        reason("E-STOP reset; re-arm required");
        e = ESP_OK;
    }
    portEXIT_CRITICAL(&mux);
    return e;
}
esp_err_t safety_arm(bool confirmed) {
    esp_err_t e = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&mux);
    if (confirmed && cfg.hardware_confirmed && hardware_ready && !maintenance &&
        status.state == CORE_DISARMED && !status.critical_battery && !status.sleeping) {
        memset(target, 0, sizeof(target));
        last_command = millis();
        status.state = core_transition(status.state, false, false, false, true, true);
        reason("Armed; waiting for fresh drive");
        e = ESP_OK;
    }
    portEXIT_CRITICAL(&mux);
    return e;
}
esp_err_t safety_drive(const float m[4]) {
    for (int i = 0; i < 4; ++i)
        if (!isfinite(m[i]) || fabsf(m[i]) > cfg.motor_limit + 0.0001f)
            return ESP_ERR_INVALID_ARG;
    esp_err_t e = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&mux);
    if (status.state == CORE_ARMED && !status.critical_battery && !status.sleeping) {
        memcpy(target, m, sizeof(target));
        last_command = millis();
        testing = false;
        e = ESP_OK;
    }
    portEXIT_CRITICAL(&mux);
    return e;
}
esp_err_t safety_motor_test(unsigned index, bool confirmed) {
    if (index >= 4)
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = safety_arm(confirmed);
    if (e != ESP_OK)
        return e;
    portENTER_CRITICAL(&mux);
    if (status.state == CORE_ARMED) {
        memset(target, 0, sizeof(target));
        target[index] = fminf(cfg.motor_limit, 0.15f);
        testing = true;
        test_started = millis();
    } else
        e = ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL(&mux);
    return e;
}
esp_err_t safety_servo_enable(bool on, bool confirmed) {
    esp_err_t e = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&mux);
    if (!on) {
        status.servo_enabled = false;
        e = ESP_OK;
    } else if (confirmed && cfg.hardware_confirmed && hardware_ready && !maintenance &&
               status.state != CORE_ESTOP && status.state != CORE_FAULT &&
               !status.critical_battery && !status.sleeping) {
        status.servo_enabled = true;
        e = ESP_OK;
    }
    portEXIT_CRITICAL(&mux);
    return e;
}
esp_err_t safety_arms(float left, float right) {
    const float a[] = {left, right};
    for (int i = 0; i < 2; ++i)
        if (!isfinite(a[i]) || a[i] < cfg.servo[i].min_deg || a[i] > cfg.servo[i].max_deg)
            return ESP_ERR_INVALID_ARG;
    esp_err_t e = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&mux);
    if (status.servo_enabled && status.state != CORE_ESTOP && status.state != CORE_FAULT &&
        !status.critical_battery) {
        arms[0] = left;
        arms[1] = right;
        e = ESP_OK;
    }
    portEXIT_CRITICAL(&mux);
    return e;
}
void safety_battery(bool low, bool critical) {
    portENTER_CRITICAL(&mux);
    status.low_battery = low;
    status.critical_battery = critical;
    portEXIT_CRITICAL(&mux);
    if (critical)
        safety_disarm("Critical battery");
}
void safety_sleep(bool sleeping) {
    if (sleeping)
        safety_disarm("Sleeping");
    portENTER_CRITICAL(&mux);
    status.sleeping = sleeping;
    portEXIT_CRITICAL(&mux);
}
static void control(void *arg) {
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ramp_t ramps[4] = {0};
    uint32_t previous = millis();
    bool servo_on = false;
    while (true) {
        uint32_t now = millis();
        float dt = core_clamp((now - previous) / 1000.0f, 0.001f, 0.02f);
        previous = now;
        /* This short critical section serializes safety changes with hardware
         * writes: once E-STOP returns, no stale target can re-enable outputs.
         * LEDC/MCPWM calls below do not wait for network, DMA or NVS. */
        portENTER_CRITICAL(&mux);
        if (status.critical_battery || status.sleeping || maintenance) {
            if (status.state == CORE_ARMED)
                status.state = CORE_DISARMED;
            memset(target, 0, sizeof(target));
            status.servo_enabled = false;
            testing = false;
        }
        if (lease_active && core_expired(now, last_heartbeat, 1000)) {
            lease_active = false;
            if (status.state == CORE_ARMED)
                status.state = CORE_DISARMED;
            memset(target, 0, sizeof(target));
            status.servo_enabled = false;
            reason("Controller heartbeat timeout; re-arm");
        }
        if (status.state == CORE_ARMED && core_expired(now, last_command, cfg.timeout_ms)) {
            status.state = CORE_DISARMED;
            memset(target, 0, sizeof(target));
            reason("Motor command timeout; re-arm");
        }
        if (testing && status.state == CORE_ARMED && core_expired(now, test_started, 200)) {
            testing = false;
            status.state = CORE_DISARMED;
            memset(target, 0, sizeof(target));
            reason("Motor test pulse ended; inspect physically");
        }
        esp_err_t e = ESP_OK;
        if (status.state != CORE_ARMED) {
            e = motor_stop(MOTOR_SLEEP);
            for (int i = 0; i < 4; ++i) {
                if (!ramps[i].at_zero) {
                    ramps[i].zero_since = now;
                    ramps[i].at_zero = true;
                }
                ramps[i].actual = 0;
            }
            memset(status.motor, 0, sizeof(status.motor));
        } else {
            e = motor_enable(true);
            for (int i = 0; i < 4; ++i)
                status.motor[i] = core_ramp(&ramps[i], target[i], cfg.accel, cfg.decel,
                                            cfg.reverse_dead_ms, now, dt);
            if (e == ESP_OK)
                e = motor_update(status.motor);
        }
        if (status.servo_enabled != servo_on) {
            esp_err_t se = servo_set_enabled(status.servo_enabled);
            servo_on = status.servo_enabled;
            if (e == ESP_OK)
                e = se;
        }
        if (hardware_ready) {
            esp_err_t se = servo_tick(arms, dt, status.servo_commanded);
            if (e == ESP_OK)
                e = se;
        }
        if (e != ESP_OK) {
            hardware_ready = false;
            testing = false;
            status.state = CORE_FAULT;
            status.servo_enabled = false;
            reason("Actuator driver error; reboot needed");
            esp_err_t stop = motor_stop(MOTOR_SLEEP), off = servo_set_enabled(false);
            if (stop != ESP_OK ||
                off != ESP_OK) { /* Physical pull-down/nSLEEP is still required. */
                status.state = CORE_FAULT;
            }
        }
        status.command_age_ms = now - last_command;
        portEXIT_CRITICAL(&mux);
        uint32_t stack_free = uxTaskGetStackHighWaterMark(NULL);
        portENTER_CRITICAL(&mux);
        status.control_stack_free = stack_free;
        portEXIT_CRITICAL(&mux);
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
    }
}
esp_err_t safety_init(const settings_t *s, bool ready) {
    cfg = *s;
    hardware_ready = ready;
    status.state = ready ? CORE_DISARMED : CORE_FAULT;
    reason(ready ? "Ready; actuators disarmed" : "Actuator initialization failed");
    for (int i = 0; i < 2; ++i) {
        arms[i] = s->servo[i].home_deg;
        status.servo_commanded[i] = arms[i];
    }
    return xTaskCreate(control, "control_safety", 4096, NULL, 10, &control_task) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}
