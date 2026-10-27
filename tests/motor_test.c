#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "motor_control.h"
#include "board_config.h"
#include "driver/ledc.h"

const board_pins_t BOARD = {.motor = {{4, 5}, {6, 7}, {8, 9}, {10, 11}}, .sleep = {2, 38}};
static uint32_t output[8], clock_us;
static bool static_level[8];
static unsigned sleep_level[2], writes;
esp_err_t ledc_timer_config(const ledc_timer_config_t *t) {
    assert(t->freq_hz == 20000 && t->duty_resolution == 10);
    return ESP_OK;
}
esp_err_t ledc_channel_config(const ledc_channel_config_t *c) {
    assert(c->gpio_num == 4 + c->channel && c->duty == 0);
    return ESP_OK;
}
esp_err_t ledc_set_duty(int mode, int ch, uint32_t duty) {
    output[ch] = duty;
    ++writes;
    return ESP_OK;
}
esp_err_t ledc_update_duty(int mode, int ch) {
    static_level[ch] = false;
    return ESP_OK;
}
esp_err_t ledc_stop(int mode, int ch, uint32_t level) {
    output[ch] = level;
    static_level[ch] = true;
    return ESP_OK;
}
esp_err_t gpio_set_level(int pin, unsigned level) {
    assert(pin == 2 || pin == 38);
    sleep_level[pin == 2 ? 0 : 1] = level;
    return ESP_OK;
}
int64_t esp_timer_get_time(void) {
    return clock_us;
}
int main(void) {
    settings_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pwm_hz = 20000;
    cfg.motor_limit = 0.25;
    for (int i = 0; i < 4; ++i)
        cfg.motor_trim[i] = 1;
    assert(motor_init(&cfg) == ESP_OK);
    assert(sleep_level[0] == 0 && sleep_level[1] == 0);
    float m[4] = {0.2, 0.2, -0.2, -0.2};
    assert(motor_update(m) == ESP_ERR_INVALID_STATE);
    assert(motor_enable(true) == ESP_OK);
    assert(sleep_level[0] == 1 && sleep_level[1] == 1);
    unsigned previous = writes;
    assert(motor_update(m) == ESP_OK && writes == previous);
    clock_us = 2000;
    assert(motor_update(m) == ESP_OK);
    assert(output[0] == 204 && output[1] == 0 && output[4] == 0 && output[5] == 204);
    previous = writes;
    m[0] = NAN;
    assert(motor_update(m) == ESP_ERR_INVALID_ARG && writes == previous);
    m[0] = 0.3;
    assert(motor_update(m) == ESP_ERR_INVALID_ARG && writes == previous);
    assert(motor_stop(MOTOR_BRAKE) == ESP_OK);
    for (int i = 0; i < 8; ++i)
        assert(static_level[i] && output[i] == 1);
    assert(motor_stop(MOTOR_COAST) == ESP_OK);
    for (int i = 0; i < 8; ++i)
        assert(static_level[i] && output[i] == 0);
    assert(motor_stop(MOTOR_SLEEP) == ESP_OK && sleep_level[0] == 0 && sleep_level[1] == 0);
    cfg.motor_invert[0] = true;
    cfg.motor_trim[0] = 1.5;
    assert(motor_init(&cfg) == ESP_OK);
    assert(motor_enable(true) == ESP_OK);
    clock_us += 2000;
    m[0] = 0.25;
    assert(motor_update(m) == ESP_OK);
    assert(output[0] == 0 && output[1] == 255);
    puts("PASS: production DRV8833 driver mapping, safe init, enable/wake delay, pin polarity, "
         "NaN/duty rejection, static brake/coast, sleep, inversion/trim clamp (hardware adapters "
         "only)");
    return 0;
}
