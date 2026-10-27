#pragma once
#include "settings_manager.h"
#include "control_core.h"
typedef struct {
    core_state_t state;
    bool servo_enabled, low_battery, critical_battery, sleeping;
    float motor[4], servo_commanded[2];
    char reason[48];
    uint32_t command_age_ms, control_stack_free;
} safety_status_t;
esp_err_t safety_init(const settings_t *cfg, bool hardware_ready);
esp_err_t safety_arm(bool confirmed);
void safety_disarm(const char *reason);
void safety_estop(void);
esp_err_t safety_reset_estop(bool confirmed);
esp_err_t safety_drive(const float motors[4]);
esp_err_t safety_motor_test(unsigned index, bool confirmed);
esp_err_t safety_arms(float left, float right);
esp_err_t safety_servo_enable(bool enable, bool confirmed);
void safety_battery(bool low, bool critical);
void safety_sleep(bool sleeping);
void safety_snapshot(safety_status_t *out);
bool safety_is_disarmed(void);
void safety_controller_lease(bool active);
void safety_controller_heartbeat(void);
void safety_maintenance(void);
