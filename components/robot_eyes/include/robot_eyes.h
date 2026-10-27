#pragma once
#include "settings_manager.h"
esp_err_t eyes_init(const settings_t *cfg);
esp_err_t eyes_expression(const char *name);
void eyes_look(float x, float y);
void eyes_options(bool auto_blink, unsigned brightness);
void eyes_test(void);
void eyes_notice(const char *message);
const char *eyes_name(void);
