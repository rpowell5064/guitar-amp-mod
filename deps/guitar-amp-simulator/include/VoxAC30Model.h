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

    static constexpr float kPreToneGain = 0.42f;
    static constexpr float kCouple12    = 0.70f;

    static float softLimit(float x) noexcept;
};
