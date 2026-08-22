#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <cmath>
#include <string>

// ── BK Butler Tube Driver (parody-safe name: "Tube Chauffeur") ────────────────
//
// The Butler Tube Driver is David Gilmour's Pulse/Division Bell-era staple: a
// REAL 12AX7 run STARVED (~±12 V op-amp rails, not 250 V B+) inside an op-amp
// loop. The starved triode clips gradually and ASYMMETRICALLY — the positive
// half compresses late and softly (grid-current limiting), the negative half
// hits cutoff earlier — giving warm even-order content and an "amp in a box"
// bloom instead of a diode edge. Full frequency range (NO Tube-Screamer mid
// hump: the lows stay in), a single treble-tilt Tone, and generous op-amp gain
// so drive-0/level-up is a fat CLEAN BOOST (the Gilmour trick — he runs one
// low-drive unit as boost/warmth and one hotter for leads). [ElectroSmash-
// style analysis; Gilmourish rig documentation.]
//
// Signal path (oversampled):
//   in → HP 30 Hz (full lows) → op-amp gain (audio-taper, FLOORED — the real
//   unit boosts at drive 0) → gain-stage LP → starved-triode asym soft clip
//   (late +half / early −half, DC-blocked → evens) → tone tilt → output LP
//   → level → out
//
// Params (OverdriveBase): "drive" "tone" "level" [0,1].
class TubeDriver final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Tube Driver"; }
    int recommendedTubeType() const noexcept override { return 3; }   // KT88 — Hiwatt platform (Gilmour)

private:
    // Gain: FLOORED audio taper (2026-08-22, the gain-floor lesson applied from
    // day one — see the TS-808/SD-1 reworks): the op-amp+triode holds ~×2.2
    // (+6.8 dB) at drive 0, the fat clean boost the pedal is famous for.
    static constexpr double kGainFloor = 2.2;
    static constexpr double kGainMax   = 55.0;   // g = kGainFloor·(kGainMax/kGainFloor)^drive
    // Starved-triode rails: positive half clips late/soft (grid limiting),
    // negative half cuts off earlier — the asymmetry is the even-order warmth.
    static constexpr double kRailPos   = 0.92;
    static constexpr double kRailNeg   = 0.58;
    static constexpr double kMakeup    = 0.62;   // unity-ish at drive .35 / level .6
    static constexpr double kInHPfc    = 30.0;   // FULL lows — no TS mid hump
    static constexpr double kStageLPfc = 7500.0; // op-amp/triode bandwidth
    static constexpr double kToneFc    = 1800.0; // treble tilt centre (single Tone knob)
    static constexpr double kOutLPfc   = 9000.0; // output smoothing

    double fs_ = 0.0;

    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.6f;
    LinearSmoother driveS_, levelS_;
    float driveCur_ = 0.5f, levelCur_ = 0.6f;

    struct Ch { BiquadFilter inHP, stageLP, toneSh, outLP, dcBlk; };
    std::array<Ch, kMaxCh> ch_;

    void recalc() noexcept;
};
