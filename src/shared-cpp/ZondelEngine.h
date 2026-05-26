/*
 * ZondelEngine — host-agnostic C++ wrapper around zondel-core that owns
 * one plugin instance's audio path.
 *
 * Per-instance pipeline state: input ring buffer, output ring buffer,
 * resampler pair (host SR ↔ 48 kHz), pipe client, connection-state
 * machine. Constructor allocates everything; process() is realtime-safe
 * (no allocation, no blocking syscalls beyond the bounded-timeout pipe
 * IPC).
 *
 * Used by:
 *   - src/vst3/ZondelProcessor.cpp  — wraps process() in VST3 AudioEffect
 *   - src/clap/zondel-clap.cpp      — wraps process() in CLAP plugin
 *
 * Threading model:
 *   - process() runs on the audio thread. Must not allocate.
 *   - status() / setPipeTimeoutMicros() are called from the UI / control
 *     thread. Lock-free reads of atomically-updated state.
 *
 * License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 */
#pragma once

#include <atomic>
#include <cstdint>

extern "C" {
#include "downmix.h"
#include "pipe-client.h"
#include "resampler.h"
#include "ring-buffer.h"
#include "zondel-state.h"
}

namespace zondel {

class Engine {
public:
    // Constructs the engine for a fixed sample rate and channel count.
    // maxBlockSize is the largest `frames` value that will ever be passed
    // to process(); buffers are sized to this once and never grow.
    //
    // pipeEndpoint = nullptr → default "\\\\.\\pipe\\Zondel".
    Engine(double sampleRate, int channels, int maxBlockSize,
           const char* pipeEndpoint = nullptr);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Realtime entry point. inputs[ch] and outputs[ch] point at planar
    // float32 buffers of `frames` samples each. `bypass` short-circuits
    // the entire pipeline to memcpy.
    //
    // Returns true if Zondel processed the audio, false if pass-through
    // (Zondel unreachable, format unsupported, or bypass). The wrapper
    // generally doesn't need this; status() exposes it via the state
    // machine.
    bool process(const float* const* inputs,
                 float* const* outputs,
                 int frames,
                 bool bypass) noexcept;

    // Plugin's contribution to host PDC, in samples at the host sample
    // rate. Currently constant for the engine's lifetime.
    uint32_t getLatencySamples() const noexcept;

    // Snapshot of the connection state. Safe to call from any thread.
    // updateCounter increments on every state mutation; the wrapper can
    // poll this and only refresh its UI when the counter has moved.
    struct StatusSnapshot {
        int status;             // values from enum zondel_status
        uint64_t updateCounter;
    };
    StatusSnapshot status() const noexcept;

    // Update the pipe round-trip timeout (microseconds). Default 5000.
    // Range 1000..20000. Out-of-range values are clamped silently.
    void setPipeTimeoutMicros(uint32_t us) noexcept;

private:
    void rebuildStatusSnapshot() noexcept;

    const double   _sampleRate;
    const int      _channels;
    const int      _maxBlockSize;

    pipe_client_t* _pipe = nullptr;
    zondel_state_t _state {};

    // SRC: 48 kHz path is the engine's contract with Zondel. host SR may
    // differ; the pair converts both directions. At 48 kHz host SR these
    // resamplers act as a passthrough.
    resampler_t    _inTo48k {};
    resampler_t    _outToHost {};
    bool           _srcActive = false;

    ring_buffer_t  _sendRing {};
    ring_buffer_t  _recvRing {};

    // Pre-allocated scratch (sized in ctor; never realloc'd).
    float*         _monoIn    = nullptr;  // downmixed input at host SR
    float*         _srate48k  = nullptr;  // resampled to 48k
    float*         _scratchSend = nullptr; // exactly 480 samples
    float*         _scratchRecv = nullptr; // exactly 480 samples
    float*         _monoOut   = nullptr;  // 48k mono response
    float*         _monoBack  = nullptr;  // resampled back to host SR
    size_t         _scratchFrames = 0;    // capacity of _monoIn/_monoOut

    std::atomic<uint32_t> _timeoutMicros { 5000 };
    std::atomic<uint64_t> _updateCounter { 0 };

    static constexpr int    kZondelChunk = 480;     // samples
    static constexpr double kZondelRate  = 48000.0;
};

} // namespace zondel
