#pragma once
#include "BiquadFilter.h"
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Abstract base class for all delay model implementations.
//
// processSample(float x, int ch) is called once per sample per channel in
// channel order (ch=0 first, then ch=1) for each sample index.
// advanceSmoothing() is called once per sample index BEFORE the per-channel
// calls, so smoothed values are consistent across both channels.
//
// No JUCE dependency — all filters and helpers are self-contained.
class DelayBase {
public:
    virtual ~DelayBase() = default;

    virtual void  prepare(double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void  reset()                                                        noexcept = 0;
    virtual void  advanceSmoothing()                                             noexcept {}
    virtual float processSample(float x, int ch)                                noexcept = 0;
    virtual void  setParameter(const std::string& id, float value)              noexcept = 0;
    virtual float getParameter(const std::string& id) const                     noexcept = 0;
    virtual const char* delayName() const noexcept = 0;

protected:
    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 512;
    int    numChannels_  = 2;

    // ── Fractional read from circular buffer (linear interpolation) ───────
    // delaySamples must satisfy 1 ≤ delaySamples ≤ bufLen-2.
    static float readFrac(const std::vector<float>& buf,
                          float delaySamples, int writeIdx) noexcept {
        const int bufLen = static_cast<int>(buf.size());
        const int intDel = static_cast<int>(delaySamples);
        const float frac = delaySamples - static_cast<float>(intDel);

        const int r0 = (writeIdx - intDel     + bufLen * 2) % bufLen;
        const int r1 = (writeIdx - intDel - 1 + bufLen * 2) % bufLen;
        return buf[r0] + frac * (buf[r1] - buf[r0]);
    }

    // ── Exponential parameter smoother (1-pole IIR) ───────────────────────
    // Advances one sample per tick().  Call from advanceSmoothing() then read
    // current() in processSample().
    struct ParamSmoother {
        float current_ = 0.0f;
        float coeff_   = 0.99f;

        void prepare(float sampleRate, float timeConstantMs) noexcept {
            coeff_ = std::exp(-1000.0f / (sampleRate * std::max(1.0f, timeConstantMs)));
        }
        void setImmediate(float v) noexcept { current_ = v; }
        void tick(float target)  noexcept {
            current_ += (1.0f - coeff_) * (target - current_);
        }
        float current() const noexcept { return current_; }
    };

    // ── Random-walk modulation source ─────────────────────────────────────
    // Produces slowly-varying noise for wow and flutter simulation.
    // Internally: LCG white noise → 1-pole LP → normalized output (σ ≈ 1.0).
    // fc determines the bandwidth:
    //   Wow:     fc ≈ 0.2 Hz → smooth, slow wandering
    //   Flutter: fc ≈ 9 Hz  → irregular, fast flutter
    struct RandomWalk {
        float    state_ = 0.0f;
        float    coeff_ = 0.0f;   // 1-pole LP coefficient
        float    scale_ = 1.0f;   // amplitude normalizer so output σ ≈ 1.0
        uint32_t seed_  = 0x9E3779B9u;

        void prepare(float fc, float sampleRate, uint32_t seed = 0x9E3779B9u) noexcept {
            coeff_ = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * fc / sampleRate);
            // Var(LP output on uniform [-1,1] noise) = (1/3) * coeff/(2-coeff)
            // scale = 1/σ_out so returned values have σ ≈ 1.0
            const float varOut = (1.0f / 3.0f) * coeff_
                                 / std::max(2.0f - coeff_, 1e-12f);
            scale_ = (varOut > 1e-12f) ? 1.0f / std::sqrt(varOut) : 1.0f;
            seed_  = seed;
        }

        void setImmediate(float v) noexcept { state_ = v; }

        float next() noexcept {
            // Galois LCG — extremely cheap, adequate spectral flatness for noise.
            seed_ = seed_ * 1664525u + 1013904223u;
            const float noise = static_cast<float>(static_cast<int32_t>(seed_))
                                * (1.0f / 2147483648.0f);  // → [-1, +1]
            state_ += coeff_ * (noise - state_);
            return state_ * scale_;  // normalized: σ ≈ 1.0
        }

        float current() const noexcept { return state_ * scale_; }
    };
};
