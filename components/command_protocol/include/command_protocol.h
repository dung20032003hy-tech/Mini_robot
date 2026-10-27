#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"
typedef enum {
    CMD_AUTH,
    CMD_CLAIM,
    CMD_RELEASE,
    CMD_HEARTBEAT,
    CMD_ARM,
    CMD_DISARM,
    CMD_DRIVE,
    CMD_MOTORS,
    CMD_ESTOP,
    CMD_RESET_ESTOP,
    CMD_ARMS,
    CMD_SERVO_ENABLE,
    CMD_EXPRESSION,
    CMD_LOOK,
    CMD_EYES_OPTIONS,
    CMD_AUDIO,
    CMD_SLEEP,
    CMD_TEST
} command_kind_t;
typedef struct {
    command_kind_t kind;
    uint32_t seq;
    float throttle, steering, speed, motors[4], left, right, x, y;
    bool enabled, confirmed, auto_blink;
    unsigned brightness, volume;
    char value[65];
} command_t;
bool protocol_parse(const char *data, size_t len, command_t *out);
bool protocol_json_unique(const cJSON *json);
bool protocol_json_frame_valid(const char *data, size_t len);
/* Returns an owned JSON object for the USB-only management envelope, or NULL.
 * The caller must cJSON_Delete it. Control commands still use protocol_parse. */
cJSON *protocol_usb_parse(const char *data, size_t len, uint32_t *sequence);
