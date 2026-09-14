#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "LinNetV.h"
#include "DnrRolloff.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// OrangeAD200BModel — "Citrus 200", the suite's second BASS amp: a schematic-exact
// Orange AD200B Mark III (200 W, 4x 6550, all valve, one channel).
// (2026-09-13; built straight as a component model — there is no fitted twin, so
// the Component Model toggle does not apply to it and its own power section is
// always in circuit, as with the Sunn.)
//
// Source: the AD200B Mark III preamp and power-amp drawings (Bernard d'Uur,
// 16-01-2010) — every resistor, capacitor and pot value below is printed there,
// including each pot's law. No DC test points are printed, so the rails are
// ESTIMATE-class and the gate is self-consistency (ad200b_verify).
//
// PREAMP:
//   ACTIVE jack -> R1 68k / PASSIVE jack -> R2 68k (R3 1M leak), mixed into
//   -> RO1 ECC83 (R5 100k plate from V1; R4 1k5 || C1 22u — fully bypassed)
//   -> C2 22n -> R6 220k -> R7 470k to ground -> GAIN P1 500k LOG
//   -> RO2 ECC83 (R8 100k plate; R9 1k5 || C3 100u)
//   -> the tone stack, an FMV/Marshall-family network solved as drawn:
//        plate -> C4 220p -> TREBLE P2 250k LIN (top)
//        plate -> R10 82k -> node Y
//        Y -> C5 47n -> node X = P2 bottom = BASS P4 500k LOG (top)
//        Y -> C6 22n -> node Z = P4 bottom = MIDDLE P5 25k LIN (top)
//        P5 bottom -> ground; P4 and P5 are wired as rheostats (wiper to one end)
//        P2 wiper = the stack output
//   -> MASTER P3 1M LIN -> the phase inverter.
// POWER AMP:
//   C14 22n -> RO7/RO8 ECC83 LONG-TAIL PAIR (R38 82k and R39 100k plate loads
//     either side of the V2 rail; cathodes joined through R34 470R to the tail
//     node, which returns through R36 10k to the feedback node; R33/R35 1M hold
//     the two grids at that node). GLOBAL FEEDBACK: the 4/8 ohm tap -> R51 2k7
//     -> R40 1k -> the R37 100R node -> C15 100n -> RO8's grid.
//   -> RO9/RO10 ECC81 cathode followers (R41 100k / R42 82k loads, as drawn —
//      the two values differ on the sheet) -> C16/C17 100n -> R45/R46 1k5 grid
//      stoppers, bias fed through R43/R44 150k
//   -> 4x 6550 (screens through R47/R48 1k from V3) -> OT -> 4 / 8 ohm.
//
// Knob map: gain = P1, bass/mid/treble = P4/P5/P2, master = P3. The amp has NO
// presence control, so the plugin's presence knob is inert here (and hidden by
// the amp-panel rule). Knobs are REAL pot rotations with their printed laws.
//
// The LTP, the global loop, the 6550 quad, the OT and the speaker load are the
// toolkit's PushPullPowerV, configured with this amp's own printed values.
//
// ESTIMATE-class simplification, flagged for a phase 2: the ECC81 follower pair
// is omitted. They are unity-gain buffers into 100k/82k with the output grids'
// 1k5 stoppers behind them, so the output grids see the LTP plates directly —
// the same signal to within their (small) loss and their own overdrive
// behaviour at full power.
// ─────────────────────────────────────────────────────────────────────────────
class OrangeAD200BModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 5; }   // 6550
    const char* modelName()           const noexcept override { return "Citrus 200"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.5f, sag_ = 0.3f;

    // Level calibration.
    float inVolts_    = 1.00f;
    // Volts at the 8 ohm tap -> plugin units. The amp's rated 200 W (39.4 V rms,
    // ad200b_verify) lands at 0.69 = -3.2 dBFS, so a dimed amp sits just under
    // full scale with headroom for the cab.
    float outScalePa_ = 0.0175f;
    // ESTIMATE-class constants (lab hooks fit0..).
    float  gainMid_   = 0.15f;   // fit0: P1 500k LOG law
    float  bassMid_   = 0.15f;   // fit1: P4 500k LOG law (treble/mid/master are LIN — printed)
    double railV1_ = 300.0, railV2_ = 380.0, railA_ = 600.0, railScreen_ = 320.0;  // fit2..fit5 (no TPs printed)
    double otHfHz_ = 55e3, zHfDb_ = 0.0, zResDb_ = 8.0, zResHz_ = 60.0;  // fit6..fit9
    double idleMa_ = 35.0, raa_ = 1700.0;        // fit10 / fit11 (4x 6550 at 200 W)
    double nfbStabHz_ = 60e3, fluxLim_ = 12.0;   // fit12 / fit13
    double kneeV_ = 0.15;                        // fit14
    int    probeTap_ = -1;                       // fit15 (lab)
    int    lutPoints_ = 1024;                    // fit18 (lab): output-tube LUT resolution
    // fit16: the toolkit's transconductance scale. The SVT's 1.4 was fitted to that
    // amp's PRINTED bias point; the Mark III prints none, so this stays at the
    // toolkit's datasheet-default 3.0 — which is also the value that lands the amp's
    // own rated 200 W into 8 ohms (203 W measured, ad200b_power).
    double iaScale_ = 3.0;                       // fit16
    double nfbScale_ = 1.0;                      // fit17 (lab: 0 = open loop)

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChState {
        evhcomp::CCStageV   ro1;
        evhcomp::RCDividerV coup2;      // C2 22n -> R6 220k / R7 470k + P1
        BiquadFilter        ro2LP;      // the P1 wiper impedance against RO2's Miller C
        evhcomp::CCStageV   ro2;
        evhcomp::LinNetV    stack;      // the FMV network, solved as drawn
        evhcomp::RCDividerV coup14;     // C14 22n into the LTP grid
        evhcomp::PushPullPowerV pa;
        DnrRolloff          dnr;

        static constexpr int kNTaps = 6;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void buildStages() noexcept;
    void recalcPots() noexcept;
    void buildStack(ChState& c) noexcept;

    static constexpr double kRp = 62.5e3;
    static constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12;
    double millerC(double Ra) const noexcept { return kCgk + kCgp * (1.0 + 100.0 * Ra / (Ra + kRp)); }
};
