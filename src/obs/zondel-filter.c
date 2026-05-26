/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 *
 * Full adaptation pipeline: channel-gate → downmix → SRC → ring-buffer →
 * 480-block pipe round-trip → ring-buffer → SRC back → upmix.
 */
#include <obs-module.h>
#include <plugin-support.h>
#include "zondel-filter.h"
#include "zondel-state.h"
#include "zondel-launch.h"
#include "pipe-client.h"
#include "ring-buffer.h"
#include "resampler.h"
#include "downmix.h"
#include <util/platform.h>

static const char *status_text_for(int status, uint64_t now_ns_, uint64_t backoff_until_ns) {
    switch (status) {
        case ZS_CONNECTED:           return obs_module_text("Zondel.Status.Connected");
        case ZS_CONNECTING:          return obs_module_text("Zondel.Status.Connecting");
        case ZS_DISCONNECTED:        return obs_module_text("Zondel.Status.Disconnected");
        case ZS_UNSUPPORTED_FORMAT:  return obs_module_text("Zondel.Status.Unsupported");
        case ZS_BACKED_OFF: {
            (void)now_ns_; (void)backoff_until_ns;
            return obs_module_text("Zondel.Status.BackedOff");
        }
        default:                     return obs_module_text("Zondel.Status.NotRunning");
    }
}

typedef struct zondel_filter {
    obs_source_t   *source;
    pipe_client_t  *pipe;
    zondel_state_t  state;
    char            pipe_name[256];
    uint32_t        timeout_us;

    /* Cached input characteristics; rebuilt whenever they change. */
    uint32_t        cur_sample_rate;
    uint32_t        cur_speakers;

    /* SRC: only used when cur_sample_rate != 48000. */
    resampler_t     in_to_48k;     /* OBS rate -> 48k */
    resampler_t     out_to_obs;    /* 48k -> OBS rate */

    /* Scratch buffers (grow on demand, never shrink). */
    float          *mono_in;
    float          *srate_in;
    float          *scratch_send;
    float          *scratch_recv;
    float          *mono_out;
    float          *mono_back;
    size_t          scratch_capacity_frames;

    /* Ring buffers (in 48 kHz mono samples). */
    ring_buffer_t   send_ring;
    ring_buffer_t   recv_ring;
} zondel_filter_t;

static const char *zondel_get_name(void *type_data) {
    (void)type_data;
    return obs_module_text("Zondel.Filter.Name");
}

static int ensure_scratch(zondel_filter_t *f, size_t frames) {
    if (f->scratch_capacity_frames >= frames) return 0;
    f->scratch_capacity_frames = frames;
    f->mono_in      = brealloc(f->mono_in,      frames * sizeof(float));
    f->srate_in     = brealloc(f->srate_in,     (frames + 16) * sizeof(float));   /* SRC max-out + slack */
    f->scratch_send = brealloc(f->scratch_send, 480 * sizeof(float));
    f->scratch_recv = brealloc(f->scratch_recv, 480 * sizeof(float));
    f->mono_out     = brealloc(f->mono_out,     frames * sizeof(float));
    f->mono_back    = brealloc(f->mono_back,    (frames + 16) * sizeof(float));
    return 0;
}

static void *zondel_create(obs_data_t *settings, obs_source_t *source) {
    zondel_filter_t *f = bzalloc(sizeof(*f));
    f->source = source;
    zondel_state_init(&f->state);

    const char *ep = obs_data_get_string(settings, "pipe_endpoint");
    if (!ep || !*ep) ep = "\\\\.\\pipe\\Zondel";
    strncpy(f->pipe_name, ep, sizeof(f->pipe_name) - 1);

    int timeout_ms = (int)obs_data_get_int(settings, "pipe_timeout_ms");
    if (timeout_ms < 1) timeout_ms = 5;
    if (timeout_ms > 20) timeout_ms = 20;
    f->timeout_us = (uint32_t)timeout_ms * 1000;

    f->pipe = pipe_client_create(f->pipe_name);

    /* 4 frames of 48 kHz mono = 1920 samples of latency budget. Enough for
     * almost any OBS callback size up to 96 kHz. */
    ring_buffer_init(&f->send_ring, 8192);
    ring_buffer_init(&f->recv_ring, 8192);

    ensure_scratch(f, 2048);

    zondel_state_set_bypass(&f->state, obs_data_get_bool(settings, "bypass") ? 1 : 0);
    return f;
}

static void zondel_destroy(void *data) {
    zondel_filter_t *f = data;
    if (!f) return;
    if (f->pipe) pipe_client_destroy(f->pipe);
    ring_buffer_free(&f->send_ring);
    ring_buffer_free(&f->recv_ring);
    bfree(f->mono_in);
    bfree(f->srate_in);
    bfree(f->scratch_send);
    bfree(f->scratch_recv);
    bfree(f->mono_out);
    bfree(f->mono_back);
    bfree(f);
}

