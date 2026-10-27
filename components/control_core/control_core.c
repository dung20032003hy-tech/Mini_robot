#include "control_core.h"
#include <math.h>

float core_clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
bool core_mix(float t, float s, float limit, float out[4]) {
    if (!isfinite(t) || !isfinite(s) || !isfinite(limit) || fabsf(t) > 1 || fabsf(s) > 1 ||
        limit < 0 || limit > 1)
        return false;
    float l = t + s, r = t - s, n = fmaxf(1.0f, fmaxf(fabsf(l), fabsf(r)));
    out[0] = out[1] = l / n * limit;
    out[2] = out[3] = r / n * limit;
    return true;
}
bool core_expired(uint32_t now, uint32_t then, uint32_t timeout) {
    return (uint32_t)(now - then) >= timeout;
}
bool core_accept_sequence(uint32_t previous, uint32_t next) {
    return next > previous && next <= 2147483647U;
}
float core_ramp(ramp_t *r, float target, float accel, float decel, uint32_t dead, uint32_t now,
                float dt) {
    int sign = target > 0 ? 1 : (target < 0 ? -1 : 0);
    /* The remembered last direction survives zero commands, so stop/reverse
     * cannot bypass dead time. The interval starts when OUTPUT reaches zero. */
    bool reverse = sign && r->last_sign && sign != r->last_sign;
    float goal = target;
    if (reverse && (!r->at_zero || !core_expired(now, r->zero_since, dead)))
        goal = 0;
    float rate = (fabsf(goal) > fabsf(r->actual)) ? accel : decel;
    float change = core_clamp(goal - r->actual, -rate * dt, rate * dt);
    r->actual += change;
    if (fabsf(r->actual) < 0.00001f) {
        r->actual = 0;
        if (!r->at_zero) {
            r->at_zero = true;
            r->zero_since = now;
        }
    } else {
        r->at_zero = false;
        r->last_sign = r->actual > 0 ? 1 : -1;
    }
    return r->actual;
}
float core_motor_output(float logical, bool invert, float trim, float limit) {
    return core_clamp(logical * trim * (invert ? -1.0f : 1.0f), -limit, limit);
}
uint32_t core_servo_pulse(float deg, bool inv, float lo, float hi, uint32_t min, uint32_t max) {
    deg = core_clamp(deg, lo, hi);
    if (inv)
        deg = lo + hi - deg;
    /* Pulse calibration applies exactly to the configured mechanical range. */
    return min + (uint32_t)((deg - lo) / (hi - lo) * (float)(max - min) + 0.5f);
}
core_state_t core_transition(core_state_t s, bool estop, bool fault, bool reset, bool arm,
                             bool safe) {
    if (fault)
        return CORE_FAULT;
    if (estop)
        return CORE_ESTOP;
    if (s == CORE_FAULT)
        return s;
    if (s == CORE_ESTOP)
        return reset && safe ? CORE_DISARMED : s;
    if (!safe)
        return CORE_DISARMED;
    return arm ? CORE_ARMED : CORE_DISARMED;
}
