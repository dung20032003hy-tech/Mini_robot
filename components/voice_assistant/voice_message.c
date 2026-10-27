#include "voice_message.h"
#include <string.h>
int voice_message_feed(voice_message_t *s, char *out, size_t capacity, unsigned opcode,
                       bool fin, size_t frame_length, const char *data, size_t n) {
    if (!s || !out || !data || !n || !capacity || (opcode != 0 && opcode != 1))
        return -1;
    if (!s->frame_read) {
        if ((opcode == 1 && s->fragmented) || (opcode == 0 && !s->fragmented))
            return -1;
        if (opcode == 1)
            s->used = 0;
    }
    if (s->used >= capacity || n >= capacity - s->used ||
        s->frame_read > frame_length || n > frame_length - s->frame_read || memchr(data, 0, n))
        return -1;
    memcpy(out + s->used, data, n);
    s->used += n;
    s->frame_read += n;
    if (s->frame_read < frame_length)
        return 0;
    s->frame_read = 0;
    s->fragmented = !fin;
    if (s->fragmented)
        return 0;
    out[s->used] = 0;
    return 1;
}
