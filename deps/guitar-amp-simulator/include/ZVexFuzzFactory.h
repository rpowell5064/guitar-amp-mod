#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── ZVex Fuzz Factory (Vexter, germanium) ────────────────────────────────────
//
// The standard/original Fuzz Factory is a 2-transistor GERMANIUM fuzz (two NOS '60s
// germanium transistors; ZVEX later added a separate silicon version). The captured
// Vexter is the germanium circuit. What makes it chaotic is that its bias points AND
// its SUPPLY VOLTAGE are brought out to knobs. Controls map to real circuit params
// (per ZVEX + build docs — NOT post-EQ):
//   Drive  (drive|sustain)  → fuzz gain / sustain
//   Comp   (comp|bias)      → Q1 bias-starvation — the velcro/spitty/gated character
//   Gate   (gate|inputtrim) → noise gate: sets the floor where the output shuts off
//   Stab   (stab|getemp)    → the OPERATING-VOLTAGE regulator = the supply rail.
//                             UP/right = full supply = TIGHT & stable; DOWN/left =
//                             starved rail = soft/squishy/gated SAG ("dying battery"),
//                             collapsing into motorboat oscillation at the far end.
//   Volume (volume|level)   → output level
//
// The starved rail SAGS dynamically under signal current and recovers via an RC — that
// sag/recover is the FF's bloom (and, fully starved, the motorboat). Runs at the
// OVERSAMPLED rate (OversamplingWrapper ×4).
class ZVexFuzzFactory final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Fuzz Factory"; }

