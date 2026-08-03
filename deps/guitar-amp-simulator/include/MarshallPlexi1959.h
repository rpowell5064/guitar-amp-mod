#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Marshall 1959 Super Lead "Plexi" (100 W, EL34) ───────────────────────────
//
// The 1959 is a NON-master-volume amp: its crunch is POWER-AMP driven (cranked EL34s),
// NOT cascaded preamp gain like the JCM800. So this model has only TWO triode gain
// stages (V1 + shared-cathode V2), a bright treble emphasis (the High-Treble input +
// the classic jumpered-channels tone), the Marshall tone stack, then a phase-inverter
// stage feeding the shared EL34 PowerAmp (which supplies the power-tube saturation).
// Brighter, rawer, lower-gain and more touch-dynamic than the JCM800. Voiced to real
// 1959 Super Lead NAM captures (CH I High Jumped) — nam_compare --model plexi.
//
// Tube: EL34 (recommendedTubeType = 1).
class MarshallPlexi1959 final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34
    const char* modelName()           const noexcept override { return "Marshall Plexi 1959"; }

private:
    double oversampledFs_ = 0.0;

    float gain_     = 0.6f;   // Vol I — the High Treble (bright) channel (capture-anchored path)
    float vol2_     = 0.0f;   // Vol II — the Normal channel, ADDED in parallel (jumpered 1959).
                              // 0 = exactly the pre-2026-07-14 voicing (no migration needed).
    float bass_     = 0.5f;
    float mid_      = 0.5f;
    float treble_   = 0.6f;
    float presence_ = 0.6f;   // plexis are bright/present
    float master_   = 0.7f;   // "master" here drives the PI → power-amp crunch
    float sag_      = 0.28f;  // solid-state rectifier but EL34 power-stage sag under crank
    // Variac v3 -- OVERVOLT (2026-08-02, user + real 1959HW "SLAMMIN" variac
    // captures, 120..196 V DIMED, Marshall 1959HW Plexi Variac [Hyper Accuracy]).
    // v1/v2 modeled the folklore LOWERED-voltage brown sound (120->89 V). The
    // user's reference pack instead sweeps voltage UP, and the measured "magic"
    // peak is ~170 V: LOUDEST (-19.6 dB), MOST saturated (THD@1k 290% vs 142%
    // at 120 V) and TIGHTEST lows (THD@110 14.8% vs 24.5%) -- both above and
    // below 170 V fall off. So the knob now RAISES voltage 120 V -> ~170 V:
    //   s(v) = 1 + (kVariacMaxS-1)*v          (s = 1.4167 at v = 1 = 170 V).
    // Fit to the captures (not the textbook headroom result -- at DIMED settings
    // the higher B+ drives the whole amp harder = MORE saturation, not cleaner):
    //   input drive  ~ s^kVariacDriveExp   (harder into every stage = saturation)
    //   output swing ~ s^kVariacSwingExp   (louder, ~+1.6 dB net at max)
    //   sag coupling  = 0.28 * s^-1.5       (s>1 STIFFENS the supply = tighter lows)
    // plus a mild bright shelf (overvolt is more aggressive up top, not browner).
    // 0 = stock wall voltage, BIT-IDENTICAL (all factors exactly 1). ONLY Brown
    // Sound '84 uses the variac, so every other Plexi preset is untouched.
    float variac_   = 0.0f;

    LinearSmoother gainSmooth_, masterSmooth_, vol2Smooth_;

    struct ChannelState {
        BiquadFilter       inputHPF;    // sub-bass cut
        BiquadFilter       brightSh;    // bright-cap treble emphasis (High-Treble input)
        TriodeComponent    stage1;      // V1a — High Treble channel half
        TriodeComponent    stage1b;     // V1b — Normal channel half (jumpered; blended by Vol II)
        BiquadFilter       normLP;      // Normal-channel darker coupling (~5 kHz rolloff)
        BiquadFilter       inter12HPF;  // couple into V2 (keep bass out of the clip)
        TriodeComponent    stage2;      // V2 (shared cathode)
        ToneStackComponent tonestack;   // Marshall type
        BiquadFilter       interPIHPF;  // PI input tighten
        TriodeComponent    stagePI;     // phase-inverter driver (master-controlled)
        BiquadFilter       presenceF;   // presence high shelf
        BiquadFilter       variacSh;    // browner top under variac (-1.5 dB @3.5k, Recto-spongy value)
        BiquadFilter       airLP;       // air LP (open — plexis are bright up top)
        BiquadFilter       bodyShelf;   // restore low-mid body after clipping

        float sagEnv    = 0.0f;
        float sagDecay  = 0.0f;
        float sagEnvF   = 0.0f;   // fast supply-RC sag node (tau = 15 ms), variac-blended
        float sagDecayF = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    // Voicing constants — tuned to the capture (nam_compare --model plexi).
    static constexpr float kPreToneGain = 0.80f;
    static constexpr float kCouple12    = 0.62f;
    static constexpr float kNormalMix   = 0.9f;   // Vol II contribution at full (jumpered blend weight)

    // Variac overvolt fit (see variac_ doc). v=0 is bit-identical; tuned so
    // variac 1.0 slams toward the ~170 V capture (much higher THD + a bit
    // louder + tighter lows). nam_compare --model plexi --variac 0/1 vs the
    // 120 V / 170 V captures.
    static constexpr float kVariacMaxS     = 1.4167f;  // 170 V / 120 V
    static constexpr float kVariacDriveExp = 1.60f;    // input drive  ~ s^exp (saturation)
    static constexpr float kVariacSwingExp = 0.18f;    // output swing ~ s^exp (~+1.6 dB net over 3 stages)

    // Variac state: 20 ms glide on the (rare) toggle + per-sample cached
    // factors, advanced ONCE per sample index in advanceSmoothing() so both
    // channels see identical values. All exactly 1.0f (and 0.28f) at v = 0.
    float vSm_      = 0.0f;      // smoothed variac position
    float vSmA_     = 0.0f;      // 1 - exp(-1/(0.020 * oversampledFs))
    float vInGm_    = 1.0f;      // input factor  = (1/s) * s^(1/3) = 1.220153 @ 89 V
    float vSwing_   = 1.0f;      // output factor = s               = 0.741667 @ 89 V
    float vSagCoup_ = 0.28f;     // 0.28 * s^-1.5                   = 0.438    @ 89 V

    static float softLimit(float x) noexcept;
    void recalcFilters() noexcept;
};
