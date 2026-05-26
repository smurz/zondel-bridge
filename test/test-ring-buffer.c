/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include <stdio.h>
#include <string.h>
#include "../src/ring-buffer.h"

static int failures = 0;
#define EXPECT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static void test_init_empty(void) {
    ring_buffer_t rb;
    EXPECT(ring_buffer_init(&rb, 1024) == 0, "init");
    EXPECT(ring_buffer_fill(&rb) == 0, "empty fill");
    EXPECT(ring_buffer_free_space(&rb) == 1024, "free space == capacity");
    float scratch[10];
    EXPECT(ring_buffer_pop(&rb, scratch, 10) == 0, "pop empty returns 0");
    ring_buffer_free(&rb);
}

static void test_push_pop_order(void) {
    ring_buffer_t rb;
    ring_buffer_init(&rb, 1024);

    float in[100];
    for (int i = 0; i < 100; i++) in[i] = (float)i;

    EXPECT(ring_buffer_push(&rb, in, 100) == 100, "push 100");
    EXPECT(ring_buffer_fill(&rb) == 100, "fill 100");

    float out[60];
    EXPECT(ring_buffer_pop(&rb, out, 60) == 60, "pop 60");
    for (int i = 0; i < 60; i++) {
        if (out[i] != (float)i) { EXPECT(0, "FIFO order"); break; }
    }
    EXPECT(ring_buffer_fill(&rb) == 40, "remaining 40");

    ring_buffer_free(&rb);
}

static void test_wrap_around(void) {
    ring_buffer_t rb;
    ring_buffer_init(&rb, 1024);

    float in[800];
    for (int i = 0; i < 800; i++) in[i] = (float)i;
    ring_buffer_push(&rb, in, 800);

    float out[800];
    ring_buffer_pop(&rb, out, 800);  /* now head and tail are both at 800 */

    /* Push again; this will wrap. */
    float in2[800];
    for (int i = 0; i < 800; i++) in2[i] = (float)(i + 1000);
    EXPECT(ring_buffer_push(&rb, in2, 800) == 800, "push 800 (wrap)");

    float out2[800];
    EXPECT(ring_buffer_pop(&rb, out2, 800) == 800, "pop 800 (wrap)");
    for (int i = 0; i < 800; i++) {
        if (out2[i] != (float)(i + 1000)) { EXPECT(0, "wrap FIFO order"); break; }
    }

    ring_buffer_free(&rb);
}

static void test_overflow_clamps(void) {
    ring_buffer_t rb;
    ring_buffer_init(&rb, 100);

    float in[200];
    for (int i = 0; i < 200; i++) in[i] = (float)i;

    size_t pushed = ring_buffer_push(&rb, in, 200);
    EXPECT(pushed == 100, "push clamps at capacity");
    EXPECT(ring_buffer_fill(&rb) == 100, "fill at capacity");

    ring_buffer_free(&rb);
}

int main(void) {
    test_init_empty();
    test_push_pop_order();
    test_wrap_around();
    test_overflow_clamps();
    if (failures) { fprintf(stderr, "%d test failure(s)\n", failures); return 1; }
    fprintf(stderr, "all tests passed\n");
    return 0;
}
