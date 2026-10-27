#include "command_protocol.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

bool protocol_json_frame_valid(const char *data, size_t len) {
    if (!data || !len || memchr(data, 0, len))
        return false;
    unsigned depth = 0;
    bool quoted = false;
    for (size_t i = 0; i < len; ++i) {
        char c = data[i];
        if (quoted && c == '\\') {
            if (i + 5 < len && !memcmp(data + i + 1, "u0000", 5))
                return false;
            if (++i >= len)
                return false;
            continue;
        }
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (quoted)
            continue;
        if (c == '{' || c == '[') {
            if (++depth > 8)
                return false;
        }
        if (c == '}' || c == ']') {
            if (depth == 0)
                return false;
            --depth;
        }
    }
    return !quoted && depth == 0;
}

bool protocol_json_unique(const cJSON *j) {
    if (!j)
        return false;
    for (const cJSON *a = j->child; a; a = a->next) {
        if (cJSON_IsObject(j))
            for (const cJSON *b = a->next; b; b = b->next)
                if (a->string && b->string && !strcmp(a->string, b->string))
                    return false;
        if (a->child && !protocol_json_unique(a))
            return false;
    }
    return true;
}
static const cJSON *get(const cJSON *j, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(j, key);
}
static bool number(const cJSON *j, const char *key, float lo, float hi, float *out) {
    const cJSON *v = get(j, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < lo ||
        v->valuedouble > hi)
        return false;
    *out = v->valuedouble;
    return true;
}
static bool text(const cJSON *j, const char *key, char *out, size_t size) {
    const cJSON *v = get(j, key);
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= size)
        return false;
    strcpy(out, v->valuestring);
    return true;
}
static bool boolean(const cJSON *j, const char *key, bool *out) {
    const cJSON *v = get(j, key);
    if (!cJSON_IsBool(v))
        return false;
    *out = cJSON_IsTrue(v);
    return true;
}
static bool fields(const cJSON *j, const char *extra) {
    for (const cJSON *v = j->child; v; v = v->next) {
        if (!strcmp(v->string, "v") || !strcmp(v->string, "seq") || !strcmp(v->string, "type"))
            continue;
        char key[80];
        if (strlen(v->string) > 60 || strchr(v->string, '|'))
            return false;
        snprintf(key, sizeof(key), "|%s|", v->string);
        if (!strstr(extra, key))
            return false;
    }
    return true;
}
cJSON *protocol_usb_parse(const char *data, size_t len, uint32_t *sequence) {
    if (!sequence || len > 1024 || !protocol_json_frame_valid(data, len))
        return NULL;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(data, len, &end, false);
    if (!j)
        return NULL;
    while (end < data + len && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
        ++end;
    char type[32];
    const cJSON *version = get(j, "v"), *seq = get(j, "seq");
    bool valid = end == data + len && cJSON_IsObject(j) && protocol_json_unique(j) &&
                 text(j, "type", type, sizeof(type)) && cJSON_IsNumber(version) &&
                 version->valuedouble == 1 && cJSON_IsNumber(seq) &&
                 isfinite(seq->valuedouble) && seq->valuedouble >= 1 &&
                 seq->valuedouble <= 2147483647 && floor(seq->valuedouble) == seq->valuedouble;
    if (valid) {
        if (!strcmp(type, "usb_settings_patch"))
            valid = fields(j, "|patch|") && cJSON_IsObject(get(j, "patch"));
        else if (!strcmp(type, "usb_ai_config"))
            valid = fields(j, "|config|") && cJSON_IsObject(get(j, "config"));
        else if (!strcmp(type, "usb_ai_pause"))
            valid = fields(j, "|paused|") && cJSON_IsBool(get(j, "paused"));
        else if (!strcmp(type, "usb_hello"))
            valid = fields(j, "") && seq->valuedouble == 1;
        else if (!strcmp(type, "usb_status") || !strcmp(type, "usb_settings_get") ||
                 !strcmp(type, "usb_reboot"))
            valid = fields(j, "");
        else
            valid = false;
    }
    if (!valid) {
        cJSON_Delete(j);
        return NULL;
    }
    *sequence = seq->valueint;
    return j;
}
bool protocol_parse(const char *data, size_t len, command_t *out) {
    if (len > 1024 || !protocol_json_frame_valid(data, len))
        return false;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(data, len, &end, false);
    if (!j)
        return false;
    bool ok = false;
    command_t c = {.speed = 1};
    char type[32];
    if (!cJSON_IsObject(j) || !protocol_json_unique(j) || !text(j, "type", type, sizeof(type)))
        goto done;
    while (end < data + len && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
        ++end;
    if (end != data + len)
        goto done;
    const cJSON *v = get(j, "v"), *seq = get(j, "seq");
    if (!cJSON_IsNumber(v) || v->valuedouble != 1 || !cJSON_IsNumber(seq) ||
        !isfinite(seq->valuedouble) || seq->valuedouble < 1 || seq->valuedouble > 2147483647 ||
        floor(seq->valuedouble) != seq->valuedouble)
        goto done;
    c.seq = seq->valueint;
#define SIMPLE(name, id)                                                                           \
    if (!strcmp(type, name)) {                                                                     \
        c.kind = id;                                                                               \
        ok = fields(j, "");                                                                        \
        goto done;                                                                                 \
    }
    SIMPLE("claim", CMD_CLAIM)
    SIMPLE("release", CMD_RELEASE) SIMPLE("heartbeat", CMD_HEARTBEAT) SIMPLE("disarm", CMD_DISARM)
        SIMPLE("estop", CMD_ESTOP) if (!strcmp(type, "auth")) {
        c.kind = CMD_AUTH;
        ok = fields(j, "|token|") && text(j, "token", c.value, sizeof(c.value)) &&
             strlen(c.value) == 64;
        goto done;
    }
    if (!strcmp(type, "arm") || !strcmp(type, "reset_estop")) {
        c.kind = !strcmp(type, "arm") ? CMD_ARM : CMD_RESET_ESTOP;
        ok = fields(j, "|confirm|") && text(j, "confirm", c.value, sizeof(c.value)) &&
             !strcmp(c.value, "WHEELS_LIFTED");
        c.confirmed = ok;
        goto done;
    }
    if (!strcmp(type, "drive")) {
        c.kind = CMD_DRIVE;
        ok = fields(j, "|throttle||steering||speed|") &&
             number(j, "throttle", -1, 1, &c.throttle) && number(j, "steering", -1, 1, &c.steering);
        if (get(j, "speed"))
            ok = ok && number(j, "speed", 0, 1, &c.speed);
        goto done;
    }
    if (!strcmp(type, "motors")) {
        c.kind = CMD_MOTORS;
        const cJSON *a = get(j, "values");
        ok = fields(j, "|values|") && cJSON_IsArray(a) && cJSON_GetArraySize(a) == 4;
        if (ok)
            for (int i = 0; i < 4; ++i) {
                const cJSON *p = cJSON_GetArrayItem(a, i);
                if (!cJSON_IsNumber(p) || !isfinite(p->valuedouble) || fabs(p->valuedouble) > 1) {
                    ok = false;
                    break;
                }
                c.motors[i] = p->valuedouble;
            }
        goto done;
    }
    if (!strcmp(type, "arms")) {
        c.kind = CMD_ARMS;
        ok = fields(j, "|left_deg||right_deg|") && number(j, "left_deg", 0, 180, &c.left) &&
             number(j, "right_deg", 0, 180, &c.right);
        goto done;
    }
    if (!strcmp(type, "servo_enable")) {
        c.kind = CMD_SERVO_ENABLE;
        ok = fields(j, "|enabled||confirm|") && boolean(j, "enabled", &c.enabled);
        if (c.enabled) {
            ok = ok && text(j, "confirm", c.value, sizeof(c.value)) &&
                 !strcmp(c.value, "ARMS_CLEAR");
            c.confirmed = ok;
        }
        goto done;
    }
    if (!strcmp(type, "expression")) {
        c.kind = CMD_EXPRESSION;
        ok = fields(j, "|value|") && text(j, "value", c.value, sizeof(c.value));
        goto done;
    }
    if (!strcmp(type, "look")) {
        c.kind = CMD_LOOK;
        ok = fields(j, "|x||y|") && number(j, "x", -1, 1, &c.x) && number(j, "y", -1, 1, &c.y);
        goto done;
    }
    if (!strcmp(type, "eyes_options")) {
        c.kind = CMD_EYES_OPTIONS;
        float b;
        ok = fields(j, "|auto_blink||brightness|") && boolean(j, "auto_blink", &c.auto_blink) &&
             number(j, "brightness", 0, 100, &b);
        if (ok) {
            ok = floorf(b) == b;
            c.brightness = b;
        }
        goto done;
    }
    if (!strcmp(type, "audio")) {
        c.kind = CMD_AUDIO;
        float vol;
        ok = fields(j, "|action||volume|") && text(j, "action", c.value, sizeof(c.value)) &&
             number(j, "volume", 0, 100, &vol);
        if (ok) {
            ok = floorf(vol) == vol;
            c.volume = vol;
        }
        goto done;
    }
    if (!strcmp(type, "sleep")) {
        c.kind = CMD_SLEEP;
        ok = fields(j, "|enabled|") && boolean(j, "enabled", &c.enabled);
        goto done;
    }
    if (!strcmp(type, "test")) {
        c.kind = CMD_TEST;
        char confirm[32];
        ok = fields(j, "|name||confirm|") && text(j, "name", c.value, sizeof(c.value)) &&
             text(j, "confirm", confirm, sizeof(confirm)) && !strcmp(confirm, "WHEELS_LIFTED");
        c.confirmed = ok;
        goto done;
    }
done:
    cJSON_Delete(j);
    if (ok)
        *out = c;
    return ok;
#undef SIMPLE
}
