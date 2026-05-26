/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#ifndef ZONDEL_RESAMPLER_H
#define ZONDEL_RESAMPLER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct resampler {
    uint32_t src_rate;
    uint32_t dst_rate;
    float    history[4];  /* trailing 4 samples from previous calls */
    double   phase;       /* fractional position in input space; 0 means "next out aligns with history[2]" */
} resampler_t;

void resampler_init(resampler_t *r, uint32_t src_rate, uint32_t dst_rate);
void resampler_reset(resampler_t *r);

/* Push `in_frames` of input, write up to `dst_capacity` output samples to `dst`.
 * Returns the number of output samples produced. */
size_t resampler_process(
    resampler_t *r,
    const float *in, size_t in_frames,
    float *dst, size_t dst_capacity);

/* Convenience: max possible output count for given input + ratio (over-estimate, +2 safety). */
size_t resampler_max_out_frames(const resampler_t *r, size_t in_frames);

#ifdef __cplusplus
}
#endif
#endif
