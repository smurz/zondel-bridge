/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include "downmix.h"

void downmix_stereo_to_mono(const float *left, const float *right, float *dst, size_t frames) {
    for (size_t i = 0; i < frames; i++) dst[i] = 0.5f * (left[i] + right[i]);
}

void upmix_mono_to_stereo(const float *src, float *dst_left, float *dst_right, size_t frames) {
    for (size_t i = 0; i < frames; i++) { dst_left[i] = src[i]; dst_right[i] = src[i]; }
}
