#include "diagnostics.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "safety_manager.h"
typedef struct {
    char module[24];
    esp_err_t result;
} result_t;
static result_t results[20];
static unsigned used;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
void diagnostics_record(const char *name, esp_err_t e) {
    portENTER_CRITICAL(&mux);
    unsigned i;
    for (i = 0; i < used; ++i)
        if (!strcmp(results[i].module, name))
            break;
    if (i < 20) {
        snprintf(results[i].module, 24, "%s", name);
        results[i].result = e;
        if (i == used)
            ++used;
    }
    portEXIT_CRITICAL(&mux);
    if (e == ESP_OK)
        ESP_LOGI("diagnostics", "%s: driver initialized (hardware unverified)", name);
    else
        ESP_LOGW("diagnostics", "%s: %s", name, esp_err_to_name(e));
}
cJSON *diagnostics_json(void) {
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return NULL;
    cJSON_AddStringToObject(j, "firmware", "2.2.0");
    cJSON_AddStringToObject(j, "idf", esp_get_idf_version());
    cJSON_AddNumberToObject(j, "uptime_s", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(j, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(j, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(j, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(
        j, "largest_internal_block",
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(j, "reset_reason", esp_reset_reason());
    safety_status_t s;
    safety_snapshot(&s);
    const char *state = s.state == CORE_ESTOP                 ? "EMERGENCY_STOP"
                        : s.state == CORE_FAULT               ? "ERROR"
                        : s.critical_battery || s.low_battery ? "LOW_BATTERY"
                        : s.sleeping                          ? "SLEEPING"
                                                              : "IDLE";
    for (int i = 0; i < 4; ++i)
        if (s.state == CORE_ARMED && !s.low_battery && s.motor[i] != 0)
            state = "MOVING";
    cJSON_AddStringToObject(j, "state", state);
    cJSON_AddStringToObject(j, "reason", s.reason);
    cJSON_AddBoolToObject(j, "armed", s.state == CORE_ARMED);
    cJSON_AddBoolToObject(j, "servo_enabled", s.servo_enabled);
    cJSON_AddBoolToObject(j, "low_battery", s.low_battery);
    cJSON_AddBoolToObject(j, "critical_battery", s.critical_battery);
    cJSON_AddNumberToObject(j, "command_age_ms", s.command_age_ms);
    cJSON_AddNumberToObject(j, "control_stack_free", s.control_stack_free);
    cJSON_AddItemToObject(j, "motor_commanded", cJSON_CreateFloatArray(s.motor, 4));
    cJSON_AddItemToObject(j, "servo_commanded_deg", cJSON_CreateFloatArray(s.servo_commanded, 2));
    cJSON_AddStringToObject(j, "hardware_verification", "Hardware verification pending");
    result_t copy[20];
    unsigned count;
    portENTER_CRITICAL(&mux);
    count = used;
    memcpy(copy, results, sizeof(copy));
    portEXIT_CRITICAL(&mux);
    cJSON *modules = cJSON_AddObjectToObject(j, "modules");
    for (unsigned i = 0; i < count; ++i)
        cJSON_AddStringToObject(modules, copy[i].module, esp_err_to_name(copy[i].result));
    return j;
}
