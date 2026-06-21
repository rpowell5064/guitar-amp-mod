#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NamModelStub — a no-op, header-only stand-in for NamModel, used when the suite
// is built with GUITARAMP_DISABLE_NAM (e.g. the 32-bit patchbox-os-arm32 target,
// where NAM neural inference would never run in realtime and pulling in
// NamCore + Eigen is undesirable).
//
// It is API-compatible with deps/guitar-amp-simulator/include/NamModel.h, but
// every method is inline and trivial, so:
//   • no nam::DSP / NamCore / Eigen symbols are referenced (the heavy static
//     archive is never pulled into the .so), and
//   • isLoaded() always returns false, so every NAM code path in the plugins
//     falls through to its existing "no model loaded → passthrough/dry" branch.
//
// This mirrors what the light plugins (gate/comp/fuzz/…) already do: link
// GuitarAmpSim without NamCore. The plugin sources select this stub via:
//     #ifdef GUITARAMP_NO_NAM
//     #include "NamModelStub.h"
//     #else
//     #include "NamModel.h"
//     #endif
// ─────────────────────────────────────────────────────────────────────────────
#include <cstring>
#include <string>

class NamModel {
public:
    NamModel() = default;
    ~NamModel() = default;
    NamModel(NamModel&&) noexcept = default;
    NamModel& operator=(NamModel&&) noexcept = default;

    // Never loads: NAM is compiled out for this target.
    bool loadFromFile(const std::string& /*path*/) noexcept { return false; }

    void reset(double /*sampleRate*/, int /*maxBlockSize*/) noexcept {}

    // Defensive passthrough; callers gate on isLoaded() so this is never hit in
    // practice, but keep it safe (and alias-tolerant) just in case.
    void processBuffer(const float* in, float* out, int numSamples) noexcept {
        if (out != in && in && out)
            std::memmove(out, in, sizeof(float) * static_cast<size_t>(numSamples));
    }

    void   clear() noexcept {}
    bool   isLoaded() const noexcept { return false; }
    double getExpectedSampleRate() const noexcept { return 0.0; }
};
