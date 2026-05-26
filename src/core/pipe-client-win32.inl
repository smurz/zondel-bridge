/* License: MIT. Copyright (c) 2026 Zondel.
 *
 * Win32 named-pipe client. Synchronous round-trip semantics, but
 * implemented over OVERLAPPED I/O so we can apply a single deadline
 * across all four wire ops (request header, request payload, response
 * header, response payload).
 *
 * Realtime safety:
 *   - The event handle used for OVERLAPPED.hEvent is allocated ONCE in
 *     pipe_client_create() and reused for every read/write. No kernel-
 *     handle churn on the audio thread.
 *   - On timeout we CancelIoEx and drain the pending operation with
 *     GetOverlappedResult(..., TRUE) before returning. Without that
 *     drain the kernel could still touch the stack-allocated overlapped
 *     storage (and the caller's payload buffer) after we've returned.
 *   - The configured timeout_us is the TOTAL deadline for the round-
 *     trip — not per-op. Each op consumes its share of remaining time.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>

struct pipe_client {
    char   endpoint[256];
    HANDLE handle;
    HANDLE event;     /* reusable OVERLAPPED.hEvent, manual-reset */
    int    state;
};

pipe_client_t *pipe_client_create(const char *endpoint) {
    pipe_client_t *c = (pipe_client_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    strncpy(c->endpoint, endpoint ? endpoint : "\\\\.\\pipe\\Zondel", sizeof(c->endpoint) - 1);
    c->handle = INVALID_HANDLE_VALUE;
    c->state  = PIPE_DISCONNECTED;
    /* Manual-reset event so we control the reset moment between ops. */
    c->event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!c->event) {
        free(c);
        return NULL;
    }
    return c;
}

void pipe_client_destroy(pipe_client_t *c) {
    if (!c) return;
    if (c->handle != INVALID_HANDLE_VALUE) CloseHandle(c->handle);
    if (c->event)                          CloseHandle(c->event);
    free(c);
}

static int try_connect(pipe_client_t *c) {
    if (c->handle != INVALID_HANDLE_VALUE) return PIPE_OK;

    HANDLE h = CreateFileA(
        c->endpoint,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL);

    if (h == INVALID_HANDLE_VALUE) {
        c->state = PIPE_DISCONNECTED;
        return PIPE_ERR_NOT_CONNECTED;
    }

    c->handle = h;
    c->state  = PIPE_CONNECTED;
    return PIPE_OK;
}

static void disconnect(pipe_client_t *c) {
    if (c->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(c->handle);
        c->handle = INVALID_HANDLE_VALUE;
    }
    c->state = PIPE_DISCONNECTED;
}

/* Cancel a pending overlapped op and wait for the kernel to acknowledge
 * the cancellation. Essential before reusing the OVERLAPPED structure or
 * letting the caller's payload buffer go out of scope: without it, the
 * kernel can keep writing into the buffer after we return. */
static void cancel_and_drain(HANDLE h, OVERLAPPED *ovl) {
    CancelIoEx(h, ovl);
    DWORD discard = 0;
    /* TRUE = wait until the cancellation completes. Returns quickly. */
    (void)GetOverlappedResult(h, ovl, &discard, TRUE);
}

/* One overlapped operation (read or write) with a remaining-time
 * budget. Returns PIPE_OK, PIPE_ERR_READ_TIMEOUT (timeout while
 * waiting), or PIPE_ERR_WRITE_FAILED (any other error). On timeout the
 * pending I/O has been cancelled and drained. */
static int overlapped_op(HANDLE h, HANDLE event, OVERLAPPED *ovl,
                         BOOL is_write, void *buf, DWORD n,
                         DWORD timeout_ms) {
    ResetEvent(event);
    ovl->Internal = 0;
    ovl->InternalHigh = 0;
    ovl->Offset = 0;
    ovl->OffsetHigh = 0;
    ovl->hEvent = event;

    DWORD transferred = 0;
    BOOL ok = is_write
        ? WriteFile(h, buf, n, &transferred, ovl)
        : ReadFile(h, buf, n, &transferred, ovl);

    if (ok) {
        return (transferred == n) ? PIPE_OK : PIPE_ERR_WRITE_FAILED;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
        return PIPE_ERR_WRITE_FAILED;
    }

    DWORD wait = WaitForSingleObject(event, timeout_ms);
    if (wait == WAIT_OBJECT_0) {
        if (!GetOverlappedResult(h, ovl, &transferred, FALSE) || transferred != n)
            return PIPE_ERR_WRITE_FAILED;
        return PIPE_OK;
    }
    if (wait == WAIT_TIMEOUT) {
        cancel_and_drain(h, ovl);
        return PIPE_ERR_READ_TIMEOUT;
    }
    cancel_and_drain(h, ovl);
    return PIPE_ERR_WRITE_FAILED;
}

