#include "voice_dsp.h"

voice_vad_result_t voice_vad_feed(voice_vad_t *v, const int16_t *pcm, size_t n,
                                unsigned threshold) {
    if (!v || !pcm || !n || !threshold)
        return VOICE_WAIT;
    uint64_t energy = 0;
    for (size_t i = 0; i < n; ++i)
        energy += (int64_t)pcm[i] * pcm[i];
    bool speech = energy / n >= (uint64_t)threshold * threshold;
    if (!v->started) {
        v->voiced = speech ? v->voiced + n : 0;
        if (v->voiced >= 1920) {
            v->started = true;
            v->quiet = 0;
            return VOICE_START;
        }
        return VOICE_WAIT;
    }
    v->quiet = speech ? 0 : v->quiet + n;
    return v->quiet >= 12800 ? VOICE_END : VOICE_CONTINUE;
}

size_t voice_resample(const int16_t *in, size_t n, unsigned from, unsigned to,
                      size_t offset, int16_t *out, size_t capacity) {
    if (!in || !out || !n || !from || !to || from > 48000 || to > 48000)
        return 0;
    size_t total = (uint64_t)n * to / from, written = 0;
    while (written < capacity && offset < total) {
        uint64_t pos = (uint64_t)offset++ * from;
        size_t index = pos / to;
        int32_t a = in[index], b = in[index + 1 < n ? index + 1 : index];
        out[written++] = (int16_t)(a + (int64_t)(b - a) * (int64_t)(pos % to) / to);
    }
    return written;
}
