/* License: MIT. Copyright (c) 2026 Zondel. */
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

namespace {

// Map zondel::Engine status (zondel-core enum) onto our string-list
// indices kStatus*. Single source of truth so the processor and
// controller can't drift.
int32 statusFromEngine(int engineStatus) {
    // zondel-core's enum zondel_status: UNKNOWN=0, CONNECTING=1,
    // CONNECTED=2, DISCONNECTED=3, BACKED_OFF=4, UNSUPPORTED_FORMAT=5.
    if (engineStatus < 0 || engineStatus > 5) return kStatusUnknown;
    return engineStatus;
}

} // namespace

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

tresult PLUGIN_API ZondelProcessor::connect(IConnectionPoint* other) {
    auto r = AudioEffect::connect(other);
    if (r == kResultTrue) {
        // One small block per state change. Two blocks queued is enough:
        // worst case the controller falls one block behind under load
        // and catches up next cycle.
        auto cfg = [](DataExchangeHandler::Config& config,
                      const ProcessSetup& /*setup*/) {
            config.blockSize    = sizeof(StatusBlock);
            config.numBlocks    = 2;
            config.alignment    = 32;
            config.userContextID = 0;
            return true;
        };
        _dataExchange = std::make_unique<DataExchangeHandler>(this, cfg);
        _dataExchange->onConnect(other, getHostContext());
    }
    return r;
}

tresult PLUGIN_API ZondelProcessor::disconnect(IConnectionPoint* other) {
    if (_dataExchange) {
        _dataExchange->onDisconnect(other);
        _dataExchange.reset();
    }
    return AudioEffect::disconnect(other);
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
    tresult r = AudioEffect::setupProcessing(newSetup);
    if (r != kResultOk)
        return r;

    SpeakerArrangement arr {};
    if (getBusArrangement(BusDirections::kInput, 0, arr) != kResultOk)
        return kResultOk;

    const int channels = SpeakerArr::getChannelCount(arr);
    if (channels != 1 && channels != 2)
        return kResultOk;

    _engine = std::make_unique<zondel::Engine>(
        newSetup.sampleRate,
        channels,
        static_cast<int>(newSetup.maxSamplesPerBlock),
        nullptr);
    _engine->setPipeTimeoutMicros(_pipeTimeoutUs);
    _lastSentStatusCounter = 0;

    return kResultOk;
}

tresult PLUGIN_API ZondelProcessor::setActive(TBool state) {
    if (state && _dataExchange)
        _dataExchange->onActivate(processSetup);
    else if (!state && _dataExchange)
        _dataExchange->onDeactivate();

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

    if (data.processMode == kOffline || data.processMode == kPrefetch) {
        passThrough();
        return kResultOk;
    }

    if (!_engine) {
        passThrough();
        return kResultOk;
    }

    _engine->process(inBus.channelBuffers32, outBus.channelBuffers32, frames, _bypass);
    outBus.silenceFlags = inBus.silenceFlags;

    // Push a status update if the engine's state has changed since the
    // last block we sent. Single int32 + uint64, fits in 16 bytes.
    if (_dataExchange) {
        const auto snap = _engine->status();
        if (snap.updateCounter != _lastSentStatusCounter) {
            auto block = _dataExchange->getCurrentOrNewBlock();
            if (block.blockID != InvalidDataExchangeBlockID && block.data) {
                auto* sb = static_cast<StatusBlock*>(block.data);
                sb->status = statusFromEngine(snap.status);
                sb->_pad   = 0;
                sb->updateCounter = snap.updateCounter;
                _dataExchange->sendCurrentBlock();
                _lastSentStatusCounter = snap.updateCounter;
            }
            // If we couldn't get a block (queue full), try again next
            // process() call — the updateCounter check ensures we'll
            // retry, not drop the message.
        }
    }

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
    if (timeoutWord < static_cast<int32>(kPipeTimeoutMinUs))
        timeoutWord = static_cast<int32>(kPipeTimeoutMinUs);
    if (timeoutWord > static_cast<int32>(kPipeTimeoutMaxUs))
        timeoutWord = static_cast<int32>(kPipeTimeoutMaxUs);
    _pipeTimeoutUs = static_cast<uint32_t>(timeoutWord);

    if (_engine) _engine->setPipeTimeoutMicros(_pipeTimeoutUs);
    return kResultOk;
}

} // namespace Zondel
