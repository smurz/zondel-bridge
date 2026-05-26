/*
 * Shared parameter identifiers used by both ZondelProcessor and
 * ZondelController. Stable across plug-in versions — DAW project files
 * reference parameters by ID.
 *
 * License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
 */
#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace Zondel {

enum ZondelParam : Steinberg::Vst::ParamID {
    kParamBypass       = 0,   // bool, flagged kIsBypass
    kParamPipeTimeout  = 1,   // int microseconds; range below
    kParamStatus       = 2,   // read-only display; populated by controller
};

static constexpr Steinberg::uint32 kPipeTimeoutMinUs = 1000;
static constexpr Steinberg::uint32 kPipeTimeoutMaxUs = 20000;

} // namespace Zondel
