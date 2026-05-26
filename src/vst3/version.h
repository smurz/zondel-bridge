/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#pragma once

#include "pluginterfaces/base/fplatform.h"

#define MAJOR_VERSION_STR  "0"
#define MAJOR_VERSION_INT  0
#define SUB_VERSION_STR    "1"
#define SUB_VERSION_INT    1
#define RELEASE_NUMBER_STR "0"
#define RELEASE_NUMBER_INT 0
#define BUILD_NUMBER_STR   "0"
#define BUILD_NUMBER_INT   0

#define FULL_VERSION_STR    MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR "." BUILD_NUMBER_STR
#define VERSION_STRING      FULL_VERSION_STR

#define stringOriginalFilename "Zondel.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription  "Zondel VST3 (64Bit)"
#else
#define stringFileDescription  "Zondel VST3"
#endif
#define stringCompanyName      "Zondel\0"
#define stringLegalCopyright   "Copyright(c) 2026 Zondel."
#define stringLegalTrademarks  "VST is a trademark of Steinberg Media Technologies GmbH"
