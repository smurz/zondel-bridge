/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include "ZondelController.h"

namespace Zondel {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API ZondelController::initialize(FUnknown* context) {
    return EditController::initialize(context);
}

} // namespace Zondel
