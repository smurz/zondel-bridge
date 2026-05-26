cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# ───────────────────────────────────────────────────────────────────────────
# Plugin format selection
#
# This repo ships three plugins on top of one shared C core:
#   - OBS Studio   (src/obs)   ← v1
#   - VST3         (src/vst3)  ← planned (see docs/PLAN-VST3-CLAP.md)
#   - CLAP         (src/clap)  ← planned (see docs/PLAN-VST3-CLAP.md)
#
# Only the OBS plugin is active today; VST3 and CLAP gates exist so the
# planning work, CMake skeleton, and CI matrix can land incrementally.
# ───────────────────────────────────────────────────────────────────────────
option(BUILD_OBS  "Build the OBS Studio plugin"        ON)
option(BUILD_VST3 "Build the VST3 plugin (planned)"    OFF)
option(BUILD_CLAP "Build the CLAP plugin (planned)"    OFF)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

# ───────────────────────────────────────────────────────────────────────────
# Shared core (host-agnostic C): named-pipe IPC, ring buffer, resampler,
# downmix, state machine, Zondel auto-launch. Compiled once, linked into
# every plugin target.
# ───────────────────────────────────────────────────────────────────────────
add_library(zondel-core STATIC
  src/core/pipe-client.c
  src/core/ring-buffer.c
  src/core/resampler.c
  src/core/downmix.c
  src/core/zondel-state.c
  src/core/zondel-launch.c
)
target_include_directories(zondel-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
if(WIN32)
  target_link_libraries(zondel-core PUBLIC advapi32 shell32)
endif()
if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
  set_property(TARGET zondel-core PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

# ───────────────────────────────────────────────────────────────────────────
# Shared C++ engine (host-agnostic; built only when a C++ plugin needs it).
# The OBS plugin is pure C and does not link this.
# ───────────────────────────────────────────────────────────────────────────
if(BUILD_VST3 OR BUILD_CLAP OR BUILD_PLUGIN_TESTS)
  add_subdirectory(src/shared-cpp)
endif()

# ───────────────────────────────────────────────────────────────────────────
# OBS Studio plugin
# ───────────────────────────────────────────────────────────────────────────
if(BUILD_OBS)
  add_library(${CMAKE_PROJECT_NAME} MODULE)

  find_package(libobs REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs zondel-core)

  if(ENABLE_FRONTEND_API)
    find_package(obs-frontend-api REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  endif()

  if(ENABLE_QT)
    find_package(Qt6 COMPONENTS Widgets Core)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
    target_compile_options(
      ${CMAKE_PROJECT_NAME}
      PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
    )
    set_target_properties(
      ${CMAKE_PROJECT_NAME}
      PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
    )
  endif()

  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      src/obs/plugin-main.c
      src/obs/zondel-filter.c
  )

  set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
endif()

# ───────────────────────────────────────────────────────────────────────────
# VST3 plugin — placeholder. The actual target is added in src/vst3/
# (see docs/PLAN-VST3-CLAP.md). Until that lands, enabling -DBUILD_VST3=ON
# fails fast with a clear error so CI matrix entries don't silently no-op.
# ───────────────────────────────────────────────────────────────────────────
if(BUILD_VST3)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/vst3/CMakeLists.txt")
    add_subdirectory(src/vst3)
  else()
    message(FATAL_ERROR
      "BUILD_VST3=ON but src/vst3/CMakeLists.txt does not exist yet. "
      "See docs/PLAN-VST3-CLAP.md for the implementation plan.")
  endif()
endif()

# ───────────────────────────────────────────────────────────────────────────
# CLAP plugin — placeholder, same shape as VST3.
# ───────────────────────────────────────────────────────────────────────────
if(BUILD_CLAP)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/clap/CMakeLists.txt")
    add_subdirectory(src/clap)
  else()
    message(FATAL_ERROR
      "BUILD_CLAP=ON but src/clap/CMakeLists.txt does not exist yet. "
      "See docs/PLAN-VST3-CLAP.md for the implementation plan.")
  endif()
endif()

# ───────────────────────────────────────────────────────────────────────────
# Tests — exercise the shared core directly, format-agnostic.
# Built only on windows-x64 CI today (see .github/workflows).
# ───────────────────────────────────────────────────────────────────────────
option(BUILD_PLUGIN_TESTS "Build all unit-test executables" OFF)

if(BUILD_PLUGIN_TESTS)
  enable_testing()

  add_executable(fake-pipe-server test/fake-pipe-server.c)
  if(WIN32)
    target_link_libraries(fake-pipe-server PRIVATE advapi32)
  endif()

  add_executable(test-pipe-client test/test-pipe-client.c src/core/pipe-client.c)
  target_include_directories(test-pipe-client PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
  if(WIN32)
    target_link_libraries(test-pipe-client PRIVATE advapi32)
  endif()
  add_test(NAME pipe-client COMMAND test-pipe-client)

  add_executable(test-zondel-state test/test-zondel-state.c src/core/zondel-state.c)
  target_include_directories(test-zondel-state PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
  add_test(NAME zondel-state COMMAND test-zondel-state)

  add_executable(test-ring-buffer test/test-ring-buffer.c src/core/ring-buffer.c)
  target_include_directories(test-ring-buffer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
  add_test(NAME ring-buffer COMMAND test-ring-buffer)

  add_executable(test-resampler test/test-resampler.c src/core/resampler.c)
  target_include_directories(test-resampler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
  if(NOT WIN32)
    target_link_libraries(test-resampler PRIVATE m)
  endif()
  add_test(NAME resampler COMMAND test-resampler)

  # C++ engine + realtime budget tests (Phase 2). Link the shared-cpp
  # library which already pulls in zondel-core.
  add_executable(test-zondel-engine test/test-zondel-engine.cpp)
  target_link_libraries(test-zondel-engine PRIVATE zondel-shared-cpp)
  add_test(NAME zondel-engine COMMAND test-zondel-engine)

  add_executable(test-realtime-budget test/test-realtime-budget.cpp)
  target_link_libraries(test-realtime-budget PRIVATE zondel-shared-cpp)
  add_test(NAME realtime-budget COMMAND test-realtime-budget)
endif()
