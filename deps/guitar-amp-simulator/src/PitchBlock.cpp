#include "PitchBlock.h"
#include <algorithm>
#include <cmath>

void PitchBlock::prepare(double sr, int maxBlock, int numCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = numCh;

    // Cap grain so that max lookback (4× grain for Up2Oct) stays inside the buffer.
    grainSamples_ = std::min(
        static_cast<int>(kGrainMs / 1000.0 * sr),
        kBufSize / 8);    // kBufSize/8 guarantees 4× lookback fits in buffer

    phaseInc_ = 1.0f / static_cast<float>(grainSamples_);

    // Alignment search limits.  A restart may move up to one period towards the
    // write head and then read a correlation window from there; the grain's
    // minimum delay is one grain length, so period <= grain/2 and window <=
    // grain/4 keep both the window and the whole grain behind the write head.
    const float fs = static_cast<float>(sr);
    maxLag_ = std::max(2, std::min(static_cast<int>(fs / 40.0f), grainSamples_ / 2));
    maxWin_ = std::max(1, grainSamples_ / 4);
    minWin_ = std::min(maxWin_, std::max(1, static_cast<int>(0.003f * fs)));
    defLag_ = std::min(maxLag_, std::max(2, static_cast<int>(0.010f * fs)));
    envA_   = 1.0f - std::exp(-1.0f / (0.005f * fs));                    // env attack 5 ms
    envR_   = 1.0f - std::exp(-1.0f / (0.060f * fs));                    // env release 60 ms
    lpA_    = 1.0f - std::exp(-kTwoPi * 350.0f / fs);                    // isolate the fundamental

    for (int ch = 0; ch < kMaxCh; ++ch) {
        buf_[ch].assign(kBufSize, 0.0f);
        writeHead_[ch] = 0;
        trk_[ch] = Tracker{};
    }

    recomputeRatio();
    resetGrains();
}

// ── Internal helpers ─────────────────────────────────────────────────────────

void PitchBlock::recomputeRatio() noexcept {
    const float target = kModeRatios[mode_];
    const float swept  = 1.0f + (target - 1.0f) * expression_;
    // Apply cent trim: multiply by 2^(cents/1200).
    // At 0 cents this is exactly 1.0.  ±50 cents = ±quarter-tone adjustment.
    const float centsMult = std::pow(2.0f, cents_ / 1200.0f);
    pitchRatio_ = swept * centsMult;
}

void PitchBlock::resetGrains() noexcept {
    // lookback guarantees the read head never overtakes the write head.
    // For pitch up (ratio > 1) the read head moves faster, so we must start
    // far enough behind that it finishes exactly at the write head each grain.
    const float lookback = std::max(1.0f, pitchRatio_) * static_cast<float>(grainSamples_);
    const int   lb       = static_cast<int>(lookback);

    for (int ch = 0; ch < kMaxCh; ++ch) {
        // Grain 0: phase starts at 0 (Hann window = 0, rising edge).
        grains_[ch][0].phase    = 0.0f;
        grains_[ch][0].readHead = static_cast<float>(
            (writeHead_[ch] - lb + kBufSize) & kBufMask);

        // Grain 1: phase starts at 0.5 (Hann window = 1, at its peak).
        // Starting half a lookback behind places it midway through its grain,
        // giving the correct COLA phase relationship from the first sample.
        grains_[ch][1].phase    = 0.5f;
        grains_[ch][1].readHead = static_cast<float>(
            (writeHead_[ch] - lb / 2 + kBufSize) & kBufMask);
    }
}

