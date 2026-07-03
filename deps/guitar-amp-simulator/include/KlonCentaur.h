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
    static constexpr double kDriveMax  = 26.0;   // clip-path gain span (OD, not fuzz)
    static constexpr double kCleanGain =  0.72;  // always-present clean blend → transparency
    static constexpr double kWetMin    =  0.30;  // clipped amount at drive 0 (clean boost)
    static constexpr double kWetSpan   =  0.85;  // extra clipped amount toward drive 1
    static constexpr double kMakeup    =  0.80;
    static constexpr double kInHPfc    =  18.0;
    static constexpr double kPreHiFc   = 900.0;  // pre-clip treble emphasis (clarity)
    static constexpr double kPreHiDb   =   4.0;
    static constexpr double kToneFc    = 1200.0; // treble tone shelf centre

    double fs_ = 0.0;

    float drive_ = 0.4f, tone_ = 0.55f, level_ = 0.6f;
    LinearSmoother driveS_, levelS_;
    float driveCur_ = 0.4f, levelCur_ = 0.6f;

    struct Ch { BiquadFilter inHP, preHi, toneHi, dcBlk; };
    std::array<Ch, kMaxCh> ch_;

    void recalc() noexcept;   // rebuilds filters incl. the tone-dependent treble shelf
};
