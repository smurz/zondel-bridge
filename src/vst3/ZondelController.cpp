/* License: MIT. Copyright (c) 2026 Zondel. */
#include "ZondelController.h"
#include "ZondelParams.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ustring.h"

namespace Zondel {

using namespace Steinberg;
using namespace Steinberg::Vst;

ZondelController::ZondelController() = default;

tresult PLUGIN_API ZondelController::initialize(FUnknown* context) {
    tresult r = EditControllerEx1::initialize(context);
    if (r != kResultOk)
        return r;

    // Bypass — kIsBypass marks this as *the* bypass parameter for hosts
    // that look for it (Studio One, Cubase render a dedicated Bypass UI).
    parameters.addParameter(
        STR16("Bypass"),
        nullptr,
        1,
        0.0,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass,
        kParamBypass);

    // Pipe timeout — integer microseconds in [1000, 20000]. Use
    // RangeParameter so hosts display plain microseconds (not a 0..1
    // normalised value) in their generic parameter panel.
    // Unicode µ (U+00B5) via \u escape — safer than UTF-8 byte escapes
    // (which STR16 would split into separate UTF-16 code units) or bare
    // literals (which require /utf-8 on MSVC).
    auto* timeoutParam = new RangeParameter(
        STR16("Pipe timeout"),
        kParamPipeTimeout,
        STR16("\u00B5s"),
        static_cast<double>(kPipeTimeoutMinUs),
        static_cast<double>(kPipeTimeoutMaxUs),
        5000.0,
        kPipeTimeoutMaxUs - kPipeTimeoutMinUs,
        ParameterInfo::kCanAutomate);
    parameters.addParameter(timeoutParam);

    // Status — read-only enum surfaced via Data Exchange from the
    // processor. The host displays this as a non-automatable label.
    // Unicode bullet U+25CF (●) via \u escape — same encoding reason.
    auto* statusParam = new StringListParameter(
        STR16("Status"),
        kParamStatus,
        nullptr,
        ParameterInfo::kIsReadOnly);
    statusParam->appendString(STR16("Unknown"));
    statusParam->appendString(STR16("Connecting"));
    statusParam->appendString(STR16("\u25CF Connected"));
    statusParam->appendString(STR16("\u25CF Disconnected"));
    statusParam->appendString(STR16("Backed off"));
    statusParam->appendString(STR16("Unsupported format"));
    parameters.addParameter(statusParam);

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

tresult PLUGIN_API ZondelController::notify(IMessage* message) {
    if (_dataExchange.onMessage(message))
        return kResultTrue;
    return EditControllerEx1::notify(message);
}

void PLUGIN_API ZondelController::queueOpened(
    DataExchangeUserContextID /*userContextID*/,
    uint32 /*blockSize*/,
    TBool& dispatchOnBackgroundThread) {
    // We want updates on the main (UI) thread so setParamNormalized is safe.
    dispatchOnBackgroundThread = false;
}

void PLUGIN_API ZondelController::queueClosed(DataExchangeUserContextID /*userContextID*/) {
}

void PLUGIN_API ZondelController::onDataExchangeBlocksReceived(
    DataExchangeUserContextID /*userContextID*/,
    uint32 numBlocks,
    DataExchangeBlock* blocks,
    TBool /*onBackgroundThread*/) {

    // Take only the latest block; older ones in the same batch are stale
    // by definition (Data Exchange coalesces under load).
    if (numBlocks == 0) return;
    const auto& block = blocks[numBlocks - 1];
    if (block.size < sizeof(StatusBlock) || block.data == nullptr) return;

    const auto* sb = static_cast<const StatusBlock*>(block.data);
    int32 status = sb->status;
    if (status < 0)                  status = kStatusUnknown;
    if (status >= kStatusCount)      status = kStatusUnknown;

    // Status param is a string list with kStatusCount entries; normalise
    // by the step count, which is (kStatusCount - 1) for a 0..N-1 range.
    const double normalised = static_cast<double>(status) /
                              static_cast<double>(kStatusCount - 1);
    setParamNormalized(kParamStatus, normalised);
}

} // namespace Zondel
