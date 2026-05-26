/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include "ZondelController.h"
#include "ZondelParams.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

namespace Zondel {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API ZondelController::initialize(FUnknown* context) {
    tresult r = EditController::initialize(context);
    if (r != kResultOk)
        return r;

    // Bypass — the kIsBypass flag tells the host this is *the* bypass
    // parameter, so it renders a dedicated bypass control (Studio One,
    // Cubase) and treats it consistently across project freeze/render.
    parameters.addParameter(
        STR16("Bypass"),
        nullptr,                       // no unit string
        1,                             // step count: 0..1 (toggle)
        0.0,                           // default
        ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass,
        kParamBypass);

    // Pipe timeout: integer microseconds in [1000, 20000]. Stored
    // normalised; the host sees discrete steps via the step count.
    parameters.addParameter(
        STR16("Pipe timeout"),
        STR16("µs"),
        kPipeTimeoutMaxUs - kPipeTimeoutMinUs,   // step count
        (5000.0 - kPipeTimeoutMinUs) /
            static_cast<double>(kPipeTimeoutMaxUs - kPipeTimeoutMinUs),
        ParameterInfo::kCanAutomate,
        kParamPipeTimeout);

    return kResultOk;
}

tresult PLUGIN_API ZondelController::setComponentState(IBStream* state) {
    if (!state) return kResultFalse;

    IBStreamer s(state, kLittleEndian);
    int32 bypassWord = 0;
    int32 timeoutWord = 5000;
    if (!s.readInt32(bypassWord)) return kResultFalse;
    if (!s.readInt32(timeoutWord)) return kResultFalse;

    setParamNormalized(kParamBypass, (bypassWord != 0) ? 1.0 : 0.0);

    if (timeoutWord < static_cast<int32>(kPipeTimeoutMinUs))
        timeoutWord = static_cast<int32>(kPipeTimeoutMinUs);
    if (timeoutWord > static_cast<int32>(kPipeTimeoutMaxUs))
        timeoutWord = static_cast<int32>(kPipeTimeoutMaxUs);

    const double normalised =
        static_cast<double>(timeoutWord - kPipeTimeoutMinUs) /
        static_cast<double>(kPipeTimeoutMaxUs - kPipeTimeoutMinUs);
    setParamNormalized(kParamPipeTimeout, normalised);

    return kResultOk;
}

} // namespace Zondel
