/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include "ZondelProcessor.h"
#include "ZondelIDs.h"
#include "ZondelParams.h"

#include "ZondelEngine.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstring>

namespace Zondel {

using namespace Steinberg;
using namespace Steinberg::Vst;

ZondelProcessor::ZondelProcessor() {
    setControllerClass(kZondelControllerUID);
}

ZondelProcessor::~ZondelProcessor() = default;

tresult PLUGIN_API ZondelProcessor::initialize(FUnknown* context) {
    tresult r = AudioEffect::initialize(context);
    if (r != kResultOk)
        return r;

    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    return kResultOk;
}

tresult PLUGIN_API ZondelProcessor::terminate() {
    _engine.reset();
    return AudioEffect::terminate();
}

tresult PLUGIN_API ZondelProcessor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts) {

    if (numIns != 1 || numOuts != 1)
        return kResultFalse;

    const SpeakerArrangement in  = inputs[0];
    const SpeakerArrangement out = outputs[0];

    if (in != out)
        return kResultFalse;
    if (in != SpeakerArr::kMono && in != SpeakerArr::kStereo)
        return kResultFalse;

    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API ZondelProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
    return (symbolicSampleSize == kSample32) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API ZondelProcessor::setupProcessing(ProcessSetup& newSetup) {
    // setupProcessing is host-thread, plugin inactive: safe to allocate.
    tresult r = AudioEffect::setupProcessing(newSetup);
    if (r != kResultOk)
        return r;

    SpeakerArrangement arr {};
    if (getBusArrangement(BusDirections::kInput, 0, arr) != kResultOk)
        return kResultOk; // Will retry on next setActive cycle.

    const int channels = SpeakerArr::getChannelCount(arr);
    if (channels != 1 && channels != 2)
        return kResultOk; // Unsupported arrangement; engine stays null, process() passes through.

    _engine = std::make_unique<zondel::Engine>(
        newSetup.sampleRate,
        channels,
        static_cast<int>(newSetup.maxSamplesPerBlock),
        nullptr);
    _engine->setPipeTimeoutMicros(_pipeTimeoutUs);

    return kResultOk;
}

tresult PLUGIN_API ZondelProcessor::setActive(TBool state) {
    // No allocation here — setActive may be called on the audio thread.
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API ZondelProcessor::process(ProcessData& data) {
    // Drain parameter changes once per block.
    if (auto changes = data.inputParameterChanges) {
        const int32 nq = changes->getParameterCount();
        for (int32 q = 0; q < nq; ++q) {
            if (auto queue = changes->getParameterData(q)) {
                ParamValue value;
                int32 sampleOffset;
                const int32 n = queue->getPointCount();
                if (n > 0 && queue->getPoint(n - 1, sampleOffset, value) == kResultTrue) {
                    switch (queue->getParameterId()) {
                        case kParamBypass:
                            _bypass = (value >= 0.5);
                            break;
                        case kParamPipeTimeout: {
                            // Stored normalised; convert back to microseconds in [1000, 20000].
                            uint32_t us = static_cast<uint32_t>(
                                kPipeTimeoutMinUs +
                                value * (kPipeTimeoutMaxUs - kPipeTimeoutMinUs) + 0.5);
                            _pipeTimeoutUs = us;
                            if (_engine) _engine->setPipeTimeoutMicros(us);
                            break;
                        }
                        default: break;
                    }
                }
            }
        }
    }

    if (data.numSamples <= 0 || data.numInputs == 0 || data.numOutputs == 0)
        return kResultTrue;

    const auto& inBus = data.inputs[0];
    auto&       outBus = data.outputs[0];
    const int32 frames = data.numSamples;
    const int32 chans  = (inBus.numChannels < outBus.numChannels)
                             ? inBus.numChannels : outBus.numChannels;

    // Helper for the pass-through fallback paths.
    auto passThrough = [&]() {
        for (int32 ch = 0; ch < chans; ++ch) {
            if (inBus.channelBuffers32[ch] != outBus.channelBuffers32[ch]) {
                std::memcpy(outBus.channelBuffers32[ch],
                            inBus.channelBuffers32[ch],
                            sizeof(float) * static_cast<size_t>(frames));
            }
        }
        outBus.silenceFlags = inBus.silenceFlags;
    };

    // Offline / prefetch rendering: pass through. The Zondel app is a
    // realtime DSP; rendering offline-faster-than-realtime would race
    // the pipe round-trip.
    if (data.processMode == kOffline || data.processMode == kPrefetch) {
        passThrough();
        return kResultOk;
    }

    // Null-engine guard: setupProcessing didn't build one (e.g. host
    // called with an unsupported arrangement). Pass-through, never crash.
    if (!_engine) {
        passThrough();
        return kResultOk;
    }

    // Bypass shortcut. Engine handles bypass internally but doing it
    // here lets us skip the pipe round-trip entirely.
    const bool processedByZondel = _engine->process(
        inBus.channelBuffers32,
        outBus.channelBuffers32,
        frames,
        _bypass);

    // Engine returns false when the data was passed through (bypass /
    // unsupported / disconnected). In any case, silence-flag bookkeeping
    // mirrors the input.
    (void)processedByZondel;
    outBus.silenceFlags = inBus.silenceFlags;

    return kResultOk;
}

uint32 PLUGIN_API ZondelProcessor::getLatencySamples() {
    return _engine ? _engine->getLatencySamples() : 0;
}

tresult PLUGIN_API ZondelProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;

    IBStreamer s(state, kLittleEndian);
    const int32 bypassWord = _bypass ? 1 : 0;
    if (!s.writeInt32(bypassWord)) return kResultFalse;
    if (!s.writeInt32(static_cast<int32>(_pipeTimeoutUs))) return kResultFalse;

    return kResultOk;
}

tresult PLUGIN_API ZondelProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;

    IBStreamer s(state, kLittleEndian);
    int32 bypassWord = 0;
    int32 timeoutWord = 5000;
    if (!s.readInt32(bypassWord)) return kResultFalse;
    if (!s.readInt32(timeoutWord)) return kResultFalse;

    _bypass = (bypassWord != 0);

    // Clamp to valid range; project files from older / future versions
    // shouldn't be able to wedge us into a bad timeout.
    if (timeoutWord < static_cast<int32>(kPipeTimeoutMinUs))
        timeoutWord = static_cast<int32>(kPipeTimeoutMinUs);
    if (timeoutWord > static_cast<int32>(kPipeTimeoutMaxUs))
        timeoutWord = static_cast<int32>(kPipeTimeoutMaxUs);
    _pipeTimeoutUs = static_cast<uint32_t>(timeoutWord);

    if (_engine) _engine->setPipeTimeoutMicros(_pipeTimeoutUs);

    return kResultOk;
}

} // namespace Zondel
