/*
 * ZondelProcessor — VST3 audio effect that bridges the host audio path
 * to the Zondel desktop app via a Windows named pipe.
 *
 * Phase 3: pushes engine state (Connected / Disconnected / etc.) to the
 * controller via the VST3 Data Exchange API (SDK >= 3.7.9). One-way,
 * lock-free, audio-thread-safe.
 *
 * License: MIT. Copyright (c) 2026 Zondel.
 */
#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/utility/dataexchange.h"

#include <cstdint>
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

    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;

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
    bool      _bypass         = false;
    uint32_t  _pipeTimeoutUs  = 5000;

    std::unique_ptr<zondel::Engine> _engine;

    // Data Exchange handler — initialised on connect(), torn down on
    // disconnect(). Activated/deactivated by setActive().
    std::unique_ptr<Steinberg::Vst::DataExchangeHandler> _dataExchange;
    uint64_t _lastSentStatusCounter = 0;
};

} // namespace Zondel
