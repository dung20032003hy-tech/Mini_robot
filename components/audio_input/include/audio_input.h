#pragma once
#include <stddef.h>
#include "settings_manager.h"
typedef struct {
    bool ready, capturing, playing;
    uint32_t captured_samples, clipped, dropped_samples, io_errors;
    uint16_t peak;
} audio_status_t;
esp_err_t audio_init(const settings_t *settings);
esp_err_t audio_capture(bool on);
size_t audio_read_pcm(int16_t *destination, size_t samples);
void audio_snapshot(audio_status_t *out);
/* Shared duplex engine; audio_output exposes these operations to consumers. */
esp_err_t audio_engine_enqueue(const int16_t *pcm, size_t count);
esp_err_t audio_engine_tone(void);
void audio_engine_stop(void);
void audio_engine_volume(unsigned percent);
