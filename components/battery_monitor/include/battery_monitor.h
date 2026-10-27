#pragma once
#include "settings_manager.h"
typedef struct {
    bool available, calibrated;
    float voltage;
} battery_status_t;
esp_err_t battery_init(const settings_t *settings);
void battery_snapshot(battery_status_t *out);
