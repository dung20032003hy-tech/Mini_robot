#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float actual;
    int8_t last_sign;
    uint32_t zero_since;
    bool at_zero;
} ramp_t;
float core_clamp(float value, float low, float high);
bool core_mix(float throttle, float steering, float limit, float out[4]);
float core_ramp(ramp_t *r, float target, float accel, float decel, uint32_t dead_ms, uint32_t now,
                float dt);
float core_motor_output(float logical, bool invert, float trim, float limit);
uint32_t core_servo_pulse(float degrees, bool invert, float low, float high, uint32_t min_us,
                          uint32_t max_us);
bool core_expired(uint32_t now, uint32_t then, uint32_t timeout);
bool core_accept_sequence(uint32_t previous, uint32_t next);
typedef enum { CORE_DISARMED, CORE_ARMED, CORE_ESTOP, CORE_FAULT } core_state_t;
core_state_t core_transition(core_state_t state, bool estop, bool fault, bool reset, bool arm,
                             bool safe);
