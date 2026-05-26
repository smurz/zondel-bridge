/*
 * Shared parameter identifiers used by both ZondelProcessor and
 * ZondelController. Stable across plug-in versions — DAW project files
 * reference parameters by ID.
 *
 * Also defines the StatusBlock wire format used by the Data Exchange
 * API (VST3 SDK >= 3.7.9) to push engine connection state from the
 * audio thread to the controller without per-block IMessage overhead.
 *
 * License: MIT. Copyright (c) 2026 Zondel.
 */
#pragma once

#include "pluginterfaces/vst/vsttypes.h"

#include <cstdint>

namespace Zondel {

enum ZondelParam : Steinberg::Vst::ParamID {
    kParamBypass       = 0,   // bool, flagged kIsBypass
    kParamPipeTimeout  = 1,   // int microseconds; range below
    kParamStatus       = 2,   // read-only string list mirrored from engine
};

static constexpr Steinberg::uint32 kPipeTimeoutMinUs = 1000;
static constexpr Steinberg::uint32 kPipeTimeoutMaxUs = 20000;

// Indices into the Status string list. MUST match the order in which
// strings are appended in ZondelController::initialize().
enum ZondelStatus : Steinberg::int32 {
    kStatusUnknown            = 0,
    kStatusConnecting         = 1,
    kStatusConnected          = 2,
    kStatusDisconnected       = 3,
    kStatusBackedOff          = 4,
    kStatusUnsupportedFormat  = 5,
    kStatusCount              = 6,
};

// Data-Exchange wire format. One block per state change from the
// audio thread. The updateCounter lets the controller detect coalesced
// changes (the host may batch blocks).
struct StatusBlock {
    int32_t   status;          // ZondelStatus enum
    int32_t   _pad;            // align updateCounter to 8 bytes
    uint64_t  updateCounter;
};

} // namespace Zondel
