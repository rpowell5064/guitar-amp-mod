#include "TubeScreamer808Block.h"
#include <cmath>
#include <algorithm>

void TubeScreamer808Block::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;
    rebuildFilters();
    for (auto& c : filt) {
        c.inputHP.reset();
        c.outputLP.reset();
        c.toneLP.reset();
    }
}

void TubeScreamer808Block::rebuildFilters() {
    // Input HP: 1-pole RC at 720 Hz (R=4.7kΩ, C=47nF in TS808 schematic).
    // 1st-order slope (6 dB/oct) is critical — a 2nd-order filter removes too
    // much body by cutting more aggressively below the corner frequency.
    const BiquadCoeffs hpCoeffs = Filters::highpass1pole(720.0, sampleRate);

    // Output LP: 1-pole RC at 3.4 kHz (R=1kΩ, C=47nF).
    // This hard-limits the high end after clipping and, together with the input
    // HP, defines the passband that produces the characteristic mid-hump.
    const BiquadCoeffs lpCoeffs = Filters::lowpass1pole(3400.0, sampleRate);

    // Tone LP: sweeps from 1 kHz (tone=0, dark) to 10 kHz (tone=1, bright)
    // on a log scale, approximating the TS808 tone pot + cap network.
    const double toneLPHz = 1000.0 * std::pow(10.0, static_cast<double>(tone));
    const double toneLPClamped = std::min(toneLPHz, sampleRate * 0.48);
    const BiquadCoeffs toneCoeffs = Filters::lowpass1pole(toneLPClamped, sampleRate);

    for (auto& c : filt) {
        c.inputHP.setCoeffs(hpCoeffs);
        c.outputLP.setCoeffs(lpCoeffs);
        c.toneLP.setCoeffs(toneCoeffs);
    }
}

void TubeScreamer808Block::setParameter(const std::string& id, float v) {
    if      (id == "drive") drive = std::clamp(v, 0.0f, 1.0f);
    else if (id == "tone")  { tone = std::clamp(v, 0.0f, 1.0f); rebuildFilters(); return; }
    else if (id == "level") level = std::clamp(v, 0.0f, 1.0f);
    else if (id == "mix")   mix   = std::clamp(v, 0.0f, 1.0f);
}

float TubeScreamer808Block::getParameter(const std::string& id) const {
    if (id == "drive") return drive;
    if (id == "tone")  return tone;
    if (id == "level") return level;
    if (id == "mix")   return mix;
    return 0.0f;
}

// Asymmetric TS808 diode clipping.
//
// The real TS808 uses 1 silicon diode on one side and 2 in series on the other,
// producing asymmetric soft clipping with different onset thresholds per polarity.
// We model this by halving the effective gain for the positive half (2-diode stack
// = higher threshold = later/softer clip) while leaving it intact for the negative
// half (1-diode = lower threshold = earlier/harder clip).
//
// tanh(g·x)/tanh(g) normalisation keeps small-signal gain at unity for all drive
// values, matching the op-amp's non-inverting configuration at low signal levels.
float TubeScreamer808Block::asymClip(float x, float driveAmt) noexcept {
    const float gain = 1.0f + 49.0f * driveAmt;   // 1 → 50 (matches op-amp gain range)

    if (x >= 0.0f) {
        // Positive: 2-diode stack → half gain = double threshold (softer clip)
        const float g2 = gain * 0.5f;
        return std::tanh(g2 * x) / std::tanh(g2);
    } else {
        // Negative: 1-diode → full gain = lower threshold (harder clip)
        return std::tanh(gain * x) / std::tanh(gain);
    }
}

void TubeScreamer808Block::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

    const int chCount = std::min(nCh, kMaxCh);
    const float wetGain = level * mix;
    const float dryGain = 1.0f - mix;

    for (int c = 0; c < chCount; ++c) {
        auto& f = filt[c];
        for (int i = 0; i < numSamples; ++i) {
            // Stage 1: input HP at 720 Hz — always in series when block is engaged,
            // matching the real TS808 where the input RC cap is before all processing.
            // Applying it to both the dry and wet paths means the low-end tightening
            // is consistent at any mix level, not just when wet signal is audible.
            const float dry = f.inputHP.process(in[c][i]);

            // Stage 2: asymmetric diode soft-clip.
            float s = asymClip(dry, drive);

            // Stage 3: 1-pole output LP at 3.4 kHz — rolls off post-clip harmonics.
            // Together with stage 1 this creates the 720 Hz–3.4 kHz mid-hump passband.
            s = f.outputLP.process(s);

            // Stage 4: tone-controlled 1-pole LP (1 kHz–10 kHz log sweep).
            // Reduces treble at low tone settings, passes full bandwidth at high tone.
            s = f.toneLP.process(s);

            // Stage 5: output level + dry/wet (dry is the HP-conditioned signal).
            out[c][i] = dryGain * dry + wetGain * s;
        }
    }

    for (int c = chCount; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}
