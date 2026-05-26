/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include "ZondelEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace zondel {

namespace {

uint64_t now_ns() noexcept {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

constexpr size_t kRingCapacity = 8192;  // matches OBS plugin
constexpr size_t kScratchSlack = 32;    // SRC overshoot tolerance

} // namespace

Engine::Engine(double sampleRate, int channels, int maxBlockSize,
               const char* pipeEndpoint)
    : _sampleRate(sampleRate)
    , _channels(channels)
    , _maxBlockSize(maxBlockSize) {

    zondel_state_init(&_state);

    if (!pipeEndpoint || !*pipeEndpoint)
        pipeEndpoint = "\\\\.\\pipe\\Zondel";
    _pipe = pipe_client_create(pipeEndpoint);

    ring_buffer_init(&_sendRing, kRingCapacity);
    ring_buffer_init(&_recvRing, kRingCapacity);

    _srcActive = (sampleRate != kZondelRate);
    if (_srcActive) {
        resampler_init(&_inTo48k,
                       static_cast<uint32_t>(sampleRate),
                       static_cast<uint32_t>(kZondelRate));
        resampler_init(&_outToHost,
                       static_cast<uint32_t>(kZondelRate),
                       static_cast<uint32_t>(sampleRate));
    }

    // Worst-case scratch sizing: maxBlockSize at host SR, plus SRC
    // overhead going up to 48 kHz (when host SR < 48 kHz, the resampled
    // output can be larger than the input).
    const double ratioUp = (sampleRate > 0.0) ? (kZondelRate / sampleRate) : 1.0;
    const size_t maxAt48k = static_cast<size_t>(
        static_cast<double>(maxBlockSize) * std::max(1.0, ratioUp) + kScratchSlack);
    const size_t scratchCap = std::max<size_t>(static_cast<size_t>(maxBlockSize),
                                                maxAt48k);

    _scratchFrames = scratchCap;
    _monoIn       = static_cast<float*>(std::calloc(scratchCap, sizeof(float)));
    _srate48k     = static_cast<float*>(std::calloc(scratchCap, sizeof(float)));
    _scratchSend  = static_cast<float*>(std::calloc(kZondelChunk, sizeof(float)));
    _scratchRecv  = static_cast<float*>(std::calloc(kZondelChunk, sizeof(float)));
    _monoOut      = static_cast<float*>(std::calloc(scratchCap, sizeof(float)));
    _monoBack     = static_cast<float*>(std::calloc(scratchCap, sizeof(float)));
}

Engine::~Engine() {
    if (_pipe) pipe_client_destroy(_pipe);
    ring_buffer_free(&_sendRing);
    ring_buffer_free(&_recvRing);
    std::free(_monoIn);
    std::free(_srate48k);
    std::free(_scratchSend);
    std::free(_scratchRecv);
    std::free(_monoOut);
    std::free(_monoBack);
}

void Engine::rebuildStatusSnapshot() noexcept {
    _updateCounter.fetch_add(1, std::memory_order_release);
}

Engine::StatusSnapshot Engine::status() const noexcept {
    StatusSnapshot s;
    s.status = _state.status;
    s.updateCounter = _updateCounter.load(std::memory_order_acquire);
    return s;
}

void Engine::setPipeTimeoutMicros(uint32_t us) noexcept {
    if (us < 1000) us = 1000;
    if (us > 20000) us = 20000;
    _timeoutMicros.store(us, std::memory_order_relaxed);
}

uint32_t Engine::getLatencySamples() const noexcept {
    // The engine introduces one chunk of delay (480 samples at 48 kHz)
    // because it accumulates a full block before sending. Convert to
    // host sample rate.
    return static_cast<uint32_t>(
        static_cast<double>(kZondelChunk) * _sampleRate / kZondelRate + 0.5);
}

bool Engine::process(const float* const* inputs,
                     float* const* outputs,
                     int frames,
                     bool bypass) noexcept {

    if (frames <= 0)
        return false;

    // Pass-through helper: planar memcpy from inputs to outputs.
    auto passThrough = [&]() {
        for (int ch = 0; ch < _channels; ++ch) {
            if (inputs[ch] != outputs[ch]) {
                std::memcpy(outputs[ch], inputs[ch],
                            sizeof(float) * static_cast<size_t>(frames));
            }
        }
    };

    if (bypass) {
        passThrough();
        return false;
    }

    if (zondel_state_should_skip(&_state, now_ns())) {
        passThrough();
        return false;
    }

    if (_channels != 1 && _channels != 2) {
        zondel_state_on_unsupported_format(&_state);
        rebuildStatusSnapshot();
        passThrough();
        return false;
    }

    // Defensive: if a host violates the maxBlockSize contract, fall back
    // to pass-through rather than overrun scratch. Should never happen
    // when wrappers honour setupProcessing/activate properly.
    if (frames > _maxBlockSize) {
        passThrough();
        return false;
    }

    // 1. Downmix to mono in scratch.
    if (_channels == 1) {
        std::memcpy(_monoIn, inputs[0], sizeof(float) * static_cast<size_t>(frames));
    } else {
        downmix_stereo_to_mono(inputs[0], inputs[1], _monoIn,
                               static_cast<size_t>(frames));
    }

    // 2. Resample to 48 kHz if needed.
    const float* src48k;
    size_t src48kFrames;
    if (!_srcActive) {
        src48k = _monoIn;
        src48kFrames = static_cast<size_t>(frames);
    } else {
        src48kFrames = resampler_process(&_inTo48k,
                                         _monoIn,
                                         static_cast<size_t>(frames),
                                         _srate48k,
                                         _scratchFrames);
        src48k = _srate48k;
    }

    // 3a. Push into send ring.
    ring_buffer_push(&_sendRing, src48k, src48kFrames);

    // 3b. Drain in 480-sample chunks.
    const uint32_t timeout = _timeoutMicros.load(std::memory_order_relaxed);
    int chunksProcessed = 0;
    while (ring_buffer_fill(&_sendRing) >= static_cast<size_t>(kZondelChunk)) {
        ring_buffer_pop(&_sendRing, _scratchSend, kZondelChunk);

        const int rc = pipe_client_send_recv(
            _pipe,
            _scratchSend, _scratchRecv,
            kZondelChunk, static_cast<uint32_t>(kZondelRate), 1,
            timeout);

        const int prevStatus = _state.status;
        if (rc == PIPE_OK) {
            zondel_state_on_success(&_state);
            ring_buffer_push(&_recvRing, _scratchRecv, kZondelChunk);
        } else {
            zondel_state_on_failure(&_state, rc, now_ns());
            // Per-block pass-through into the recv ring so latency stays
            // constant and audio doesn't drop.
            ring_buffer_push(&_recvRing, _scratchSend, kZondelChunk);
        }
        if (_state.status != prevStatus)
            rebuildStatusSnapshot();
        ++chunksProcessed;

        // Defensive: bound how many chunks we drain in one block to
        // avoid worst-case starvation on large frame inputs.
        if (chunksProcessed >= 4)
            break;
    }

    // 4. Pull src48kFrames worth from recv ring; zero-fill on short.
    size_t got = ring_buffer_pop(&_recvRing, _monoOut, src48kFrames);
    if (got < src48kFrames) {
        std::memset(_monoOut + got, 0, sizeof(float) * (src48kFrames - got));
    }

    // 5. Resample back to host SR.
    const float* outAtHost;
    size_t outFrames;
    if (!_srcActive) {
        outAtHost = _monoOut;
        outFrames = src48kFrames;
    } else {
        outFrames = resampler_process(&_outToHost,
                                      _monoOut, src48kFrames,
                                      _monoBack, _scratchFrames);
        outAtHost = _monoBack;
    }

    // 6. Upmix back to planar outputs.
    const size_t copyN = std::min<size_t>(static_cast<size_t>(frames), outFrames);
    if (_channels == 1) {
        std::memcpy(outputs[0], outAtHost, sizeof(float) * copyN);
        if (copyN < static_cast<size_t>(frames)) {
            std::memset(outputs[0] + copyN, 0,
                        sizeof(float) * (static_cast<size_t>(frames) - copyN));
        }
    } else {
        // Broadcast mono into both channels (mirrors OBS upmix path).
        upmix_mono_to_stereo(outAtHost, outputs[0], outputs[1], copyN);
        if (copyN < static_cast<size_t>(frames)) {
            const size_t tail = static_cast<size_t>(frames) - copyN;
            std::memset(outputs[0] + copyN, 0, sizeof(float) * tail);
            std::memset(outputs[1] + copyN, 0, sizeof(float) * tail);
        }
    }

    return true;
}

} // namespace zondel
