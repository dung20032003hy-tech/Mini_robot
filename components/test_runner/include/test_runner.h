#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
esp_err_t test_run(const char *name, bool confirmed, char *message, size_t capacity);
