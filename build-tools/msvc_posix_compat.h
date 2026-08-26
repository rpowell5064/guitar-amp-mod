// MSVC-only forced include (/FI) for building the UNMODIFIED hexforge_plugin.cpp
// into the hexforge_golden host on Windows. Supplies the two POSIX calls the
// monolith uses (::mkdir(path, mode) and clock_gettime) so the golden baseline
// can be captured before the hf_platform.h refactor lands. Retired once the
// engine extraction routes these through hf_platform.h.
#pragma once
#ifdef _MSC_VER
#include <direct.h>
#include <ctime>
static inline int mkdir(const char* path, int /*mode*/) { return _mkdir(path); }
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
// Only consumer is the per-block CPU meter (delta of two reads inside run());
// timespec_get(TIME_UTC) is a fine stand-in — never affects the audio path.
static inline int clock_gettime(int /*clk*/, struct timespec* ts) {
    return timespec_get(ts, TIME_UTC) == TIME_UTC ? 0 : -1;
}
#endif
#endif
