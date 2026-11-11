#pragma once
#include "GasName.h"
#include "AmpModelBase.h"
#include "SsDkNetwork.h"
#include "SpeakerModel.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// PeaveyBackstageComponentModel — component twin of the Peavey Backstage Plus
// (solid-state 35 W combo, 1983-85), built from Peavey's service schematic
// "BACKSTAGE PLUS", BD. # 96030260, drawn 11/1/83 (P/N 81501156). The shipped
// PeaveyBackstageModel is untouched and stays the Component-OFF path.
//
// Signal path, every element as drawn (1N4148 diodes, 4558 op-amps, ±15 V):
//
//   INPUT → R1 4k7 / C2 100p → C1 .1µ, R2 220k → U1B (+)
//   U1B "Saturation" stage (non-inverting):
//     out → C3 100p → (−);  (−) → R5 3k3 → node P;  P → C4 .0068µ → out
//     SATURATION R7A 10k: track M ... out, wiper = P (CW end = M)
//     P → R6 1k5 → M;  M → C6 .47µ → R10 470 → K;  K → footswitch to ground
//     (−) → C5 2.2µ → Q;  Q → R8 470 → PRE R12 10k (wiper grounded) ;  Q → R9 3k3 →
//       C8 .033µ → S (R13 100k to ground, PULL BRIGHT S1 shorts S to ground)
//     out → R3 1k5 → A (R4 470 to ground);  CR3/CR4 antiparallel between A and Q
//       — the soft limit: past ~0.6 V at A the diodes feed the output back into
//       the gain leg, holding the stage near ±2.5 V
//     out → C7 22µ → SATURATION R7B 1k (wiper W, CW end = J) → J;  CR5/CR6
//       antiparallel K–J — with the footswitch closed, the hard clip
//     W → R11 2k7 → node T, which the PRE pot's other half (T to ground) loads:
//       PRE raises the stage gain and the level at once (0 = mute)
//   TONE STACK (T): C10 270p → HIGH 250k top; R15 47k → U; U → C11 .033µ → HIGH
//     bottom → R17 10k → LOW 250k audio rheostat; U → C12 .1µ → V; V → C13
//     .015µ and MID 50k rheostat to ground; PULL THICK S2 adds C9 .015µ (R14 220k
//     bleed) across the 270p; HIGH wiper → C14 .1µ → U1A (+) (R20 470k)
//   U1A: ×(1 + 47k/6k8) (C15 100p, C16 2.2µ) → C17 2.2µ → R23 1k → CR7/CR8 to
//     ground (the second hard clip) → POST R24 10k → R25 22k → U2A inverting
//     (R29 56k ∥ C18 100p) → PRE OUT / PWR IN (R30, R31 1k) → R41 3k3 → C28 .1µ,
//     R42 47k → U2B power amp
//   POWER AMP: U2B drives the complementary output stage (±39 V rails); feedback
//     R60 47k ∥ C39 39p to the (−) input, C40 22µ → R61 6k8 to ground, and R62 1k5
//     from the 0.33 Ω speaker-return sense resistor R64: current feedback — the
//     amp's output impedance is ~10 Ω, so the speaker's own impedance curve
//     shapes the response (resonance and coil rise), as in a valve amp.
//     R59 1k5 + CR24/CR25 (back-to-back zeners) from the output to the (−) input:
//     the clip limiter. Zobel R63 22 Ω + C41 .1µ. Load: the combo's speaker.
//
// Knob map (the shipped model's ids, presets map 1:1): gain = SATURATION (the
// dual pot), master = POST, bass/mid/treble = LOW/MID/HIGH. New ids: pre = PRE
// gain, bright = PULL BRIGHT, thick = PULL THICK, satsw = the saturation
// footswitch (1 = saturation in; with no footswitch plugged the jack's contacts
// ground K — ESTIMATE from the drawing's jack symbol). presence and sag are
// inert (the amp has neither; the supply is a stiff ±39 V with the limiter well
// inside it). The valve twins' extras ids are accepted and ignored — there are no
// valves — except cabload/cabrow: with ~10 Ω output impedance the load really
// changes the response, so the chosen cab's speaker is the load.
//
// ESTIMATE: the zener voltage (the part number is printed, not the voltage; 22 V
// puts the limiter knee at the rated 16.5 Vrms into 8 Ω), the LOW pot's audio law
// (15 % at mid-rotation), the speaker (a 10" Peavey driver, open back: Thiele–
// Small class values), the 4558's swing (±13.5 V on ±15 V rails: U1B and U1A
// clip there, as a hot pickup with PRE and HIGH up drives them; U2A and the
// power amp stay inside theirs, CR7/CR8 and the limiter see to that). The 4558's
// slew and GBW sit above the audio band at these gains: not modelled. Reverb
// (level 0) and the effects loop are left out: at reverb 0 they do not touch the
// signal path.
// ─────────────────────────────────────────────────────────────────────────────
class PeaveyBackstageComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; }   // n/a (solid-state)
    const char* modelName()           const noexcept override { return GAS_NAME("Peavey Backstage Component"); }

private:
    double fs_ = 0.0;

    // Knobs (targets) and the values the networks were last compiled with.
    enum Pot { kSat, kPre, kPost, kLow, kMid, kHigh, kNumPots };
    float target_[kNumPots] = { 0.5f, 0.5f, 0.7f, 0.5f, 0.5f, 0.5f };
    float cur_[kNumPots]    = { 0.5f, 0.5f, 0.7f, 0.5f, 0.5f, 0.5f };
    float presence_ = 0.5f, sag_ = 0.0f;
    bool  bright_ = true, thick_ = true, satSw_ = true;   // the shipped model bakes Bright + Thick ON
    bool  cabLoad_ = false;
    int   cabRow_ = -1;
    int   smoothCount_ = 0;
    bool  preDirty_ = true, paDirty_ = true;

    float inVolts_  = 0.5f;        // fit0: input-jack volts per unit
    float outScale_ = 1.0f / 66.0f;// speaker volts -> units: level parity with the shipped Backstage model at the default knobs
    double zenerV_  = 22.0;        // fit1: CR24/CR25 zener voltage (ESTIMATE)
    float lowMid_   = 0.15f;       // fit2: LOW pot audio law (fraction at mid-rotation)
    double preLaw_ = 1.0;          // fit5: PRE wiper = 1 - (1 - rotation)^preLaw_ (1 = linear)
    double spkFs_ = 0.0, spkQes_ = 0.0;   // fit3 / fit4 (lab): the speaker's resonance and Qes, 0 = the native row

    using PreNet = ssdk::Network<34, 18, 5, 3>;   // 3 diode pairs + U1B, U1A at their rails
    using PaNet  = ssdk::Network<11, 7, 1, 2>;
    PreNet pre_;
    PaNet  pa_;
    std::array<PreNet::State, kMaxCh> preSt_ {};
    std::array<PaNet::State,  kMaxCh> paSt_ {};

    double dcR_ = 0.9998, dcX_[kMaxCh] = {}, dcY_[kMaxCh] = {};   // output DC block
    // lab: peak |V| at U1B out, U1A out, U2A out, the power amp's output node, the speaker
    float pk_[5] = {};

    void buildPre() noexcept;
    void buildPa() noexcept;
    SpeakerParams loadSpeaker() const noexcept;
};
