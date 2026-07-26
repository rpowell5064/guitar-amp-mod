#pragma once
#include "AudioBlock.h"
#include "BiquadFilter.h"
#include <array>
#include <vector>
#include <string>

enum class TubeType {
    Tube_6L6GC = 0,
    Tube_EL34  = 1,
    Tube_EL84  = 2,
    Tube_KT88  = 3
};

// Power amp stage: power tube transfer function, dynamic sag, output transformer,
// speaker impedance interaction, and amp-in-the-room feel.
//
// Signal path:
//   in → sag reduction → 2x upsample → tube waveshaper → 2x downsample
//      → NFB loop → transformer model → speaker impedance → presence/depth EQ
//      → [early reflections + LF bloom] → master volume → out
class PowerAmpProcessor : public AudioBlock {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    void     setTubeType(TubeType type) noexcept;
    TubeType getTubeType() const noexcept { return tubeType; }

    // Per-amp historically calibrated power-amp defaults.
    // ampModelIdx matches the plugin's amp_model choice parameter:
    //   0=Fender Deluxe, 1=JCM800, 2=EVH5150, 3=NAM, 4=SunnModelT, 5=Rockerverb50.
    struct AmpDefaults {
        float master;
        float presence;
        float depth;   // LF resonance depth ("Resonance" control on real amps)
        float nfb;
        float sag;
        float bloomVca; // post-saturation sag-VCA depth (bloom + recovery), per amp
    };
    static AmpDefaults getDefaultsForModel(int ampModelIdx) noexcept;

private:
    static constexpr int kMaxCh    = 2;
    static constexpr int kERBufLen = 2048; // max early-reflection delay line

    // ── User parameters ───────────────────────────────────────────────────────
    TubeType tubeType  = TubeType::Tube_EL34;
    float    presence  = 0.5f;  // [0,1] — NFB-loop presence cutoff
    float    depth     = 0.5f;  // [0,1] — LF resonance depth
    float    sagAmount = 0.3f;  // [0,1] — power-supply droop amount
    float    masterVol = 0.7f;  // [0,1]
    float    nfbAmount = 0.4f;  // [0,1] — global negative feedback
    float    resonance = 0.5f;  // [0,1] — speaker resonance peak level
    bool     airFeelOn = false; // enables early reflections + LF bloom
    // Speaker-impedance COUPLING (2026-07-23, additive, default 0 = bit-identical):
    // how much of the speaker's impedance curve (cone-resonance thump + voice-coil
    // HF rise) reaches the output. The blend rides a drive envelope, so it BLOOMS
    // as the power stage works harder — real damping collapse, the Fractal-class
    // speaker interaction feel. Uses the previously dormant spkrPeak filters.
    float    coupling  = 0.0f;  // [0,1]
    // Class-AB crossover notch (item 25, additive default 0 = bit-identical): a soft
    // gain dip near the zero crossing = both power tubes near cutoff. Gives the
    // "dirty when quiet" crossover grit on note decays; Phase-2 sets per-amp depth.
    float    xoverDepth_ = 0.12f;   // class-AB crossover ON, modest (2026-07-26 Phase-2)
    // Flux-domain OT saturation (item 26, additive default off): the transformer core
    // saturates on FLUX (∫V·dt), so low frequencies saturate far earlier than highs
    // (pushed small-iron LF grind). Implemented as a self-inverting leaky integrator
    // → tanh(flux) → differentiator, so it is EXACTLY unity + uncoloured until the
    // flux actually clips. Off = the original instantaneous-voltage tanh.
    bool     fluxOT_    = true;    // flux-domain OT saturation ON (2026-07-26 Phase-2)
    float    fluxPole_  = 0.0f;    // leaky-integrator pole (OT LF corner ~25 Hz)
    float    fluxDrive_ = 0.015f;  // flux-saturation onset (Phase-2 tunable)

    // ── Per-tube model constants ───────────────────────────────────────────────
    struct TubeParams {
        float driveScale;       // waveshaper input drive
        float biasShift;        // cathode bias offset (creates asymmetry)
        float dcOffset;         // output DC nulling (computed in recalcTubeParams)
        float screenComp;       // positive-peak compression (screen grid current)
        float cathodeComp;      // general 2nd-order compression (cathode follower)
        float outputGain;       // post-waveshaper normalisation
        float sagDepth;         // max supply droop ratio [0..1]
        float sagAttackMs;
        float sagReleaseMs;
        float rippleHz;         // mains ripple frequency on B+ supply
        float xfmrHPHz;         // output transformer LF rolloff
        float xfmrHPQ;          // resonance of LF rolloff (transformer leakage)
        float xfmrLPHz;         // output transformer HF rolloff
        float spkrResHz;        // speaker cone resonance
        float spkrResQ;         // speaker resonance Q
        float spkrLPHz;         // speaker voice-coil HF rolloff
    };

