#pragma once
#include "DelayBase.h"
#include "BiquadFilter.h"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

// ── Analog BBD delay (2026-10-18) ───────────────────────────────────────────
//
// A bucket-brigade echo built from the published circuit of the classic
// compander BBD pedals (Boss DM-2 audio path; the 8192-stage line of the
// two-chip Memory Man family for the longer reach):
//
//   x + fb -> op-amp headroom -> NE570 COMPRESSOR (2:1, feedback form,
//   average detector, tau 10k*220n = 2.2 ms) -> PRE-FILTER (fixed 3rd-order
//   low-pass, -3 dB ~3.2 kHz) -> BBD: N/2 buckets clocked at
//   f_clk = N / (2 * delay), sampled + held, slight bucket nonlinearity and
//   noise -> POST-FILTER (3rd + 2nd order "corner correction", fixed) ->
//   NE570 EXPANDER (1:2) -> coupling-cap HP -> wet. The regen is tapped at the
//   expander output and summed back BEFORE the compressor, so every repeat
//   passes the whole chain again: each echo is darker and grainier than the last.
//
// The bucket line runs at an internal 4x rate (192 kHz at 48 k) with a real
// clock: the held BBD output goes through the post-filter at that rate before
// decimation, exactly like the analog filter ahead of an ADC. So the BBD's own
// aliasing/imaging appears where the hardware puts it — long delays (low clock)
// get gritty, short ones stay clean — and changing Time glides the pitch.
//
// Compander: the expander undoes the compressor only where the filters let the
// signal through, so transients "breathe" and the BBD hiss is pushed down in
// quiet passages — the dynamic character of these pedals.
//
// Parameters: timeMs [20, 550], feedback [0, 0.98], mix [0, 1], stereoWidth [0, 1]
// (R clock 3 % slower at full width; 0 = both channels identical, the pedal is mono).
class BbdDelay final : public DelayBase {
public:
    static constexpr int   kMaxCh      = 2;
    static constexpr int   kOS         = 4;        // internal bucket-line rate multiple
    static constexpr int   kStages     = 8192;     // 2 x 4096-stage BBD
    static constexpr int   kBuckets    = kStages / 2;
    static constexpr float kMinMs      = 20.0f;
    static constexpr float kMaxMs      = 550.0f;

    void prepare(double sr, int maxBlock, int numCh) override {
        sampleRate_ = sr; maxBlockSize_ = maxBlock; numChannels_ = numCh;
        const double fsi = sr * kOS;
        for (auto& c : ch_) {
            c.pre1.setCoeffs(Filters::lowpass(3200.0, 1.0, fsi));    // Sallen-Key pair, mild peak
            c.pre2.setCoeffs(Filters::lowpass1pole(5000.0, fsi));
            c.post1.setCoeffs(Filters::lowpass(4000.0, 0.9, fsi));
            c.post2.setCoeffs(Filters::lowpass1pole(5500.0, fsi));
            c.post3.setCoeffs(Filters::lowpass(6000.0, 0.7, fsi));   // corner correction
            c.dcHP.setCoeffs(Filters::highpass1pole(20.0, sr));
            c.bucket.assign(kBuckets, 0.0f);
        }
        timeSmoother_.prepare(static_cast<float>(sr), 60.0f);       // clock glide
        feedbackSmoother_.prepare(static_cast<float>(sr), 5.0f);
        mixSmoother_.prepare(static_cast<float>(sr), 5.0f);
        envA_ = 1.0f - std::exp(-1.0f / (0.0022f * static_cast<float>(sr)));
        reset();
    }

    void reset() noexcept override {
        for (auto& c : ch_) {
            std::fill(c.bucket.begin(), c.bucket.end(), 0.0f);
            c.bw = 0; c.phase = 0.0; c.held = 0.0f; c.prevC = 0.0f; c.preOut = 0.0f;
            c.envC = kRef; c.envE = kRef; c.lastWet = 0.0f; c.noise = 0x2545F491u;
            c.pre1.reset(); c.pre2.reset(); c.post1.reset(); c.post2.reset(); c.post3.reset(); c.dcHP.reset();
        }
        timeSmoother_.setImmediate(std::clamp(timeMs_, kMinMs, kMaxMs));
        feedbackSmoother_.setImmediate(feedback_);
        mixSmoother_.setImmediate(mix_);
    }

    void advanceSmoothing() noexcept override {
        timeSmoother_.tick(std::clamp(timeMs_, kMinMs, kMaxMs));
        feedbackSmoother_.tick(feedback_);
        mixSmoother_.tick(mix_);
    }

