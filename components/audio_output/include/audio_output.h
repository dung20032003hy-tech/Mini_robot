#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t audio_output_pcm(const int16_t *samples, size_t count);
esp_err_t audio_output_tone(void);
void audio_output_stop(void);
void audio_output_volume(unsigned percent);
/* Canonical 44-byte PCM WAV header, mono 16 kHz signed 16-bit LE only. */
bool audio_output_wav_header(const uint8_t header[44], size_t length, uint32_t *pcm_bytes);
