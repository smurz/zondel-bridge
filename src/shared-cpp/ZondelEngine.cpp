/* License: MIT. Copyright (c) 2026 Zondel. */
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

// Floor for the ring buffer capacity (48 kHz mono samples). The actual
// capacity is derived from maxBlockSize in the constructor — we need
// room for the worst-case host block resampled up to 48 kHz, the
// one-chunk PDC pre-fill in recv, and a few chunks of headroom while
// drain catches up. This floor covers up to ~8 k host samples
// comfortably; anything bigger gets a proportionally larger ring.
constexpr size_t kRingCapacityFloor = 16384;
constexpr size_t kScratchSlack     = 32;    // SRC overshoot tolerance

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

    // Ring capacity scaled to the worst case so a single very large host
    // block can drain enough chunks without overflowing recv (defends
    // against Codex's "8192-sample block + 17-chunk drain > 8192 ring"
    // overflow scenario).
    const size_t ringCap = std::max<size_t>(
        kRingCapacityFloor,
        maxAt48k * 2 + static_cast<size_t>(kZondelChunk) * 4);
    // ring_buffer_init returns 0 on success, non-zero on allocation
    // failure. primeRecvRing() and process() must not touch an
    // uninitialised ring (would modulo by zero capacity in
    // ring-buffer.c). Mark non-viable on either failure.
    if (ring_buffer_init(&_sendRing, ringCap) != 0 ||
        ring_buffer_init(&_recvRing, ringCap) != 0) {
        _viable = false;
    }
    _ringCapacity = ringCap;

    _scratchFrames = scratchCap;
    _monoIn       = static_cast<float*>(std::calloc(scratchCap, sizeof(float)));
    _srate48k     = static_cast<float*>(std::calloc(scratchCap, sizeof(float)));
    _scratchSend  = static_cast<float*>(std::calloc(kZondelChunk, sizeof(float)));
    _scratchRecv  = static_cast<float*>(std::calloc(kZondelChunk, sizeof(float)));
    _monoOut      = static_cast<float*>(std::calloc(scratchCap, sizeof(float)));
    _monoBack     = static_cast<float*>(std::calloc(scratchCap, sizeof(float)));
    if (!_monoIn || !_srate48k || !_scratchSend || !_scratchRecv ||
        !_monoOut || !_monoBack) {
        // OOM — process() will short-circuit to pass-through.
        _viable = false;
    }

    // Pre-fill the recv ring with one chunk of zeros so the steady-state
    // output is delayed by exactly one chunk (matching getLatencySamples
    // and host PDC). Without this, when the host block size doesn't line
    // up with kZondelChunk (e.g. 256/512/1024 at 48 kHz), recv would
    // sometimes underflow and we'd zero-pad mid-stream — audible clicks.
    primeRecvRing();
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
    // Each atomic is independently release/acquire-synchronised. The
    // counter is only used as a change-detector by the wrapper (see
    // ZondelProcessor::process); the snapshot can be "torn" across the
    // pair of loads but that is benign — the wrapper would just emit
    // one extra Data Exchange block with the latest status to catch up.
    _statusMirror.store(_state.status, std::memory_order_release);
    _updateCounter.fetch_add(1, std::memory_order_release);
}

Engine::StatusSnapshot Engine::status() const noexcept {
    StatusSnapshot s;
    s.updateCounter = _updateCounter.load(std::memory_order_acquire);
    s.status        = _statusMirror.load(std::memory_order_acquire);
    return s;
}

void Engine::setPipeTimeoutMicros(uint32_t us) noexcept {
    if (us < 1000) us = 1000;
    if (us > 20000) us = 20000;
    _timeoutMicros.store(us, std::memory_order_relaxed);
}

uint32_t Engine::getLatencySamples() const noexcept {
    // Non-viable engines fall through to pass-through with zero latency.
    if (!_viable) return 0;
    // The engine introduces one chunk of delay (480 samples at 48 kHz)
    // because it accumulates a full block before sending. Convert to
    // host sample rate.
    return static_cast<uint32_t>(
        static_cast<double>(kZondelChunk) * _sampleRate / kZondelRate + 0.5);
}

