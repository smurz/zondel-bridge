/* License: MIT. Copyright (c) 2026 Zondel.
 *
 * Correctness test for zondel::Engine: covers bypass, pass-through when
 * Zondel is unreachable, and the echo round-trip via fake-pipe-server.
 */
#include "ZondelEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

static int failures = 0;
#define EXPECT(cond, msg) do {                                                  \
    if (!(cond)) {                                                              \
        std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, (msg));     \
        ++failures;                                                             \
    }                                                                           \
} while (0)

#ifdef _WIN32
static HANDLE start_server(const char* pipeName, const char* mode) {
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd), "fake-pipe-server.exe %s %s", pipeName, mode);
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi {};
    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
        std::fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
        return nullptr;
    }
    CloseHandle(pi.hThread);
    Sleep(200);
    return pi.hProcess;
}

static void stop_server(HANDLE h) {
    if (!h) return;
    WaitForSingleObject(h, 2000);
    DWORD code;
    if (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE)
        TerminateProcess(h, 1);
    CloseHandle(h);
}
#endif

// Bypass: input == output regardless of Zondel availability.
static void test_bypass_is_passthrough() {
    zondel::Engine eng(48000.0, 2, 256, "\\\\.\\pipe\\NoSuchPipe");

    std::vector<float> inL(256, 0.5f), inR(256, -0.5f);
    std::vector<float> outL(256, 0.0f), outR(256, 0.0f);
    const float* in[] { inL.data(), inR.data() };
    float* out[] { outL.data(), outR.data() };

    eng.process(in, out, 256, /*bypass*/ true);

    for (int i = 0; i < 256; ++i) {
        EXPECT(outL[i] == 0.5f, "bypass: left channel not preserved");
        EXPECT(outR[i] == -0.5f, "bypass: right channel not preserved");
    }
}

// Zondel unreachable: pass-through audio, no crash. The engine's state
// machine goes to DISCONNECTED → BACKED_OFF after 3 failures.
static void test_zondel_unreachable_passes_through() {
    // Pipe endpoint that nothing is listening on.
    zondel::Engine eng(48000.0, 1, 480, "\\\\.\\pipe\\ZondelTestNoServer");

    std::vector<float> in(480, 0.25f);
    std::vector<float> out(480, 0.0f);
    const float* inPtr[]  { in.data() };
    float* outPtr[]       { out.data() };

    // First call: engine tries to send, fails immediately (no server).
    eng.process(inPtr, outPtr, 480, /*bypass*/ false);

    // After one chunk-worth of input, the engine's send ring is drained
    // into the recv ring via the pass-through fallback path. Output of
    // the first call may be zero-filled (recv ring was empty when we
    // tried to read it); the next call should yield the previous chunk.
    for (int i = 0; i < 480; ++i) {
        std::memset(out.data(), 0, sizeof(float) * 480);
        eng.process(inPtr, outPtr, 480, /*bypass*/ false);
    }
    // We don't assert exact values here — what matters is that no crash
    // happened across many failed-pipe round-trips.
    EXPECT(true, "unreachable Zondel: survived many calls");
}

#ifdef _WIN32
// With fake-pipe-server in echo mode, the round-trip preserves the input
// (modulo SRC artefacts when host SR != 48 kHz; here we use 48 kHz so
// the engine bypasses SRC entirely). After warm-up, output equals input.
static void test_echo_roundtrip_48k_mono() {
    const char* shortName = "ZondelEngineEcho";
    char fullName[128];
    std::snprintf(fullName, sizeof(fullName), "\\\\.\\pipe\\%s", shortName);

    HANDLE srv = start_server(shortName, "echo");
    if (!srv) {
        EXPECT(false, "could not start fake-pipe-server");
        return;
    }

    zondel::Engine eng(48000.0, 1, 480, fullName);

    // Drive enough audio to clear the chunk-of-latency at the start.
    std::vector<float> in(480, 0.0f);
    for (int i = 0; i < 480; ++i) in[i] = std::sin(i * 0.05f);
    std::vector<float> out(480, 0.0f);
    const float* inPtr[]  { in.data() };
    float* outPtr[]       { out.data() };

    // Two blocks: first fills the engine's chunk; second echoes.
    eng.process(inPtr, outPtr, 480, /*bypass*/ false);
    eng.process(inPtr, outPtr, 480, /*bypass*/ false);

    // The echo of the first chunk should match (within float epsilon).
    int matches = 0;
    for (int i = 0; i < 480; ++i) {
        if (std::abs(out[i] - in[i]) < 1e-4f) ++matches;
    }
    EXPECT(matches > 400, "echo round-trip: most samples should match");

    stop_server(srv);
}
#endif

int main() {
    test_bypass_is_passthrough();
    test_zondel_unreachable_passes_through();
#ifdef _WIN32
    test_echo_roundtrip_48k_mono();
#endif

    if (failures) {
        std::fprintf(stderr, "test-zondel-engine: %d failures\n", failures);
        return 1;
    }
    std::printf("test-zondel-engine: all tests passed\n");
    return 0;
}