/* Remaining milliseconds against a tick-count deadline. Returns 0 if
 * already past the deadline so callers can short-circuit. */
static DWORD remaining_ms(ULONGLONG deadline_tick) {
    ULONGLONG now = GetTickCount64();
    if (now >= deadline_tick) return 0;
    return (DWORD)(deadline_tick - now);
}

int pipe_client_send_recv(pipe_client_t *c,
                          const float *send_interleaved, float *recv_interleaved,
                          uint32_t frames, uint32_t sample_rate, uint16_t channels,
                          uint32_t timeout_us) {
    if (!c || !c->event) return PIPE_ERR_NOT_CONNECTED;

    int rc = try_connect(c);
    if (rc != PIPE_OK) return rc;

    DWORD total_timeout_ms = (timeout_us + 999) / 1000;
    if (total_timeout_ms == 0) total_timeout_ms = 1;
    const ULONGLONG deadline = GetTickCount64() + total_timeout_ms;

    uint32_t payload_bytes = frames * channels * sizeof(float);

    /* 10-byte little-endian request header */
    unsigned char reqHdr[10];
    reqHdr[0] = (unsigned char)(frames        & 0xff);
    reqHdr[1] = (unsigned char)((frames >>  8) & 0xff);
    reqHdr[2] = (unsigned char)((frames >> 16) & 0xff);
    reqHdr[3] = (unsigned char)((frames >> 24) & 0xff);
    reqHdr[4] = (unsigned char)(sample_rate        & 0xff);
    reqHdr[5] = (unsigned char)((sample_rate >>  8) & 0xff);
    reqHdr[6] = (unsigned char)((sample_rate >> 16) & 0xff);
    reqHdr[7] = (unsigned char)((sample_rate >> 24) & 0xff);
    reqHdr[8] = (unsigned char)(channels        & 0xff);
    reqHdr[9] = (unsigned char)((channels >>  8) & 0xff);

    OVERLAPPED ovl = {0};

    rc = overlapped_op(c->handle, c->event, &ovl, TRUE, reqHdr, 10,
                       remaining_ms(deadline));
    if (rc != PIPE_OK) { disconnect(c); return rc; }

    rc = overlapped_op(c->handle, c->event, &ovl, TRUE,
                       (void *)send_interleaved, payload_bytes,
                       remaining_ms(deadline));
    if (rc != PIPE_OK) { disconnect(c); return rc; }

    unsigned char respHdr[5];
    rc = overlapped_op(c->handle, c->event, &ovl, FALSE, respHdr, 5,
                       remaining_ms(deadline));
    if (rc != PIPE_OK) { disconnect(c); return rc; }

    uint32_t echoedFrames = (uint32_t)respHdr[0]
                          | ((uint32_t)respHdr[1] <<  8)
                          | ((uint32_t)respHdr[2] << 16)
                          | ((uint32_t)respHdr[3] << 24);
    if (echoedFrames != frames) {
        disconnect(c);
        return PIPE_ERR_PROTOCOL;
    }
    /* Status byte (per docs/PROTOCOL.md): 0 = ok. Reject anything else
     * as a protocol/remote-side failure; we still drain the payload to
     * keep the pipe in sync with the server. */
    const unsigned char respStatus = respHdr[4];

    rc = overlapped_op(c->handle, c->event, &ovl, FALSE, recv_interleaved,
                       payload_bytes, remaining_ms(deadline));
    if (rc != PIPE_OK) { disconnect(c); return rc; }

    if (respStatus != 0) {
        /* Pipe is still in sync (we drained the payload) but the server
         * signalled a logical error — pass it up as a protocol failure
         * so the caller can fall back to pass-through this block. */
        return PIPE_ERR_PROTOCOL;
    }

    return PIPE_OK;
}

int pipe_client_state(const pipe_client_t *c) {
    return c ? c->state : PIPE_DISCONNECTED;
}
