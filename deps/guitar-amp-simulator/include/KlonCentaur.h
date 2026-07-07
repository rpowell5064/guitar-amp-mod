#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <cmath>
#include <string>

// ── Klon Centaur style transparent overdrive  (parody name: "Gilded Horse") ───
//
// The Klon's trick is a CLEAN BLEND: the dry signal is always summed with a
// germanium-diode soft-clipped path, so it adds gain + a treble/clarity lift
// WITHOUT losing the note's dynamics or low end — the "transparent" boost/OD
// behind modern blues-rock leads (John Mayer's Klon-into-Dumble singing tone).
//
// Signal path (oversampled):
//   in → HP → treble pre-emphasis → germanium soft clip (warm, slightly asym)
//      → SUM with the clean path → DC block → treble tone shelf → level → out.
//
// Params (OverdriveBase): "drive" "tone" "level" [0,1].
class KlonCentaur final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Klon Centaur"; }
    int recommendedTubeType() const noexcept override { return 0; }   // 6L6 (Fender/Dumble-ish)

private:
    // Tuned to a pedal-direct Klon NAM capture (nam_compare --model klon): the real pedal is
    // a lower-gain, near-symmetric (odd-dominant, ~0% h2), mid-focused OD — NOT bass-heavy or
    // even-harmonic-rich. Gain span dropped 26→16, germanium bias 0.12→0.05 (kill excess h2),
    // a low-shelf trims ~3 dB of deep bass, and the tone control now sweeps a real dark→bright
    // range (shelf + tone-tracked LP) instead of only a bright shelf.
    static constexpr double kDriveMax  = 16.0;   // clip-path gain span (OD, not fuzz)
    static constexpr double kCleanGain =  0.85;  // always-present clean blend → transparency (raised: the
                                                 // capture's THD is near-FLAT vs pick force — clean-dominant)
    static constexpr double kWetMin    =  0.22;  // clipped amount at drive 0 (clean boost)
    static constexpr double kWetSpan   =  0.70;  // extra clipped amount toward drive 1
    static constexpr double kMakeup    =  0.80;
    static constexpr double kInHPfc    =  18.0;
    static constexpr double kLoShFc    = 340.0;  // deep-bass trim (Klon is mid-focused, not bassy)
    static constexpr double kLoShDb    =  -3.5;
    static constexpr double kPreHiFc   = 900.0;  // pre-clip treble emphasis (clarity)
    static constexpr double kPreHiDb   =   4.0;
    static constexpr double kToneFc    = 1900.0; // treble tone shelf centre (above the upper-mids so tone-cut doesn't scoop 0.8–1.5k)
    static constexpr double kGeBias    =  0.05;  // germanium clip asymmetry (near-symmetric)

    double fs_ = 0.0;

    float drive_ = 0.4f, tone_ = 0.55f, level_ = 0.6f;
    LinearSmoother driveS_, levelS_;
    float driveCur_ = 0.4f, levelCur_ = 0.6f;

    struct Ch { BiquadFilter inHP, preHi, loSh, toneHi, toneLP, dcBlk; };
    std::array<Ch, kMaxCh> ch_;

    void recalc() noexcept;   // rebuilds filters incl. the tone-dependent treble shelf
};
