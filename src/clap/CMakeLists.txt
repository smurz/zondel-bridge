# License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
#
# Zondel.clap target. Built when -DBUILD_CLAP=ON is set.
#
# CLAP is header-only — we only need to add the include path. No SDK
# build step, no library to link.

include(FetchContent)

FetchContent_Declare(
  clap
  GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG        1.2.7
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(clap)

add_library(zondel-clap MODULE
  zondel-clap.cpp
)
target_compile_features(zondel-clap PUBLIC cxx_std_17)
target_link_libraries(zondel-clap PRIVATE zondel-shared-cpp clap)

# CLAP plug-ins use a `.clap` file extension on every platform. The
# MODULE library is renamed and the suffix overridden.
set_target_properties(zondel-clap PROPERTIES
  OUTPUT_NAME "Zondel"
  PREFIX ""
  SUFFIX ".clap"
)

if(WIN32)
  # CLAP on Windows is a single DLL renamed to .clap; no bundle layout.
  # Linking nothing else here — zondel-shared-cpp pulls in advapi32 etc.
endif()
