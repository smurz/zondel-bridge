/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/pipe-client.h"

static int failures = 0;
#define EXPECT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static HANDLE start_fake_server(const char *pipe_name, const char *mode) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "fake-pipe-server.exe %s %s", pipe_name, mode);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
        return NULL;
    }
    CloseHandle(pi.hThread);
    /* Server prints "READY" then calls ConnectNamedPipe. We can't easily read its
     * stdout because we inherited the parent's handles. Just sleep briefly. */
    Sleep(150);
    return pi.hProcess;
}

static void stop_fake_server(HANDLE h) {
    if (!h) return;
    /* Closing the client side causes the server's ReadFile to return 0 -> exit. */
    WaitForSingleObject(h, 2000);
    DWORD exitCode;
    if (GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE) {
        TerminateProcess(h, 1);
    }
    CloseHandle(h);
}

static void test_round_trip_echo(void) {
    HANDLE srv = start_fake_server("ZondelTestEcho", "echo");
    if (!srv) { EXPECT(0, "could not start fake server"); return; }

    pipe_client_t *c = pipe_client_create("\\\\.\\pipe\\ZondelTestEcho");
    EXPECT(c != NULL, "create");

    float send[480];
    float recv[480];
    for (int i = 0; i < 480; i++) send[i] = (float)i * 0.001f;
    memset(recv, 0, sizeof(recv));

    int rc = pipe_client_send_recv(c, send, recv, 480, 48000, 1, 5000);
    EXPECT(rc == PIPE_OK, "round-trip failed");
    EXPECT(pipe_client_state(c) == PIPE_CONNECTED, "state should be CONNECTED");

    for (int i = 0; i < 480; i++) {
        if (recv[i] != send[i]) { EXPECT(0, "echo payload mismatch"); break; }
    }

    pipe_client_destroy(c);
    stop_fake_server(srv);
}

static void test_bad_header_returns_protocol_error(void) {
    HANDLE srv = start_fake_server("ZondelTestBad", "bad-header");
    if (!srv) { EXPECT(0, "could not start fake server"); return; }

    pipe_client_t *c = pipe_client_create("\\\\.\\pipe\\ZondelTestBad");
    EXPECT(c != NULL, "create");

    float send[480] = {0}, recv[480] = {0};
    int rc = pipe_client_send_recv(c, send, recv, 480, 48000, 1, 5000);
    EXPECT(rc == PIPE_ERR_PROTOCOL, "expected PROTOCOL error on bad header");

    pipe_client_destroy(c);
    stop_fake_server(srv);
}

static void test_no_server_returns_not_connected_quickly(void) {
    pipe_client_t *c = pipe_client_create("\\\\.\\pipe\\ZondelTestNobody");
    EXPECT(c != NULL, "create returned NULL");

    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);

    float send[480] = {0}, recv[480] = {0};
    int rc = pipe_client_send_recv(c, send, recv, 480, 48000, 1, 5000);

    QueryPerformanceCounter(&b);
    double elapsed_us = (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart;

    EXPECT(rc == PIPE_ERR_NOT_CONNECTED, "expected PIPE_ERR_NOT_CONNECTED");
    EXPECT(elapsed_us < 1000.0, "no-server probe took > 1 ms");

    pipe_client_destroy(c);
}

static void test_timeout_closes_handle_and_next_call_reconnects(void) {
    HANDLE srv = start_fake_server("ZondelTestSlow", "sleep-ms:100");
    if (!srv) { EXPECT(0, "could not start fake server"); return; }

    pipe_client_t *c = pipe_client_create("\\\\.\\pipe\\ZondelTestSlow");
    EXPECT(c != NULL, "create");

    float send[480] = {0}, recv[480] = {0};

    /* First call: 5 ms timeout but server sleeps 100 ms -> timeout */
    int rc = pipe_client_send_recv(c, send, recv, 480, 48000, 1, 5000);
    EXPECT(rc == PIPE_ERR_READ_TIMEOUT, "expected READ_TIMEOUT");
    EXPECT(pipe_client_state(c) == PIPE_DISCONNECTED, "should be DISCONNECTED after timeout");

    /* The slow server can only serve one connection at a time, but its sleep
     * happens after read. Stop it to allow a fresh server for the reconnect test. */
    stop_fake_server(srv);

    /* Restart in echo mode so the next call succeeds against a fresh handle. */
    HANDLE srv2 = start_fake_server("ZondelTestSlow", "echo");
    if (!srv2) { EXPECT(0, "could not start fake server 2"); pipe_client_destroy(c); return; }

    int rc2 = pipe_client_send_recv(c, send, recv, 480, 48000, 1, 5000);
    EXPECT(rc2 == PIPE_OK, "second call should reconnect on fresh handle and succeed");
    EXPECT(pipe_client_state(c) == PIPE_CONNECTED, "should be CONNECTED after recovery");

    pipe_client_destroy(c);
    stop_fake_server(srv2);
}

static void test_repeated_timeouts_do_not_leak_handles(void) {
    /* Spin up 10 slow servers in sequence; each connection times out. We don't
     * have a portable way to query "handles in use by this process" without
     * Windows debug APIs, but the very-loose check is that we can do this 10
     * times without crashing or exhausting the system. */
    for (int i = 0; i < 10; i++) {
        char pipe[64];
        snprintf(pipe, sizeof(pipe), "ZondelTestLeak%d", i);

        HANDLE srv = start_fake_server(pipe, "sleep-ms:100");
        if (!srv) { EXPECT(0, "could not start fake server (leak test)"); return; }

        char endpoint[128];
        snprintf(endpoint, sizeof(endpoint), "\\\\.\\pipe\\%s", pipe);
        pipe_client_t *c = pipe_client_create(endpoint);

        float send[480] = {0}, recv[480] = {0};
        int rc = pipe_client_send_recv(c, send, recv, 480, 48000, 1, 5000);
        EXPECT(rc == PIPE_ERR_READ_TIMEOUT, "expected timeout in leak loop");

        pipe_client_destroy(c);
        stop_fake_server(srv);
    }
}

int main(void) {
    test_no_server_returns_not_connected_quickly();
    test_round_trip_echo();
    test_bad_header_returns_protocol_error();
    test_timeout_closes_handle_and_next_call_reconnects();
    test_repeated_timeouts_do_not_leak_handles();
    if (failures) { fprintf(stderr, "%d test failure(s)\n", failures); return 1; }
    fprintf(stderr, "all tests passed\n");
    return 0;
}
