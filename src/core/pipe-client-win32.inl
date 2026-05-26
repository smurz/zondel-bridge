/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 * Win32 named-pipe client. Filled in across tasks 6-8.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>

struct pipe_client {
    char  endpoint[256];
    HANDLE handle;
    int    state;
};

pipe_client_t *pipe_client_create(const char *endpoint) {
    pipe_client_t *c = (pipe_client_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    strncpy(c->endpoint, endpoint ? endpoint : "\\\\.\\pipe\\Zondel", sizeof(c->endpoint) - 1);
    c->handle = INVALID_HANDLE_VALUE;
    c->state  = PIPE_DISCONNECTED;
    return c;
}

void pipe_client_destroy(pipe_client_t *c) {
    if (!c) return;
    if (c->handle != INVALID_HANDLE_VALUE) CloseHandle(c->handle);
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

/* Synchronous overlapped write that waits up to timeout_ms. */
static int overlapped_write(HANDLE h, const void *buf, DWORD n, DWORD timeout_ms) {
    OVERLAPPED ovl = {0};
    ovl.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!ovl.hEvent) return PIPE_ERR_WRITE_FAILED;

    int rc = PIPE_OK;
    DWORD written = 0;
    BOOL ok = WriteFile(h, buf, n, &written, &ovl);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        DWORD wait = WaitForSingleObject(ovl.hEvent, timeout_ms);
        if (wait == WAIT_OBJECT_0) {
            if (!GetOverlappedResult(h, &ovl, &written, FALSE) || written != n)
                rc = PIPE_ERR_WRITE_FAILED;
        } else {
            rc = PIPE_ERR_WRITE_FAILED;
        }
    } else if (!ok || written != n) {
        rc = PIPE_ERR_WRITE_FAILED;
    }

    CloseHandle(ovl.hEvent);
    return rc;
}

/* Synchronous overlapped read with explicit timeout. Returns PIPE_OK, PIPE_ERR_READ_TIMEOUT,
 * or PIPE_ERR_WRITE_FAILED on other errors. timed_out is set 1 only on timeout. */
static int overlapped_read(HANDLE h, void *buf, DWORD n, DWORD timeout_ms, int *timed_out) {
    *timed_out = 0;
    OVERLAPPED ovl = {0};
    ovl.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!ovl.hEvent) return PIPE_ERR_WRITE_FAILED;

    int rc = PIPE_OK;
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, n, &got, &ovl);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        DWORD wait = WaitForSingleObject(ovl.hEvent, timeout_ms);
        if (wait == WAIT_OBJECT_0) {
            if (!GetOverlappedResult(h, &ovl, &got, FALSE) || got != n)
                rc = PIPE_ERR_WRITE_FAILED;
        } else if (wait == WAIT_TIMEOUT) {
            *timed_out = 1;
            rc = PIPE_ERR_READ_TIMEOUT;
        } else {
            rc = PIPE_ERR_WRITE_FAILED;
        }
    } else if (!ok || got != n) {
        rc = PIPE_ERR_WRITE_FAILED;
    }

    CloseHandle(ovl.hEvent);
    return rc;
}

int pipe_client_send_recv(pipe_client_t *c,
                          const float *send_interleaved, float *recv_interleaved,
                          uint32_t frames, uint32_t sample_rate, uint16_t channels,
                          uint32_t timeout_us) {
    int rc = try_connect(c);
    if (rc != PIPE_OK) return rc;

    DWORD timeout_ms = (timeout_us + 999) / 1000;
    if (timeout_ms == 0) timeout_ms = 1;

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

    rc = overlapped_write(c->handle, reqHdr, 10, timeout_ms);
    if (rc != PIPE_OK) { disconnect(c); return rc; }

    rc = overlapped_write(c->handle, send_interleaved, payload_bytes, timeout_ms);
    if (rc != PIPE_OK) { disconnect(c); return rc; }

    int timed_out = 0;
    unsigned char respHdr[5];
    rc = overlapped_read(c->handle, respHdr, 5, timeout_ms, &timed_out);
    if (rc != PIPE_OK) { disconnect(c); return rc; }

    uint32_t echoedFrames = (uint32_t)respHdr[0]
                          | ((uint32_t)respHdr[1] <<  8)
                          | ((uint32_t)respHdr[2] << 16)
                          | ((uint32_t)respHdr[3] << 24);
    if (echoedFrames != frames) {
        disconnect(c);
        return PIPE_ERR_PROTOCOL;
    }

    rc = overlapped_read(c->handle, recv_interleaved, payload_bytes, timeout_ms, &timed_out);
    if (rc != PIPE_OK) { disconnect(c); return rc; }

    return PIPE_OK;
}

int pipe_client_state(const pipe_client_t *c) {
    return c ? c->state : PIPE_DISCONNECTED;
}
