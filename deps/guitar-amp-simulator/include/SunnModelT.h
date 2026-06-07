#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "SunnT_TriodeStage.h"
#include "SunnModelTToneStack.h"
#include "SunnPowerAmp6550.h"
#include "NegativeFeedbackLoop.h"
#include "OutputTransformerModel.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// SunnModelT — First-Generation Sunn Model T (circa 1973, pre-Fender)
// ─────────────────────────────────────────────────────────────────────────────
//
// Authentic dual-channel topology with full component-accurate signal path.
//
// FRONT PANEL CONTROLS:
//   Channel 1: Vol1, Bass1, Mid1, Treble1, Bright1
//   Channel 2: Vol2, Bass2, Mid2, Treble2, Bright2
//   Master: Master Volume, Presence, Sag
//   Advanced: ChannelLink (independent/parallel/series), InputPad
//
// SIGNAL PATH (all at oversampled rate):
//
//   Input
//     │ [InputPad: 0 dB or −6 dB]
//     │
//     ├─ [V1A: 12AX7 — Ch1 input gain] ← kSunn_S1
//     │    Vol1 drives V1A input level
//     │    → InterHPF @ 60 Hz, coupling × 0.55
//     │    → Ch1 Tonestack (bass1, mid1, treble1)
//     │    → [Bright1 switch: +3 dB shelf @ 2.5 kHz]
//     │    → [V2A: 12AX7 — Ch1 recovery/driver] ← kV2A
//     │
//     ├─ [V1B: 12AX7 — Ch2 input gain] ← kV1B
//     │    Vol2 drives V1B input level
//     │    → InterHPF @ 60 Hz, coupling × 0.55
//     │    → Ch2 Tonestack (bass2, mid2, treble2)
//     │    → [Bright2 switch: +3 dB shelf @ 2.5 kHz]
//     │    → [V2B: 12AX7 — Ch2 recovery/driver] ← kV2A
//     │
//     CHANNEL LINK:
//     │  Independent: only Ch1 path active
//     │  Parallel:    (Ch1_out + Ch2_out) × 0.5
//     │  Series:      Ch1_out → V1B input (Ch1 drives Ch2 for doom cascade)
//     │
//     → [PostMix HPF @ 55 Hz — sub-bass cut]
//     │
//     → [Master Volume attenuator] (pre-PI, authentic MV position)
//     │
//     → [V3: 12AX7 cathodyne PI]
//          outPos → Push tube (top of primary)
//          outNeg → Pull tube (bottom of primary, slight imbalance)
//     │
//     → [SunnPowerAmp6550: 4× 6550A ultralinear]
//          B+ sag + screen sag
//     │
//     ← [NFB loop: shelved high-pass, presence-controlled]
//          Feeds back to PI cathode (modelled as one-sample delay)
//     │
//     → [OutputTransformerModel: kModelT]
//          LF rolloff @ 22 Hz (big primary inductance)
//          HF rolloff @ 14 kHz
//          Leakage resonance peak @ 12 kHz +1.2 dB
//     │
//     → [Air LP @ 16 kHz] → output
//
// CATHODYNE PI (V3):
//   Single 12AX7, equal plate and cathode loads.
//   Balanced push-pull split, gain ≈ ±0.95 per output.
//   Slight asymmetry (imbalance = 0.975) models real-world component tolerance.
//
// TUBE: 6550A (recommendedTubeType = 0 — 6L6GC family).
//
// NOTE on master volume:
//   Original Model T has NO master volume.  MasterVol=1.0 (full) bypasses the
//   attenuator completely, giving authentic non-MV behavior.  MasterVol<1.0
//   attenuates before the PI, allowing PI/power-amp distortion at lower output.
// ─────────────────────────────────────────────────────────────────────────────
class SunnModelT final : public AmpModelBase {
public:
    enum class ChannelLink { Independent = 0, Parallel = 1, Series = 2 };

    static constexpr int kMaxCh = 2;  // stereo audio channels (L/R)

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; } // 6L6GC / 6550
    const char* modelName()           const noexcept override { return "Sunn Model T"; }

