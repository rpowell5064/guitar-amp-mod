#include "UniVibeEffect.h"
#include <cassert>

static constexpr float kPiF = static_cast<float>(M_PI);

// ── prepare / reset ───────────────────────────────────────────────────────────

void UniVibeEffect::prepare(double sr, int /*maxBlock*/, int /*numChannels*/) {
    sampleRate_ = sr;
    rebuildCoeffs();
    reset();
}

void UniVibeEffect::rebuildCoeffs() noexcept {
    const float sr = static_cast<float>(sampleRate_);

    // Pre-emphasis 1-pole HPF: y[n] = α·(y[n-1] + x[n] - x[n-1])
    hpAlpha_ = std::exp(-2.0f * kPiF * kPreEmphHz / sr);

    // De-emphasis 1-pole LPF: y[n] += α·(x[n] - y[n-1])
    lpAlpha_ = 1.0f - std::exp(-2.0f * kPiF * kDeEmphHz / sr);

    // Lamp rise/fall: α = 1 - exp(-1/(τ·sr))
    riseAlpha_ = 1.0f - std::exp(-1.0f / (kRiseTauS * sr));
    fallAlpha_ = 1.0f - std::exp(-1.0f / (kFallTauS * sr));

    // Depth-change smoothing: 10 ms τ
    depthCoeff_ = 1.0f - std::exp(-1.0f / (0.010f * sr));

    // Capacitor values so each stage hits kApfCtrHz[k] at R_pcell = kRpcellCtr:
    //   f_k = 1/(2π·R·C_k)  →  C_k = 1/(2π·f_k·kRpcellCtr)
    for (int k = 0; k < 4; ++k)
        C_[k] = 1.0f / (2.0f * kPiF * kApfCtrHz[k] * kRpcellCtr);
}

void UniVibeEffect::reset() noexcept {
    lfoPhase_    = 0.0f;
    depthSmooth_ = depth_;
    for (int c = 0; c < kMaxCh; ++c)
        ch_[c] = ChannelState{};  // lampBright initialised to 0.5 by struct default
}

// ── process ───────────────────────────────────────────────────────────────────

void UniVibeEffect::process(float** in, float** out,
                             int numSamples, int numChannels) noexcept {
    const int   chCount = std::min(numChannels, kMaxCh);
    const float sr      = static_cast<float>(sampleRate_);

    const float lfoHz   = kRateMinHz + rate_ * (kRateMaxHz - kRateMinHz);
    const float lfoIncr = lfoHz / sr;

    // Output gain: outputLevel_ ∈ [0,1] → -6..+6 dB (0.5 = 0 dB)
    const float outGain = std::pow(2.0f, (outputLevel_ - 0.5f) * 2.0f);

    for (int i = 0; i < numSamples; ++i) {
        // ── Smooth depth ────────────────────────────────────────────────────
        depthSmooth_ += depthCoeff_ * (depth_ - depthSmooth_);

        // ── Advance global LFO (once per sample) ───────────────────────────
        lfoPhase_ += lfoIncr;
        if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;

        for (int c = 0; c < chCount; ++c) {
            ChannelState& s = ch_[c];

            // ── Per-channel LFO phase (ch1 offset for stereo widening) ─────
            float phi = lfoPhase_;
            if (c == 1) {
                phi += stereoWidth_ * 0.25f;   // 0 = 0°, 1.0 = 90° offset
                if (phi >= 1.0f) phi -= 1.0f;
            }

            // ── Lamp brightness (asymmetric IIR smoothing) ─────────────────
            // Triangle LFO drives lamp target: 0 = dark (high R), 1 = bright (low R)
            const float lampTarget = 0.5f + 0.5f * depthSmooth_ * triangle(phi);
            const float lampAlpha  = (lampTarget > s.lampBright) ? riseAlpha_ : fallAlpha_;
            s.lampBright += lampAlpha * (lampTarget - s.lampBright);

            // ── Photocell resistance ────────────────────────────────────────
            // R = R_min + (1 - L) * (R_max - R_min)
            const float R_pcell = kRpcellMin
                                + (1.0f - s.lampBright) * (kRpcellMax - kRpcellMin);

            // ── Pre-emphasis HPF ────────────────────────────────────────────
            float x       = in[c][i];
            const float dry = x;

            {
                const float hpOut = hpAlpha_ * (s.hpY + x - s.hpXp);
                s.hpXp = x;
                s.hpY  = hpOut;
                x = hpOut;
            }

            // ── 4-stage photocell all-pass ──────────────────────────────────
            // Each stage: y[n] = -a·x[n] + x[n-1] + a·y[n-1]
            // a = (1-g)/(1+g),  g = tan(π·f_k/sr),  f_k = 1/(2π·R·C_k)
            for (int k = 0; k < 4; ++k) {
                const float f_k = 1.0f / (2.0f * kPiF * R_pcell * C_[k]);
                const float a   = apfCoeff(f_k);
                const float y   = -a * x + s.apX[k] + a * s.apY[k];
                s.apX[k] = x;
                s.apY[k] = y;
                x = y;
            }

            // ── De-emphasis LPF ─────────────────────────────────────────────
            s.lpY += lpAlpha_ * (x - s.lpY);
            const float wet = s.lpY;

            // ── Mix (Chorus = wet/dry blend; Vibrato = wet only) ────────────
            const float mixed = vibrato_
                ? wet
                : dry * (1.0f - mix_) + wet * mix_;

            out[c][i] = mixed * outGain;
        }

        // Passthrough channels beyond kMaxCh unchanged
        for (int c = chCount; c < numChannels; ++c)
            if (in[c] != out[c]) out[c][i] = in[c][i];
    }
}

// ── parameters ───────────────────────────────────────────────────────────────

void UniVibeEffect::setParameter(const std::string& id, float v) {
    if      (id == "rate")        rate_        = std::max(0.0f, std::min(1.0f, v));
    else if (id == "depth")       depth_       = std::max(0.0f, std::min(1.0f, v));
    else if (id == "mix")         mix_         = std::max(0.0f, std::min(1.0f, v));
    else if (id == "stereoWidth") stereoWidth_ = std::max(0.0f, std::min(1.0f, v));
    else if (id == "mode")        vibrato_     = (v > 0.5f);
    else if (id == "outputLevel") outputLevel_ = std::max(0.0f, std::min(1.0f, v));
    // "preampOn" from CE-2 is silently ignored here
}

float UniVibeEffect::getParameter(const std::string& id) const {
    if (id == "rate")        return rate_;
    if (id == "depth")       return depth_;
    if (id == "mix")         return mix_;
    if (id == "stereoWidth") return stereoWidth_;
    if (id == "mode")        return vibrato_ ? 1.0f : 0.0f;
    if (id == "outputLevel") return outputLevel_;
    return 0.0f;
}
