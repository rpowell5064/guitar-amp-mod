#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Electro-Harmonix Big Muff Pi ─────────────────────────────────────────────
//
// Component-informed model of the EHX Big Muff Pi fuzz pedal
// (Triangle variant, circa 1969–1973).
//
// Signal path (runs at oversampled rate):
//
//   in → inputHP   (R=47 kΩ, C=100 nF → fc=33.9 Hz, 1-pole coupling cap)
//      → stage1    (NPN transistor amp: y = tanh(gain·x), symmetric silicon clip)
//      → stageLP   (interstage transistor bandwidth limit, fc=4.8 kHz, 1-pole)
//      → stage2    (identical second transistor clipping stage)
//      → toneStack (passive LP/HP voltage-divider blend, mid-scoop characteristic)
//      → volume    (linear gain [0, 2])
//      → out
//
// Clipping stages (both identical, gain shared via "sustain"):
//   Open-loop gain: g = 2 + 98 · sustain   → 2× to 100× as sustain 0→1
//   Clip function:  y = tanh(g · x)
//   Models two 1N914 silicon diodes anti-parallel in transistor feedback.
//
// Tone network (passive LP/HP voltage-divider, characteristic mid-scoop):
//   LP: 2nd-order Butterworth LP at 300 Hz  (bass path)
//   HP: 2nd-order Butterworth HP at 2.0 kHz (treble path)
//   out = (1−tone)·LP(x) + tone·HP(x)
//   → tone=0: bass-heavy; tone=0.5: deep mid-scoop (~10 dB); tone=1: treble-heavy
//
// Parameter mapping (standard OverdriveBase IDs):
//   "drive"  [0,1] → sustain pot (0 = low gain/mild fuzz, 1 = full fuzz)
//   "tone"   [0,1] → tone network (0 = bass, 1 = treble)
//   "level"  [0,1] → volume pot, maps to [0, 2] gain (centre ≈ unity)
//   "mix","octave" → ignored
//
// Factory preset: drive = 0.55, tone = 0.50, level = 0.65
//
class EHXBigMuff final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Big Muff Pi"; }

private:
    double fs_ = 0.0;

    float sustain_ = 0.55f;
    float tone_    = 0.50f;
    float volume_  = 0.65f;

    LinearSmoother sustainSmooth_, volSmooth_;
    float sustainCur_ = 0.55f, volCur_ = 0.65f;

    struct ChannelState {
        BiquadFilter inputHP;    // 33.9 Hz, 1-pole — dc blocking / input coupling cap
        BiquadFilter stageLP;    // 4.8 kHz, 1-pole — interstage transistor bandwidth
        BiquadFilter toneLP;     // 300 Hz, 2nd-order Butterworth — bass path
        BiquadFilter toneHP;     // 2.0 kHz, 2nd-order Butterworth — treble path
    };
    std::array<ChannelState, kMaxCh> ch_;

    void recalcFilters() noexcept;

    // Symmetric soft-clip modelling two 1N914 diodes anti-parallel.
    // y = tanh(gain · x).  No amplitude normalisation — output saturates to ±1
    // at high gains, which is the intended fuzz behaviour.
    static float clipStage(float x, float gain) noexcept;
};