private:
    double oversampledFs_ = 0.0;

    // ── Channel 1 parameters ─────────────────────────────────────────────────
    float vol1_    = 0.50f;
    float bass1_   = 0.50f;
    float mid1_    = 0.50f;
    float treble1_ = 0.50f;
    bool  bright1_ = false;

    // ── Channel 2 parameters ─────────────────────────────────────────────────
    float vol2_    = 0.50f;
    float bass2_   = 0.50f;
    float mid2_    = 0.50f;
    float treble2_ = 0.50f;
    bool  bright2_ = false;

    // ── Master section ────────────────────────────────────────────────────────
    float master_   = 1.0f;   // 1.0 = no attenuation (authentic non-MV)
    float presence_ = 0.45f;  // NFB shelf [0=max boost, 1=flat]; slight HF bloom
    float sag_      = 0.30f;  // power supply sag depth

    // ── Advanced controls ─────────────────────────────────────────────────────
    ChannelLink channelLink_ = ChannelLink::Independent;
    float       inputPad_    = 1.0f;  // 1.0 = 0 dB; 0.5 = −6 dB

    LinearSmoother vol1Smooth_, vol2Smooth_, masterSmooth_;

    // ── Per-audio-channel state ───────────────────────────────────────────────
    struct AudioChannelState {
        // Input pad / HPF
        BiquadFilter inputHPF;        // sub-bass cut @ 55 Hz

        // Channel 1 preamp (V1A → TS1 → V2A)
        SunnT_TriodeStage   v1a;      // kV1A: Rk=2.7k+1µF bypass → bloom
        BiquadFilter        ch1CoupHP;// 60 Hz HPF coupling cap
        SunnModelTToneStack ts1;      // Model T voicing, bass1/mid1/treble1
        BiquadFilter        bright1F; // +3 dB shelf @ 2.5 kHz (bright switch)
        SunnT_TriodeStage   v2a;      // kV2A: Rk=1.5k+25µF → tighter drive

        // Channel 2 preamp (V1B → TS2 → V2B)
        SunnT_TriodeStage   v1b;      // kV1B: Rk=2.7k, no bypass → cold asymmetric
        BiquadFilter        ch2CoupHP;// 60 Hz HPF
        SunnModelTToneStack ts2;
        BiquadFilter        bright2F;
        SunnT_TriodeStage   v2b;      // kV2A: Rk=1.5k+25µF → tighter drive
        // Frequency-dependent response of the Brite volume pot + bright cap.
        // Coefficients are recomputed whenever vol2 or briteCapValue changes.
        BiquadFilter        briteCapShelf;

        // Post-mix
        BiquadFilter     postMixHP;   // 55 Hz sub-bass cut after channel sum
        BiquadFilter     airLP;       // 16 kHz air rolloff
        // Output voicing correction, tuned to the real Model T NAM capture (nam_compare):
        // the model honked at ~1 kHz and lacked presence at 3-5 kHz vs the reference,
        // which read as a boxy/fuzzy rather than open amp voice.
        BiquadFilter     voiceCut;    // -1.6 dB peaking @ 1 kHz  (tame upper-mid honk)
        BiquadFilter     voicePres;   // +2.6 dB peaking @ 3.8 kHz (restore amp presence)

        // Cathodyne PI (V3: 12AX7)
        TriodeComponent  v3pi;        // unity-gain PI triode model

        // Power amp (6550 UL)
        SunnPowerAmp6550 powerAmp;

        // NFB loop (presence + global NFB)
        NegativeFeedbackLoop nfb;
        float nfbPrev = 0.0f;         // one-sample delayed output for NFB

        // Output transformer
        OutputTransformerModel xfmr;
    };
    std::array<AudioChannelState, kMaxCh> ch_;

    // ── Bright-cap value ──────────────────────────────────────────────────────
    float briteCapValue_    = 120e-12f;  // Brite channel bright cap (F)
    float lastBriteCapVol2_ = -1.0f;    // vol2 used for last coeff update; -1=never

    // ── Passive mixing network constants (Ω) ─────────────────────────────────
    // Triode plate output impedance: Ra(100 kΩ) ∥ rp(≈67 kΩ) ≈ 40 kΩ for kSunn_S3
    static constexpr float kRsrc  = 40.0e3f;
    static constexpr float kRpot  = 1.0e6f;  // both volume pots: 1 MΩ audio taper
    static constexpr float kRgrid = 1.0e6f;  // next-stage grid-leak resistor

    // ── Cathodyne PI constants ────────────────────────────────────────────────
    static constexpr float kPIGain      = 0.95f;
    static constexpr float kPIImbalance = 0.975f;

    // ── Pre-tonestack normalisation ───────────────────────────────────────────
    static constexpr float kPreToneGain = 0.45f;

    // ── Stage coupling constants ──────────────────────────────────────────────
    static constexpr float kCouple12 = 0.55f;

    // ── Helpers ──────────────────────────────────────────────────────────────
    void updateBriteCapCoeffs(float vol2) noexcept;

    // Returns the audio-taper wiper fraction for a knob position α ∈ [0,1].
    // Approximates a 1 MΩ log-taper pot: 10% resistance at 50% rotation.
    static float audioTaper(float alpha) noexcept;

    // Passive mixing-node voltage.
    // v1/v2: normalised channel outputs; alpha1/alpha2: pot positions [0,1].
    float passiveMixNode(float v1, float v2,
                         float alpha1, float alpha2) const noexcept;

    // Same as passiveMixNode but for a single active channel (Independent mode).
    float singleChannelMix(float v1, float alpha1) const noexcept;

    // Process Ch1 path: returns V2A output (post-tonestack triode).
    // Volume pot is NOT applied here; it is applied at the mixing stage.
    float processCh1(AudioChannelState& s, float x) noexcept;
    // Process Ch2 path: returns V2B output (including bright-cap shelf).
    float processCh2(AudioChannelState& s, float x) noexcept;

    static float cathodyneSplit(float x, float& outPos, float& outNeg) noexcept;
    static float softLimit(float x) noexcept;
};
