#pragma once
#include "BiquadFilter.h"
#include <array>
#include <vector>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// DR_SpringReverb — Accutronics 4EB3 spring tank simulation
// ─────────────────────────────────────────────────────────────────────────────
//
// The AB763 uses a 3-spring Accutronics tank (model 4EB3-A or 4EB3C1B).
// Each spring has a different delay time, producing the characteristic dense
// "boing" that overlaps into a continuous reverb tail.
//
// Architecture (per spring):
//   input → [2 allpass diffusers] → [delay line] → feedback path
//                                                  → [1-pole damping LP]
// Three springs run in parallel and their outputs are summed.
//
// The reverb driver (pre-tank) and recovery (post-tank) stages are mild
// soft-saturators representing V2A and V2B (12AX7 halves in the AB763).
//
// CPU: ~40 ops/sample at native 48 kHz. Safe for Pi 3/4.
// ─────────────────────────────────────────────────────────────────────────────
class DR_SpringReverb final {
public:
    // mix:    0 = fully dry, 1 = fully wet  (Reverb knob on the amp)
    // decay:  RT60 approximation, scaled internally — leave this fixed in the
    //         AB763 model; only mix is user-facing.
    void prepare(double sampleRate, int maxBlockSize);
    void reset()  noexcept;

    void setMix   (float v) noexcept { mix_    = v; }
    void setDecay (float v) noexcept { decay_  = v; recalcFeedback(); }
    void setDamping(float v) noexcept { damp_  = v; recalcDamping();  }

    // Process a mono block in-place.  numSamples ≤ maxBlockSize.
    void processBlock(float* data, int numSamples) noexcept;

private:
    // ── Delay line ────────────────────────────────────────────────────────────
    struct DelayLine {
        std::vector<float> buf;
        int writeIdx = 0;

        void resize(int len) { buf.assign(static_cast<size_t>(len + 4), 0.0f); writeIdx = 0; }

        void write(float v) noexcept {
            buf[static_cast<size_t>(writeIdx)] = v;
            if (++writeIdx >= static_cast<int>(buf.size())) writeIdx = 0;
        }

        float read(int delayLen) const noexcept {
            const int len = static_cast<int>(buf.size());
            int idx = writeIdx - delayLen;
            if (idx < 0) idx += len;
            return buf[static_cast<size_t>(idx)];
        }
    };

    // ── Schroeder allpass diffuser ────────────────────────────────────────────
    struct AllpassDiffuser {
        DelayLine dl;
        float g = 0.68f;

        void resize(int len) { dl.resize(len); }

        float process(float x) noexcept {
            const float d = dl.read(dl.buf.size() > 1 ? static_cast<int>(dl.buf.size()) - 4 : 1);
            const float v = x + g * d;
            dl.write(v);
            return d - g * v;
        }
        void reset() noexcept { std::fill(dl.buf.begin(), dl.buf.end(), 0.0f); dl.writeIdx = 0; }
    };

    // ── One spring ────────────────────────────────────────────────────────────
    struct Spring {
        AllpassDiffuser ap[2];  // pre-delay diffusion (dispersion)
        DelayLine       delay;  // main spring delay
        float           fb     = 0.82f;  // feedback coefficient
        float           damp   = 0.25f;  // 1-pole LP coefficient (damping)
        float           lpState= 0.0f;   // LP filter state

        float process(float x) noexcept {
            x = ap[0].process(x);
            x = ap[1].process(x);

            const int len = static_cast<int>(delay.buf.size()) - 4;
            const float d = delay.read(len);
            lpState = d * (1.0f - damp) + lpState * damp; // 1-pole LP damping
            delay.write(x + fb * lpState);
            return d;
        }

        void reset() noexcept {
            ap[0].reset(); ap[1].reset();
            std::fill(delay.buf.begin(), delay.buf.end(), 0.0f);
            delay.writeIdx = 0;
            lpState = 0.0f;
        }
    };

    static constexpr int kNumSprings = 3;

    // Spring delay times (ms) — Accutronics 4EB3 approximate values.
    // Slight non-integer ratios prevent monotonic comb resonances.
    static constexpr float kSpringMs[kNumSprings] = { 28.3f, 32.7f, 37.1f };
    // Allpass lengths (ms) per spring, for dispersion simulation.
    static constexpr float kAP0Ms[kNumSprings]    = { 6.2f, 5.1f, 7.3f };
    static constexpr float kAP1Ms[kNumSprings]    = { 9.7f, 8.4f, 11.1f };

    std::array<Spring, kNumSprings> springs_;

    float mix_   = 0.25f;
    float decay_ = 0.6f;
    float damp_  = 0.25f;

    // Drive/recovery: simple soft-saturator representing V2A/V2B triodes.
    // No full Koren model needed — mild saturation is enough for reverb drive.
    static float softSaturate(float x, float drive) noexcept;

    void recalcFeedback() noexcept;
    void recalcDamping()  noexcept;

    double sampleRate_ = 48000.0;
};