    static const TubeParams k6L6GC;
    static const TubeParams kEL34;
    static const TubeParams kEL84;
    static const TubeParams kKT88;
    TubeParams tp{};

    // ── Sag (mono envelope, applied before oversampling) ─────────────────────
    float sagEnv        = 0.0f;
    float sagAttackCoef = 0.0f;
    float sagRelCoef    = 0.0f;

    // ── Post-saturation sag VCA (per-channel, applied after the waveshaper) ───
    // The pre-saturation sag above shapes tone but is masked by the waveshaper
    // limiter; this clean output-side VCA delivers the recoverable compression and
    // pick "bloom" the real power amp shows. Depth is per-amp (getDefaultsForModel).
    float bloomVcaDepth     = 0.0f;
    float bloomVcaEnv[kMaxCh] = {};
    float bloomVcaAttCoef   = 0.0f, bloomVcaRelCoef = 0.0f;

    // ── Power-supply ripple LFO ────────────────────────────────────────────────
    float ripplePhase = 0.0f;

    // ── 2× oversampling anti-alias filters (4th-order Butterworth per channel) ─
    // Two cascaded biquads at 0.45*fs, run at 2*fs, for both up and down paths.
    struct OsFilter {
        BiquadFilter s0, s1; // stages with Q=1.3066 and Q=0.5412
        float process(float x) noexcept { return s1.process(s0.process(x)); }
        void  reset()  noexcept { s0.reset(); s1.reset(); }
    };
    std::array<OsFilter, kMaxCh> upAA, downAA;
    std::vector<float> osBuf[kMaxCh]; // 2 × maxBlockSize scratch

    // ── Negative feedback loop ─────────────────────────────────────────────────
    // HP filters the feedback signal so NFB only affects mids/highs.
    // Presence knob sets the cutoff: higher presence → higher cutoff → more open HF.
    std::array<BiquadFilter, kMaxCh> nfbHP;
    float nfbPrev[kMaxCh] = {};

    // ── Output transformer model ───────────────────────────────────────────────
    std::array<BiquadFilter, kMaxCh> xfmrHP; // LF resonant pole pair
    std::array<BiquadFilter, kMaxCh> xfmrLP; // HF leakage-inductance rolloff
    float xfmrSatState[kMaxCh] = {};          // smoothed output for soft-clip history
    float fluxState[kMaxCh]   = {};           // core flux (leaky integral) — item 26
    float fluxSatPrev[kMaxCh] = {};           // previous saturated flux (differentiator)

    // ── Speaker impedance curve ───────────────────────────────────────────────
    std::array<BiquadFilter, kMaxCh> spkrPeak; // peaking at cone resonance
    std::array<BiquadFilter, kMaxCh> spkrLP;   // voice-coil inductance rolloff
    std::array<BiquadFilter, kMaxCh> cplShelf; // coupling: voice-coil HF impedance rise
    float cplEnv[kMaxCh] = {};                 // coupling drive envelope
    float cplAtt = 0.0f, cplRel = 0.0f;

    // ── Post-stage EQ ─────────────────────────────────────────────────────────
    std::array<BiquadFilter, kMaxCh> presEQ;  // high-shelf presence
    std::array<BiquadFilter, kMaxCh> depthEQ; // low-shelf depth/resonance

    // ── Amp-in-the-room ───────────────────────────────────────────────────────
    float erBuf[kMaxCh][kERBufLen]{};
    int   erWritePos = 0;
    int   erTap[3]   = {};   // tap delay lengths in samples (computed in prepare)

    std::array<BiquadFilter, kMaxCh> bloomLP; // ~150 Hz low-pass for LF bloom
    float bloomEnv[kMaxCh]  = {};
    float bloomEnvCoef      = 0.0f; // ~100ms smoothing

    // ── Helpers ───────────────────────────────────────────────────────────────
    void  recalcTubeParams() noexcept;
    void  recalcFilters();

    // Static waveshaper — no state; safe to call at OS rate.
    // xover = class-AB crossover depth (0 = none, bit-identical).
    static float tubeWaveshaper(float x, const TubeParams& p, float xover) noexcept;
};
