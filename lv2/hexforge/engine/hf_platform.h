// ─────────────────────────────────────────────────────────────────────────────
// Hex Forge engine — platform seam (M0 step 3)
//
// The engine calls ONLY these hooks for OS services, so the same engine code
// builds for the LV2/POSIX plugin and the JUCE desktop plugin. On POSIX every
// implementation is byte-identical in behavior to the pre-split monolith
// (getenv("HOME")-based config dir, ::mkdir(0755), CLOCK_MONOTONIC).
//
// Desktop hosts may call setConfigDir() BEFORE instantiating the engine to
// relocate the out-of-band preset-store backup (e.g. the JUCE user app-data
// dir); when unset the original POSIX resolution runs.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <string>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#if defined(_WIN32)
  #include <direct.h>
#else
  #include <sys/stat.h>
#endif

namespace hfplat {

inline int makeDir(const char* path) {
#if defined(_WIN32)
    return _mkdir(path);
#else
    return ::mkdir(path, 0755);
#endif
}

inline std::string& configDirOverride() { static std::string s; return s; }
inline void setConfigDir(const std::string& dir) { configDirOverride() = dir; }

// Backup dir for the preset store (no trailing slash). Mirrors the original
// hfBackupDir(): $HOME/.config/hexchain, else /tmp.
inline std::string configDir() {
    if (!configDirOverride().empty()) return configDirOverride();
    const char* home = std::getenv("HOME");
    return (home && home[0]) ? std::string(home) + "/.config/hexchain"
                             : std::string("/tmp");
}

// Create configDir() and its parent — the original two-::mkdir sequence.
inline void ensureConfigDir() {
    if (!configDirOverride().empty()) { makeDir(configDirOverride().c_str()); return; }
    const char* home = std::getenv("HOME");
    if (home && home[0]) { std::string cfg = std::string(home) + "/.config"; makeDir(cfg.c_str()); }
    makeDir(configDir().c_str());
}

// Binary file open. One funnel so the desktop build can later widen this for
// non-ASCII Windows paths (_wfopen) without touching engine call sites.
inline FILE* fopenBinary(const char* path, const char* mode) {
    return std::fopen(path, mode);
}

// Monotonic seconds for the per-block CPU meters (display-only).
inline double monotonicSeconds() {
#if defined(_WIN32)
    timespec ts; timespec_get(&ts, TIME_UTC);
    return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
#else
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
#endif
}

} // namespace hfplat
