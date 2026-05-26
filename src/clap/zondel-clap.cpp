/*
 * Zondel CLAP plug-in entry point + extensions.
 *
 * The DSP path is entirely in zondel::Engine (src/shared-cpp/) — same
 * code that backs the VST3 plug-in. This file is a thin wrapper that
 * adapts CLAP's v-table to the engine and declares the extensions the
 * host needs to render the plug-in correctly:
 *
 *   - audio-ports : 1 stereo input, 1 stereo output
 *   - params      : Bypass (kIsBypass) + Pipe timeout (kIsStepped)
 *   - state       : same int32+int32 blob as VST3
 *   - latency     : reports engine.getLatencySamples()
 *   - render      : hard-realtime requirement (Zondel app is realtime)
 *
 * License: MIT. Copyright (c) 2026 Zondel.
 */
#include "ZondelEngine.h"

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace {

// ────────────────────────────────────────────────────────────────────
// Plug-in identity
// ────────────────────────────────────────────────────────────────────

constexpr const char* kPluginId      = "com.zondel.bridge.clap";
constexpr const char* kPluginName    = "Zondel";
constexpr const char* kPluginVendor  = "Zondel";
constexpr const char* kPluginUrl     = "https://zondel.net";
constexpr const char* kPluginVersion = "0.1.0";
constexpr const char* kPluginDesc    = "Routes DAW audio through the Zondel desktop app.";

const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    "noise-suppressor",
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT,
    kPluginId,
    kPluginName,
    kPluginVendor,
    kPluginUrl,
    nullptr,                  // manual_url
    "https://github.com/smurz/zondel-bridge/issues",  // support_url
    kPluginVersion,
    kPluginDesc,
    kFeatures,
};

// ────────────────────────────────────────────────────────────────────
// Parameter identity. Matches VST3 ZondelParams.h so cross-format
// project recall would be coherent if a host ever asks.
// ────────────────────────────────────────────────────────────────────

enum : clap_id {
    kParamBypass      = 0,
    kParamPipeTimeout = 1,
};

constexpr double kPipeTimeoutMin = 1000.0;   // µs
constexpr double kPipeTimeoutMax = 20000.0;  // µs

// ────────────────────────────────────────────────────────────────────
// Per-instance state
// ────────────────────────────────────────────────────────────────────

struct ZondelClap {
    clap_plugin_t plugin;
    const clap_host_t* host = nullptr;

    std::unique_ptr<zondel::Engine> engine;

    std::atomic<double> bypassValue  { 0.0 };       // CLAP stores plain values
    std::atomic<double> timeoutValue { 5000.0 };
};

inline ZondelClap* self(const clap_plugin_t* p) noexcept {
    return static_cast<ZondelClap*>(p->plugin_data);
}

// ────────────────────────────────────────────────────────────────────
// clap_plugin_t v-table
// ────────────────────────────────────────────────────────────────────

bool CLAP_ABI plugin_init(const clap_plugin_t* /*plugin*/) { return true; }

void CLAP_ABI plugin_destroy(const clap_plugin_t* plugin) {
    delete self(plugin);
}

bool CLAP_ABI plugin_activate(const clap_plugin_t* plugin,
                              double               sample_rate,
                              uint32_t             /*min_frames*/,
                              uint32_t             max_frames) {
    auto* s = self(plugin);
    // CLAP doesn't pre-negotiate the channel count: we promised stereo
    // via audio-ports. Reuse that here.
    s->engine = std::make_unique<zondel::Engine>(
        sample_rate, 2, static_cast<int>(max_frames), nullptr);
    s->engine->setPipeTimeoutMicros(static_cast<uint32_t>(s->timeoutValue.load()));
    return true;
}

void CLAP_ABI plugin_deactivate(const clap_plugin_t* plugin) {
    self(plugin)->engine.reset();
}

bool CLAP_ABI plugin_start_processing(const clap_plugin_t*) { return true; }
void CLAP_ABI plugin_stop_processing(const clap_plugin_t*)  {}
void CLAP_ABI plugin_reset(const clap_plugin_t*)            {}

