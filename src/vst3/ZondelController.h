/*
 * ZondelController — VST3 EditController.
 *
 * Phase 3: receives connection-state updates from the processor over the
 * Data Exchange API and surfaces them via a read-only "Status" string-
 * list parameter. No custom UI (option 3b from the plan); host renders
 * the parameter list itself.
 *
 * License: MIT. Copyright (c) 2026 Zondel.
 */
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "pluginterfaces/vst/ivstdataexchange.h"

namespace Zondel {

class ZondelController
    : public Steinberg::Vst::EditControllerEx1
    , public Steinberg::Vst::IDataExchangeReceiver
{
public:
    ZondelController();

    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IEditController*>(new ZondelController);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // IDataExchangeReceiver
    void PLUGIN_API queueOpened(Steinberg::Vst::DataExchangeUserContextID userContextID,
                                Steinberg::uint32 blockSize,
                                Steinberg::TBool& dispatchOnBackgroundThread) override;
    void PLUGIN_API queueClosed(Steinberg::Vst::DataExchangeUserContextID userContextID) override;
    void PLUGIN_API onDataExchangeBlocksReceived(
        Steinberg::Vst::DataExchangeUserContextID userContextID,
        Steinberg::uint32 numBlocks,
        Steinberg::Vst::DataExchangeBlock* blocks,
        Steinberg::TBool onBackgroundThread) override;

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(EditControllerEx1)
    DELEGATE_REFCOUNT(EditControllerEx1)

private:
    Steinberg::Vst::DataExchangeReceiverHandler _dataExchange { this };
};

} // namespace Zondel
