/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#ifndef ZONDEL_STATE_H
#define ZONDEL_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zondel_status {
    ZS_UNKNOWN              = 0,
    ZS_CONNECTING           = 1,
    ZS_CONNECTED            = 2,
    ZS_DISCONNECTED         = 3,
    ZS_BACKED_OFF           = 4,
    ZS_UNSUPPORTED_FORMAT   = 5,
};

typedef struct {
    int      status;
    int      fail_streak;
    uint64_t backoff_until_ns;
    int      bypass;
    int      logged_unsupported_format;
} zondel_state_t;

void zondel_state_init(zondel_state_t *s);

/* Returns 1 if filter_audio should skip the pipe call entirely (bypass,
 * back-off, unsupported format). Pure function over (state, now_ns). */
int zondel_state_should_skip(const zondel_state_t *s, uint64_t now_ns);

/* Called after a successful pipe round-trip. Clears fail_streak, sets CONNECTED. */
void zondel_state_on_success(zondel_state_t *s);

/* Called after pipe_client_send_recv returned a negative result.
 * Increments fail_streak; if streak >= 3, schedules a 2 s back-off. */
void zondel_state_on_failure(zondel_state_t *s, int pipe_result, uint64_t now_ns);

/* Called per callback when format triplet does NOT match (48000, 1, 480). */
void zondel_state_on_unsupported_format(zondel_state_t *s);

/* Called when the user toggles bypass. */
void zondel_state_set_bypass(zondel_state_t *s, int bypass);

#ifdef __cplusplus
}
#endif
#endif