    float processSample(float x, int ch) noexcept override {
        auto& s = ch_[ch];
        // regen summed at the input op-amp, which clips softly at its rails
        float u = x + feedbackSmoother_.current() * s.lastWet;
        u = kRail * softClip(u / kRail);
        // NE570 compressor, feedback form: gain from the average of its OUTPUT
        const float c = u * kRef / std::max(s.envC, kFloor);
        s.envC += envA_ * (std::fabs(c) - s.envC);
        // clock for this channel
        float ms = timeSmoother_.current();
        if (ch == 1) ms *= 1.0f + stereoWidth_ * 0.03f;
        const double fclk = kStages / (2.0 * 0.001 * ms);              // Hz
        const double step = fclk / (sampleRate_ * kOS);                // clock ticks per internal sample
        float y = 0.0f;
        for (int j = 1; j <= kOS; ++j) {
            // linear interpolation up to the internal rate; the pre-filter is the AA
            const float xi = s.prevC + (c - s.prevC) * (static_cast<float>(j) / kOS);
            s.preOut = s.pre2.process(s.pre1.process(xi));
            s.phase += step;
            while (s.phase >= 1.0) {                                   // a clock edge: shift one bucket
                s.phase -= 1.0;
                float v = std::clamp(s.preOut, -1.2f, 1.2f);
                v = v - kA * v * v - kB * v * v * v;                   // bucket transfer nonlinearity
                s.noise = s.noise * 1664525u + 1013904223u;
                v += kNoise * (static_cast<float>(s.noise >> 8) * (1.0f / 8388608.0f) - 1.0f);
                s.held = s.bucket[s.bw];                               // oldest charge leaves...
                s.bucket[s.bw] = v;                                    // ...newest enters
                if (++s.bw >= kBuckets) s.bw = 0;
            }
            y = s.post3.process(s.post2.process(s.post1.process(s.held)));   // analog post-filter
        }
        s.prevC = c;
        // NE570 expander: gain from the average of its INPUT
        s.envE += envA_ * (std::fabs(y) - s.envE);
        float wet = y * s.envE / kRef;
        wet = s.dcHP.process(wet);                                     // coupling cap (bucket even-order DC)
        s.lastWet = wet;
        return x + mixSmoother_.current() * wet;                        // dry unity + wet on top
    }

    void setParameter(const std::string& id, float v) noexcept override {
        if      (id == "timeMs")      timeMs_      = std::max(1.0f, v);
        else if (id == "feedback")    feedback_    = std::clamp(v, 0.0f, 0.98f);
        else if (id == "mix")         mix_         = std::clamp(v, 0.0f, 1.0f);
        else if (id == "stereoWidth") stereoWidth_ = std::clamp(v, 0.0f, 1.0f);
    }
    float getParameter(const std::string& id) const noexcept override {
        if (id == "timeMs")      return timeMs_;
        if (id == "feedback")    return feedback_;
        if (id == "mix")         return mix_;
        if (id == "stereoWidth") return stereoWidth_;
        return 0.0f;
    }
    const char* delayName() const noexcept override { return "Analog BBD"; }

private:
    // Compander reference level (unity point) ~ -12 dBFS, rail headroom, detector floor.
    static constexpr float kRef   = 0.25f;
    static constexpr float kRail  = 1.6f;
    static constexpr float kFloor = 1.0e-3f;
    // Bucket transfer polynomial (Raffel & Smith, DAFx-10) scaled for the
    // compressed level range; noise ~ -66 dB re the reference before expansion.
    static constexpr float kA = 1.0f / 16.0f, kB = 1.0f / 36.0f;
    static constexpr float kNoise = 1.2e-4f;

    float timeMs_ = 300.0f, feedback_ = 0.4f, mix_ = 0.3f, stereoWidth_ = 0.0f;
    float envA_ = 0.01f;
    ParamSmoother timeSmoother_, feedbackSmoother_, mixSmoother_;

    struct ChannelState {
        std::vector<float> bucket;
        int      bw = 0;
        double   phase = 0.0;
        float    held = 0.0f, prevC = 0.0f, preOut = 0.0f;
        float    envC = 0.25f, envE = 0.25f, lastWet = 0.0f;
        uint32_t noise = 0x2545F491u;
        BiquadFilter pre1, pre2, post1, post2, post3, dcHP;
    };
    std::array<ChannelState, kMaxCh> ch_;
};
