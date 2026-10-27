#pragma once
#include "settings_manager.h"
esp_err_t lcd_init(const settings_t *cfg);
int lcd_width(void);
int lcd_height(void);
esp_err_t lcd_present(const uint16_t *rgb565);