// Apply a single param value to engine state. Called from process().
void apply_param(ZondelClap* s, clap_id id, double value) {
    switch (id) {
        case kParamBypass:
            s->bypassValue.store(value >= 0.5 ? 1.0 : 0.0);
            break;
        case kParamPipeTimeout: {
            const double clamped = std::clamp(value, kPipeTimeoutMin, kPipeTimeoutMax);
            s->timeoutValue.store(clamped);
            if (s->engine) s->engine->setPipeTimeoutMicros(static_cast<uint32_t>(clamped));
            break;
        }
        default: break;
    }
}

clap_process_status CLAP_ABI plugin_process(const clap_plugin_t*  plugin,
                                            const clap_process_t* proc) {
    auto* s = self(plugin);

    // Drain input events. CLAP delivers param changes via the event
    // stream; pull the latest value for each param we care about.
    if (proc->in_events) {
        const uint32_t n = proc->in_events->size(proc->in_events);
        for (uint32_t i = 0; i < n; ++i) {
            const clap_event_header_t* h = proc->in_events->get(proc->in_events, i);
            if (!h) continue;
            if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
            if (h->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
                apply_param(s, ev->param_id, ev->value);
            }
        }
    }

    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0)
        return CLAP_PROCESS_CONTINUE;

    const auto& in  = proc->audio_inputs[0];
    auto&       out = proc->audio_outputs[0];
    const int32_t frames = static_cast<int32_t>(proc->frames_count);
    if (frames <= 0) return CLAP_PROCESS_CONTINUE;

    // Only 32-bit float is supported. If the host gave us 64-bit
    // pointers only, fall back to silence (shouldn't happen — we don't
    // claim 64-bit support via audio-ports flags).
    if (in.data32 == nullptr || out.data32 == nullptr) {
        for (uint32_t ch = 0; ch < out.channel_count; ++ch) {
            if (out.data32 && out.data32[ch])
                std::memset(out.data32[ch], 0, sizeof(float) * static_cast<size_t>(frames));
        }
        return CLAP_PROCESS_CONTINUE;
    }

    if (!s->engine) {
        // Pass-through if not activated. Shouldn't happen but defensive.
        const uint32_t chans = std::min(in.channel_count, out.channel_count);
        for (uint32_t ch = 0; ch < chans; ++ch) {
            std::memcpy(out.data32[ch], in.data32[ch],
                        sizeof(float) * static_cast<size_t>(frames));
        }
        return CLAP_PROCESS_CONTINUE;
    }

    const bool bypass = (s->bypassValue.load() >= 0.5);
    s->engine->process(in.data32, out.data32, frames, bypass);
    return CLAP_PROCESS_CONTINUE;
}

void CLAP_ABI plugin_on_main_thread(const clap_plugin_t*) {}

// Extension forward decls — defined below.
extern const clap_plugin_audio_ports_t kAudioPortsExt;
extern const clap_plugin_params_t      kParamsExt;
extern const clap_plugin_state_t       kStateExt;
extern const clap_plugin_latency_t     kLatencyExt;
extern const clap_plugin_render_t      kRenderExt;

const void* CLAP_ABI plugin_get_extension(const clap_plugin_t* /*plugin*/, const char* id) {
    if (id == nullptr) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPortsExt;
    if (std::strcmp(id, CLAP_EXT_PARAMS)      == 0) return &kParamsExt;
    if (std::strcmp(id, CLAP_EXT_STATE)       == 0) return &kStateExt;
    if (std::strcmp(id, CLAP_EXT_LATENCY)     == 0) return &kLatencyExt;
    if (std::strcmp(id, CLAP_EXT_RENDER)      == 0) return &kRenderExt;
    return nullptr;
}

// ────────────────────────────────────────────────────────────────────
// audio-ports extension
// ────────────────────────────────────────────────────────────────────

uint32_t CLAP_ABI ports_count(const clap_plugin_t* /*p*/, bool /*is_input*/) {
    return 1;
}

bool CLAP_ABI ports_get(const clap_plugin_t* /*p*/, uint32_t index,
                        bool /*is_input*/, clap_audio_port_info_t* info) {
    if (index != 0 || !info) return false;
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "Stereo");
    info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type     = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t kAudioPortsExt = { ports_count, ports_get };

