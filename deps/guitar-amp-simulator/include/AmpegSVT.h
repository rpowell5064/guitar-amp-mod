#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Ampeg SVT "Blue Liner" (1969 blue-line SVT, Channel 1) — BASS amp ────────
//
// The suite's first bass amp. One channel, no modes; the panel switches are:
//   "ultralo"  Ultra-Lo   — primarily a MID NOTCH (−10 dB @ 500 Hz) plus a
//                           small +2 dB @ 40 Hz shelf: the illusion of more
//                           bass without LF overload (measured, not folklore)
//   "ultrahi"  Ultra-Hi   — +9 dB high shelf @ 8 kHz
//   "midfreq"  Mid select — 3-position mid CENTER: 0=220 / 1=800 / 2=3000 Hz
//
// Tone stack (Ampeg-published, deliberately asymmetric — baked into the knob
// mapping): Bass ±12 dB @ 40 Hz; Mid +10/−20 dB @ selected center;
// Treble +15/−20 dB @ 4 kHz. Built IN-MODEL (not ToneStackComponent): the
// runtime mid selector and asymmetric ranges don't fit the static TypeSpec
// table.
//
// Preamp: 12AX7 input + 12DW7 recovery — clean-headroom front end (Fender
// V1/V2 circuit params); the driven-SVT growl budget deliberately lives in
// the DOWNSTREAM PowerAmpProcessor (6×6550 row, PA case 11, Tube_6550):
// keep the cascade polite. Input HP is 25 Hz — bass fundamentals live an
// octave below the guitar amps' 70-130 Hz corners.
//
// VOICED to public captures via nam_compare --model svt (TONE3000 SVT packs);
// see the Blue Liner plan. No DNR (clean-headroom amp; the plugin gate covers
// the rig floor).
class AmpegSVT final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 5; } // 6550
    const char* modelName()           const noexcept override { return "Blue Liner"; }

private:
    static constexpr float kMidCenters[3] = { 220.0f, 800.0f, 3000.0f };

    double oversampledFs_ = 0.0;
    int    ultraLo_ = 0, ultraHi_ = 0, midFreq_ = 1;   // midfreq default 800 Hz
    float  gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float  presence_ = 0.5f, master_ = 0.6f, sag_ = 0.5f;

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChannelState {
        BiquadFilter    inHP;                       // 25 Hz — bass fundamentals pass
        BiquadFilter    ultraLoPk, ultraLoSh;       // −10 dB @ 500 + 2 dB @ 40 (engaged)
        BiquadFilter    ultraHiSh;                  // +9 dB @ 8 kHz (engaged)
        BiquadFilter    bassSh, midPk, trebSh;      // the asymmetric Baxandall-ish stack
        BiquadFilter    presSh;                     // shared Presence knob: gentle 3 kHz shelf
                                                    // (real SVT has none, but a DEAD visible
                                                    // knob is the FF-Tone-knob bug class)
        BiquadFilter    airLP, dcBlk;
        TriodeComponent stage1, stage2, stageDrv;   // 12AX7 in, 12DW7 recovery, driver
        float sagEnv = 0.0f, sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    static float softLimit(float x) noexcept;
    void recalcFilters() noexcept;
};
