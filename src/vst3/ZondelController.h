/*
 * ZondelController — VST3 EditController stub for Phase 1.
 *
 * No parameters yet. Phase 3 declares Bypass (with kIsBypass flag) and
 * Pipe timeout, plus a Data Exchange API receiver for live status text.
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
};

} // namespace Zondel
