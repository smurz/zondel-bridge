/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
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

    // Pipe timeout — integer microseconds in [1000, 20000].
    parameters.addParameter(
        STR16("Pipe timeout"),
        STR16("\xC2\xB5s"),   // "µs" UTF-8
        kPipeTimeoutMaxUs - kPipeTimeoutMinUs,
        (5000.0 - kPipeTimeoutMinUs) /
            static_cast<double>(kPipeTimeoutMaxUs - kPipeTimeoutMinUs),
        ParameterInfo::kCanAutomate,
        kParamPipeTimeout);

    // Status — read-only enum surfaced via Data Exchange from the
    // processor. The host displays this as a non-automatable label.
    auto* statusParam = new StringListParameter(
        STR16("Status"),
        kParamStatus,
        nullptr,
        ParameterInfo::kIsReadOnly);
    statusParam->appendString(STR16("Unknown"));
    statusParam->appendString(STR16("Connecting"));
    statusParam->appendString(STR16("\xE2\x97\x8F Connected"));      // ● Connected
    statusParam->appendString(STR16("\xE2\x97\x8F Disconnected"));   // ● Disconnected
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
