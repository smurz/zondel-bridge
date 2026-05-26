/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include <stdio.h>
#include <string.h>
#include "../src/zondel-state.h"
#include "../src/pipe-client.h"

static int failures = 0;
#define EXPECT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static void test_init_state(void) {
    zondel_state_t s;
    zondel_state_init(&s);
    EXPECT(s.status == ZS_UNKNOWN, "init status should be UNKNOWN");
    EXPECT(s.fail_streak == 0, "init fail_streak");
    EXPECT(s.bypass == 0, "init bypass");
    EXPECT(zondel_state_should_skip(&s, 0) == 0, "init should not skip");
}

static void test_bypass_skips(void) {
    zondel_state_t s; zondel_state_init(&s);
    zondel_state_set_bypass(&s, 1);
    EXPECT(zondel_state_should_skip(&s, 1000) == 1, "bypass should skip");
}

static void test_three_failures_trigger_backoff(void) {
    zondel_state_t s; zondel_state_init(&s);
    uint64_t now = 1000000000ULL;
    zondel_state_on_failure(&s, PIPE_ERR_READ_TIMEOUT, now);
    zondel_state_on_failure(&s, PIPE_ERR_READ_TIMEOUT, now);
    EXPECT(zondel_state_should_skip(&s, now) == 0, "should not skip yet (streak=2)");
    zondel_state_on_failure(&s, PIPE_ERR_READ_TIMEOUT, now);
    EXPECT(s.status == ZS_BACKED_OFF, "status should be BACKED_OFF after 3 failures");
    EXPECT(zondel_state_should_skip(&s, now) == 1, "should skip during back-off window");
    EXPECT(zondel_state_should_skip(&s, now + 2000000000ULL + 1) == 0, "should retry after 2s");
}

static void test_success_resets_streak(void) {
    zondel_state_t s; zondel_state_init(&s);
    uint64_t now = 0;
    zondel_state_on_failure(&s, PIPE_ERR_READ_TIMEOUT, now);
    zondel_state_on_failure(&s, PIPE_ERR_READ_TIMEOUT, now);
    zondel_state_on_success(&s);
    EXPECT(s.fail_streak == 0, "success clears fail_streak");
    EXPECT(s.status == ZS_CONNECTED, "success sets CONNECTED");
}

static void test_unsupported_format_logs_once(void) {
    zondel_state_t s; zondel_state_init(&s);
    zondel_state_on_unsupported_format(&s);
    EXPECT(s.status == ZS_UNSUPPORTED_FORMAT, "unsupported sets status");
    EXPECT(s.logged_unsupported_format == 1, "first call sets log flag");
    zondel_state_on_unsupported_format(&s);
    EXPECT(s.logged_unsupported_format == 1, "second call leaves flag at 1");
}

int main(void) {
    test_init_state();
    test_bypass_skips();
    test_three_failures_trigger_backoff();
    test_success_resets_streak();
    test_unsupported_format_logs_once();
    if (failures) { fprintf(stderr, "%d test failure(s)\n", failures); return 1; }
    fprintf(stderr, "all tests passed\n");
    return 0;
}
