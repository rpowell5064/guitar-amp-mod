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
    // Drive taper: loop gain into the fuzz core (raised — the FF is a HIGH-gain fuzz, was too tame)
    static constexpr double kGainMin = 3.0, kGainMax = 48.0;
    // Octave: full-wave rectification of the (shape-retaining) soft fuzz → EVEN harmonics (h2 octave),
    // blended with the fuzz for body/odds; then a post-drive sharpens the blend for THD. Comp = octave amount.
    static constexpr double kOctFc     = 50.0;   // DC-block after the rectifier
    static constexpr double kOctMixMin = 0.18, kOctMixMax = 0.55; // Comp -> even-octave (raised: base THD was too low vs captures — more octave buries the fundamental)
    // Comp-driven STARVE (class-C): at high Comp the FF's Q1 bias collapses so the stage conducts
    // only a NARROW slice of the cycle → the fundamental starves and h3/h5/THD blow up past a square's
    // ceiling (measured NAM h3 64% > 33% square-max → only possible with a starved fundamental).
    static constexpr double kStarveMax   = 0.90;   // Comp -> bias offset (narrows the conduction window; raised for more base THD)
    static constexpr double kStarveFloor = 0.06;   // soft floor below the bias (keeps body, limits alias)
    static constexpr double kPostGain    = 10.5;   // post-drive on the starved blend (raised — base THD was too low vs the captures)
    // Bias is a BLEND of a FIXED offset and an envelope-tracked one. Fully-tracked (=1) was level-independent
    // which INVERTED the pedal's dynamics (real FF distorts MORE when louder: THD 113→123→147% over -24→-6 dBFS).
    // A mostly-fixed bias (kBiasTrack low) lets a louder signal cross the bias for more of the cycle → more THD.
    static constexpr double kBiasTrack   = 0.60;   // 0 = fixed bias, 1 = fully envelope-tracked
    static constexpr double kNomLevel    = 0.08;   // fixed bias floor (LOWERED — 0.16 starved quiet notes → "cut out")
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
    static constexpr double kStabOct    = 0.46;  // Stab → extra octave/rectification (suppresses the fundamental → THD ↑)
    static constexpr double kHeadLo = 0.95, kHeadHi = 1.85;  // Stab → clip headroom (~6 dB level rise, S0 not too quiet)
    static constexpr double kHeadMin    = 0.12;  // floor on the sagging headroom (RT safety / no /0)
    static constexpr double kSagDepth   = 0.20;  // dynamic supply sag under load (the bloom/squish feel)
    static constexpr double kSagAtt     = 0.012; // sag-envelope attack (per oversampled sample)
    static constexpr double kSagRel     = 4.5e-5;// sag-envelope release ≈ 115 ms tau (the bloom)
    // OSCILLATION (the FF's unruly self-oscillation at high Stab). A resonant bandpass of the output is fed BACK
    // into the fuzz input; above kFbOnset the loop gain exceeds unity and it SCREAMS. GATED by note-presence (a
    // FIXED input floor) so it only oscillates while you're playing + dies on silence (no standalone whine — the
    // earlier complaint). The note rides in the same loop → the oscillation INTERMODULATES with it = unruly/chaotic.
    static constexpr double kFbOnset = 0.28;     // Stab above this starts to self-oscillate (earlier = gradual ramp, not a top-end explosion)
    static constexpr double kFbStab  = 0.62;     // feedback loop-gain scale past the onset (drives the ramp; gentle enough to tame the C10/S10 blow-up)
    static constexpr double kSvfFc   = 360.0;    // oscillation band centre (a growl/squeal, not a piercing whine)
    static constexpr double kSvfQ    = 0.15;     // resonator damping (small = rings/oscillates)
    static constexpr double kFbClamp = 2.0;      // SOFT-limit ceiling on the feedback node (tanh — organic, won't stick at the rail)
    static constexpr double kNoteFloor = 0.004;  // input floor: the oscillation lives only while a note is present
    static constexpr double kGeBias  = 0.05;     // germanium bias asymmetry (a "sloppy" equilibrium → organic onset + even-harmonic warmth)
    // EQ (the harness fits these against the captures)
    static constexpr double kInHPfc    = 30.0;
    static constexpr double kInterHPfc = 120.0;  // kills DC/motorboat mud between the stages
    static constexpr double kOutHPfc   = 24.0;
    static constexpr double kOutLPfc   = 7500.0; // FF is a BRIGHT/raspy fuzz — keep the square's harmonics (89% THD)
    // Output
    static constexpr double kMakeup    = 0.135;  // output trim (harness levels this) — FF captures are quiet (Vol 4)

    struct ChannelState {
        BiquadFilter inHP, interHP, outHP, outLP, octHP;
        float  env  = 0.0f;                // input envelope (Comp starve-bias + oscillation note-floor)
        double sag  = 0.0;                 // supply SAG envelope (slow) → the Stab bloom
        double svfLP = 0.0, svfBP = 0.0;   // Chamberlin SVF resonator (the oscillation band)
        double fbState = 0.0;              // feedback injected next sample (the self-oscillation)
    };
    std::array<ChannelState, kMaxCh> ch_;

    double svfF_ = 0.0;   // SVF tuning coefficient (2·sin(π·fc/fs)), set in prepare()
    static inline double lerp(double a, double b, double t) noexcept { return a + (b - a) * t; }
    static inline double clampd(double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
};
