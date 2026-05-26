/*
 * ZondelIDs.h — stable VST3 class identifiers for the Zondel plug-in.
 *
 * These FUIDs are the plug-in's permanent identity. DAW projects refer
 * to plug-in instances by these UUIDs. Changing them breaks every saved
 * project that contains an instance of this plug-in.
 *
 * Generated: 2026-05-26. DO NOT REGENERATE.
 *
 * License: MIT. Copyright (c) 2026 Zondel.
 */
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Zondel {

static const Steinberg::FUID kZondelProcessorUID  (0x62ECD105, 0xA8054101, 0xB7403E57, 0x603E1E63);
static const Steinberg::FUID kZondelControllerUID (0xD99062FE, 0x5FE24D70, 0x89D531B6, 0x8575BDBB);

#define ZondelVST3Category "Fx|Restoration"

} // namespace Zondel
