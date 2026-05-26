/*
 * fake-pipe-server.c — minimal in-CI substitute for Zondel's pipe server.
 *
 * Usage:
 *   fake-pipe-server <pipe-name> <mode>
 *   modes: echo | sleep-ms:<N> | bad-header | exit-after:<N>
 *     echo          - read each request, write request payload back unchanged
 *     sleep-ms:<N>  - sleep N milliseconds before responding (forces client timeout)
 *     bad-header    - respond with wrong frame size in the 5-byte header
 *     exit-after:<N> - close pipe after N successful round-trips
 *
 * License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fprintf(stderr, "Usage: fake-pipe-server <pipe-name> <mode>\n");
    fprintf(stderr, "  modes: echo | sleep-ms:<N> | bad-header | exit-after:<N>\n");
    exit(2);
}

static int read_exact(HANDLE h, void *buf, DWORD n) {
    DWORD total = 0, got;
    while (total < n) {
        if (!ReadFile(h, (char *)buf + total, n - total, &got, NULL) || got == 0)
            return 0;
        total += got;
    }
    return 1;
}

static int write_exact(HANDLE h, const void *buf, DWORD n) {
    DWORD total = 0, put;
    while (total < n) {
        if (!WriteFile(h, (const char *)buf + total, n - total, &put, NULL) || put == 0)
            return 0;
        total += put;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3) usage();

    const char *pipeName = argv[1];
    const char *mode = argv[2];
    int sleepMs = 0;
    int badHeader = 0;
    int exitAfter = -1;

    if (strcmp(mode, "echo") == 0) {
        /* nothing extra */
    } else if (strncmp(mode, "sleep-ms:", 9) == 0) {
        sleepMs = atoi(mode + 9);
    } else if (strcmp(mode, "bad-header") == 0) {
        badHeader = 1;
    } else if (strncmp(mode, "exit-after:", 11) == 0) {
        exitAfter = atoi(mode + 11);
    } else {
        usage();
    }

    char fullName[256];
    snprintf(fullName, sizeof(fullName), "\\\\.\\pipe\\%s", pipeName);

    HANDLE pipe = CreateNamedPipeA(
        fullName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 65536, 65536, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateNamedPipeA failed: %lu\n", GetLastError());
        return 1;
    }

    /* Signal readiness to whoever launched us. */
    fprintf(stdout, "READY\n");
    fflush(stdout);

    if (!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
        fprintf(stderr, "ConnectNamedPipe failed: %lu\n", GetLastError());
        return 1;
    }

    int roundTrips = 0;
    unsigned char reqHeader[10];
    unsigned char respHeader[5];

    while (1) {
        if (!read_exact(pipe, reqHeader, 10)) break;

        unsigned int frameSize  = reqHeader[0] | (reqHeader[1]<<8) | (reqHeader[2]<<16) | (reqHeader[3]<<24);
        unsigned int sampleRate = reqHeader[4] | (reqHeader[5]<<8) | (reqHeader[6]<<16) | (reqHeader[7]<<24);
        unsigned int channels   = reqHeader[8] | (reqHeader[9]<<8);

        (void)sampleRate;
        unsigned int payloadBytes = frameSize * channels * 4;
        if (payloadBytes == 0 || payloadBytes > 4 * 48000 * 2) break;

        unsigned char *payload = (unsigned char *)malloc(payloadBytes);
        if (!read_exact(pipe, payload, payloadBytes)) { free(payload); break; }

        if (sleepMs > 0) Sleep((DWORD)sleepMs);

        unsigned int echoedSize = badHeader ? (frameSize + 1) : frameSize;
        respHeader[0] = (echoedSize      ) & 0xff;
        respHeader[1] = (echoedSize >>  8) & 0xff;
        respHeader[2] = (echoedSize >> 16) & 0xff;
        respHeader[3] = (echoedSize >> 24) & 0xff;
        respHeader[4] = 0;

        if (!write_exact(pipe, respHeader, 5)) { free(payload); break; }
        if (!write_exact(pipe, payload, payloadBytes)) { free(payload); break; }

        free(payload);
        roundTrips++;
        if (exitAfter >= 0 && roundTrips >= exitAfter) break;
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    return 0;
}
