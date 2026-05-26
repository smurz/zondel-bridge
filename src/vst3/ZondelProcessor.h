/*
 * ZondelProcessor — VST3 audio effect that bridges the host audio path
 * to the Zondel desktop app via a Windows named pipe.
 *
 * Phase 2: wired to zondel::Engine; supports getState/setState for
 * project persistence; reports PDC via getLatencySamples; passes through
 * cleanly in kOffline mode and when the engine is unavailable.
 *
 * License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 */
#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <memory>

namespace zondel { class Engine; }

namespace Zondel {

class ZondelProcessor : public Steinberg::Vst::AudioEffect {
public:
    ZondelProcessor();
    ~ZondelProcessor() override;

    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new ZondelProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;

    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;

    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;

    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;

    Steinberg::uint32 PLUGIN_API getLatencySamples() override;

    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;

private:
    // Persisted parameters (mirrored to controller via setComponentState).
    bool      _bypass         = false;
    uint32_t  _pipeTimeoutUs  = 5000;

    // Engine owned for the active processing session. Allocated in
    // setupProcessing (host thread, plugin inactive); never reallocated
    // in setActive or process.
    std::unique_ptr<zondel::Engine> _engine;
};

} // namespace Zondel
