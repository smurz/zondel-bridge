/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#ifndef ZONDEL_DOWNMIX_H
#define ZONDEL_DOWNMIX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* dst[i] = 0.5f * (left[i] + right[i])   — average, no clipping risk for [-1,1] inputs */
void downmix_stereo_to_mono(const float *left, const float *right, float *dst, size_t frames);

/* dst_left[i] = dst_right[i] = src[i]    — broadcast mono into both stereo channels */
void upmix_mono_to_stereo(const float *src, float *dst_left, float *dst_right, size_t frames);

#ifdef __cplusplus
}
#endif
#endif