static void zondel_get_defaults(obs_data_t *s) {
    obs_data_set_default_string(s, "pipe_endpoint", "\\\\.\\pipe\\Zondel");
    obs_data_set_default_int(s, "pipe_timeout_ms", 5);
    obs_data_set_default_bool(s, "bypass", false);
}

static void reconfigure_resamplers(zondel_filter_t *f, uint32_t obs_rate) {
    if (obs_rate == f->cur_sample_rate) return;
    f->cur_sample_rate = obs_rate;
    if (obs_rate == 48000) {
        /* No SRC needed; reset state so we don't carry over stale history. */
        resampler_init(&f->in_to_48k, 48000, 48000);
        resampler_init(&f->out_to_obs, 48000, 48000);
    } else {
        resampler_init(&f->in_to_48k, obs_rate, 48000);
        resampler_init(&f->out_to_obs, 48000, obs_rate);
    }
    /* SRC change discards in-flight buffer state. */
    ring_buffer_reset(&f->send_ring);
    ring_buffer_reset(&f->recv_ring);
}

static uint64_t now_ns_(void) { return os_gettime_ns(); }

static struct obs_audio_data *zondel_filter_audio(void *data, struct obs_audio_data *audio) {
    zondel_filter_t *f = data;
    if (!f || !audio || !audio->data[0]) return audio;

    /* 1. Bypass / back-off / unsupported-format check. */
    if (zondel_state_should_skip(&f->state, now_ns_()))
        return audio;

    /* 2. Channel-count gate: support mono and stereo only. */
    struct obs_audio_info ai;
    obs_get_audio_info(&ai);
    int channels;
    switch (ai.speakers) {
        case SPEAKERS_MONO:   channels = 1; break;
        case SPEAKERS_STEREO: channels = 2; break;
        default:
            if (!f->state.logged_unsupported_format) {
                obs_log(LOG_WARNING,
                        "unsupported channel layout: speakers=%d (only MONO/STEREO supported in v1). "
                        "Audio will pass through unprocessed.", (int)ai.speakers);
            }
            zondel_state_on_unsupported_format(&f->state);
            return audio;
    }

    if (ai.samples_per_sec != f->cur_sample_rate || (uint32_t)ai.speakers != f->cur_speakers) {
        reconfigure_resamplers(f, ai.samples_per_sec);
        f->cur_speakers = (uint32_t)ai.speakers;
    }

    uint32_t obs_rate = ai.samples_per_sec;
    uint32_t frames   = audio->frames;
    ensure_scratch(f, frames);

    /* 3. Downmix to mono. */
    if (channels == 1)
        memcpy(f->mono_in, audio->data[0], frames * sizeof(float));
    else
        downmix_stereo_to_mono((const float *)audio->data[0],
                               (const float *)audio->data[1],
                               f->mono_in, frames);

    /* 4. Resample to 48k if needed; otherwise srate_in aliases mono_in. */
    float *src_48k_ptr;
    size_t src_48k_frames;
    if (obs_rate == 48000) {
        src_48k_ptr = f->mono_in;
        src_48k_frames = frames;
    } else {
        size_t cap = resampler_max_out_frames(&f->in_to_48k, frames);
        if (cap > f->scratch_capacity_frames + 16) ensure_scratch(f, cap);
        src_48k_frames = resampler_process(&f->in_to_48k, f->mono_in, frames, f->srate_in, cap);
        src_48k_ptr = f->srate_in;
    }

    /* 5a. Push the 48 kHz mono samples into the send ring buffer. */
    ring_buffer_push(&f->send_ring, src_48k_ptr, src_48k_frames);

    /* 5b. Drain in 480-sample blocks while we have enough data. */
    while (ring_buffer_fill(&f->send_ring) >= 480) {
        ring_buffer_pop(&f->send_ring, f->scratch_send, 480);

        int rc = pipe_client_send_recv(
            f->pipe,
            f->scratch_send, f->scratch_recv,
            480, 48000, 1,
            f->timeout_us);

        if (rc == PIPE_OK) {
            zondel_state_on_success(&f->state);
            ring_buffer_push(&f->recv_ring, f->scratch_recv, 480);
        } else {
            zondel_state_on_failure(&f->state, rc, now_ns_());
            /* Pass-through for this block: push the original samples to the recv ring. */
            ring_buffer_push(&f->recv_ring, f->scratch_send, 480);
        }
    }

    /* 6. Pull `src_48k_frames` worth of samples from the recv ring; zero-pad if short. */
    size_t want = src_48k_frames;
    size_t got = ring_buffer_pop(&f->recv_ring, f->mono_out, want);
    if (got < want) {
        memset(f->mono_out + got, 0, (want - got) * sizeof(float));
    }

    /* 7. Resample 48k -> obs_rate if needed. */
    float *out_at_obs_rate;
    size_t out_frames;
    if (obs_rate == 48000) {
        out_at_obs_rate = f->mono_out;
        out_frames = src_48k_frames;
    } else {
        size_t cap = resampler_max_out_frames(&f->out_to_obs, src_48k_frames);
        if (cap > f->scratch_capacity_frames + 16) ensure_scratch(f, cap);
        out_frames = resampler_process(&f->out_to_obs, f->mono_out, src_48k_frames, f->mono_back, cap);
        out_at_obs_rate = f->mono_back;
    }