void PitchBlock::trackPeriod(float x, int ch) noexcept {
    // Same estimator as OctaveBlock: rising crossings of the low-passed input,
    // Schmitt hysteresis proportional to the envelope so a strong 2nd harmonic
    // can't double-trigger, crossings implying < 0.6x the tracked period rejected.
    Tracker& t = trk_[ch];
    const float fs = static_cast<float>(sampleRate);
    const float r  = std::fabs(x);
    t.env += (r > t.env ? envA_ : envR_) * (r - t.env);
    t.lp  += lpA_ * (x - t.lp);

    const float h = kHystFrac * t.env;
    bool rising = false;
    if      (!t.armed && t.lp >  h) { rising = true; t.armed = true; }
    else if ( t.armed && t.lp < -h) { t.armed = false; }
    if (rising && t.f0 > 0.0f && static_cast<float>(t.sinceCross) < 0.6f * (fs / t.f0))
        rising = false;

    if (rising) {
        if (t.env > 0.003f && t.sinceCross > 8 && t.sinceCross < static_cast<int>(fs / 40.0f)) {
            const float f0raw = fs / static_cast<float>(t.sinceCross);
            t.f0 = (t.f0 <= 0.0f) ? f0raw : t.f0 + 0.30f * (f0raw - t.f0);
        }
        t.sinceCross = 0;
    } else if (t.sinceCross < kBufSize) {
        ++t.sinceCross;
    }
}

float PitchBlock::alignedStart(int ch, float nominal, float ref) const noexcept {
    // WSOLA-style restart (Verhelst & Roelands 1993): among start offsets
    // lag in [0, period) ahead of the nominal start, pick the one whose next
    // `win` input samples best match the samples the still-playing grain is
    // about to read (normalised cross-correlation r^2/E with r > 0).  Both
    // grains then advance at the same rate, so a match at the restart holds
    // through the whole crossfade.  Searching one full period covers every
    // phase; searching only towards the write head never adds delay.
    //
    // Cost is bounded: <= kCorrTaps taps per lag (window decimated to fit),
    // at most kCoarseLags coarse lags across the period, then 2 lags per
    // halving of the grid step around the winner down to one sample.
    const float* b = buf_[ch].data();
    const Tracker& t = trk_[ch];

    int period = defLag_;
    if (t.f0 > 0.0f)
        period = static_cast<int>(static_cast<float>(sampleRate) / t.f0 + 0.5f);
    period = std::clamp(period, 2, maxLag_);

    const int win    = std::clamp(period, minWin_, maxWin_);
    const int stride = (win + kCorrTaps - 1) / kCorrTaps;
    const int taps   = std::max(1, win / stride);

    // Candidates keep the reference grain's fractional position so the two read
    // heads stay a whole number of samples apart; lag 0 is the first such
    // position that is not behind the nominal start.
    const float refFloor = std::floor(ref);
    const float refFrac  = ref - refFloor;
    const int   r0 = static_cast<int>(refFloor) & kBufMask;
    int c0 = static_cast<int>(nominal);
    if (static_cast<float>(c0) + refFrac < nominal) ++c0;
    c0 &= kBufMask;

    float bestScore = 0.0f;
    int   bestLag   = -1;
    auto  tryLag = [&](int lag) noexcept {
        float rr = 0.0f, ee = 0.0f;
        int ia = r0, ib = (c0 + lag) & kBufMask;
        for (int k = 0; k < taps; ++k) {
            const float c = b[ib];
            rr += b[ia] * c;
            ee += c * c;
            ia = (ia + stride) & kBufMask;
            ib = (ib + stride) & kBufMask;
        }
        if (rr > 0.0f) {
            const float s = rr * rr / (ee + 1.0e-20f);
            if (s > bestScore) { bestScore = s; bestLag = lag; }
        }
    };

    const int step = (period + kCoarseLags - 1) / kCoarseLags;
    for (int lag = 0; lag < period; lag += step) tryLag(lag);
    if (bestLag < 0) return nominal;   // silence / no positive match: keep the nominal start

    // Refine by halving the grid step around the running best (2 lags per level).
    for (int s = step / 2; s >= 1; s /= 2) {
        const int centre = bestLag;
        if (centre - s >= 0)     tryLag(centre - s);
        if (centre + s < period) tryLag(centre + s);
    }

    return static_cast<float>((c0 + bestLag) & kBufMask) + refFrac;
}

