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

    float gain_     = 0.6f;
    float bass_     = 0.5f;
    float mid_      = 0.5f;
    float treble_   = 0.6f;
    float presence_ = 0.6f;   // plexis are bright/present
    float master_   = 0.7f;   // "master" here drives the PI → power-amp crunch
    float sag_      = 0.28f;  // solid-state rectifier but EL34 power-stage sag under crank

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChannelState {
        BiquadFilter       inputHPF;    // sub-bass cut
        BiquadFilter       brightSh;    // bright-cap treble emphasis (High-Treble input)
        TriodeComponent    stage1;      // V1
        BiquadFilter       inter12HPF;  // couple into V2 (keep bass out of the clip)
        TriodeComponent    stage2;      // V2 (shared cathode)
        ToneStackComponent tonestack;   // Marshall type
        BiquadFilter       interPIHPF;  // PI input tighten
        TriodeComponent    stagePI;     // phase-inverter driver (master-controlled)
        BiquadFilter       presenceF;   // presence high shelf
        BiquadFilter       airLP;       // air LP (open — plexis are bright up top)
        BiquadFilter       bodyShelf;   // restore low-mid body after clipping

        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    // Voicing constants — tuned to the capture (nam_compare --model plexi).
    static constexpr float kPreToneGain = 0.80f;
    static constexpr float kCouple12    = 0.62f;

    static float softLimit(float x) noexcept;
    void recalcFilters() noexcept;
};
