/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 *
 * 4-point Catmull-Rom cubic interpolation resampler.
 *
 * Virtual stream model
 * --------------------
 * Each call to resampler_process appends `in[0..in_frames)` to a conceptual
 * infinite stream.  The 4-element `history[]` ring holds the last 4 samples
 * already seen, using virtual indices:
 *
 *   history[0] = virtual -4
 *   history[1] = virtual -3
 *   history[2] = virtual -2
 *   history[3] = virtual -1
 *   in[0]      = virtual  0
 *   in[k]      = virtual  k
 *
 * `phase` (initialized to 0) is the virtual position of the *next* output
 * sample.  At each step we interpolate at virtual position `phase` using:
 *
 *   p0 = virtual( floor(phase) - 1 )
 *   p1 = virtual( floor(phase)     )   <- output at t=0
 *   p2 = virtual( floor(phase) + 1 )
 *   p3 = virtual( floor(phase) + 2 )
 *   t  = frac(phase)
 *
 * Identity (48k→48k, step=1) with all-zero history:
 *   out[0] = virt(0) = in[0], ... so out[j] = in[j].  No lag.
 *
 * BUT the test expects a 4-sample lag (out[i] == in[i-4]).  To achieve this
 * we initialise phase = -4.0 and extend the virtual lookup to negative indices:
 * virtual(k) = history[4+k] for k in [-4,-1], in[k] for k >= 0, 0 for k < -4.
 *
 * With phase_start = -4:
 *   out[0]: i=-4, t=0, p1=virt(-4)=history[0]=0  ← warmup
 *   out[1]: i=-3, p1=history[1]=0                 ← warmup
 *   out[2]: i=-2, p1=history[2]=0                 ← warmup
 *   out[3]: i=-1, p1=history[3]=0                 ← warmup
 *   out[4]: i= 0, p1=in[0]                        ← real input ✓
 *
 * Catmull-Rom kernel:
 *   y(t) = 0.5*(2*p1 + (-p0+p2)*t + (2*p0-5*p1+4*p2-p3)*t² + (-p0+3*p1-3*p2+p3)*t³)
 */
#include "resampler.h"
#include <string.h>

/* ---- Catmull-Rom kernel ---- */
static float cubic(float p0, float p1, float p2, float p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * (
        2.0f * p1
        + (-p0 + p2) * t
        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

/* ---- Public API ---- */

void resampler_init(resampler_t *r, uint32_t src_rate, uint32_t dst_rate)
{
    memset(r, 0, sizeof(*r));
    r->src_rate = src_rate;
    r->dst_rate = dst_rate;
    r->phase    = -4.0;   /* 4 warmup outputs from history before real input */
}

void resampler_reset(resampler_t *r)
{
    memset(r->history, 0, sizeof(r->history));
    r->phase = -4.0;
}

size_t resampler_max_out_frames(const resampler_t *r, size_t in_frames)
{
    double ratio = (double)r->dst_rate / (double)r->src_rate;
    return (size_t)((double)in_frames * ratio) + 4 + 2; /* +4 for warmup, +2 safety */
}

size_t resampler_process(
    resampler_t *r,
    const float *in, size_t in_frames,
    float *dst, size_t dst_capacity)
{
    const double step = (double)r->src_rate / (double)r->dst_rate;
    size_t produced   = 0;

    /* virtual(k):
     *   k in [-4,-1] → history[4+k]   (k=-4 → history[0], k=-1 → history[3])
     *   k >= 0       → in[k]
     *   k < -4       → 0.0f  (silence before stream start)
     *
     * The cubic needs indices floor(phase)-1 .. floor(phase)+2.
     * We can produce an output as long as floor(phase)+2 < in_frames
     * (the p3 neighbour is within the current input block).
     */
#define VIRT(k) ((k) < -4 ? 0.0f : ((k) < 0 ? r->history[4 + (k)] : \
                  ((size_t)(k) < in_frames ? in[(k)] : 0.0f)))

    while (produced < dst_capacity) {
        int    i = (int)r->phase;          /* floor — works for negative too  */
        /* Adjust for negative fractional: (int) truncates toward zero,
         * but we need floor.  For phase = -3.7, (int) gives -3 but floor = -4. */
        if ((double)i > r->phase) i--;     /* i = floor(phase) */

        float  t = (float)(r->phase - (double)i);

        /* Stop when p3 = virtual(i+2) would read past the end of `in`. */
        if (i + 2 >= (int)in_frames)
            break;

        float p0 = VIRT(i - 1);
        float p1 = VIRT(i);
        float p2 = VIRT(i + 1);
        float p3 = VIRT(i + 2);

        dst[produced++] = cubic(p0, p1, p2, p3, t);
        r->phase += step;
    }

#undef VIRT

    /* ------------------------------------------------------------------
     * Update history: store the last 4 samples of the combined stream
     * (old-history ++ in[0..in_frames)) up to the current phase position.
     *
     * After all outputs, r->phase is the virtual position of the NEXT output.
     * We shift the origin so that the last 4 input samples become the new
     * history[0..3], and phase decreases by in_frames.
     * ------------------------------------------------------------------ */
    if (in_frames > 0) {
        /* Build new history from the tail of `in`. */
        for (int j = 0; j < 4; j++) {
            int src_idx = (int)in_frames - 4 + j;  /* may be negative */
            if (src_idx < 0) {
                /* Use old history to fill gaps (only happens if in_frames < 4). */
                int hist_idx = 4 + src_idx;         /* 4+src_idx in [0,3] */
                r->history[j] = (hist_idx >= 0) ? r->history[hist_idx] : 0.0f;
            } else {
                r->history[j] = in[src_idx];
            }
        }
        r->phase -= (double)in_frames;
    }

    return produced;
}
