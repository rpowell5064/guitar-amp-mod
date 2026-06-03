#pragma once
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// PowerSupplySag — standalone envelope-following B+ voltage droop model
// ─────────────────────────────────────────────────────────────────────────────
//
// Models the dynamic voltage drop in a tube amp's power supply under load:
//   • Tube rectifier (5AR4, 5U4G): large sag, slow recovery — Fender-style.
//   • Solid-state rectifier with filter caps: small/fast sag — modern amps.
//
// Two-rate envelope follower:
//   Attack  τ ≈ 1 ms   (rectifier turns on fast)
//   Release τ ≈ 50–300 ms (filter cap discharges through winding resistance)
//
// Output: multiplicative gain factor applied to the signal.
//   sagFactor = 1 – depth × env × drainScale
//   where env is the peak-hold envelope of the input signal.
//
// Real-time safe: no allocation, no branches dependent on input value.
// ─────────────────────────────────────────────────────────────────────────────
class PowerSupplySag {
public:
    struct Params {
        float attackMs  = 1.0f;    // attack time constant (ms)
        float releaseMs = 80.0f;   // release time constant (ms)
        float depth     = 0.30f;   // sag depth [0 = stiff, 1 = maximum]
        float drainScale = 0.25f;  // sag-to-gain scaling (keeps effect musical)
    };

    // Fender AB763 5AR4 tube rectifier — large sag, 80 ms release
    static const Params kFender_AB763;
    // Marshall JCM800 solid-state — moderate sag, 50 ms release
    static const Params kMarshall_JCM800;
    // EVH 5150 III — tight, fast, low sag (solid-state with large caps)
    static const Params kEVH_5150;
    // Orange Rockerverb — EL34 sag character, 120 ms release
    static const Params kOrange_RVB;

    void prepare(double sampleRate, const Params& p) noexcept;
    void setDepth(float d) noexcept { params_.depth = std::clamp(d, 0.0f, 1.0f); }
    float getSagFactor() const noexcept;

    // Apply sag: returns x scaled by the current sag factor,
    // and updates the envelope follower for the next sample.
    float process(float x) noexcept;

    void reset() noexcept { env_ = 0.0f; }

private:
    Params params_;
    float  env_         = 0.0f;
    float  attackCoef_  = 0.0f;
    float  releaseCoef_ = 0.0f;
};
