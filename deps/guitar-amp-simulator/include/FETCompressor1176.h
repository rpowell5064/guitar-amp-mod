#pragma once
#include <cmath>
#include <algorithm>
#include <array>

// 1176-style FET compressor.
//
// Key characteristics modelled:
//   • Peak detector with 1 ms feed-forward look-ahead
//   • Non-linear gain computer via tanh-compressed curve
//   • Program-dependent release: two-stage (fast + slow), take the minimum
//   • FET saturation on the gain element: tanh(gain*drive)/tanh(drive)
//   • Transformer output colouration: even-order harmonic emphasis
//   • "All-buttons-in" mode (ratios 4+8+20+All) for the crushing brick-wall sound
//
// Parameter ranges match the front-panel convention of the hardware unit.
//   input  : +4 to +44 dBu  — pass normalised 0-1, maps internally
//   output : -20 to +4 dBu  — pass normalised 0-1, maps internally
//   attack : fastest (~20 µs) = 1.0,  slowest (~800 µs) = 0.0  (inverted like hardware)
//   release: fastest (~50 ms) = 1.0,  slowest (~1100 ms)= 0.0  (inverted like hardware)
//   ratio  : 4 | 8 | 20 | "all" (pass 4, 8, 20, or 0 for all-buttons-in)
class FETCompressor1176 {
public:
    static constexpr int  kLookAheadMs = 1;   // 1 ms look-ahead

    void prepare(double sampleRate) noexcept {
        fs_        = sampleRate;
        laLen_     = std::max(1, (int)(sampleRate * kLookAheadMs * 0.001));
        laBuffer_.fill(0.0f);
        laHead_    = 0;
        reset();
        recalcTimeConstants();
    }

    void reset() noexcept {
        peakEnv_     = 0.0f;
        grFast_      = 0.0f;
        grSlow_      = 0.0f;
        for (auto& s : laBuffer_) s = 0.0f;
        laHead_      = 0;
    }

    // 0-1 normalised input/output trim
    void setInputGain (float n01) noexcept { inputGain_  = std::pow(10.0f, n01 * 30.0f * 0.05f); } // 0 → +30 dB
    void setOutputGain(float n01) noexcept { outputGain_ = std::pow(10.0f, (n01 * 24.0f - 20.0f) * 0.05f); }

    // attack/release: 1 = fastest (hardware convention)
    void setAttack (float n01) noexcept { attackNorm_  = n01; recalcTimeConstants(); }
    void setRelease(float n01) noexcept { releaseNorm_ = n01; recalcTimeConstants(); }

    // ratio: 4, 8, 20, or 0 = all-buttons-in
    void setRatio(int ratio) noexcept { ratio_ = ratio; }

    float process(float x) noexcept {
        // Write input into look-ahead delay; read the delayed sample
        laBuffer_[laHead_] = x * inputGain_;
        laHead_ = (laHead_ + 1) % laLen_;
        const float delayed = laBuffer_[laHead_]; // 1 ms delayed input signal

        // Peak detector on un-delayed signal (looking ahead into the future)
        const float absIn = std::abs(x * inputGain_);
        if (absIn > peakEnv_)
            peakEnv_ = attackCoeff_  * peakEnv_ + (1.0f - attackCoeff_)  * absIn;
        else
            peakEnv_ = releaseCoeff_ * peakEnv_ + (1.0f - releaseCoeff_) * absIn;

        peakEnv_ = std::max(peakEnv_, 1e-30f);
        const float xDb  = 20.0f * std::log10(peakEnv_);
        const float over = xDb - threshold_;

        // Non-linear gain computer: tanh-shaped compression curve
        float targetGR = 0.0f;
        if (over > 0.0f) {
            const float slope = computeSlope();
            // tanh softens the knee without an explicit width parameter
            targetGR = -slope * over * (0.5f + 0.5f * std::tanh(over * 0.4f - 1.0f));
        }

        // Program-dependent release: fast and slow envelopes, take the lesser GR
        grFast_ = fastRelCoeff_ * grFast_ + (1.0f - fastRelCoeff_) * targetGR;
        grSlow_ = slowRelCoeff_ * grSlow_ + (1.0f - slowRelCoeff_) * targetGR;
        const float grDb = std::min(grFast_, grSlow_); // minimum = most gain reduction

        // FET gain element with saturation character
        const float gainLin = std::pow(10.0f, grDb * 0.05f);
        const float drive    = 1.0f + std::abs(grDb) * 0.04f; // more drive at high GR
        float out = delayed * gainLin;
        if (drive > 1.001f)
            out = std::tanh(out * drive) / std::tanh(drive);

        // Output transformer colouration: subtle even-order harmonic distortion
        const float sat  = out;
        out = std::tanh(out) + 0.03f * (sat * sat - 0.5f);

        return out * outputGain_;
    }

    float getGainReductionDb() const noexcept {
        return std::min(grFast_, grSlow_);
    }

private:
    float computeSlope() const noexcept {
        // All-buttons-in: extreme limiting (~20:1 equivalent with extra bite)
        if (ratio_ == 0) return (20.0f - 1.0f) / 20.0f * 1.15f;
        return (float)(ratio_ - 1) / (float)ratio_;
    }

    void recalcTimeConstants() noexcept {
        if (fs_ <= 0.0) return;
        // Hardware attack: ~20 µs (fastest) … ~800 µs (slowest) — inverted knob
        const float atkMs  = 0.020f + (1.0f - attackNorm_)  * 0.780f;
        // Hardware release: ~50 ms (fastest) … ~1100 ms (slowest) — inverted knob
        const float relMs  = 50.0f  + (1.0f - releaseNorm_) * 1050.0f;

        attackCoeff_   = std::exp(-1.0f / (float)(fs_ * atkMs  * 0.001f));
        releaseCoeff_  = std::exp(-1.0f / (float)(fs_ * relMs  * 0.001f));

        // Fast release: 1/4 of set release; slow: 4×
        fastRelCoeff_  = std::exp(-1.0f / (float)(fs_ * relMs * 0.00025f));
        slowRelCoeff_  = std::exp(-1.0f / (float)(fs_ * relMs * 0.004f));
    }

    double fs_ = 44100.0;

    float inputGain_   = 1.0f;
    float outputGain_  = 1.0f;
    float threshold_   = -20.0f; // dBFS — fixed threshold, input gain sets the drive
    float attackNorm_  = 0.5f;
    float releaseNorm_ = 0.5f;
    int   ratio_       = 4;

    float attackCoeff_  = 0.0f;
    float releaseCoeff_ = 0.0f;
    float fastRelCoeff_ = 0.0f;
    float slowRelCoeff_ = 0.0f;

    float peakEnv_ = 0.0f;
    float grFast_  = 0.0f;
    float grSlow_  = 0.0f;

    static constexpr int kMaxLookAhead = 4096;
    std::array<float, kMaxLookAhead> laBuffer_{};
    int laLen_  = 1;
    int laHead_ = 0;
};
