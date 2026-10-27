#pragma once
#include "settings_manager.h"
esp_err_t servo_init(const settings_t *settings);
esp_err_t servo_set_enabled(bool enabled);
esp_err_t servo_tick(const float target[2], float dt, float commanded[2]);
