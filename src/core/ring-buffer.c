/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include "ring-buffer.h"
#include <stdlib.h>
#include <string.h>

int ring_buffer_init(ring_buffer_t *rb, size_t capacity) {
    rb->buf = (float *)calloc(capacity, sizeof(float));
    if (!rb->buf) return -1;
    rb->capacity = capacity;
    rb->head = rb->tail = rb->fill = 0;
    return 0;
}

void ring_buffer_free(ring_buffer_t *rb) {
    free(rb->buf);
    rb->buf = NULL;
    rb->capacity = rb->head = rb->tail = rb->fill = 0;
}

void ring_buffer_reset(ring_buffer_t *rb) {
    rb->head = rb->tail = rb->fill = 0;
}

size_t ring_buffer_fill(const ring_buffer_t *rb) { return rb->fill; }
size_t ring_buffer_free_space(const ring_buffer_t *rb) { return rb->capacity - rb->fill; }

size_t ring_buffer_push(ring_buffer_t *rb, const float *src, size_t n) {
    size_t free_n = rb->capacity - rb->fill;
    if (n > free_n) n = free_n;

    size_t first = rb->capacity - rb->head;
    if (first > n) first = n;
    memcpy(rb->buf + rb->head, src, first * sizeof(float));

    size_t second = n - first;
    if (second) memcpy(rb->buf, src + first, second * sizeof(float));

    rb->head = (rb->head + n) % rb->capacity;
    rb->fill += n;
    return n;
}

size_t ring_buffer_pop(ring_buffer_t *rb, float *dst, size_t n) {
    if (n > rb->fill) n = rb->fill;

    size_t first = rb->capacity - rb->tail;
    if (first > n) first = n;
    memcpy(dst, rb->buf + rb->tail, first * sizeof(float));

    size_t second = n - first;
    if (second) memcpy(dst + first, rb->buf, second * sizeof(float));

    rb->tail = (rb->tail + n) % rb->capacity;
    rb->fill -= n;
    return n;
}
