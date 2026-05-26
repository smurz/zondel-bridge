/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include <stdlib.h>

struct pipe_client { int unused; };

pipe_client_t *pipe_client_create(const char *endpoint) {
    (void)endpoint;
    return NULL;
}

void pipe_client_destroy(pipe_client_t *c) { (void)c; }

int pipe_client_send_recv(pipe_client_t *c,
                          const float *send_interleaved, float *recv_interleaved,
                          uint32_t frames, uint32_t sample_rate, uint16_t channels,
                          uint32_t timeout_us) {
    (void)c; (void)send_interleaved; (void)recv_interleaved;
    (void)frames; (void)sample_rate; (void)channels; (void)timeout_us;
    return PIPE_ERR_NOT_CONNECTED;
}

int pipe_client_state(const pipe_client_t *c) { (void)c; return PIPE_DISCONNECTED; }
