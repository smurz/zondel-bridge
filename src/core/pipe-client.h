/*
 * pipe-client.h — synchronous bidirectional IPC client for the Zondel pipe.
 *
 * Single-threaded, synchronous, suitable for direct use inside OBS's
 * filter_audio callback. See ../docs/PROTOCOL.md for the wire format.
 *
 * License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 */
#ifndef ZONDEL_PIPE_CLIENT_H
#define ZONDEL_PIPE_CLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pipe_client pipe_client_t;

enum pipe_client_state {
    PIPE_DISCONNECTED = 0,
    PIPE_CONNECTING   = 1,
    PIPE_CONNECTED    = 2,
};

enum pipe_client_result {
    PIPE_OK                  =  0,
    PIPE_ERR_NOT_CONNECTED   = -1, /* CreateFile failed; caller should pass-through */
    PIPE_ERR_WRITE_FAILED    = -2, /* mid-write disconnect */
    PIPE_ERR_READ_TIMEOUT    = -3, /* read exceeded timeout_us; handle is now closed */
    PIPE_ERR_PROTOCOL        = -4, /* response header had wrong frame size */
};

/* Lifetime. endpoint format: "\\\\.\\pipe\\Name" on Win32, "/tmp/..." on Unix. */
pipe_client_t *pipe_client_create(const char *endpoint);
void           pipe_client_destroy(pipe_client_t *c);

/* Single synchronous round-trip. Blocks up to timeout_us. */
int pipe_client_send_recv(
    pipe_client_t *c,
    const float *send_interleaved,
    float *recv_interleaved,
    uint32_t frames,
    uint32_t sample_rate,
    uint16_t channels,
    uint32_t timeout_us);

int pipe_client_state(const pipe_client_t *c);

#ifdef __cplusplus
}
#endif
#endif /* ZONDEL_PIPE_CLIENT_H */
