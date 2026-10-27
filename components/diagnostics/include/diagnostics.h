#pragma once
#include "esp_err.h"
#include "cJSON.h"
void diagnostics_record(const char *module, esp_err_t result);
cJSON *diagnostics_json(void);
