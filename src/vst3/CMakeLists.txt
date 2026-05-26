# License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
#
# Zondel.vst3 target. Built when -DBUILD_VST3=ON is set on the parent
# CMake invocation. Pulls the Steinberg VST3 SDK via FetchContent.

include(FetchContent)

# Disable SDK examples + VSTGUI so FetchContent doesn't pull and build
# tens of MB of demo plug-ins we don't need.
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT        OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR            OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK           OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  vst3sdk
  GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
  GIT_TAG        v3.8.0_build_66
  GIT_SHALLOW    TRUE
  GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(vst3sdk)

# `smtg_add_vst3plugin` becomes available after the SDK is added.
smtg_add_vst3plugin(Zondel
  ZondelIDs.h
  ZondelProcessor.h
  ZondelProcessor.cpp
  ZondelController.h
  ZondelController.cpp
  factory.cpp
  version.h
)

target_compile_features(Zondel
  PUBLIC
    cxx_std_17
)

target_link_libraries(Zondel
  PRIVATE
    sdk
    zondel-core    # statically linked; pulls in pipe-client/etc for Phase 2 use
)

target_include_directories(Zondel PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

smtg_target_configure_version_file(Zondel)

if(WIN32)
  target_sources(Zondel PRIVATE zondel.rc)
endif()
