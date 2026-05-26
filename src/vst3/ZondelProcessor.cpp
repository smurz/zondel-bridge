/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include "ZondelProcessor.h"
#include "ZondelIDs.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstring>

namespace Zondel {

using namespace Steinberg;
using namespace Steinberg::Vst;

ZondelProcessor::ZondelProcessor() {
    setControllerClass(kZondelControllerUID);
}

tresult PLUGIN_API ZondelProcessor::initialize(FUnknown* context) {
    tresult r = AudioEffect::initialize(context);
    if (r != kResultOk)
        return r;

    // Default to stereo at instantiation; setBusArrangements re-negotiates.
    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    return kResultOk;
}

tresult PLUGIN_API ZondelProcessor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts) {

    // Accept exactly one input bus + one output bus, both either mono or
    // stereo, and matching each other. Anything else: reject. This is the
    // contract VST3 hosts negotiate before process() ever runs.
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

tresult PLUGIN_API ZondelProcessor::process(ProcessData& data) {
    if (data.numSamples <= 0 || data.numInputs == 0 || data.numOutputs == 0)
        return kResultTrue;

    // Phase 1 pass-through. Phase 2 replaces this with ZondelEngine::process().
    const auto& inBus  = data.inputs[0];
    auto& outBus       = data.outputs[0];
    const int32 frames = data.numSamples;
    const int32 chans  = (inBus.numChannels < outBus.numChannels) ? inBus.numChannels : outBus.numChannels;

    for (int32 ch = 0; ch < chans; ++ch) {
        const float* src = inBus.channelBuffers32[ch];
        float* dst       = outBus.channelBuffers32[ch];
        if (src != dst)
            std::memcpy(dst, src, sizeof(float) * static_cast<size_t>(frames));
    }

    // Silence flag propagation: if input was silent, output is too.
    outBus.silenceFlags = inBus.silenceFlags;

    return kResultOk;
}

} // namespace Zondel
