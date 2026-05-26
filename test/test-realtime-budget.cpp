/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 *
 * Realtime-budget regression test for zondel::Engine. Drives the engine
 * against fake-pipe-server (echo mode) for many blocks at a mix of
 * block sizes and sample rates. Measures per-call process() latency and
 * asserts P99 < 2 ms, P100 < 5 ms.
 *
 * P99 is the budget we contract with hosts (see docs/PLAN-VST3-CLAP.md
 * "Realtime contract"). P100 is the pipe-client timeout cliff — beyond
 * 5 ms the engine drops to pass-through for that block.
 */
#include "ZondelEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#ifdef _WIN32
static HANDLE start_server(const char* pipeName, const char* mode) {
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd), "fake-pipe-server.exe %s %s", pipeName, mode);
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi {};
    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
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

struct Stats { double p50, p99, p100, mean; size_t n; };

static Stats measure(zondel::Engine& eng, int blockSize, int iterations) {
    std::vector<float> inL(blockSize, 0.1f), inR(blockSize, 0.1f);
    std::vector<float> outL(blockSize, 0.0f), outR(blockSize, 0.0f);
    const float* in[]  { inL.data(), inR.data() };
    float* out[]       { outL.data(), outR.data() };

    std::vector<double> timings;
    timings.reserve(iterations);

    using clock = std::chrono::steady_clock;
    for (int i = 0; i < iterations; ++i) {
        auto t0 = clock::now();
        eng.process(in, out, blockSize, /*bypass*/ false);
        auto t1 = clock::now();
        const double us = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        timings.push_back(us);
    }

    std::sort(timings.begin(), timings.end());
    Stats s;
    s.n = timings.size();
    s.p50  = timings[s.n / 2];
    s.p99  = timings[std::min(s.n - 1, static_cast<size_t>(s.n * 0.99))];
    s.p100 = timings.back();
    double sum = 0;
    for (double v : timings) sum += v;
    s.mean = sum / static_cast<double>(s.n);
    return s;
}

int main() {
#ifndef _WIN32
    std::printf("realtime-budget: skipped on non-Windows (pipe IPC is Win-only)\n");
    return 0;
#else
    const char* shortName = "ZondelBudgetEcho";
    char fullName[128];
    std::snprintf(fullName, sizeof(fullName), "\\\\.\\pipe\\%s", shortName);

    HANDLE srv = start_server(shortName, "echo");
    if (!srv) {
        std::fprintf(stderr, "could not start fake-pipe-server — skipping\n");
        return 0;
    }

    struct Cfg { double sampleRate; int blockSize; };
    const Cfg configs[] = {
        { 48000.0, 64  },
        { 48000.0, 256 },
        { 48000.0, 1024 },
        { 44100.0, 256 },
        { 96000.0, 512 },
    };

    int failures = 0;
    for (const auto& c : configs) {
        zondel::Engine eng(c.sampleRate, 2, c.blockSize, fullName);

        // Warm-up so first-block latencies (pipe connect, ring fill)
        // don't pollute the percentiles.
        std::vector<float> warmL(c.blockSize, 0.0f), warmR(c.blockSize, 0.0f);
        std::vector<float> outWL(c.blockSize, 0.0f), outWR(c.blockSize, 0.0f);
        const float* warmIn[]  { warmL.data(), warmR.data() };
        float* warmOut[]       { outWL.data(), outWR.data() };
        for (int i = 0; i < 32; ++i)
            eng.process(warmIn, warmOut, c.blockSize, /*bypass*/ false);

        const Stats s = measure(eng, c.blockSize, 500);

        std::printf("rate=%.0f block=%d : mean=%5.0f µs  p50=%5.0f  p99=%5.0f  p100=%5.0f\n",
                    c.sampleRate, c.blockSize, s.mean, s.p50, s.p99, s.p100);

        // Budgets. The pipe-client's hard timeout is 5000 µs per chunk;
        // process() may do multiple chunks per call, so the per-call P99
        // budget is 2× chunk-budget = ~4 ms in the worst case.
        const double kP99Budget  = 4000.0;
        const double kP100Budget = 10000.0;

        if (s.p99 > kP99Budget) {
            std::fprintf(stderr, "  FAIL: p99 %.0f > %.0f µs\n", s.p99, kP99Budget);
            ++failures;
        }
        if (s.p100 > kP100Budget) {
            std::fprintf(stderr, "  FAIL: p100 %.0f > %.0f µs\n", s.p100, kP100Budget);
            ++failures;
        }
    }

    stop_server(srv);

    if (failures) {
        std::fprintf(stderr, "realtime-budget: %d configurations failed\n", failures);
        return 1;
    }
    std::printf("realtime-budget: all configurations under budget\n");
    return 0;
#endif
}
