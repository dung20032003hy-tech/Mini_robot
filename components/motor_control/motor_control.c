#include "motor_control.h"
#include "board_config.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "control_core.h"
#include "esp_timer.h"
#include <math.h>

static bool ready, enabled;
static int64_t wake_at;
static settings_t cfg;
static esp_err_t duty(int ch, uint32_t value) {
    esp_err_t e = ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, value);
    return e == ESP_OK ? ledc_update_duty(LEDC_LOW_SPEED_MODE, ch) : e;
}
esp_err_t motor_init(const settings_t *s) {
    cfg = *s;
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                 .duty_resolution = LEDC_TIMER_10_BIT,
                                 .timer_num = LEDC_TIMER_0,
                                 .freq_hz = s->pwm_hz,
                                 .clk_cfg = LEDC_AUTO_CLK};
    esp_err_t e = ledc_timer_config(&timer);
    if (e != ESP_OK)
        return e;
    for (int i = 0; i < 8; ++i) {
        ledc_channel_config_t ch = {.gpio_num = BOARD.motor[i / 2][i % 2],
                                    .speed_mode = LEDC_LOW_SPEED_MODE,
                                    .channel = i,
                                    .intr_type = LEDC_INTR_DISABLE,
                                    .timer_sel = LEDC_TIMER_0,
                                    .duty = 0,
                                    .hpoint = 0};
        if ((e = ledc_channel_config(&ch)) != ESP_OK)
            return e;
    }
    ready = true;
    return motor_stop(MOTOR_SLEEP);
}
esp_err_t motor_stop(motor_stop_t mode) {
    esp_err_t first = ESP_OK;
    if (ready)
        for (int i = 0; i < 8; ++i) {
            /* Static 11 is brake; 1023/1024 PWM would still contain coast slots. */
            esp_err_t e = ledc_stop(LEDC_LOW_SPEED_MODE, i, mode == MOTOR_BRAKE ? 1 : 0);
            if (e != ESP_OK)
                first = e;
        }
    if (mode == MOTOR_SLEEP) {
        for (int i = 0; i < 2; ++i)
            if (BOARD.sleep[i] >= 0) {
                esp_err_t e = gpio_set_level(BOARD.sleep[i], 0);
                if (e != ESP_OK)
                    first = e;
            }
        enabled = false;
    }
    return first;
}
esp_err_t motor_enable(bool enable) {
    if (!enable)
        return motor_stop(MOTOR_SLEEP);
    if (!ready)
        return ESP_ERR_INVALID_STATE;
    if (!enabled) {
        esp_err_t e = motor_stop(MOTOR_COAST);
        if (e != ESP_OK)
            return e;
        for (int i = 0; i < 2; ++i)
            if (BOARD.sleep[i] >= 0) {
                e = gpio_set_level(BOARD.sleep[i], 1);
                if (e != ESP_OK)
                    return e;
            }
        wake_at = esp_timer_get_time() + 2000;
        enabled = true;
    }
    return ESP_OK;
}
esp_err_t motor_update(const float command[4]) {
    if (!ready || !enabled)
        return ESP_ERR_INVALID_STATE;
    for (int i = 0; i < 4; ++i)
        if (!isfinite(command[i]) || fabsf(command[i]) > cfg.motor_limit + 0.001f)
            return ESP_ERR_INVALID_ARG;
    if (esp_timer_get_time() < wake_at)
        return ESP_OK; /* Nonblocking nSLEEP wake delay. */
    for (int i = 0; i < 4; ++i) {
        float v =
            core_motor_output(command[i], cfg.motor_invert[i], cfg.motor_trim[i], cfg.motor_limit);
        uint32_t pwm = (uint32_t)(fabsf(v) * 1023);
        /* Clear the inactive pin FIRST. IN1/IN2 00 coast, 11 brake; never
         * introduce a brake pulse during a forward/reverse update. */
        esp_err_t e = duty(i * 2 + (v >= 0 ? 1 : 0), 0);
        if (e != ESP_OK)
            return e;
        e = duty(i * 2 + (v >= 0 ? 0 : 1), pwm);
        if (e != ESP_OK)
            return e;
    }
    return ESP_OK;
}