private:
    double fs_ = 0.0;

    // Controls (0..1)
    float drive_ = 0.55f, comp_ = 0.50f, gate_ = 0.70f, stab_ = 0.20f, volume_ = 0.60f;
    LinearSmoother driveSm_, volSm_;
    float driveCur_ = 0.55f, volCur_ = 0.60f;

    // ── HARNESS-TUNABLE constants (kept up top so nam_compare can sweep the voicing) ──
    // Silicon device (normalised supply domain, Vsupply = 1, Is·Rc = 1)
    static constexpr double kVt      = 0.05;     // silicon thermal voltage (sharper knee than the Ge Bender's 0.10)
    static constexpr double kLeak    = 1.0e-5;   // ~no leakage (silicon)
    // Drive taper: loop gain into the fuzz core. kGainMin LOWERED 3.0->0.8 (2026-07-14): the core is so
    // saturated that gain 3 already produced 50% THD — the knob's whole travel spanned ~13 THD points
    // ("Drive does nothing"). 0.8 lets the low half actually clean up so the sweep has a real range.
    static constexpr double kGainMin = 0.8, kGainMax = 48.0;
    // Octave: full-wave rectification of the (shape-retaining) soft fuzz → EVEN harmonics (h2 octave),
    // blended with the fuzz for body/odds; then a post-drive sharpens the blend for THD. Comp = octave amount.
    static constexpr double kOctFc     = 50.0;   // DC-block after the rectifier
    static constexpr double kOctLPfc   = 2000.0; // octave-product lowpass (2026-07-14): the real pedal's
                                                 // rectification weakens with frequency — its 1 kHz THD stays
                                                 // ~90-240% while an unfiltered octave path drove ours past 900%.
                                                 // NOTE: the corner must stay WELL above the guitar band — a low
                                                 // corner (600-850) phase-rotates the octave against the fuzz
                                                 // blend and CANCELS it (1 kHz THD collapsed to ~30). At 2 kHz:
                                                 // 220 Hz products -0.5 dB, 2 kHz products -6 dB.
    static constexpr double kOctDrive  = 12.0;   // octave-path drive on the raw SPIKE (2026-07-14): rectifying the
                                                 // SATURATED fuzz killed the octave exactly when driven hard (|square|
                                                 // = constant = no 2f0) — the capture's THD collapsed only in the model.
                                                 // The spike tip is naturally ~level-independent, so this stays soft.
    static constexpr double kOctMixMin = 0.26, kOctMixMax = 0.52; // Comp -> even-octave (tuned on the harness 2026-07-14 with the spike-sourced rectifier: .72 buried the fundamental 2x too deep at -12 dBFS; max .52 lands the C8 h2..h9 ladder ON the capture, min raised for the C5 base)
    // Comp-driven STARVE (class-C): at high Comp the FF's Q1 bias collapses so the stage conducts
    // only a NARROW slice of the cycle → the fundamental starves and h3/h5/THD blow up past a square's
    // ceiling (measured NAM h3 64% > 33% square-max → only possible with a starved fundamental).
    static constexpr double kStarveMax   = 0.95;   // Comp -> bias offset (narrows the conduction window; raised for more base THD)
    static constexpr double kStarveFloor = 0.04;   // soft floor below the bias (keeps body, limits alias; lowered — the leak linearizes a narrow window)
    // Signal-dependent starve KNEE (2026-07-14): the real FF conducts FULLY at low signal (nam_compare:
    // NAM gives +8.6 dB gain to a -30 dBFS input while the old model gave -5.7 — 32 dB of compression vs
    // our 18) and self-starves under drive (AC-coupled bias shift). starve = env/(env+knee): ~0 quiet
    // (tails sustain, quiet notes get the full germanium gain) -> 1 loud (deep class-C starve = THD rises
    // with level, matching the real 113->147%). Replaces the old kBiasTrack/kNomLevel fixed-bias blend,
    // whose fixed part starved QUIET notes hardest (backwards THD-vs-level + killed sustain).
    static constexpr double kStarveKnee  = 0.06;
    static constexpr double kStarveCap   = 0.88;   // max fraction of the signal peak the bias may eat — the
                                                   // cycle tip ALWAYS conducts (over-starving passed only the
                                                   // linear leak floor = a CLEAN sine, THD collapsed; 0.91+
                                                   // re-entered that regime at -6 dBFS, 0.88 is the sweet spot)
    static constexpr double kPostGain    = 10.5;   // post-drive on the starved blend (raised — base THD was too low vs the captures)
    // Supply-sag -> bias-starve coupling (the "dying battery" bloom, 2026-07-14). A SLOW envelope of the
    // input deepens the starve while you dig in: louder playing = saggier supply = deeper class-C starve =
    // MORE THD (the real FF measures THD 113->147% rising over -24->-6 dBFS — our old model had it
    // backwards), and on the decay the bias recovers over ~115 ms so the note blooms back open.
    static constexpr double kSagBias   = 0.60;     // starve-threshold multiplier depth at full input sag
    static constexpr double kSagInAtt  = 5.0e-4;   // input-sag attack (~10 ms at the 4x rate)
    static constexpr double kSagInRel  = 4.5e-5;   // input-sag release ~115 ms tau (the bloom)
    static constexpr double kSagInRef  = 0.35;     // input level (peak env) that counts as "digging in"
    // (kBiasTrack/kNomLevel fixed-bias blend REMOVED 2026-07-14 — superseded by kStarveKnee above.)
    // Gate keys on the FUZZ OUTPUT envelope (NOT the input): a fuzz massively sustains, so an input-keyed
    // gate slams shut mid-note while the fuzz is still sounding (measured: 60 dB drop with input still at
    // -27 dB). Output-keying tracks the real sustain — low Gate ≈ transparent, high Gate = a clean tail-cut.
    // GATE = the Tr3 OUTPUT-STAGE BIAS (schematic: the Gate 10k pot biases the output germanium transistor via
    // R3). Modeled circuit-accurately as a class-C DEAD-BAND on the output: low Gate = wide dead-band = gated/
    // sputtery + mutes low-level hiss (Tr3 starved toward cutoff); high Gate = open = smooth/sustaining (lets the
    // oscillation through). LEAKY floor (germanium never fully cuts off) so it sputters instead of hard-chopping
    // — the fix for "cuts out". A sustained fuzz (|y|~1) sails over the dead-band; only quiet tails/silence mute.
    static constexpr double kGateBandMax = 0.20;   // widest dead-band (at Gate = 0)
    static constexpr double kGateFloor   = 0.12;   // leaky conduction inside the dead-band (germanium leakage)
    // STAB = supply voltage (ZVEX "operating-voltage regulator"), calibrated to the real STAB SWEEP: Stab UP →
    // louder + MUCH more saturation (measured THD 89%@S0 → 196%@S10; RMS -28.7 → -22.6 dBFS), harmonics going
    // from a decaying LADDER (S0) to a FLAT broadband spectrum where they EXCEED the fundamental (S10). Modeled
    // as: more supply → harder DRIVE into the core + more OCTAVE (buries the fundamental) + a resonant FEEDBACK
    // that self-OSCILLATES at high Stab (the real pedal is unruly/oscillates cranked) + a headroom/level rise.
    static constexpr double kStabDrive  = 2.6;   // Stab → extra drive into the fuzz core (harder saturation, odds)
    static constexpr double kStabOct    = 0.56;  // Stab → extra octave/rectification (0.46 undershot the mid-Stab captures, 0.60 overshot)
    static constexpr double kHeadLo = 0.95, kHeadHi = 1.85;  // Stab → clip headroom (~6 dB level rise, S0 not too quiet)
    static constexpr double kHeadMin    = 0.12;  // floor on the sagging headroom (RT safety / no /0)
    static constexpr double kSagDepth   = 0.20;  // dynamic supply sag under load (the bloom/squish feel)
    static constexpr double kSagAtt     = 0.012; // sag-envelope attack (per oversampled sample)
    static constexpr double kSagRel     = 4.5e-5;// sag-envelope release ≈ 115 ms tau (the bloom)
    // OSCILLATION — TRUE self-oscillation, GATE-controlled (reworked 2026-07-14; the old note-gated loop
    // could NEVER actually scream: measured 0.0% inharmonic output at every Stab — the loop just locked to
    // the note's harmonics and died <0.2 s after note-end. User: the old whine "wasn't authentic").
    // Authentic behavior (like the real pedal): above kFbOnset the loop gain genuinely exceeds unity and the
    // pedal SCREAMS — including into silence — and you kill it with the GATE knob (schematic: everything,
    // squeal included, passes through the Gate-biased Tr3; FF players tame the squeal with Gate). The
    // resonator taps the POST-gate output, scaled by a Gate-authority smoothstep: Gate below kOscGateLo the
    // squeal can't sustain; above kOscGateHi it's fully live. While a note sounds, the saturated operating
    // point suppresses/injection-locks the loop, so the scream blooms in gaps and tails = unruly/chaotic.
    // Squeal PITCH follows Comp (the real FF's squeal pitch famously rides the bias knobs): kSvfFcLo..Hi.
    static constexpr double kFbOnset = 0.38;     // Stab above this arms the self-oscillation
    static constexpr double kFbStab  = 3.00;     // loop-gain scale at full Stab (linear ramp past the onset; hot
                                                 // enough that at low Comp the loop dominates a sustained note —
                                                 // the real C5/G8 captures oscillate OVER the probe tone)
    // Squeal authority FALLS with Comp — (1-comp)^2 (capture-verified 2026-07-14: the C5/G8 set is the one
    // that truly runs away, THD 252->971->478% over S5..S10 — non-monotonic, classic chaos — while C8/C10
    // stay squeal-free through S7. Physically: a Comp-starved Q1 has no loop gain left to oscillate.)
    static constexpr double kSvfFcLo = 280.0;    // squeal centre at Comp 0 (low growl)
    static constexpr double kSvfFcHi = 1500.0;   // squeal centre at Comp 1 (high whistle)
    static constexpr double kSvfQ    = 0.15;     // resonator damping (small = rings/oscillates)
    static constexpr double kFbClamp = 2.0;      // SOFT-limit ceiling on the feedback node (tanh — organic, won't stick at the rail)
    static constexpr double kOscGateLo = 0.35, kOscGateHi = 0.65;  // Gate knob -> squeal authority (below Lo: squeal muted)
    static constexpr double kGeBias  = 0.05;     // germanium bias asymmetry (a "sloppy" equilibrium → organic onset + even-harmonic warmth)
    // EQ (the harness fits these against the captures)
    static constexpr double kInHPfc    = 30.0;
    static constexpr double kInterHPfc = 120.0;  // kills DC/motorboat mud between the stages
    static constexpr double kOutHPfc   = 60.0;   // raised 24->60 (2026-07-14): model measured +3 dB fat at 50-200 Hz vs capture
    static constexpr double kOutLPfc   = 9500.0; // raised 7500->9500: model was -4 dB dark at 8 kHz vs capture (bright raspy fuzz)
    // Output
    static constexpr double kMakeup    = 0.131;  // output trim (balanced across the 3 capture Comp levels)
    // Comp raises the real pedal's output level (bias starve shifts the operating point UP the load line):
    // captures at C8/C10 measured +9..+12 dB louder than C5 at the same Volume. dB makeup ramps above 0.50.
    static constexpr double kCompLvlDb = 12.0;   // dB of extra makeup at Comp = 1 (0 below kCompLvlLo)
    static constexpr double kCompLvlLo = 0.50;   // Comp where the level ramp starts

    struct ChannelState {
        BiquadFilter inHP, interHP, outHP, outLP, octHP, octLP;
        float  env  = 0.0f;                // input envelope (Comp starve-bias tracking)
        double sag  = 0.0;                 // supply SAG envelope (slow, output-keyed) → the Stab headroom bloom
        double sagIn = 0.0;                // INPUT sag envelope (slow) → bias-starve coupling (THD rises with level)
        double svfLP = 0.0, svfBP = 0.0;   // Chamberlin SVF resonator (the oscillation band)
        double fbState = 0.0;              // feedback injected next sample (the self-oscillation)
    };
    std::array<ChannelState, kMaxCh> ch_;

    double svfF_ = 0.0;        // SVF tuning coefficient (2·sin(π·fc/fs)); follows Comp (squeal pitch)
    double compMakeup_ = 1.0;  // Comp-dependent output makeup (linear, from kCompLvlDb)
    float  svfComp_ = -1.0f;   // comp value the cached terms were computed for (recompute on change)
    void   updateComp() noexcept;
    static inline double lerp(double a, double b, double t) noexcept { return a + (b - a) * t; }
    static inline double clampd(double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
};
