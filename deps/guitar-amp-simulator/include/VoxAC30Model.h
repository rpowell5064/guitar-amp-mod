#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "BiquadFilter.h"
#include "DnrRolloff.h"
#include <array>
#include <string>

// ── Vox AC30 Top Boost (component-flavoured model) ────────────────────────────
//
// Class-A, cathode-biased EL84 CHIME: bright, jangly, glassy top with a springy
// sag/compression when pushed. Two bright triode stages driven a little harder than
// a Fender clean (the Top Boost channel's extra gain), the Vox (Top Boost Baxandall)
// tonestack, a strong brilliance shelf + extended top, and a soft cathode-biased sag.
// The chime/jangle behind The Edge, Brian May, Radiohead, the Beatles, the Byrds.
//
// Tube: EL84 power section (recommendedTubeType = 2).
class VoxAC30Model final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  recalcFit() noexcept;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 2; } // EL84
    const char* modelName()           const noexcept override { return "Vox AC30 Top Boost"; }

private:
    double oversampledFs_ = 0.0;

    float gain_     = 0.5f;
    float bass_     = 0.5f;
    float mid_      = 0.5f;
    float treble_   = 0.5f;
    float presence_ = 0.5f;
    float master_   = 0.7f;
    float sag_      = 0.30f;  // cathode-biased Class-A — noticeable springy sag

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChannelState {
        BiquadFilter       inputHPF;
        BiquadFilter       preHi;        // pre-distortion treble emphasis (drives highs into breakup)
        TriodeComponent    stage1;
        BiquadFilter       inter12HPF;
        TriodeComponent    stage2;
        ToneStackComponent tonestack;   // Vox (Top Boost) voicing
        BiquadFilter       airLP;        // extended top
        BiquadFilter       brightShelf;  // strong brilliance/chime shelf
        BiquadFilter       bodyShelf;    // gentle low-shelf (Vox stays brighter/less bassy)
        BiquadFilter       lowBody;      // wide 180 Hz body peak (LF-THD round 2, 2026-07-29)
        BiquadFilter       chimePk;      // top-octave air peak (item #26 exact-TS re-fit, 2026-07-29)
        BiquadFilter       fitPk, fitHs, fitLo;  // the reference rig-fit residual voicing

        DnrRolloff         dnr;          // decay darkener (2026-07-29: the Vox never got
                                         // the 2026-07-14 DNR extension; with the PA no
                                         // longer crushing idle HF, open-gate note decays
                                         // amplified the rig's hum floor through +18 dB of
                                         // bright voicing = the user's Chime Thirty whine.
                                         // Engaged when gain > 0.45 -- the chimey clean
                                         // presets (gain .2-.35) stay untouched)

        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;
    // the reference rig fit hooks (2026-09-04 "Class-A 30W TB" probe): the reference rig TB is a
    // level-invariant railed core (42-49% THD@1k flat) our polite 2-stage
    // model can't reach even dimed. fit0 stage-span scale | fit1 input dB |
    // fit2 softLimit rail dB | fit3 knee | fit4 post pk dB @ fit5 Hz | fit6
    // post hs4k dB. Neutral defaults.
    // BAKED 2026-09-04 (config W1 of the reference Class-A 30W top-boost probe): mean
    // 48.8 -> 26.6. the reference rig TB is a level-invariant railed core our polite
    // 2-stage model couldn't reach even dimed: stage span x1.8 (+ gain-
    // coupled 0.8: the top of the dial runs hotter, the bottom cleaner),
    // input +3 dB, terminal rail +18 dB / knee 0.2, and the AC30 honk
    // restored (pk150 -8 un-fattens the low mids, pk1800 -7 + hs4k -5 tame
    // the scoop brightness — ours was smiley where the real amp is
    // mid-forward). Knob laws measured CORRECT (noon<->0.5, g25<->0.2).
    static constexpr int kNFit = 9;
    // NOISE PASS 2026-09-05 (user: "more noise even while playing"). fit1 was a
    // +3 dB INPUT drive — a straight pre-gain boost, so it lifted the rig's hiss
    // by 3 dB before the amp ever saw it. Measured against the real input floor
    // (-60.1 dBFS, captured off the device) and the reference rig Vox takes:
    //
    //                 SNR g0.35 / g0.65      reference spectral match
    //   +3 dB (was)      9.4 / 23.8 dB           8.02 dB
    //    0 dB (now)     11.3 / 26.5 dB           7.81 dB
    //
    // Better on BOTH axes — the stage span (fit0) already supplies the drive the
    // real TB has, so the extra input gain was buying nothing but noise. Cutting
    // span as well would buy another ~3 dB of quiet but costs the hardware match
    // (8.76 dB), i.e. it undoes the point of the re-voice — left alone.
    // [2026-09-05 NOISE PASS] stage span 1.8 -> 1.3. The span is what let this
    // polite 2-stage model reach the reference rig's crank, but it multiplies the rig floor
    // along with the signal. Measured (real -60 dBFS floor / the reference rig Vox takes):
    //   span 1.8: SNR 11.3/26.5   match 7.81 dB      (pre-the reference rig was 18.3/38.9)
    //   span 1.3: SNR 15.6/32.0   match 8.57 dB
    // ~5 dB of noise bought for ~0.76 dB of match, on the user's explicit call.
    // Going to 1.1 would restore the old noise floor entirely (18.1/34.8, match
    // 8.46) if the amp still reads too noisy — the knee is shallow down there.
    float fit_[kNFit] = { 1.3f, 0.0f, 18.0f, 0.2f, -7.0f, 1800.0f, -5.0f, -8.0f, 0.8f };
    float slDrive_ = 1.0f, slNorm_ = 1.0f, inDrive_ = 1.0f;

    static constexpr float kPreToneGain = 0.42f;
    static constexpr float kCouple12    = 0.70f;

    float softLimit(float x) noexcept;
};
