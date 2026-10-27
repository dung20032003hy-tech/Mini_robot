#include "servo_control.h"
#include "board_config.h"
#include "control_core.h"
#include "driver/mcpwm_prelude.h"
#include <math.h>

static mcpwm_timer_handle_t timer;
static mcpwm_oper_handle_t oper;
static mcpwm_cmpr_handle_t comparator[2];
static mcpwm_gen_handle_t generator[2];
static settings_t cfg;
static float angle[2];
static bool enabled, ready;
esp_err_t servo_init(const settings_t *s) {
    cfg = *s;
    mcpwm_timer_config_t t = {.group_id = 1,
                              .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
                              .resolution_hz = 1000000,
                              .period_ticks = 20000,
                              .count_mode = MCPWM_TIMER_COUNT_MODE_UP};
    esp_err_t e = mcpwm_new_timer(&t, &timer);
    if (e != ESP_OK)
        return e;
    mcpwm_operator_config_t op = {.group_id = 1};
    if ((e = mcpwm_new_operator(&op, &oper)) != ESP_OK)
        return e;
    if ((e = mcpwm_operator_connect_timer(oper, timer)) != ESP_OK)
        return e;
    for (int i = 0; i < 2; ++i) {
        mcpwm_comparator_config_t c = {.flags.update_cmp_on_tez = true};
        if ((e = mcpwm_new_comparator(oper, &c, &comparator[i])) != ESP_OK)
            return e;
        mcpwm_generator_config_t g = {.gen_gpio_num = BOARD.servo[i]};
        if ((e = mcpwm_new_generator(oper, &g, &generator[i])) != ESP_OK)
            return e;
        if ((e = mcpwm_generator_set_force_level(generator[i], 0, true)) != ESP_OK)
            return e;
        if ((e = mcpwm_generator_set_action_on_timer_event(
                 generator[i],
                 MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY,
                                              MCPWM_GEN_ACTION_HIGH))) != ESP_OK)
            return e;
        if ((e = mcpwm_generator_set_action_on_compare_event(
                 generator[i],
                 MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator[i],
                                                MCPWM_GEN_ACTION_LOW))) != ESP_OK)
            return e;
        const servo_settings_t *a = &s->servo[i];
        angle[i] = a->home_deg;
        if ((e = mcpwm_comparator_set_compare_value(
                 comparator[i], core_servo_pulse(angle[i], a->invert, a->min_deg, a->max_deg,
                                                 a->min_us, a->max_us))) != ESP_OK)
            return e;
    }
    if ((e = mcpwm_timer_enable(timer)) != ESP_OK)
        return e;
    if ((e = mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP)) != ESP_OK)
        return e;
    ready = true;
    return ESP_OK; /* force LOW persists until explicit user arm. */
}
esp_err_t servo_set_enabled(bool on) {
    if (!ready)
        return on ? ESP_ERR_INVALID_STATE : ESP_OK;
    for (int i = 0; i < 2; ++i) {
        esp_err_t e = mcpwm_generator_set_force_level(generator[i], on ? -1 : 0, true);
        if (e != ESP_OK)
            return e;
    }
    enabled = on;
    return ESP_OK;
}
esp_err_t servo_tick(const float target[2], float dt, float commanded[2]) {
    if (!ready)
        return ESP_ERR_INVALID_STATE;
    for (int i = 0; i < 2; ++i) {
        const servo_settings_t *a = &cfg.servo[i];
        if (!isfinite(target[i]) || target[i] < a->min_deg || target[i] > a->max_deg)
            return ESP_ERR_INVALID_ARG;
        if (enabled) {
            angle[i] += core_clamp(target[i] - angle[i], -a->speed * dt, a->speed * dt);
            esp_err_t e = mcpwm_comparator_set_compare_value(
                comparator[i], core_servo_pulse(angle[i], a->invert, a->min_deg, a->max_deg,
                                                a->min_us, a->max_us));
            if (e != ESP_OK)
                return e;
        }
        commanded[i] = angle[i];
    }
    return ESP_OK;
}
