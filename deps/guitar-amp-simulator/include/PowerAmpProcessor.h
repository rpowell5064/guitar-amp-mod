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
// Signal path (item #24, 2026-07-28: NFB now wraps the whole power stage,
// closing sample-by-sample around the nonlinearity — was a post-hoc correction
// on the already-clipped output, now a true closed loop):
//   in → [NFB: subtract HP(prev fully-processed output)] → sag reduction
//      → 2x upsample → tube waveshaper → 2x downsample → transformer model
//      → speaker impedance ─┘ (feeds NFB for the next sample)
//      → presence/depth EQ [outside the loop]
//      → [early reflections + LF bloom] → master volume → out
class PowerAmpProcessor : public AudioBlock {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    void     setTubeType(TubeType type) noexcept;
    TubeType getTubeType() const noexcept { return tubeType; }

    // Supply-sag envelope (item #22, 2026-07-28): the mono, pre-oversampling
    // envelope that already drives the power tube's own sag gain reduction
    // (process(), "Pre-compute sag" section) — tracks abs(ch0 input) with the
    // per-tube attack/release pair. Exposed read-only so the PREAMP stages can
    // be fed the same physically-shared supply droop (real amps: one B+ rail
    // feeds every stage). Same units as the audio signal (not normalised to
    // [0,1]) — callers scale by their own coupling coefficient.
    float getSagEnvNorm() const noexcept { return sagEnv; }

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
        float duty = 0.0f; // push-pull duty asymmetry → even harmonics (0 = symmetric)
        float paDrive  = 1.0f; // pre-waveshaper drive (PA distortion contribution)
        float paMakeup = 1.0f; // post-waveshaper level restore (loudness-neutral pair)
        // Current-dependent mains ripple (item #27, 2026-07-28): the rectified-mains
        // ripple riding the B+ has historically been a fixed −50 dBFS AM term
        // regardless of how hard the amp is being driven. Real ripple grows with
        // rectifier current draw — this scales additional ripple depth by the sag
        // envelope so louder playing = deeper ripple = real "ghost note"
        // intermodulation, not just a constant hum floor. 0 = bit-identical (the
        // fixed base term is unchanged either way).
        float rippleSagCoupling = 0.0f;
        // LTP tail coupling (item #29, 2026-07-28): a long-tailed-pair phase
        // inverter's two triodes share one large tail resistor, so when BOTH
        // grids swing toward conduction together (louder/harder playing), the
        // tail voltage rises and pulls BOTH grids colder in common-mode — a
        // LEVEL-DEPENDENT imbalance/compression that grows under drive, not a
        // fixed offset. The suite's real per-amp LTP presets already exist in
        // PhaseInverter.cpp (kMarshall_LTP/kEVH_LTP/kOrange_LTP) but that class
        // isn't wired into the shared PowerAmpProcessor at all (only Sunn/DR use
        // it) and its imbalance is a STATIC constant, not level-dependent — this
        // adds the missing dynamic piece directly to the existing lumped
        // waveshaper (see tubeWaveshaper's ltpBias param) rather than
        // introducing a full two-path PI split. 0 = bit-identical.
        float ltpTail = 0.0f;
        // Flux-domain OT saturation, per-amp (2026-07-29): ON suite-wide since
        // Phase-2, but a git-bisect + toggle probe proved it single-handedly
        // killed the EVH Red's real ~300 ms attack swell AND caused its LF
        // over-distortion (THD@110 23-25% vs the capture's 15-16; exact with
        // flux off) -- the flux integrator's LF-selective grind eats exactly
        // the slow LF dynamics that ARE the 5150's feel. Default true keeps
        // every other amp bit-identical.
        bool fluxOT = true;
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
    // Push-pull duty asymmetry (2026-07-26, additive default 0 = bit-identical): a DC
    // offset on the WAVESHAPER ARGUMENT (inside the clip window, so it survives full
    // rail — the RAT duty-cycle lesson) models unmatched power tubes / imperfect PI
    // balance = the EVEN harmonics (h2/h4/h6) every capture shows but symmetric
    // preamp cascades can't make. Zero-input response exactly compensated. Per-amp
    // via AmpDefaults.duty; only amps whose captures demand it get a non-zero value.
    float    duty_      = 0.0f;    // [0,1] → up to ±0.8 tanh-arg offset
    // PA-as-distortion-contributor (2026-07-26, the "promote the PA" project): the
    // shared PA barely distorted at stock gain-staging (preamps dominate → the
    // suite-wide missing evens + THD@1k-at-half). paDrive pushes INTO the waveshaper,
    // paMakeup restores the level after — tuned as a loudness-neutral pair per amp
    // vs its capture. Both 1.0 = bit-identical.
    float    paDrive_   = 1.0f;    // [0.25,8] pre-waveshaper drive
    float    paMakeup_  = 1.0f;    // [0.1,4]  post-waveshaper level restore
    float    rippleSagCoupling_ = 0.0f;   // item #27: extra ripple depth per unit sagEnv, 0 = off
    float    fluxPole_  = 0.0f;    // leaky-integrator pole (OT LF corner ~25 Hz)
    float    fluxDrive_ = 0.015f;  // flux-saturation onset (Phase-2 tunable)
    // Flux shear (2026-07-28, PA project): linear term blended into the flux
    // saturator, modeling the B-H curve's residual deep-saturation slope
    // (air-core inductance) + winding resistance -- real iron NEVER goes
    // truly flat. Without it, deep LF overdrive pins the tanh, consecutive
    // saturated samples cancel in the differentiate step, and the stage
    // COLLAPSES toward silence (measured -67 dBFS from a +12 dB LF shelf --
    // the EVH bass-cutout mechanism). With shear a, deep saturation degrades
    // gracefully to ~a*x instead. Small-signal response is unchanged (slope
    // still exactly 1).
    float    fluxShear_ = 0.12f;
    // LTP tail coupling (item #29): coupling depth (0 = off) + fast per-channel
    // envelope of the pre-waveshaper drive (proxy for "how hard both grids are
    // swinging together" absent a literal two-path PI split). Time constants
    // are fast (tail-resistor-scale, not power-supply-scale like sag).
    float    ltpTail_   = 0.0f;
    float    ltpEnv[kMaxCh] = {};
    float    ltpAtt_ = 0.0f, ltpRel_ = 0.0f;

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
    // Dual-corner asymmetric coupling (duty mechanism, derived offline in
    // tools/evens_harness): the + and − halves of the squared PA output are high-
    // passed at DIFFERENT corners (sign-split by a fast selector), so the two flat-
    // tops tilt/curve differently → the EVEN harmonics (h2/h4) real push-pull stages
    // show but a symmetric preamp cascade + memoryless PA cannot make. duty_=0 → off.
    // Verified on the Rockerverb dimed capture: h2 5.5→~18, h4 1.5→~11 at depth 0.8.
    float dcLpA[kMaxCh] = {}, dcLpB[kMaxCh] = {}, dcSgn[kMaxCh] = {};
    float dcKA = 0.0f, dcKB = 0.0f, dcKSgn = 0.0f;   // corners set in prepare
    float bloomVcaAttCoef   = 0.0f, bloomVcaRelCoef = 0.0f;

