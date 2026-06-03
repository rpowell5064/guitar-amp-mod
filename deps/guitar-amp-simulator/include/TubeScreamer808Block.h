#pragma once
#include "AudioBlock.h"
#include "BiquadFilter.h"
#include <array>

// Ibanez TS-808 Tube Screamer model.
//
// Analog circuit topology (component values from original schematic):
//   input → inputHP (720 Hz, 1-pole RC: R=4.7kΩ C=47nF)  ← always in series
//          → asymmetric diode clip (1 diode / 2 diodes, drive-controlled gain)
//          → outputLP (3.4 kHz, 1-pole RC: R=1kΩ C=47nF)
//          → toneLP (sweeping 1-pole LP: 1 kHz dark → 10 kHz bright)
//          → level
//
// The inputHP is applied before the dry/wet split so it conditions both paths
// when the block is engaged — matching the real TS808 where the input cap is
// in series with the buffer, not just the clipping stage.  True bypass passes
// the raw signal without the HP.
//
// The mid-hump (~720–3400 Hz, +5–7 dB) emerges from the bandpass formed
// by the HPF/LPF pair — no explicit peak EQ needed or applied.
//
// Asymmetric clip: positive side uses gain/2 (2-diode stack, higher threshold),
// negative side uses full gain (1-diode, lower threshold).  Unity small-signal
// gain is preserved at all drive settings via tanh(g·x)/tanh(g) normalisation.
class TubeScreamer808Block : public AudioBlock {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(float** in, float** out, int numSamples, int numChannels) override;
    void setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

private:
    float drive = 0.5f;   // 0 = minimum gain, 1 = maximum saturation
    float tone  = 0.5f;   // 0 = dark (1 kHz LP), 1 = bright (10 kHz LP)
    float level = 0.5f;   // output level [0,1]
    float mix   = 1.0f;   // dry/wet [0=dry, 1=wet]

    static constexpr int kMaxCh = 2;

    struct ChannelFilters {
        BiquadFilter inputHP;   // 1-pole HP at 720 Hz
        BiquadFilter outputLP;  // 1-pole LP at 3.4 kHz
        BiquadFilter toneLP;    // 1-pole LP, sweeps with tone knob
    };
    std::array<ChannelFilters, kMaxCh> filt;

    void rebuildFilters();
    // Asymmetric diode soft-clip: pos=2-diode (softer), neg=1-diode (harder).
    static float asymClip(float x, float driveAmt) noexcept;
};
