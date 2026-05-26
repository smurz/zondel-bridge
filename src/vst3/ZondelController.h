/*
 * ZondelController — VST3 EditController.
 *
 * Phase 2: declares Bypass + Pipe timeout parameters with stable IDs
 * so getState/setState persistence round-trips cleanly across project
 * save/load. The Bypass parameter is flagged kIsBypass so DAWs that
 * inspect it render a dedicated bypass button (Studio One, Cubase).
 *
 * Phase 3 will add the Data Exchange API receiver for live status text
 * and an optional VSTGUI view.
 *
 * License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 */
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace Zondel {

class ZondelController : public Steinberg::Vst::EditController {
public:
    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IEditController*>(new ZondelController);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
};

} // namespace Zondel
