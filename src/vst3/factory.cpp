/*
 * VST3 module entry point — declares the plug-in factory.
 *
 * License: MIT. Copyright (c) 2026 Zondel.
 */
#include "ZondelIDs.h"
#include "ZondelProcessor.h"
#include "ZondelController.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "Zondel"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace Zondel;

BEGIN_FACTORY_DEF("Zondel",
                  "https://zondel.net",
                  "mailto:support@zondel.net")

    DEF_CLASS2(INLINE_UID_FROM_FUID(kZondelProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               stringPluginName,
               Vst::kDistributable,
               ZondelVST3Category,
               FULL_VERSION_STR,
               kVstVersionString,
               ZondelProcessor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(kZondelControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               stringPluginName "Controller",
               0,
               "",
               FULL_VERSION_STR,
               kVstVersionString,
               ZondelController::createInstance)

END_FACTORY