void Engine::primeRecvRing() noexcept {
    // Re-establish the steady-state one-chunk delay in recv after a
    // reset. `_scratchSend` is zero-initialised by calloc on construction
    // and gets zeroed again every time we send/receive, so we use it as
    // a convenient pre-fill source. (Even if it had stale content from a
    // previous chunk, that content was already pushed to the ring as the
    // round-trip result; we're just pushing zeros from its buffer.)
    if (!_viable) return;
    static const float zeros[kZondelChunk] = {0};
    ring_buffer_push(&_recvRing, zeros, kZondelChunk);
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

    // Engine in not-viable state (alloc failure in ctor) or pipe-client
    // creation failed — pass through quietly.
    if (!_viable || !_pipe) {
        passThrough();
        return false;
    }

    // Short-circuit transition: when entering bypass / back-off /
    // unsupported, the rings still hold pre-bypass delayed audio. Letting
    // them sit and then unbypassing causes that stale audio to surface
    // ~10 ms later. Reset and re-prime so resumed processing starts clean.
    const bool shortCircuit =
        bypass ||
        zondel_state_should_skip(&_state, now_ns()) ||
        (_channels != 1 && _channels != 2);

    if (shortCircuit) {
        if (!_inShortCircuit) {
            ring_buffer_reset(&_sendRing);
            ring_buffer_reset(&_recvRing);
            primeRecvRing();
            _inShortCircuit = true;
        }
        if (_channels != 1 && _channels != 2) {
            zondel_state_on_unsupported_format(&_state);
            rebuildStatusSnapshot();
        }
        passThrough();
        return false;
    }
    _inShortCircuit = false;

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
    // The drain cap must keep up with the host block — at large block
    // sizes (e.g. 2048 samples at 48 kHz) we need 4+ chunks per call to
    // avoid ring overflow / send-ring growth. Compute a dynamic ceiling
    // with +2 headroom for SRC overshoot, but also bound by recv free
    // space so we don't overrun the ring on absurd block sizes.
    const uint32_t timeout = _timeoutMicros.load(std::memory_order_relaxed);
    const size_t recvFree =
        _ringCapacity - ring_buffer_fill(&_recvRing);
    const int recvCapChunks = static_cast<int>(recvFree / kZondelChunk);
    const int demandChunks = std::max(4,
        static_cast<int>(src48kFrames / kZondelChunk) + 2);
    const int maxChunks = std::min(demandChunks, recvCapChunks);
    // After the first pipe failure in this block, stop issuing IPC for
    // the remaining chunks — each failed round-trip can take up to the
    // full timeout, so a backlogged block with 4+ chunks could hang the
    // audio thread for tens of milliseconds. Fall back to pass-through
    // via the recv ring so the engine stays in steady-state latency.
    int chunksProcessed = 0;
    bool pipeFailedThisBlock = false;
    while (ring_buffer_fill(&_sendRing) >= static_cast<size_t>(kZondelChunk)) {
        ring_buffer_pop(&_sendRing, _scratchSend, kZondelChunk);

        const int prevStatus = _state.status;
        if (!pipeFailedThisBlock) {
            const int rc = pipe_client_send_recv(
                _pipe,
                _scratchSend, _scratchRecv,
                kZondelChunk, static_cast<uint32_t>(kZondelRate), 1,
                timeout);
            if (rc == PIPE_OK) {
                zondel_state_on_success(&_state);
                ring_buffer_push(&_recvRing, _scratchRecv, kZondelChunk);
            } else {
                zondel_state_on_failure(&_state, rc, now_ns());
                ring_buffer_push(&_recvRing, _scratchSend, kZondelChunk);
                pipeFailedThisBlock = true;
            }
        } else {
            // Cheap pass-through path for remaining chunks this block.
            ring_buffer_push(&_recvRing, _scratchSend, kZondelChunk);
        }
        if (_state.status != prevStatus)
            rebuildStatusSnapshot();
        ++chunksProcessed;

        if (chunksProcessed >= maxChunks)
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
