#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <cmath>
#include <string>

// ── Dallas Rangemaster Treble Booster, 1965 (parody-safe: "Treble Ranger") ────
//
// The original boost-into-a-cranked-amp — Tony Iommi (every Sabbath record to
// 1979), Rory Gallagher, Marc Bolan, Brian May's early years. (The Clapton
// "Beano" attribution is a 1990s internet claim that has never been confirmed,
// so it is deliberately not repeated here.) Dallas built it because British
// amps were dark: the more you turned a Vox/Selmer/Marshall up, the more the
// low-mids took over.
//
// It is not a distortion pedal. It is ONE germanium PNP common-emitter stage
// with a deliberately tiny 5 nF input coupling cap, so only treble reaches the
// transistor. Unity gain lands at ~80 Hz — the open low E — and everything
// above it is progressively boosted, ~+30 dB by 5 kHz. The amp then distorts on
// treble content instead of on low-frequency energy, which is why a dimed amp
// stays articulate instead of turning to mud. That ~28 dB tilt IS the pedal.
//
// Circuit (positive ground, −9 V rail, Mullard OC44):
//   C1 5 nF input · R1 470k / R2 68k base divider · R3 3.9k emitter, bypassed
//   by C2 47 µF · the 10k "Set" pot IS the collector load · C3 10 nF output.
//
// ── What the measurements say, and what that forces the model to do ──────────
//
// 1. BIAS NEAR CUTOFF. The stock network puts the collector at −6.7 to −7.1 V
//    of −9 V (R.G. Keen measured this on originals; it also falls straight out
//    of a solve with measured OC44 parameters). That is NOT mid-supply: 6.7 V
//    of headroom toward saturation but only 2.3 V toward cutoff. Mullard's own
//    manual warns against running these parts below 0.3 mA "where the non-
//    linearity produces excessive distortion" — the Rangemaster sits at 0.2 mA,
//    deliberately below that floor. The cutoff half therefore soft-limits first
//    while the other half keeps swinging, which is where the asymmetric,
//    2nd-harmonic-dominant grind and the touch compression come from. Bias it
//    at mid-supply instead and you get a clean boost with none of the character.
//
// 2. THE PICKUP LOADING IS THE VOICE. From a low-impedance bench source the
//    pedal is a plain first-order high-pass shelf: corner ~3 kHz, plateau
//    ~+30 dB, flat to 10-12 kHz. From a REAL GUITAR it is a band-pass peaking
//    near 1-1.5 kHz, because a pickup's inductive source impedance climbs with
//    frequency (a 4 H humbucker is 26k at 1 kHz but 120k at 5 kHz) and divides
//    against this pedal's ~10 kΩ input. Feed the bench transfer function a
//    buffered DI — exactly what this plugin gets — and the result is shrill and
//    wrong, so the loading is modeled explicitly here.
//
// 3. IT IS NOT MEMORYLESS. Rectified base current walks the operating point
//    under drive, through two very different time constants: C1 into the bias
//    divider (5 nF × 59.4k ≈ 300 µs) gives a fast transient shift, and the
//    emitter bypass (47 µF × 3.9k ≈ 183 ms) gives slow sag and bloom. Both push
//    the stage AWAY from cutoff as you dig in. A static waveshaper reproduces
//    neither, and that difference is most of what separates "germanium boost"
//    from "a tilt EQ into a clipper".
//
// The stage is solved as Ebers-Moll collector current against its load line by
// Newton-Raphson, in a supply-normalised domain, using the OC44 parameters
// measured by Holmes/Holters/van Walstijn (DAFx-17). Clipping is the collector
// reaching its saturation and cutoff rails — not a tanh.
//
// Controls map to circuit parameters, not post-EQ:
//   "drive" [0,1] → how hard the germanium is pushed. FLOORED: the real unit
//                   has no gain control at all, only an output pot.
//   "tone"  [0,1] → the INPUT CAP — the one mod every owner does. 5 nF treble
//                   (0) → 22 nF mid (0.5) → 100 nF full-range (1). Keen's rule
//                   of thumb: past ~22 nF it stops being a treble booster.
//   "level" [0,1] → the "Set" output pot.
//   "tbload"[0,1] → pickup-loading depth (1 = guitar, 0 = bench/low-Z source).
//                   Defaults ON; the verification harness turns it off to
//                   compare directly against Keen's bench measurement.
class TrebleBooster final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Treble Ranger"; }
    int recommendedTubeType() const noexcept override { return 2; }   // EL84 — its natural partner

