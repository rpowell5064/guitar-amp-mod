#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// JCM800ComponentModel — component-exact Marshall JCM800 2203 (100 W, EL34)
// (component-amp programme, 2026-09-10; the shipped JCM800Model is untouched)
//
// Every value below is read off the Marshall factory drawings:
//   * "2203 STD / Preamp", Jim Marshall Products Ltd, Iss. 4, 19-5-88
//     (file 2203PRE.DGM) — component values
//   * "2203 STD / Output Stage & PSU", Iss. 6, 7-12-88 — PI, NFB, output stage
//   * "JCM 800 LEAD SERIES 50W & 100W PREAMP CIRCUIT DIAGRAM", 24-4-81 —
//     its 17-node DC voltage table supplies the operating points this model is
//     gated against (100 W / MV column).
//
// Signal path (single channel — the 2203 has one):
//
//   J1 High → R3 68k grid stop (R2 1M leak)
//     → V1a  (R4 100k plate, R1 2k7 ∥ C1 0.68µ cathode, C2 100p plate-cathode)
//     → C3 .022µ → PREAMP VOLUME VR1 1M log
//                  (R5 470k ∥ C4 470p bright feed, C5 1n0 wiper to ground)
//     → V1b  (R7 100k plate, R6 10k cathode UNBYPASSED — the 2203 cold clipper)
//     → C7 .022µ → R10 470k ∥ C8 470p
//     → V2a  (R12 100k plate, R9 820Ω cathode)
//     → V2b  cathode follower (R13 100k)
//     → tone stack (R15 33k slope, C10 470p, VR3 220k lin treble,
//                   VR5 1M log bass, VR4 22k lin mid, C11/C12 .022µ)
//     → MASTER VR2 1M log
//     → V3 ECC83 long-tail PI (R18 82k / R21 100k plates, R27 15k tail)
//     → 4× EL34 (R31-34 5k6 grid stops, R35-38 1k 5W screens, R24/R25 220k
//       bias feeds) → output transformer
//     ← global NFB: R22 100k from the secondary into the R23 4k7 leg, with
//       VR6 22k presence + C17 .1µ shunting it
//
// Knob map: gain = VR1 preamp volume, master = VR2, bass/mid/treble = the
// stack, presence = the NFB network. The 2203 has no resonance/depth control
// and no channel switch, so those parameters are inert here.
//
// DC gate (1981 table, 100 W MV column) — see jcm800_component_verify:
//   V1a Vk 1.75 V / Vp 210 V · V1b Vk 2.6 V / Vp 250 V
//   V2a Vk 1.0 V  / Vp 167 V · V2b(CF) Vk 165 V · rails 280 V / 290 V
// ─────────────────────────────────────────────────────────────────────────────
class JCM800ComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34
    const char* modelName()           const noexcept override { return "Marshall JCM800 Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.5f, sag_ = 0.3f;

    // Level calibration (the only free parameters).
    // 2026-09-10 gain-law pass: the reference rig runs the front end hotter than the
    // 0.35 V/unit inherited from the EVH calibration; 0.70 V improves every take
    // (g25/noon/gmax 16.1/26.2/26.1 -> 14.7/18.1/19.8 % at master 0.7), where the
    // VR1 pot law only helped below noon.
    // 2026-10-07 (evening): KNEE-ANCHORED like the Friedman twin. 0.70 came from THD at -24..-6 dBFS,
    // rows that sit in saturation at any drive within 20 dB, so the fit never saw where clipping
    // starts: the twin crossed 30 % THD at 1 kHz 14 dB later than the shipped model at every gain
    // (10 / 5 / 2.5) and 30 dB later than the real amp's capture at preamp volume 10. At the
    // captures' own scale (0 dBFS = +19 dBu, 9.76 V/unit) the stage chain lands the real knee
    // within 1 dB, so the chain is right; 2.8 V/unit is the DEVICE full scale (0 dBFS = +6 dBu),
    // the same number the Friedman twin settled on from its own ear anchor, and puts the twin's
    // knee within 2 dB of the shipped model's at every gain. Known shape residuals left open: a
    // soft ~27 % THD shelf 12 dB below the knee (the real amp reads 6-8 % there) and 110 Hz THD
    // about twice the real amp's at saturation. Lab: --refgain -13 feeds the capture at its scale.
    float inVolts_    = 2.8f;
    float outScalePa_ = 0.0048f;
    bool  dynLoad_ = false;   // Phase 5 (2026-09-21): dynamic speaker load in the power section (lab toggle)

    float gainMid_ = 0.25f;   // VR1 1M log: fraction at half rotation (fit0 in the lab harness)
    evhcomp::KorenP v1bTube_{ 100.0, 1.4, 1060.0, 600.0, 300.0 };   // lab (fit4/fit5): cold-clipper tube set; used only when v1bTubeOn_
    bool v1bTubeOn_ = false;
    int  probeTap_ = -1;
    // SIR #34 (2026-10-07): the S.I.R. rental-fleet hot-rod of the 2203, built from the circulated
    // spec sheet for the PCB amps (ten steps, drawing designators), not from the old model's sketch:
    //   A remove C2 (100p plate-cathode snub)          B C3 .022u -> 500p (bass out ahead of the clipper)
    //   C R3 68k -> 33k + the "hot shield" (input cable braid on V1a's plate = ~33 pF plate-grid)
    //   D C4 bright feed 470p -> 2200p                 E remove C5 1n0 (wiper to ground; not modelled)
    //   F .1u across R6 10k (cold clipper bypass)      G .47u across R9 820R (V2a bypass)
    //   H C17 presence cap .1u -> .47u                 I/J preamp filter caps 16+16 / 32+32 uF (static rails here: not modelled)
    // Everything is the drawn part at the drawn node; the one ESTIMATE is the shield capacitance.
    bool  sir34_ = false;
    float sirShieldPf_ = 33.0f;   // fit6
    // 2026-10-07: ON. The follower's previous-sample grid clamp (see CFStageV::Params) rectified the
    // signal at tiny levels: with every stage ahead of it clean, the twin read 16-27 % THD at 1 kHz from
    // -72 dBFS up (the real amp 0.8-8 %), a crunch on every quiet note and decay. The joint solve takes
    // that to 1.3 / 15 % at -72 / -60 (real 0.8 / 5.8). Cost: 50-125 Hz read 1.1-1.5 dB lower (the
    // follower's bootstrapped input loads V2a's plate at LF), accepted.
    bool cfJoint_ = true;   // lab (fit19): V2b follower joint grid-conduction solve   // lab (fit14): return one tap instead of the output
    // Output-transformer low-resonance depth (dB) at ~110 Hz. Sets the low-mid weight;
    // the printed value humped the low-mids, so it is a tuning lever. fit2 in the lab harness.
    // zResDb REVISED 2026-09-26, AFTER the OT saturation stage was corrected. The two belong
    // together: the old stage soft-limited the low band at a fixed few volts, so a large LF
    // resonance boost was needed to claw the lows back. With saturation modelled properly in
    // the flux domain that boost is excessive, and far less of it is right.
    // (derivation kept out of the public tree)
    float zResDb_  = 2.0f;
    // HF loop terms (fit20/21/22). 2026-10-06, re-fitted over the seven captures once the power stage's feedback loop
    // was closed without its sample delay and the phase inverter solved as a pair: the HF loss the loop needs now sits
    // in the OT corner (22 k -> 12 k) and a smaller reflected-load HF rise (8 -> 5.3 dB); the stability lag is off
    // (200 k). Mean shape 1.75 -> 1.27 dB, worst 2.73 -> 1.89.
    double otHfHz_ = 12e3, zHfDb_ = 5.33, nfbStabHz_ = 200e3;
    bool   paLegacy_ = false;   // lab A/B ("palegacy"): the rev-189 power stage with its 22 k / 8 dB / 20 k loop terms
    // 2026-10-07: MEASURED NOT the 110 Hz over-clipper (rows identical at 56.6 / 85 / off with the
    // Dynamic Load kept on); that excess is the dynamic load's speaker-impedance peak. Kept at the
    // rated-output anchor.
    double fluxSatV_ = 56.6;   // fit3: OT core saturation, peak volts at 40 Hz (100 W into 16 ohms)
    float  paDrive_ = 0.30f;       // fit40 (2026-10-05): drive scale into the power stage. CALIBRATED: master .6 kept 9.0/7.7/5.2 dB of the stack's 13.5/10.8/8.3 (80/125/200 Hz) at x1.0; 12.2/9.6/7.1 at x0.3. The twins were driving their
                                   //       power section harder than the real amp at the same MASTER (the stack's bass
                                   //       travel was being compressed away at master .6); calibrated per twin by the
                                   //       bass-travel criterion (lab reports/bass-ab-2026-10-05/pa-drive-*.txt).

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChState {
        evhcomp::CCStageV   v1a;
        evhcomp::ShelfV     v1aSnub;    // C2 100p across the plate-cathode
        evhcomp::RCDividerV coup3;      // C3 .022µ into the volume network
        evhcomp::ShelfV     brightVR1;  // R5 470k ∥ C4 470p feed + C5 1n0
        evhcomp::CCStageV   v1b;        // 10k unbypassed cold clipper
        evhcomp::RCDividerV coup7;      // C7 .022µ
        evhcomp::ShelfV     r10c8;      // R10 470k ∥ C8 470p
        evhcomp::CCStageV   v2a;
        float               cfDiv = 1.0f;
        evhcomp::CFStageV   v2b;        // R13 100k cathode follower
        YehSmithToneStack   ts;
        evhcomp::PushPullPowerV pa;

        static constexpr int kNTaps = 8;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void recalcPots() noexcept;

    // Rails from the 1981 DC table (100 W MV): V1 pair at 280 V, V2 at 290 V.
    static constexpr double kRailV1 = 280.0;
    static constexpr double kRailV2 = 290.0;
    static constexpr double kRp     = 62.5e3;   // 12AX7 plate resistance (tube physics)
    // R15 33k is the stack's own slope resistor; the cathode follower's output
    // impedance is low enough that no extra series term is folded in.
    static constexpr double kZthStack = 1.5e3;
};
