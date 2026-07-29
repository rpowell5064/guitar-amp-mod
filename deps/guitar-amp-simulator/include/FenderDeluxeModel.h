#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include <array>
#include <string>

// ── Fender Deluxe Reverb (component model) ────────────────────────────────────
//
// Signal path (all at oversampled rate):
//
//   in → InputHPF(35 Hz) — bright cap coupling replica
//      → [gain] Stage 1  (kFenderV1 — full bypass, Fender bloom)
//      → inter12 HPF(40 Hz) × coupling(0.65)
//      → [gain] Stage 2  (kFenderV2 — hotter bias, more asymmetry)
//      → ×0.50 pre-tonestack normalisation
//      → Fender tonestack (bass/mid/treble) + presence
//      → air LP 1-pole @ 18 kHz
//      → [master] level scale
//      → sag (envelope compression, 180 ms) → softLimit
//
// Tube: 6V6 power section (recommendedTubeType = 0 / 6L6GC family).
class FenderDeluxeModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; } // 6L6GC/6V6 family
    const char* modelName()           const noexcept override { return "Fender Deluxe Reverb"; }

private:
    double oversampledFs_ = 0.0;

    float gain_     = 0.5f;
    float bass_     = 0.5f;
    float mid_      = 0.5f;
    float treble_   = 0.5f;
    float presence_ = 0.45f; // warm but not dark; AB763 is characteristically clean-bright
    float master_   = 0.65f;
    float sag_      = 0.45f; // tube rectifier (5AR4) with under-rated PSU — meaningful sag

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChannelState {
        BiquadFilter     inputHPF;      // 35 Hz HPF
        TriodeComponent  stage1;        // kFenderV1
        BiquadFilter     inter12HPF;    // 40 Hz HPF
        TriodeComponent  stage2;        // kFenderV2
        ToneStackComponent tonestack;   // Fender type
        BiquadFilter     airLP;         // 18 kHz LP
        // Voicing correction toward the real Deluxe Reverb DI (nam_compare): the model
        // measured ~13 dB too dark at 3-8 kHz (tonestack treble shelf + power-amp
        // presence cut) and a touch too bright at 800 Hz-1.2 k. These restore the
        // bright Fender voice; the Cab plugin then supplies the speaker rolloff.
        BiquadFilter     voiceShelf;    // treble recovery (high-shelf)
        BiquadFilter     voiceCut;      // presence-region dip (peaking)
        BiquadFilter     voiceMidBoost; // low-mid restore (peaking) -- item #28/#25 re-voice
        BiquadFilter     voiceBassShelf;// bass restore (low-shelf) -- item #28/#25 re-voice

        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    static constexpr float kPreToneGain = 0.35f;
    static constexpr float kCouple12    = 0.65f;

    static float softLimit(float x) noexcept;
};
