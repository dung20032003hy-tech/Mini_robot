#pragma once
#include "settings_manager.h"
typedef enum { MOTOR_COAST, MOTOR_BRAKE, MOTOR_SLEEP } motor_stop_t;
esp_err_t motor_init(const settings_t *settings);
esp_err_t motor_enable(bool enable);
esp_err_t motor_update(const float command[4]);
esp_err_t motor_stop(motor_stop_t mode);
