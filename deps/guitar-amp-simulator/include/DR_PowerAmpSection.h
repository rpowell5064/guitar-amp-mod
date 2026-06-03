#pragma once
#include "DR_PowerTube6V6.h"
#include "DR_PhaseInverter.h"
#include "BiquadFilter.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// DR_PowerAmpSection — AB763 push-pull 6V6GT output stage
// ─────────────────────────────────────────────────────────────────────────────
//
// Signal path (per sample, at oversampled rate):
//
//   in ─┬─ NFB subtraction ──────────────────────────────────────────────────╮
//       │                                                                      │
//       └→ PhaseInverter → V5 (6V6GT, in-phase) ─┬─ push-pull sum           │
//                        → V6 (6V6GT, inverted)  ─┘                          │
//                                                   → sag scale → output ────╯
//
// Negative feedback (NFB):
//   Taken from the output transformer secondary (post-xfmr) back to the
//   cathode of V4 (PI).  Modelled here as a shelved HP loop: the NFB has a
//   1-pole HP characteristic (presence frequency) so high-frequency content
//   feeds back more strongly, giving the classic presence-cut effect.
//   AB763 NFB amount ≈ 22 dB (large — keeps the amp very clean and punchy).
//
// Sag (power-supply droop):
//   A simple RC follower on the rectified output models the 5AR4 tube rectifier
//   sag.  Attack is fast (rectifier turns on quickly), release is slow (filter
//   capacitor C5 = 32 µF discharges through the transformer winding resistance).
//   AB763 values: attack τ ≈ 1 ms, release τ ≈ 80 ms.
//
// Oversampling note:
//   This class processes at the OVERSAMPLED rate.  DeluxeReverbAmpAB763
//   handles the up/down sampling around this stage.
// ─────────────────────────────────────────────────────────────────────────────
class DR_PowerAmpSection final {
public:
    void prepare(double oversampledSampleRate) noexcept;
    void reset()  noexcept;

    // masterVol [0, 1]:  overall output scaling (non-original "master volume").
    // sag       [0, 1]:  supply droop depth (0 = stiff supply, 1 = full sag).
    // presence  [0, 1]:  NFB HP cutoff (0 = most presence, 1 = least).
    void setMasterVol(float v) noexcept { masterVol_ = v; }
    void setSag      (float v) noexcept { sag_       = v; }
    void setPresence (float v) noexcept;

    float processSample(float x) noexcept;
    void  processBlock (float* data, int numSamples) noexcept;

private:
    DR_PhaseInverter pi_;
    DR_PowerTube6V6  v5_, v6_;

    // NFB highpass (models presence-dependent feedback characteristic).
    // Higher presence → lower HP cutoff → more feedback → more high-mid cut.
    BiquadFilter nfbHP_;
    float        nfbPrev_    = 0.0f;  // one-sample delay in the feedback path

    // AB763 global NFB amount (fixed per schematic: 22 dB ≈ factor 0.079).
    static constexpr float kNFBAmount = 0.079f;

    // Sag envelope follower state.
    float sagEnv_        = 0.0f;
    float sagAttackCoef_ = 0.0f;
    float sagRelCoef_    = 0.0f;

    float masterVol_ = 0.7f;
    float sag_       = 0.35f;   // moderate sag, matches new-tubes AB763
    float presence_  = 0.5f;    // stored to recompute NFB HP cutoff on change

    double oversampledFs_ = 0.0;

    void recalcSagCoefs()     noexcept;
    void recalcPresenceHP()   noexcept;
};
