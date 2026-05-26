/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include <obs-module.h>
#include <plugin-support.h>
#include "zondel-filter.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("zondel-obs-plugin", "en-US")

bool obs_module_load(void) {
    obs_log(LOG_INFO, "Zondel OBS plugin loaded (version %s)", PLUGIN_VERSION);
    obs_register_source(&zondel_filter_info);
    return true;
}

void obs_module_unload(void) {
    obs_log(LOG_INFO, "Zondel OBS plugin unloaded");
}
