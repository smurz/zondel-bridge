/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#ifndef ZONDEL_RING_BUFFER_H
#define ZONDEL_RING_BUFFER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ring_buffer {
    float  *buf;
    size_t  capacity;
    size_t  head;   /* next write index */
    size_t  tail;   /* next read index */
    size_t  fill;   /* count of valid samples */
} ring_buffer_t;

int  ring_buffer_init(ring_buffer_t *rb, size_t capacity);
void ring_buffer_free(ring_buffer_t *rb);
void ring_buffer_reset(ring_buffer_t *rb);

size_t ring_buffer_fill(const ring_buffer_t *rb);
size_t ring_buffer_free_space(const ring_buffer_t *rb);

/* Push n samples. Returns the number actually written (less than n if full). */
size_t ring_buffer_push(ring_buffer_t *rb, const float *src, size_t n);

/* Pop up to n samples. Returns the number actually read. */
size_t ring_buffer_pop(ring_buffer_t *rb, float *dst, size_t n);

#ifdef __cplusplus
}
#endif
#endif
