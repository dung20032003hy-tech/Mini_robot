#pragma once
#include <stddef.h>
#include <stdbool.h>
typedef struct {
    size_t used, frame_read;
    bool fragmented;
} voice_message_t;
/* Feed a data-frame fragment supplied by esp_transport_ws. No allocation.
 * Returns -1 invalid/oversized, 0 incomplete, 1 complete NUL-terminated JSON. */
int voice_message_feed(voice_message_t *state, char *destination, size_t capacity,
                       unsigned opcode, bool fin, size_t frame_length,
                       const char *chunk, size_t length);
