#pragma once
#include <cmath>
#include <algorithm>
#include "BiquadFilter.h"

// Feed-forward VCA compressor with soft-knee RMS detection.
//
// All parameters use normalized 0-1 values (except threshold in dBFS and
// ratio as actual ratio).  The host is responsible for unit conversion
// before calling setX().
//
// Signal path:
//   input → RMS² leaky integrator → level-in-dB
//         → soft-knee gain computer → target GR (dB)
//         → attack/release smoother → applied GR
//         → gain (makeup + GR) → output
class VCACompressor {
public:
    void prepare(double sampleRate) noexcept {
        fs_ = sampleRate;
        recalcTimeConstants();
        reset();
    }

    void reset() noexcept {
        rmsState_  = 0.0f;
        grState_   = 0.0f;
        grSlow_    = 0.0f;
        scHP_.reset();
    }

    // Detector-only sidechain high-pass (Hz). 0 = OFF (default, bit-identical).
    // Keeps sub-bass / hum out of the level detector so the low string doesn't
    // pump the whole signal. Audio path is never filtered.
    void setSidechainHP(float hz) noexcept {
        scHz_ = std::max(0.0f, hz);
        if (scHz_ > 0.0f && fs_ > 0.0)
            scHP_.setCoeffs(Filters::highpass(scHz_, 0.707, fs_));
    }

    // Program-dependent (dual-time-constant) release. false = OFF (default,
    // bit-identical single-TC path). true = fast+slow release envelopes, output
    // the greater reduction — fast recovery after transients, slow after sustain.
    void setProgramRelease(bool on) noexcept { programRel_ = on; }

    // threshold: dBFS  (-60 … 0),  default -18
    void setThreshold(float dBFS) noexcept { threshold_ = dBFS; }

    // ratio: 1 … inf (pass 1e6f for limiting)
    void setRatio(float r) noexcept { ratio_ = std::max(1.0f, r); }

    // knee: dB width of the soft-knee region (0 = hard knee)
    void setKnee(float dB) noexcept { knee_ = std::max(0.0f, dB); }

    // attack / release in seconds
    void setAttack(float seconds) noexcept  { attackTime_  = seconds; recalcTimeConstants(); }
    void setRelease(float seconds) noexcept { releaseTime_ = seconds; recalcTimeConstants(); }

    // makeupGain: linear gain (1.0 = unity)
    void setMakeupGain(float linear) noexcept { makeup_ = std::max(0.0f, linear); }

    float process(float x) noexcept {
        // Detector signal — optionally sidechain-highpassed (audio path stays x).
        const float d = (scHz_ > 0.0f) ? scHP_.process(x) : x;

        // RMS² leaky integrator
        rmsState_ = rmsCoeff_ * rmsState_ + (1.0f - rmsCoeff_) * d * d;
        const float rmsLin = std::sqrt(std::max(rmsState_, 1e-30f));
        const float xDb    = 20.0f * std::log10(rmsLin);

        // Soft-knee gain computer
        float targetGR = 0.0f; // gain reduction in dB (≤ 0)
        const float halfKnee = knee_ * 0.5f;
        const float over     = xDb - threshold_;

        if (knee_ > 0.0f && over > -halfKnee && over < halfKnee) {
            // Quadratic blend inside the knee region
            const float t = (over + halfKnee) / knee_; // 0 → 1
            targetGR = (1.0f / ratio_ - 1.0f) * (t * t) * halfKnee;
        } else if (over >= halfKnee) {
            targetGR = over * (1.0f / ratio_ - 1.0f);
        }

        // Attack / release envelope on GR signal
        if (!programRel_) {
            // Single-TC path (default, bit-identical to the original).
            if (targetGR < grState_)
                grState_ = attackCoeff_  * grState_ + (1.0f - attackCoeff_)  * targetGR;
            else
                grState_ = releaseCoeff_ * grState_ + (1.0f - releaseCoeff_) * targetGR;
        } else {
            // Program-dependent release: fast + slow envelopes; take the greater
            // reduction (more-negative GR). Attack is shared/fast for both.
            if (targetGR < grState_)
                grState_ = attackCoeff_ * grState_ + (1.0f - attackCoeff_) * targetGR;
            else
                grState_ = releaseCoeff_ * grState_ + (1.0f - releaseCoeff_) * targetGR;
            if (targetGR < grSlow_)
                grSlow_ = attackCoeff_ * grSlow_ + (1.0f - attackCoeff_) * targetGR;
            else
                grSlow_ = slowRelCoeff_ * grSlow_ + (1.0f - slowRelCoeff_) * targetGR;
            grState_ = std::min(grState_, grSlow_);
        }

        const float gainLin = std::pow(10.0f, grState_ * 0.05f) * makeup_;
        return x * gainLin;
    }

    float getGainReductionDb() const noexcept { return grState_; }

private:
    void recalcTimeConstants() noexcept {
        if (fs_ <= 0.0) return;
        const float rmsWindow = 0.010f; // 10 ms RMS window
        rmsCoeff_     = std::exp(-1.0f / (float)(fs_ * rmsWindow));
        attackCoeff_  = std::exp(-1.0f / (float)(fs_ * attackTime_));
        releaseCoeff_ = std::exp(-1.0f / (float)(fs_ * releaseTime_));
        slowRelCoeff_ = std::exp(-1.0f / (float)(fs_ * releaseTime_ * 4.0f)); // program-dependent slow tail
        if (scHz_ > 0.0f) scHP_.setCoeffs(Filters::highpass(scHz_, 0.707, fs_));
    }

    double fs_ = 44100.0;

    float threshold_   = -18.0f;
    float ratio_       = 4.0f;
    float knee_        = 6.0f;
    float attackTime_  = 0.010f;
    float releaseTime_ = 0.100f;
    float makeup_      = 1.0f;

    float rmsCoeff_     = 0.0f;
    float attackCoeff_  = 0.0f;
    float releaseCoeff_ = 0.0f;
    float slowRelCoeff_ = 0.0f;

    float rmsState_ = 0.0f;
    float grState_  = 0.0f;
    float grSlow_   = 0.0f;

    // Sidechain high-pass (detector only) + program-dependent release. Both
    // default OFF so an untouched VCACompressor is bit-identical to the original.
    BiquadFilter scHP_;
    float        scHz_       = 0.0f;
    bool         programRel_ = false;
};
