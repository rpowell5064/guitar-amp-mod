#pragma once
#include "AudioBlock.h"
#include "DR_SpringReverb.h"
#include <vector>
#include <array>

// Plate reverb using a Schroeder / Moorer-style network:
//   input → pre-delay → 4 series allpass diffusers
//         → 4 parallel modulated comb filters (with damping LP)
//         → stereo output (L = sum of combs 0,2, R = sum of combs 1,3)
//
// Modulation on comb filter delay times creates the lush, smeared density
// characteristic of real plate transducers.
class PlateReverbBlock : public AudioBlock {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(float** in, float** out, int numSamples, int numChannels) override;
    void setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;
    // Clear every tail buffer (2026-07-23: seamless preset/effect switching —
    // a bypassed reverb otherwise holds its tank and replays it on re-engage).
    void reset() noexcept {
        auto clear = [](DelayLine& d) { std::fill(d.buf.begin(), d.buf.end(), 0.0f); d.writeIdx = 0; };
        clear(preDelay);
        for (auto& a : ap) clear(a.dl);
        for (auto& c : combs) { clear(c.dl); c.lastLP = 0.0f; }
    }

private:
    // Parameters
    float preDelayMs = 10.0f;
    float decayTime  =  1.5f; // seconds (RT60)
    float damping    =  0.3f; // 0=bright, 1=dark
    float modDepth   =  0.5f; // LFO depth on comb delays (normalised)
    float modRate    =  0.8f; // Hz
    float mix        =  0.3f;
    bool  dense      = false; // Density switch (see kNumComb comment)
    // Type switch (2026-07-23): 0 = plate tank (everything above), 1 = SPRING —
    // the Accutronics 4EB3 three-spring model already in the codebase (built for
    // the Deluxe Reverb amp), exposed as a block type. Mono tank (authentic),
    // decay/damping/mix map onto the spring's own controls; predelay/mod are
    // plate-only. Default plate = bit-identical.
    bool  springOn   = false;
    DR_SpringReverb spring;
    std::vector<float> springBuf;

    // ---- Internal delay-line wrappers ----
    struct DelayLine {
        std::vector<float> buf;
        int writeIdx = 0;
        int nominalLen = 0; // base length in samples

        void resize(int len) { buf.assign(len + 8, 0.0f); nominalLen = len; writeIdx = 0; }

        void write(float v) noexcept {
            buf[writeIdx] = v;
            if (++writeIdx >= static_cast<int>(buf.size())) writeIdx = 0;
        }

        float read(int delay) const noexcept {
            const int len = static_cast<int>(buf.size());
            const int idx = (writeIdx - delay + len * 2) % len;
            return buf[idx];
        }

        // Linear interpolation read for fractional delay
        float readFrac(float delay) const noexcept {
            const int len  = static_cast<int>(buf.size());
            const int d0   = static_cast<int>(delay);
            const float fr = delay - static_cast<float>(d0);
            const int i0   = (writeIdx - d0     + len * 2) % len;
            const int i1   = (writeIdx - d0 - 1 + len * 2) % len;
            return buf[i0] + fr * (buf[i1] - buf[i0]);
        }

        // 4-point Hermite read (2026-07-14): the tank combs are LFO-modulated, and a linear
        // moving tap dulls/grains the tail; Hermite keeps damping the only HF loss. Needs
        // delay in [1, len-3] — the tank mod excursion is a few samples inside the +8 slack.
        float readFracHermite(float delay) const noexcept {
            const int len  = static_cast<int>(buf.size());
            const int d0   = static_cast<int>(delay);
            const float fr = delay - static_cast<float>(d0);
            const float xm1 = buf[(writeIdx - d0 + 1 + len * 2) % len];
            const float x0  = buf[(writeIdx - d0     + len * 2) % len];
            const float x1  = buf[(writeIdx - d0 - 1 + len * 2) % len];
            const float x2  = buf[(writeIdx - d0 - 2 + len * 2) % len];
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * fr + c2) * fr + c1) * fr + x0;
        }
    };

    // Schroeder allpass: H(z) = (z^{-N} - g) / (1 - g*z^{-N})
    struct AllpassDelay {
        DelayLine dl;
        float g = 0.7f;

        void resize(int len) { dl.resize(len); }

        float process(float x) noexcept {
            const float d = dl.read(dl.nominalLen);
            const float v = x + g * d;
            dl.write(v);
            return d - g * v;
        }
    };

    // Feedback comb with per-filter damping (1-pole LP in feedback path)
    struct CombDelay {
        DelayLine dl;
        float feedback  = 0.7f;
        float damping   = 0.3f;
        float lastLP    = 0.0f;

        void resize(int len) { dl.resize(len); }

        float process(float x, float modDelaySamples) noexcept {
            const float out = dl.readFracHermite(modDelaySamples);
            // 1-pole LP for high-frequency damping
            lastLP = out * (1.0f - damping) + lastLP * damping;
            dl.write(x + feedback * lastLP);
            return out;
        }
    };

    // Pre-delay line (stereo input summed to mono before reverb network)
    DelayLine preDelay;

    // Density switch (2026-07-23, OPT-IN): 0 = the classic 4-comb/4-AP tank every
    // preset was voiced on (bit-identical); 1 = a dense 8-comb/6-AP plate — lusher,
    // less metallic tail. Densifying was measured as a blanket change and REJECTED
    // (it re-leveled ~50 presets by up to 3 dB), so it ships as a per-preset switch.
    // Arrays are sized for the dense tank; classic mode uses the first 4/4.
    static constexpr int kNumAP   = 6;
    static constexpr int kNumComb = 8;
    static constexpr int kClassicAP   = 4;
    static constexpr int kClassicComb = 4;

    AllpassDelay ap[kNumAP];
    CombDelay    combs[kNumComb];

    // Per-comb LFO state
    float lfoPhase[kNumComb] = {};

    // Scaled delay lengths (set in prepare())
    int apLengths[kNumAP]     = {};
    int combLengths[kNumComb] = {};

    void recalcFeedback();
    void recalcDamping();
    void syncSpring();
};
