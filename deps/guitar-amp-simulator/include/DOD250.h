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
    static constexpr double kGainMin = 1.2;    // gain at drive 0 (EXPONENTIAL/audio taper)
    static constexpr double kGainMax = 41.0;   // gain at drive 1 — g = kGainMin·(kGainMax/kGainMin)^drive
                                               // (linear 1+drive·40 slammed the diodes even at low drive =
                                               //  static ~40% THD; the real DOD cleans up at low gain/soft pick)
    static constexpr double kVf      = 0.42;   // shunt-diode clamp (V) — hard clip threshold
    static constexpr double kHard    = 2.6;    // clip hardness (higher = squarer/harder)
    static constexpr double kMakeup  = 0.85;   // output trim (matched to capture loudness)
    static constexpr double kInHPfc  = 125.0;  // input coupling HP — the DOD 250 cuts bass HARD (captures show
                                               // -3.6..-6.8 dB @50 Hz rel 500, tighter with more gain); was 80 = too boomy
    static constexpr double kFbLpFc  = 6000.0; // gain-stage bandwidth limit → mid-forward (was 3200 = too dark up top)
    static constexpr double kToneFc  = 1500.0; // post-clip tilt centre
    static constexpr double kLmCutFc = 150.0;  // low-mid trim: the hard clip regenerates ~+3 dB @125 Hz that
    static constexpr double kLmCutDb = -2.5;   // the real DOD doesn't have (its bass clips less) — flatten it
    static constexpr double kLmCutQ  = 0.9;

    double fs_ = 0.0;

    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.6f;
    LinearSmoother driveS_, levelS_;
    float driveCur_ = 0.5f, levelCur_ = 0.6f;

    struct Ch { BiquadFilter inHP, fbLP, toneSh, outLP, dcBlk, lmCut; };
    std::array<Ch, kMaxCh> ch_;

    void recalc() noexcept;
};
