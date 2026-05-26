/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include "zondel-state.h"
#include "pipe-client.h"
#include <string.h>

#define BACKOFF_NS (2000000000ULL)  /* 2 seconds */
#define FAIL_THRESHOLD 3

void zondel_state_init(zondel_state_t *s) {
    memset(s, 0, sizeof(*s));
    s->status = ZS_UNKNOWN;
}

int zondel_state_should_skip(const zondel_state_t *s, uint64_t now_ns) {
    if (s->bypass) return 1;
    if (s->status == ZS_UNSUPPORTED_FORMAT) return 1;
    if (s->status == ZS_BACKED_OFF && now_ns < s->backoff_until_ns) return 1;
    return 0;
}

void zondel_state_on_success(zondel_state_t *s) {
    s->fail_streak = 0;
    s->status = ZS_CONNECTED;
}

void zondel_state_on_failure(zondel_state_t *s, int pipe_result, uint64_t now_ns) {
    (void)pipe_result;
    if (s->fail_streak < 1000) s->fail_streak++;
    s->status = ZS_DISCONNECTED;
    if (s->fail_streak >= FAIL_THRESHOLD) {
        s->status = ZS_BACKED_OFF;
        s->backoff_until_ns = now_ns + BACKOFF_NS;
    }
}

void zondel_state_on_unsupported_format(zondel_state_t *s) {
    s->status = ZS_UNSUPPORTED_FORMAT;
    s->logged_unsupported_format = 1;
}

void zondel_state_set_bypass(zondel_state_t *s, int bypass) {
    s->bypass = bypass ? 1 : 0;
}
