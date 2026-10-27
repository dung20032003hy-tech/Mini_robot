#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t audio_engine_enqueue(const int16_t *, size_t);
esp_err_t audio_engine_tone(void);
void audio_engine_stop(void);
void audio_engine_volume(unsigned);
