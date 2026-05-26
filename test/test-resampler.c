/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "../src/resampler.h"

static int failures = 0;
#define EXPECT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void test_identity_pass_through(void) {
    resampler_t r;
    resampler_init(&r, 48000, 48000);

    float in[1024], out[1024];
    for (int i = 0; i < 1024; i++) in[i] = (float)(i % 100) / 100.0f;
    memset(out, 0, sizeof(out));

    size_t produced = resampler_process(&r, in, 1024, out, 1024);
    EXPECT(produced >= 1020 && produced <= 1024, "identity should produce ~same count");

    /* In identity mode, first few samples lag by the history depth (3). After that, equal. */
    int mismatches = 0;
    for (size_t i = 4; i < produced; i++) {
        if (fabsf(out[i] - in[i - 4]) > 1e-5f) mismatches++;
    }
    EXPECT(mismatches == 0, "identity values match (after 4-sample warmup)");
}

static void test_sine_44100_to_48000_preserves_frequency(void) {
    resampler_t r;
    resampler_init(&r, 44100, 48000);

    /* 1 kHz sine at 44.1 kHz, 100 ms = 4410 samples */
    int N = 4410;
    float in[4410];
    for (int i = 0; i < N; i++) in[i] = sinf(2.0f * (float)M_PI * 1000.0f * (float)i / 44100.0f);

    size_t cap = resampler_max_out_frames(&r, (size_t)N);
    float *out = (float *)malloc(cap * sizeof(float));
    size_t produced = resampler_process(&r, in, N, out, cap);

    /* Coarse frequency check: count zero-crossings in the back half */
    int zc = 0;
    size_t start = produced / 2;
    for (size_t i = start + 1; i < produced; i++) {
        if ((out[i-1] >= 0.0f && out[i] < 0.0f) || (out[i-1] < 0.0f && out[i] >= 0.0f)) zc++;
    }
    /* Expected: 1 kHz × (produced/2 / 48000) × 2 crossings/cycle */
    double expected_zc = 1000.0 * ((double)(produced - start) / 48000.0) * 2.0;
    EXPECT(fabs((double)zc - expected_zc) < 5.0, "44.1k->48k preserves 1 kHz");

    free(out);
}

static void test_phase_coherence_across_calls(void) {
    /* Feed a continuous sine in two halves; verify the second half doesn't
     * have a glitch at the boundary. */
    resampler_t r;
    resampler_init(&r, 48000, 48000);  /* identity for clarity */

    int N = 1024;
    float in1[1024], in2[1024];
    for (int i = 0; i < N; i++) {
        in1[i] = sinf(2.0f * (float)M_PI * 1000.0f * (float)i / 48000.0f);
        in2[i] = sinf(2.0f * (float)M_PI * 1000.0f * (float)(i + N) / 48000.0f);
    }

    float out1[1024], out2[1024];
    size_t n1 = resampler_process(&r, in1, N, out1, 1024);
    size_t n2 = resampler_process(&r, in2, N, out2, 1024);

    EXPECT(n1 + n2 >= 2 * N - 10, "two halves produce ~2N samples total");

    /* Slope at the boundary: the diff between out1[last] and out2[first] should
     * match the local slope of the continuous sine, not have a discontinuity. */
    if (n1 > 0 && n2 > 0) {
        float boundary_step = fabsf(out2[0] - out1[n1 - 1]);
        EXPECT(boundary_step < 0.05f, "no boundary glitch >0.05 amplitude");
    }
}

int main(void) {
    test_identity_pass_through();
    test_sine_44100_to_48000_preserves_frequency();
    test_phase_coherence_across_calls();
    if (failures) { fprintf(stderr, "%d test failure(s)\n", failures); return 1; }
    fprintf(stderr, "all tests passed\n");
    return 0;
}
