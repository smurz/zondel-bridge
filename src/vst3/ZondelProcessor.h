/*
 * ZondelProcessor — VST3 audio effect that bridges the host audio path
 * to the Zondel desktop app via a Windows named pipe.
 *
 * Phase 1: skeleton only. setBusArrangements enforces mono+mono or
 * stereo+stereo; process() copies in→out unchanged. Subsequent phases
 * wire the shared ZondelEngine in src/shared-cpp/ to drive real DSP.
 *
 * License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 */
#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

namespace Zondel {

class ZondelProcessor : public Steinberg::Vst::AudioEffect {
public:
    ZondelProcessor();
    ~ZondelProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new ZondelProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;

    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;

    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;

    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
};

} // namespace Zondel
