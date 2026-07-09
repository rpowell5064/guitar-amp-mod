#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Hiwatt DR103 (component-flavoured model) ──────────────────────────────────
//
// High-headroom British clean (Partridge transformers, hi-fi-grade, very loud and
// bright). The platform under David Gilmour's Big Muff: it stays clean, tight and
// full while the fuzz supplies the dirt. Two clean Fender-type triode stages run at
// low drive for big headroom, a British (Marshall-type) tonestack, a "brilliance"
// high-shelf, extended top and a stiff supply (almost no sag).
//
// Tube: EL34 power section (recommendedTubeType = 1).
class HiwattDR103Model final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34
    const char* modelName()           const noexcept override { return "Hiwatt DR103"; }

private:
    double oversampledFs_ = 0.0;

    float gain_     = 0.5f;
    float bass_     = 0.5f;
    float mid_      = 0.5f;
    float treble_   = 0.5f;
    float presence_ = 0.5f;
    float master_   = 0.7f;
    float sag_      = 0.12f;  // stiff Partridge supply — minimal sag

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChannelState {
        BiquadFilter     inputHPF;     // 30 Hz — full but controlled lows
        BiquadFilter     inputBright;  // Brilliant-channel bright cap PRE gain (survives drive)
        TriodeComponent  stage1;       // kFenderV1 (clean, high headroom)
        BiquadFilter     inter12HPF;   // 35 Hz
        TriodeComponent  stage2;       // kFenderV2
        ToneStackComponent tonestack;  // Marshall (British) voicing
        BiquadFilter     airLP;        // 20 kHz — extended top
        BiquadFilter     brightShelf;  // brilliance shelf — broad top-end lift
        BiquadFilter     presencePk;   // presence PEAK @ ~3 kHz (Hiwatt hi-fi plateau)
        BiquadFilter     bodyShelf;    // low-shelf — TIGHTEN the lows (cut)

        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    static constexpr float kPreToneGain = 0.42f;
    static constexpr float kCouple12    = 0.70f;

    static float softLimit(float x) noexcept;
};