// ────────────────────────────────────────────────────────────────────
// params extension
// ────────────────────────────────────────────────────────────────────

uint32_t CLAP_ABI params_count(const clap_plugin_t*) { return 2; }

bool CLAP_ABI params_get_info(const clap_plugin_t* /*p*/, uint32_t index,
                              clap_param_info_t* info) {
    if (!info) return false;
    std::memset(info, 0, sizeof(*info));
    switch (index) {
        case 0:
            info->id            = kParamBypass;
            info->flags         = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS |
                                  CLAP_PARAM_IS_AUTOMATABLE;
            std::snprintf(info->name, sizeof(info->name), "Bypass");
            info->min_value     = 0;
            info->max_value     = 1;
            info->default_value = 0;
            return true;
        case 1:
            info->id            = kParamPipeTimeout;
            info->flags         = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
            std::snprintf(info->name, sizeof(info->name), "Pipe timeout");
            info->min_value     = kPipeTimeoutMin;
            info->max_value     = kPipeTimeoutMax;
            info->default_value = 5000;
            return true;
        default:
            return false;
    }
}

bool CLAP_ABI params_get_value(const clap_plugin_t* plugin, clap_id id,
                               double* out_value) {
    if (!out_value) return false;
    auto* s = self(plugin);
    switch (id) {
        case kParamBypass:      *out_value = s->bypassValue.load();  return true;
        case kParamPipeTimeout: *out_value = s->timeoutValue.load(); return true;
        default: return false;
    }
}

bool CLAP_ABI params_value_to_text(const clap_plugin_t*, clap_id id, double value,
                                   char* out_buffer, uint32_t out_buffer_capacity) {
    if (!out_buffer || out_buffer_capacity == 0) return false;
    switch (id) {
        case kParamBypass:
            std::snprintf(out_buffer, out_buffer_capacity,
                          value >= 0.5 ? "On" : "Off");
            return true;
        case kParamPipeTimeout:
            std::snprintf(out_buffer, out_buffer_capacity, "%d \xC2\xB5s",
                          static_cast<int>(value + 0.5));
            return true;
        default: return false;
    }
}

bool CLAP_ABI params_text_to_value(const clap_plugin_t*, clap_id id,
                                   const char* text, double* out_value) {
    if (!text || !out_value) return false;
    switch (id) {
        case kParamBypass:
            *out_value = (std::strstr(text, "On") || std::strstr(text, "on") ||
                          std::strcmp(text, "1") == 0) ? 1.0 : 0.0;
            return true;
        case kParamPipeTimeout: {
            int us = std::atoi(text);
            if (us <= 0) return false;
            *out_value = std::clamp(static_cast<double>(us),
                                    kPipeTimeoutMin, kPipeTimeoutMax);
            return true;
        }
        default: return false;
    }
}

void CLAP_ABI params_flush(const clap_plugin_t* plugin,
                           const clap_input_events_t*  in,
                           const clap_output_events_t* /*out*/) {
    auto* s = self(plugin);
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        if (!h) continue;
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (h->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
            apply_param(s, ev->param_id, ev->value);
        }
    }
}

const clap_plugin_params_t kParamsExt = {
    params_count, params_get_info, params_get_value,
    params_value_to_text, params_text_to_value, params_flush,
};

// ────────────────────────────────────────────────────────────────────
// state extension — same two-int32 wire format as VST3
// ────────────────────────────────────────────────────────────────────

bool CLAP_ABI state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    auto* s = self(plugin);
    int32_t bypassWord  = (s->bypassValue.load() >= 0.5) ? 1 : 0;
    int32_t timeoutWord = static_cast<int32_t>(s->timeoutValue.load() + 0.5);
    auto writeAll = [stream](const void* buf, size_t n) {
        size_t total = 0;
        while (total < n) {
            int64_t w = stream->write(stream, static_cast<const uint8_t*>(buf) + total,
                                       n - total);
            if (w <= 0) return false;
            total += static_cast<size_t>(w);
        }
        return true;
    };
    if (!writeAll(&bypassWord, sizeof(bypassWord))) return false;
    if (!writeAll(&timeoutWord, sizeof(timeoutWord))) return false;
    return true;
}