private:
    // ── Circuit constants ────────────────────────────────────────────────────
    static constexpr double kVsupply   = 9.0;       // −9 V rail
    // Input coupling cap: stock treble part → the "full range" mod.
    static constexpr double kCapTreble = 5.0e-9;
    static constexpr double kCapFull   = 100.0e-9;
    // Input impedance: 470k ∥ 68k ∥ rpi. Computed 9.8-10.6 kΩ, Keen measured
    // ~12 kΩ, and it is flat across the audio band. With 5 nF this puts the
    // corner at 2.65 kHz. (Computation alone favours 10 kΩ/3.2 kHz; Keen's
    // measured CURVE is what we fit, so use his measured impedance.)
    static constexpr double kZin       = 12000.0;
    // Quiescent collector: −6.71 V of −9 V. NEAR CUTOFF, not mid-supply.
    static constexpr double kQc        = 0.746;
    // Ebers-Moll, normalised (Is·Rc/Vsupply and Vt/Vsupply) from the measured
    // OC44: Is = 2.03 µA, Vt = 25.5 mV. These give Vbe ≈ 0.11 V at the 0.23 mA
    // operating point, matching the 0.128 V measured on the real device — the
    // widely repeated "germanium Vbe = 0.3 V" is wrong at this current and puts
    // the bias in the wrong place.
    static constexpr double kIsN       = 0.00226;
    static constexpr double kVtN       = 0.00283;
    // Collector-emitter leakage (Iceo ~90 µA against 0.9 mA full scale). In
    // germanium this is a bias-setting current, not an error term.
    static constexpr double kLeak      = 0.10;
    // Emitter degeneration left after the 47 µF bypass: bulk Re + rbb'/beta.
    // (The 47 µF is not a tone control — its pole sits at 29 Hz, and swapping it
    // for 22 µF moves the response by 0.1 dB above 80 Hz.)
    static constexpr double kRe        = 0.003;
    // Base-current sag. The base is fed THROUGH the 5 nF coupling cap, whose
    // reactance is ~32 kΩ at 1 kHz — a large series source impedance. As the
    // stage conducts, Ib = Ic/beta drawn through that makes the base voltage
    // sag back, which is what stops the conducting half reaching the saturation
    // rail. Xc/(beta*Rc) at 1 kHz with beta = 108.
    //
    // This term decides the whole high-drive character. Too small and the stage
    // rails BOTH ways, the waveform squares off and goes H3-dominant. Correct,
    // and only the CUTOFF half pins while the conducting half stays clean —
    // i.e. it approaches half-wave rectification, which is exactly what the
    // measured circuit does (H2 -6 dB with H3 down at -33 dB at 1 V). It is
    // also the mechanism behind germanium "blocking" on hard transients.
    static constexpr double kRbase     = 0.015;
    // Base drive span, centred on the real circuit's unity (the pedal has no
    // gain knob; this is "how hard you hit it").
    static constexpr double kDriveFloor = 0.55;
    static constexpr double kDriveMax   = 5.20;
    // Output loading: the plateau is +36-38 dB open-circuit but ~+30 dB measured
    // at the jack into a real load. Model the loaded figure.
    static constexpr double kMakeup    = 0.67;
    // SATURATION is not a wall. A real collector approaches Vce(sat) gradually,
    // so the saturating half compresses progressively instead of squaring off.
    // With a hard clamp the waveform becomes a symmetric square at high drive
    // and goes H3-dominant; the measured circuit stays H2-dominant (H2 -6 dB,
    // H3 -33 dB) even at 1 V, because one half soft-limits while the other
    // asymptotes into cutoff. kSatKnee is the softness, in supply units.
    static constexpr double kVceSat    = 0.02;    // ~0.18 V
    static constexpr double kSatKnee   = 0.16;
    // Dynamic bias: base side fast (C1 × 59.4k ≈ 300 µs), emitter side slow
    // (47 µF × 3.9k ≈ 183 ms). Both move the stage away from cutoff under drive
    // — the measured collector walks ~+290 mV at a 1 V input.
    static constexpr double kTauFast   = 0.000297;
    static constexpr double kTauSlow   = 0.183;
    static constexpr double kBiasFast  = 0.0020;
    static constexpr double kBiasSlow  = 0.0015;
    // Stage bandwidth. The OC44 is an RF part (ft 15 MHz): the shelf stays flat
    // to 10-12 kHz and only then droops. An OC71/75 build (ft 0.3 MHz) would sit
    // 3-5 dB darker up top — the real reason those units read "thicker".
    static constexpr double kStageLPfc = 40000.0;
    // Output coupling sees the pot AND the amp's ~1 M input, so its corner is
    // well below the guitar band: the INPUT cap is the only 6 dB/oct shaper in
    // the audio range (stacking a second one gave 12 dB/oct, twice the measured
    // slope).
    static constexpr double kOutHPfc   = 25.0;
    // Pickup loading (humbucker): the 10 kΩ input against an inductive source.
    // This is the difference between the 1 M-loaded DI this plugin receives and
    // what the pedal actually sees, and it is what moves the peak from ~10 kHz
    // down to ~1.5 kHz.
    static constexpr double kLoadFc    = 1200.0;
    static constexpr double kLoadQ     = 0.70;

    double fs_ = 0.0;

    float drive_ = 0.5f, tone_ = 0.0f, level_ = 0.6f, load_ = 1.0f;
    float toneApplied_ = -1.0f, loadApplied_ = -1.0f;
    LinearSmoother driveS_, levelS_;
    float driveCur_ = 0.5f, levelCur_ = 0.6f;
    double aFast_ = 0.0, aSlow_ = 0.0;

    struct Ch {
        BiquadFilter inLoad;    // pickup loading against the low input Z
        BiquadFilter inHP;      // the input coupling cap — THE tone control
        BiquadFilter stageLP;   // transistor bandwidth
        BiquadFilter outHP;     // output coupling
        BiquadFilter dcBlk;     // the asymmetric clip leaves DC
        double warm  = 0.254;   // Newton-Raphson warm start (quiescent conduction)
        double envF  = 0.0;     // fast (base-side) bias envelope
        double envS  = 0.0;     // slow (emitter-side) bias envelope
    };
    std::array<Ch, kMaxCh> ch_;

    static double geStage(double vbase, double& warm) noexcept;
    void recalc() noexcept;
};
