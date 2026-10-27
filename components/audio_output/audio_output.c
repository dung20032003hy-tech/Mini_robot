#include "audio_output.h"
#include "audio_input.h"
#include <string.h>
esp_err_t audio_output_pcm(const int16_t *p, size_t n) {
    return audio_engine_enqueue(p, n);
}
esp_err_t audio_output_tone(void) {
    return audio_engine_tone();
}
void audio_output_stop(void) {
    audio_engine_stop();
}
void audio_output_volume(unsigned n) {
    audio_engine_volume(n);
}
static uint32_t le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
bool audio_output_wav_header(const uint8_t h[44], size_t length, uint32_t *bytes) {
    if (length < 44 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVEfmt ", 8) || le32(h + 16) != 16 ||
        h[20] != 1 || h[21] != 0 || h[22] != 1 || h[23] != 0 || le32(h + 24) != 16000 ||
        le32(h + 28) != 32000 || h[32] != 2 || h[33] != 0 || h[34] != 16 || h[35] != 0 ||
        memcmp(h + 36, "data", 4))
        return false;
    *bytes = le32(h + 40);
    return *bytes % 2 == 0 && (uint64_t)*bytes + 44 == length &&
           (uint64_t)le32(h + 4) + 8 == length;
}
