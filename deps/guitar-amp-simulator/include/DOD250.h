#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <cmath>
#include <string>

// ── DOD Overdrive Preamp 250 (parody-safe name: "Preamp 250") ────────────────
//
// The DOD 250 (and its cousin the MXR Distortion+) is a single op-amp gain stage
// into a HARD SHUNT diode clipper (a pair of diodes to ground at the output). Just
// Gain + Level — NO tone control. The gain stage is bandwidth-limited by a feedback
// cap, which is what makes it mid-forward and slightly dark; the hard shunt clip is
// aggressive and compressed (harder/squarer than a Tube Screamer's feedback soft clip).
// Fredrik Åkesson used one for Ghost's Skeleta solos.
//
// Signal path (oversampled):
//   in → HP → op-amp gain (× (1+drive·K)) → feedback LP (gain-stage bandwidth)
//      → hard shunt diode clip (±Vf, symmetric) → DC block → output LP → level → out
//
// Params (OverdriveBase): "drive" "level" [0,1]. "tone" is a gentle post-clip
// treble tilt (the real pedal has none, but a dead knob is bad UX — kept subtle).
class DOD250 final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "DOD 250"; }
    int recommendedTubeType() const noexcept override { return 1; }   // EL34 (Marshall-ish)

private:
    // Voiced to a pedal-direct DOD 250 NAM capture at Gain=noon (nam_compare --model dod250).
    static constexpr double kGainMax = 40.0;   // op-amp gain span (1 → 41×)
    static constexpr double kVf      = 0.42;   // shunt-diode clamp (V) — hard clip threshold
    static constexpr double kHard    = 2.6;    // clip hardness (higher = squarer/harder)
    static constexpr double kMakeup  = 0.85;   // output trim (matched to capture loudness)
    static constexpr double kInHPfc  = 80.0;   // input coupling HP — the DOD has a fairly tight low end (mild sub-bass cut)
    static constexpr double kFbLpFc  = 3200.0; // gain-stage bandwidth limit → mid-forward
    static constexpr double kToneFc  = 1500.0; // post-clip tilt centre

    double fs_ = 0.0;

    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.6f;
    LinearSmoother driveS_, levelS_;
    float driveCur_ = 0.5f, levelCur_ = 0.6f;

    struct Ch { BiquadFilter inHP, fbLP, toneSh, outLP, dcBlk; };
    std::array<Ch, kMaxCh> ch_;

    void recalc() noexcept;
};
