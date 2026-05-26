/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#include "pipe-client.h"

#if defined(_WIN32)
#  include "pipe-client-win32.inl"
#elif defined(__linux__) || defined(__APPLE__)
#  include "pipe-client-unix.inl"
#else
#  error "Unsupported platform — only Win32, Linux, macOS are supported."
#endif
