#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"

#define SETTINGS_SCHEMA 2
typedef struct {
    float min_deg, max_deg, home_deg, speed;
    uint16_t min_us, max_us;
    bool invert;
} servo_settings_t;
typedef struct {
    uint32_t schema;
    bool hardware_confirmed, lcd_enabled, audio_enabled, battery_enabled;
    bool motor_invert[4];
    float motor_trim[4], motor_limit, accel, decel;
    uint32_t pwm_hz, timeout_ms, reverse_dead_ms;
    servo_settings_t servo[2];
    uint16_t lcd_width, lcd_height, lcd_x_offset, lcd_y_offset;
    uint8_t lcd_rotation, brightness, volume;
    bool lcd_bgr, lcd_invert, mic_right, auto_blink;
    float battery_ratio, battery_low, battery_critical;
    char wifi_ssid[33], wifi_password[65];
    char ap_password[25], token[65];
} settings_t;
esp_err_t settings_init(settings_t *out);
void settings_defaults(settings_t *s);
bool settings_validate(const settings_t *s);
esp_err_t settings_save(const settings_t *s);
esp_err_t settings_factory_reset(void);
cJSON *settings_public_json(const settings_t *s);
bool settings_patch(settings_t *s, const cJSON *json);
