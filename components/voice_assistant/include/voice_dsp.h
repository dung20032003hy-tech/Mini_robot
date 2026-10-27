#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t voiced, quiet;
    bool started;
} voice_vad_t;
typedef enum { VOICE_WAIT, VOICE_START, VOICE_CONTINUE, VOICE_END } voice_vad_result_t;
/* 16 kHz mono; require 120 ms voice onset and 800 ms trailing silence. */
voice_vad_result_t voice_vad_feed(voice_vad_t *vad, const int16_t *pcm, size_t count,
                                unsigned threshold);
/* Rational interpolation; absolute output offset preserves chunk boundaries. */
size_t voice_resample(const int16_t *input, size_t count, unsigned input_rate,
                      unsigned output_rate, size_t output_offset,
                      int16_t *output, size_t capacity);