bool CLAP_ABI state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    auto* s = self(plugin);
    auto readAll = [stream](void* buf, size_t n) {
        size_t total = 0;
        while (total < n) {
            int64_t r = stream->read(stream, static_cast<uint8_t*>(buf) + total,
                                      n - total);
            if (r <= 0) return false;
            total += static_cast<size_t>(r);
        }
        return true;
    };
    int32_t bypassWord = 0, timeoutWord = 5000;
    if (!readAll(&bypassWord, sizeof(bypassWord))) return false;
    if (!readAll(&timeoutWord, sizeof(timeoutWord))) return false;
    timeoutWord = std::clamp(timeoutWord, static_cast<int32_t>(kPipeTimeoutMin),
                                          static_cast<int32_t>(kPipeTimeoutMax));
    s->bypassValue.store(bypassWord ? 1.0 : 0.0);
    s->timeoutValue.store(static_cast<double>(timeoutWord));
    if (s->engine) s->engine->setPipeTimeoutMicros(static_cast<uint32_t>(timeoutWord));
    return true;
}

const clap_plugin_state_t kStateExt = { state_save, state_load };

// ────────────────────────────────────────────────────────────────────
// latency extension — same value as the VST3 plugin
// ────────────────────────────────────────────────────────────────────

uint32_t CLAP_ABI latency_get(const clap_plugin_t* plugin) {
    auto* s = self(plugin);
    return s->engine ? s->engine->getLatencySamples() : 0;
}

const clap_plugin_latency_t kLatencyExt = { latency_get };

// ────────────────────────────────────────────────────────────────────
// render extension — hard realtime requirement
// ────────────────────────────────────────────────────────────────────

bool CLAP_ABI render_has_hard_realtime_requirement(const clap_plugin_t*) {
    // The Zondel app processes on its own clock; offline rendering
    // can't sync to it without quality loss. Tell the host so.
    return true;
}

bool CLAP_ABI render_set(const clap_plugin_t*, clap_plugin_render_mode mode) {
    // We accept only the realtime mode. CLAP hosts that respect
    // has_hard_realtime_requirement won't ask for offline.
    return mode == CLAP_RENDER_REALTIME;
}

const clap_plugin_render_t kRenderExt = { render_has_hard_realtime_requirement, render_set };

// ────────────────────────────────────────────────────────────────────
// Factory + entry point
// ────────────────────────────────────────────────────────────────────

const clap_plugin_t* CLAP_ABI factory_create_plugin(const clap_plugin_factory_t* /*factory*/,
                                                    const clap_host_t* host,
                                                    const char*       plugin_id) {
    if (!plugin_id || std::strcmp(plugin_id, kPluginId) != 0) return nullptr;
    if (!clap_version_is_compatible(host->clap_version)) return nullptr;

    auto* s = new (std::nothrow) ZondelClap;
    if (!s) return nullptr;
    s->host = host;
    s->plugin.desc           = &kDescriptor;
    s->plugin.plugin_data    = s;
    s->plugin.init           = plugin_init;
    s->plugin.destroy        = plugin_destroy;
    s->plugin.activate       = plugin_activate;
    s->plugin.deactivate     = plugin_deactivate;
    s->plugin.start_processing = plugin_start_processing;
    s->plugin.stop_processing  = plugin_stop_processing;
    s->plugin.reset          = plugin_reset;
    s->plugin.process        = plugin_process;
    s->plugin.get_extension  = plugin_get_extension;
    s->plugin.on_main_thread = plugin_on_main_thread;
    return &s->plugin;
}

uint32_t CLAP_ABI factory_get_plugin_count(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* CLAP_ABI factory_get_plugin_descriptor(
    const clap_plugin_factory_t*, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_factory_t kPluginFactory = {
    factory_get_plugin_count,
    factory_get_plugin_descriptor,
    factory_create_plugin,
};

bool CLAP_ABI entry_init(const char* /*plugin_path*/) { return true; }
void CLAP_ABI entry_deinit() {}

const void* CLAP_ABI entry_get_factory(const char* factory_id) {
    if (factory_id && std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kPluginFactory;
    return nullptr;
}

} // anonymous namespace

extern "C" {
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    entry_init,
    entry_deinit,
    entry_get_factory,
};
}
