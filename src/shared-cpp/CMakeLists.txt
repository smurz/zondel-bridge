# License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
#
# zondel-shared-cpp — single source-of-truth C++ wrapper around
# zondel-core. Linked by every C++ plugin target (VST3, CLAP).

add_library(zondel-shared-cpp STATIC
    ZondelEngine.h
    ZondelEngine.cpp
)

target_compile_features(zondel-shared-cpp PUBLIC cxx_std_17)
target_include_directories(zondel-shared-cpp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(zondel-shared-cpp PUBLIC zondel-core)

if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
    set_property(TARGET zondel-shared-cpp PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()
