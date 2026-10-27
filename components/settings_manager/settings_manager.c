#include "settings_manager.h"
#include <math.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include "esp_log.h"

void settings_defaults(settings_t *s) {
    memset(s, 0, sizeof(*s));
    s->schema = SETTINGS_SCHEMA;
    s->motor_limit = 0.25f;
    s->accel = 0.6f;
    s->decel = 1.5f;
    s->pwm_hz = 20000;
    s->timeout_ms = 400;
    s->reverse_dead_ms = 100;
    for (int i = 0; i < 4; ++i)
        s->motor_trim[i] = 1;
    for (int i = 0; i < 2; ++i)
        s->servo[i] = (servo_settings_t){60, 120, 90, 30, 1200, 1800, false};
    s->lcd_width = 76;
    s->lcd_height = 284;
    s->lcd_rotation = 1;
    s->brightness = 80;
    s->volume = 20;
    s->auto_blink = true;
    s->battery_ratio = 4.0f;
    s->battery_low = 7.0f;
    s->battery_critical = 6.6f;
}
static bool range(float x, float lo, float hi) {
    return isfinite(x) && x >= lo && x <= hi;
}
bool settings_validate(const settings_t *s) {
    if (s->schema != SETTINGS_SCHEMA || !range(s->motor_limit, 0.05f, 0.8f) ||
        !range(s->accel, 0.1f, 5) || !range(s->decel, 0.1f, 10) || s->pwm_hz < 18000 ||
        s->pwm_hz > 20000 || s->timeout_ms < 300 || s->timeout_ms > 500 ||
        s->reverse_dead_ms < 20 || s->reverse_dead_ms > 1000)
        return false;
    for (int i = 0; i < 4; ++i)
        if (!range(s->motor_trim[i], 0.5f, 1.5f))
            return false;
    for (int i = 0; i < 2; ++i) {
        const servo_settings_t *v = &s->servo[i];
        if (!range(v->min_deg, 0, 179) || !range(v->max_deg, v->min_deg + 1, 180) ||
            !range(v->home_deg, v->min_deg, v->max_deg) || !range(v->speed, 1, 90) ||
            v->min_us < 500 || v->max_us > 2500 || v->min_us >= v->max_us)
            return false;
    }
    if (s->lcd_width < 16 || s->lcd_width > 240 || s->lcd_height < 16 || s->lcd_height > 320 ||
        s->lcd_x_offset + s->lcd_width > 240 || s->lcd_y_offset + s->lcd_height > 320 ||
        s->lcd_rotation > 3 || s->brightness > 100 || s->volume > 100)
        return false;
    if (!range(s->battery_ratio, 3, 10) || !range(s->battery_critical, 6, 7.5f) ||
        !range(s->battery_low, s->battery_critical + 0.1f, 8.3f))
        return false;
    if (!memchr(s->wifi_ssid, 0, sizeof(s->wifi_ssid)) ||
        !memchr(s->wifi_password, 0, sizeof(s->wifi_password)) ||
        !memchr(s->ap_password, 0, sizeof(s->ap_password)) ||
        !memchr(s->token, 0, sizeof(s->token)))
        return false;
    size_t n = strlen(s->wifi_password);
    return (n == 0 || (n >= 8 && n <= 63)) && strlen(s->ap_password) >= 12 &&
           strlen(s->token) == 64;
}
static void random_hex(char *out, size_t n) {
    static const char h[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i)
        out[i] = h[esp_random() & 15];
    out[n] = 0;
}
esp_err_t settings_save(const settings_t *s) {
    if (!settings_validate(s))
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open("walle", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    e = nvs_set_blob(h, "settings_v2", s, sizeof(*s));
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    return e;
}
esp_err_t settings_init(settings_t *out) {
    esp_err_t e = nvs_flash_init();
    /* Never erase all NVS silently; a damaged partition requires explicit repair. */
    if (e != ESP_OK)
        return e;
    settings_t loaded;
    size_t len = sizeof(loaded);
    nvs_handle_t h;
    e = nvs_open("walle", NVS_READONLY, &h);
    if (e == ESP_OK) {
        e = nvs_get_blob(h, "settings_v2", &loaded, &len);
        nvs_close(h);
        if (e == ESP_OK && len == sizeof(loaded) && settings_validate(&loaded)) {
            *out = loaded;
            return ESP_OK;
        }
    }
    ESP_LOGW("settings", "Using safe defaults (absent/invalid schema); credentials never logged");
    settings_defaults(out);
    /* Called only during early boot, before ADC, I2S or radio initialization. */
    bootloader_random_enable();
    random_hex(out->ap_password, 20);
    random_hex(out->token, 64);
    bootloader_random_disable();
    return settings_save(out);
}
esp_err_t settings_factory_reset(void) {
    nvs_handle_t h;
    esp_err_t e = nvs_open("walle", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    e = nvs_erase_all(h);
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    return e;
}
cJSON *settings_public_json(const settings_t *s) {
    cJSON *o = cJSON_CreateObject();
    if (!o)
        return NULL;
#define NUM(k) cJSON_AddNumberToObject(o, #k, s->k)
#define BOOL(k) cJSON_AddBoolToObject(o, #k, s->k)
    NUM(schema);
    BOOL(hardware_confirmed);
    BOOL(lcd_enabled);
    BOOL(audio_enabled);
    BOOL(battery_enabled);
    NUM(motor_limit);
    NUM(accel);
    NUM(decel);
    NUM(pwm_hz);
    NUM(timeout_ms);
    NUM(reverse_dead_ms);
    NUM(lcd_width);
    NUM(lcd_height);
    NUM(lcd_x_offset);
    NUM(lcd_y_offset);
    NUM(lcd_rotation);
    NUM(brightness);
    NUM(volume);
    BOOL(lcd_bgr);
    BOOL(lcd_invert);
    BOOL(mic_right);
    BOOL(auto_blink);
    NUM(battery_ratio);
    NUM(battery_low);
    NUM(battery_critical);
    cJSON_AddStringToObject(o, "wifi_ssid", s->wifi_ssid);
    cJSON *inv = cJSON_AddArrayToObject(o, "motor_invert"),
          *trim = cJSON_AddArrayToObject(o, "motor_trim");
    for (int i = 0; i < 4; ++i) {
        cJSON_AddItemToArray(inv, cJSON_CreateBool(s->motor_invert[i]));
        cJSON_AddItemToArray(trim, cJSON_CreateNumber(s->motor_trim[i]));
    }
    cJSON *arms = cJSON_AddArrayToObject(o, "servo");
    for (int i = 0; i < 2; ++i) {
        cJSON *v = cJSON_CreateObject();
        const servo_settings_t *a = &s->servo[i];
        cJSON_AddNumberToObject(v, "min_deg", a->min_deg);
        cJSON_AddNumberToObject(v, "max_deg", a->max_deg);
        cJSON_AddNumberToObject(v, "home_deg", a->home_deg);
        cJSON_AddNumberToObject(v, "speed", a->speed);
        cJSON_AddNumberToObject(v, "min_us", a->min_us);
        cJSON_AddNumberToObject(v, "max_us", a->max_us);
        cJSON_AddBoolToObject(v, "invert", a->invert);
        cJSON_AddItemToArray(arms, v);
    }
    return o;
#undef NUM
#undef BOOL
}
bool settings_patch(settings_t *s, const cJSON *j) {
    if (!cJSON_IsObject(j))
        return false;
    settings_t next = *s;
    /* Reject unknown keys and wrong types; credentials are write-only. */
    for (const cJSON *v = j->child; v; v = v->next) {
        bool known = false;
#define NUMBER(k, type)                                                                            \
    if (!strcmp(v->string, #k)) {                                                                  \
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble))                                       \
            return false;                                                                          \
        next.k = (type)v->valuedouble;                                                             \
        known = true;                                                                              \
    }
#define INTEGER(k, type, max)                                                                      \
    if (!strcmp(v->string, #k)) {                                                                  \
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||               \
            v->valuedouble > (max) || floor(v->valuedouble) != v->valuedouble)                     \
            return false;                                                                          \
        next.k = (type)v->valuedouble;                                                             \
        known = true;                                                                              \
    }
#define BOOLEAN(k)                                                                                 \
    if (!strcmp(v->string, #k)) {                                                                  \
        if (!cJSON_IsBool(v))                                                                      \
            return false;                                                                          \
        next.k = cJSON_IsTrue(v);                                                                  \
        known = true;                                                                              \
    }
#define STRING(k)                                                                                  \
    if (!strcmp(v->string, #k)) {                                                                  \
        if (!cJSON_IsString(v) || strlen(v->valuestring) >= sizeof(next.k))                        \
            return false;                                                                          \
        strcpy(next.k, v->valuestring);                                                            \
        known = true;                                                                              \
    }
        NUMBER(motor_limit, float)
        NUMBER(accel, float) NUMBER(decel, float) NUMBER(battery_ratio, float) NUMBER(
            battery_low, float) NUMBER(battery_critical, float) INTEGER(pwm_hz, uint32_t, 20000)
            INTEGER(timeout_ms, uint32_t, 500) INTEGER(reverse_dead_ms, uint32_t, 1000)
                INTEGER(lcd_width, uint16_t, 240) INTEGER(lcd_height, uint16_t, 320)
                    INTEGER(lcd_x_offset, uint16_t, 240) INTEGER(lcd_y_offset, uint16_t, 320)
                        INTEGER(lcd_rotation, uint8_t, 3) INTEGER(brightness, uint8_t, 100)
                            INTEGER(volume, uint8_t, 100) BOOLEAN(hardware_confirmed)
                                BOOLEAN(lcd_enabled) BOOLEAN(audio_enabled) BOOLEAN(battery_enabled)
                                    BOOLEAN(lcd_bgr) BOOLEAN(lcd_invert) BOOLEAN(mic_right)
                                        BOOLEAN(auto_blink) STRING(wifi_ssid) STRING(
                                            wifi_password) if (!strcmp(v->string, "motor_invert") ||
                                                               !strcmp(v->string, "motor_trim")) {
            if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) != 4)
                return false;
            for (int i = 0; i < 4; ++i) {
                cJSON *a = cJSON_GetArrayItem(v, i);
                if (!strcmp(v->string, "motor_invert")) {
                    if (!cJSON_IsBool(a))
                        return false;
                    next.motor_invert[i] = cJSON_IsTrue(a);
                } else {
                    if (!cJSON_IsNumber(a))
                        return false;
                    next.motor_trim[i] = a->valuedouble;
                }
            }
            known = true;
        }
        if (!strcmp(v->string, "servo")) {
            if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) != 2)
                return false;
            for (int i = 0; i < 2; ++i) {
                cJSON *a = cJSON_GetArrayItem(v, i);
                if (!cJSON_IsObject(a))
                    return false;
                for (cJSON *p = a->child; p; p = p->next) {
                    servo_settings_t *arm = &next.servo[i];
                    if (!strcmp(p->string, "invert")) {
                        if (!cJSON_IsBool(p))
                            return false;
                        arm->invert = cJSON_IsTrue(p);
                        continue;
                    }
                    if (!cJSON_IsNumber(p) || !isfinite(p->valuedouble))
                        return false;
                    if (!strcmp(p->string, "min_deg"))
                        arm->min_deg = p->valuedouble;
                    else if (!strcmp(p->string, "max_deg"))
                        arm->max_deg = p->valuedouble;
                    else if (!strcmp(p->string, "home_deg"))
                        arm->home_deg = p->valuedouble;
                    else if (!strcmp(p->string, "speed"))
                        arm->speed = p->valuedouble;
                    else if (!strcmp(p->string, "min_us") || !strcmp(p->string, "max_us")) {
                        if (p->valuedouble < 500 || p->valuedouble > 2500 ||
                            floor(p->valuedouble) != p->valuedouble)
                            return false;
                        if (!strcmp(p->string, "min_us"))
                            arm->min_us = p->valueint;
                        else
                            arm->max_us = p->valueint;
                    } else
                        return false;
                }
            }
            known = true;
        }
        if (!known)
            return false;
    }
    if (!settings_validate(&next))
        return false;
    *s = next;
    return true;
#undef NUMBER
#undef INTEGER
#undef BOOLEAN
#undef STRING
}