    // ── Power-supply ripple LFO ────────────────────────────────────────────────
    float ripplePhase = 0.0f;

    // ── 2× oversampling anti-alias filters (2026-07-27: raised 4th->8th order to
    // match OversamplingWrapper's fix — this stage runs the tube waveshaper's hard
    // nonlinearity and was still on the WEAKER 4th-order design the preamp wrapper
    // was upgraded FROM ("left a measurable alias floor on high-gain amps"); the
    // power amp had never gotten the same fix, despite doing its own saturation.
    // Four cascaded biquads (prewarped bilinear 8th-order Butterworth, see
    // OversamplingWrapper::computeAACoeffs — same design, factor=2 here) at 0.45*fs.
    struct OsFilter {
        BiquadFilter s0, s1, s2, s3;
        float process(float x) noexcept { return s3.process(s2.process(s1.process(s0.process(x)))); }
        void  reset()  noexcept { s0.reset(); s1.reset(); s2.reset(); s3.reset(); }
    };
    std::array<OsFilter, kMaxCh> upAA, downAA;

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
    // Gentle FIXED pre-saturation low-pass (2026-07-27): the flux integrate->
    // saturate->differentiate design is a self-inverting pair whose "1-sample
    // delay" is calibrated for the NATIVE rate — two attempts to protect it with
    // dedicated oversampling both measurably changed an already-tuned amp's
    // distortion character (the differentiator's implicit 1/T gain doesn't cancel
    // cleanly at 2x rate; reverted both times, see PowerAmpProcessor.cpp history).
    // This is a much lower-risk mitigation instead: a plain native-rate LP right
    // before the nonlinearity, trimming only the top octave or so of energy that's
    // most alias-prone, without touching the calibrated recursive design at all.
    std::array<BiquadFilter, kMaxCh> preSatLP;

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
    // ltpBias = item #29 tail-coupling bias offset (0 = none, bit-identical),
    // computed per-sample from the stateful envelope in process() and passed
    // in here since this function itself holds no state.
    static float tubeWaveshaper(float x, const TubeParams& p, float xover,
                                float duty = 0.0f, float ltpBias = 0.0f) noexcept;
};