float PitchBlock::processOneSample(float x, int ch) noexcept {
    // Write new sample into ring buffer.
    buf_[ch][writeHead_[ch]] = x;
    writeHead_[ch] = (writeHead_[ch] + 1) & kBufMask;
    if (align_) trackPeriod(x, ch);

    const float lookback = std::max(1.0f, pitchRatio_) * static_cast<float>(grainSamples_);
    float wet = 0.0f;

    for (int g = 0; g < kNumGrains; ++g) {
        // Hann window: 0.5*(1 - cos(2π*phase)).
        // Two grains offset by 0.5 always sum to exactly 1.0 (COLA).
        const float w = 0.5f * (1.0f - std::cos(kTwoPi * grains_[ch][g].phase));

        // Linear interpolation in the ring buffer.
        const float rh   = grains_[ch][g].readHead;
        const int   i0   = static_cast<int>(rh) & kBufMask;
        const int   i1   = (i0 + 1) & kBufMask;
        const float frac = rh - std::floor(rh);
        wet += w * (buf_[ch][i0] * (1.0f - frac) + buf_[ch][i1] * frac);

        // Advance read head at pitch ratio and wrap within the ring buffer.
        grains_[ch][g].readHead += pitchRatio_;
        if (grains_[ch][g].readHead >= static_cast<float>(kBufSize))
            grains_[ch][g].readHead -= static_cast<float>(kBufSize);

        // Advance grain phase; on completion reset the read head near the write head.
        grains_[ch][g].phase += phaseInc_;
        if (grains_[ch][g].phase >= 1.0f) {
            grains_[ch][g].phase -= 1.0f;
            float newHead = static_cast<float>(writeHead_[ch]) - lookback;
            // Wrap into [0, kBufSize)
            while (newHead < 0.0f)                        newHead += static_cast<float>(kBufSize);
            while (newHead >= static_cast<float>(kBufSize)) newHead -= static_cast<float>(kBufSize);
            // Unity ratio: both grains read at the same fixed delay, already in phase.
            if (align_ && pitchRatio_ != 1.0f) {
                // Partner grain's read position on the sample this grain restarts at
                // (grain 0 restarts before grain 1 has advanced this sample).
                const int o = g ^ 1;
                float ref = grains_[ch][o].readHead + (o > g ? pitchRatio_ : 0.0f);
                if (ref >= static_cast<float>(kBufSize)) ref -= static_cast<float>(kBufSize);
                newHead = alignedStart(ch, newHead, ref);
            }
            grains_[ch][g].readHead = newHead;
        }
    }

    return wet;
}

// ── AudioBlock interface ─────────────────────────────────────────────────────

void PitchBlock::process(float** in, float** out, int numSamples, int numCh) {
    if (bypassed) { copyBlock(in, out, numSamples, numCh); return; }

    const int   chCount = std::min(numCh, kMaxCh);
    const float mix     = mix_;

    for (int ch = 0; ch < chCount; ++ch) {
        for (int i = 0; i < numSamples; ++i) {
            const float dry = in[ch][i];
            const float wet = processOneSample(dry, ch);
            out[ch][i] = dry + mix * (wet - dry);   // equivalent to (1-mix)*dry + mix*wet
        }
    }

    // Pass through any extra channels unmodified.
    for (int ch = chCount; ch < numCh; ++ch)
        if (in[ch] != out[ch])
            for (int i = 0; i < numSamples; ++i)
                out[ch][i] = in[ch][i];
}

void PitchBlock::setParameter(const std::string& id, float value) {
    if (id == "mode") {
        const int m = std::clamp(static_cast<int>(value), 0, 4);
        if (m != mode_) { mode_ = m; recomputeRatio(); }
    } else if (id == "expression") {
        expression_ = std::clamp(value, 0.0f, 1.0f);
        recomputeRatio();
    } else if (id == "mix") {
        mix_ = std::clamp(value, 0.0f, 1.0f);
    } else if (id == "cents") {
        cents_ = std::clamp(value, -50.0f, 50.0f);
        recomputeRatio();
    } else if (id == "align") {
        align_ = value >= 0.5f;
    }
}

float PitchBlock::getParameter(const std::string& id) const {
    if (id == "mode")       return static_cast<float>(mode_);
    if (id == "expression") return expression_;
    if (id == "mix")        return mix_;
    if (id == "cents")      return cents_;
    if (id == "align")      return align_ ? 1.0f : 0.0f;
    return 0.0f;
}