    /* The downstream OBS expects exactly `frames` samples per channel. If the
     * SRC produced slightly fewer (rounding) or more (rare), truncate / zero-pad. */
    size_t copy_n = out_frames < frames ? out_frames : frames;

    /* 8. Upmix back to OBS channel layout. */
    if (channels == 1) {
        memcpy(audio->data[0], out_at_obs_rate, copy_n * sizeof(float));
        if (copy_n < frames)
            memset((char *)audio->data[0] + copy_n * sizeof(float), 0, (frames - copy_n) * sizeof(float));
    } else {
        upmix_mono_to_stereo(out_at_obs_rate,
                             (float *)audio->data[0], (float *)audio->data[1],
                             copy_n);
        if (copy_n < frames) {
            memset((char *)audio->data[0] + copy_n * sizeof(float), 0, (frames - copy_n) * sizeof(float));
            memset((char *)audio->data[1] + copy_n * sizeof(float), 0, (frames - copy_n) * sizeof(float));
        }
    }

    return audio;
}

static bool open_zondel_clicked(obs_properties_t *props, obs_property_t *prop, void *data) {
    (void)props; (void)prop; (void)data;
    zondel_launch_open_app();
    return false;
}

static obs_properties_t *zondel_get_properties(void *data) {
    zondel_filter_t *f = data;
    obs_properties_t *p = obs_properties_create();

    /* Status (read-only info text, rebuilt on every property-pane refresh). */
    int status = f ? f->state.status : ZS_UNKNOWN;
    obs_property_t *status_prop = obs_properties_add_text(
        p, "status", status_text_for(status, now_ns_(),
                                     f ? f->state.backoff_until_ns : 0),
        OBS_TEXT_INFO);
    (void)status_prop;

    /* Bypass toggle. */
    obs_properties_add_bool(p, "bypass", obs_module_text("Zondel.Property.Bypass"));

    /* Open Zondel button. */
    obs_properties_add_button(p, "open_zondel",
                              obs_module_text("Zondel.Property.OpenZondel"),
                              open_zondel_clicked);

    /* Advanced group (collapsed by default in OBS UI when added as group). */
    obs_properties_t *adv = obs_properties_create();
    obs_properties_add_text(adv, "pipe_endpoint",
                            obs_module_text("Zondel.Property.PipeEndpoint"),
                            OBS_TEXT_DEFAULT);
    obs_properties_add_int_slider(adv, "pipe_timeout_ms",
                                  obs_module_text("Zondel.Property.PipeTimeout"),
                                  1, 20, 1);
    obs_properties_add_group(p, "advanced",
                             obs_module_text("Zondel.Property.Advanced"),
                             OBS_GROUP_NORMAL, adv);

    return p;
}

static void zondel_update(void *data, obs_data_t *settings) {
    zondel_filter_t *f = data;
    if (!f) return;
    zondel_state_set_bypass(&f->state, obs_data_get_bool(settings, "bypass") ? 1 : 0);

    if (f->state.status == ZS_UNSUPPORTED_FORMAT) {
        f->state.status = ZS_UNKNOWN;
        f->state.logged_unsupported_format = 0;
    }

    /* Drop any pending ring contents so a sample-rate change doesn't surface stale audio. */
    ring_buffer_reset(&f->send_ring);
    ring_buffer_reset(&f->recv_ring);

    const char *ep = obs_data_get_string(settings, "pipe_endpoint");
    if (ep && *ep && strcmp(ep, f->pipe_name) != 0) {
        strncpy(f->pipe_name, ep, sizeof(f->pipe_name) - 1);
        if (f->pipe) pipe_client_destroy(f->pipe);
        f->pipe = pipe_client_create(f->pipe_name);
    }

    int timeout_ms = (int)obs_data_get_int(settings, "pipe_timeout_ms");
    if (timeout_ms >= 1 && timeout_ms <= 20)
        f->timeout_us = (uint32_t)timeout_ms * 1000;
}

static void zondel_tick(void *data, float seconds) {
    zondel_filter_t *f = data;
    (void)seconds;
    if (!f) return;
    obs_source_update_properties(f->source);
}

struct obs_source_info zondel_filter_info = {
    .id            = "zondel_audio_filter",
    .type          = OBS_SOURCE_TYPE_FILTER,
    .output_flags  = OBS_SOURCE_AUDIO,
    .get_name      = zondel_get_name,
    .create        = zondel_create,
    .destroy       = zondel_destroy,
    .update        = zondel_update,
    .get_defaults  = zondel_get_defaults,
    .get_properties = zondel_get_properties,
    .filter_audio  = zondel_filter_audio,
    .video_tick    = zondel_tick,    /* runs on UI thread; cheap */
};
