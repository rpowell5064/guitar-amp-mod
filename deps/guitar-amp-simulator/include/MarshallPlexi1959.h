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
    // Variac v2 (2026-07-31 physics audit; v1 2026-07-30): the EVH brown-sound
    // trick -- the 1959 run off a variac at 89 V instead of 120 V. Continuous
    // 0..1 (the port ships a toggle): v maps to mains V = 120 - 31*v, so
    //   s(v) = V/120 = 1 - 0.258333*v   (s = 0.741667 at v = 1 = 89 V).
    // Every supply node (B+, screens, preamp A/B/C) rides the same PT, so ONE
    // scale is exact. Per nonlinear stage the equivalent transform is
    //   y = s * f(gm * x / s)
    // which moves the clip knee to s*knee (earlier by 1/s = 1.348 = 2.6 dB),
    // caps swing at s, and keeps the interior s*(1/s) pairs cancelling so
    // clean gain scales as gm^3 = s and clipped ceiling as the final s:
    // total swing prop. to s, power prop. to s^2 (-5.2 dB at 89 V). gm loss uses the
    // triode space-charge law gm prop. to Ip^(1/3) -> s^(1/3) = 0.904941 (a linear
    // gm*s would be 3x too strong). Sag coupling grows as the FRACTIONAL
    // droop R*I/V ~ s^-1.5 (0.28 -> 0.438), plus a 15 ms fast supply-RC
    // component (0.35 blend) so the squish grabs on pick attack. Heater/
    // emission loss stays the behavioral -1.5 dB @3.5k shelf crossfade.
    // 0 = stock wall voltage, BIT-IDENTICAL (all factors exactly 1).
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
