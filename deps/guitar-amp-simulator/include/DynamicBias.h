#pragma once
#include <cmath>
#include <algorithm>
// ── Dynamic operating-point shift ("bias shift" / "blocking distortion") ──────
// An AC-coupled transistor/op-amp clip stage charges its coupling cap from the
// rectified, asymmetric signal, so the clip's DC operating point drifts over
// ~ms–tens-of-ms: "bark" on the pick attack, a settling body, sputter on decay.
// Static waveshapers miss this entirely (constant bias). Modelled here as a slow
// rectified envelope of the stage output, fed back as a bias offset added to the
// stage input before the clip. depth = 0 → no shift (bit-identical default).
// Refs: Aiken, "What Is Blocking Distortion?"; gray-box fuzz bias modelling
// (arXiv 2502.14405); DingoTone GHF technical notes.
// Modelled as the DIFFERENCE of a fast and a slow rectified envelope of the stage
// output — i.e. the transient CHANGE in level, not the absolute level. In steady
// state fast==slow so the offset is 0 and the stage settles back to its nominal
// voicing (no permanent asymmetry); a pick attack spikes fast above slow (bark) and
// a decay drops it below (sputter). That transient-only behaviour is the coupling-
// cap charge/discharge character; the steady DC it would leave is blocked downstream.
struct DynamicBias {
    float fast  = 0.0f, slow = 0.0f;
    float cFast = 0.0f, cSlow = 0.0f;
    float depth = 0.0f;   // 0 = off

    void prepare(float sampleRate, float fastMs = 3.0f, float slowMs = 60.0f) noexcept {
        cFast = 1.0f - std::exp(-1.0f / (std::max(0.1f, fastMs) * 0.001f * sampleRate));
        cSlow = 1.0f - std::exp(-1.0f / (std::max(0.1f, slowMs) * 0.001f * sampleRate));
    }
    void setDepth(float d) noexcept { depth = std::max(0.0f, d); }
    void reset() noexcept { fast = slow = 0.0f; }

    // Bias offset to ADD to the stage input (call before the clip).
    float offset() const noexcept { return depth * (fast - slow); }
    // Update both envelopes from the stage OUTPUT (call after the clip).
    void update(float clipOut) noexcept {
        const float a = std::fabs(clipOut);
        fast += cFast * (a - fast);
        slow += cSlow * (a - slow);
    }
};
