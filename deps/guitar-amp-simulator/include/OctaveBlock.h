#pragma once
#include "AudioBlock.h"
#include <cmath>
#include <algorithm>

// Octave block — analog-style (Boss OC-2 voicing) sub-octave + octave-up generator
// blended with dry. The sub-octave is a flip-flop divider (a square at half the input
// frequency, gated by the input envelope); the octave-up doubles the isolated
// fundamental (a note-tracking low-pass, its analytic signal squared; upVoice 0 = the
// old full-wave rectifier). Both track MONOPHONICALLY — clean and singing on single
// notes (leads), gnarlier on chords, exactly like a real analog octaver (not a
// polyphonic POG). For Mastodon-style octave leads and synth-y sub textures.
//
// Microtonal shimmer voice ("Angine de Poitrine" quarter-tone):
//   The synth voices above produce EXACT f/2 and 2f (waveform folding), so they can't
//   be nudged off-pitch. A granular pitch-shifter was tried and REJECTED: a 2-grain
//   overlap-add shifter can't render a clean quarter-tone (the crossfading grains just
//   reconstruct the original pitch; the shift smears into sidebands — verified in
//   build-tools/octave_micro_test.cpp, and the reference PitchBlock fails the same way).
//   Instead this is a PITCH-TRACKED single-sideband frequency shifter: a Hilbert
//   quadrature network (two polyphase all-pass paths ~90° apart) modulated by a
//   note-tracked oscillator. The fundamental is tracked from the same zero-crossings
//   the sub-octave uses, so the shift Δ = f0·(2^(cents/1200) − 1) lands the fundamental
//   on the exact 24-TET interval and beats cleanly against the dry note (~6.5 Hz for a
//   quarter-tone at 220 Hz). SSB shifts every partial by the same Δ Hz, so the voice's
//   overtones go slightly inharmonic — a metallic/Gamelan shimmer, on-brand for a
//   24-TET band. Monophonic, like the octaver itself.
//
// Params (0..1 unless noted): up (octave-up level), down (sub-octave level),
//   dry (dry blend), micro (shimmer voice level), interval (0..5 = 24-TET step),
//   mix (the whole pedal against the clean input; 1 = the pedal as set).
class OctaveBlock : public AudioBlock {
public:
    // 24-TET microtonal intervals for the shimmer voice, in cents. Odd quarter-tone
    // steps (50/150/350/850) are the intervals a normal 12-TET guitar can't fret.
    static constexpr int kNumIntervals = 6;
    static constexpr float kIntervalCents[kNumIntervals] = {
        +50.0f,    // 0: quarter-tone up (default) — the classic beating shimmer
        -50.0f,    // 1: quarter-tone down
        +150.0f,   // 2: neutral 2nd  (three-quarter tone)
        +350.0f,   // 3: neutral 3rd
        +850.0f,   // 4: neutral 6th
        +1250.0f,  // 5: octave + quarter-tone (airy high shimmer)
    };

    void prepare(double sr, int /*maxBlock*/, int /*nch*/) override {
        sampleRate = sr; fs_ = static_cast<float>(sr);
        const float twoPi = 2.0f * 3.14159265f;
        envA_   = 1.0f - std::exp(-1.0f / (0.005f * fs_));        // env attack 5 ms
        envR_   = 1.0f - std::exp(-1.0f / (0.060f * fs_));        // env release 60 ms
        lpInA_  = 1.0f - std::exp(-twoPi * 350.0f / fs_);         // isolate the fundamental
        subLPA_ = 1.0f - std::exp(-twoPi * 500.0f / fs_);         // round the square sub
        upLPA_  = 1.0f - std::exp(-twoPi * 110.0f / fs_);         // DC tracker for octave-up
        recomputeRatio();
        for (auto& c : ch_) c = {};
    }
    void process(float** in, float** out, int n, int nch) override {
        if (bypassed) { copyBlock(in, out, n, nch); return; }
        const int chs = std::min(nch, 2);
        const bool doMicro = micro_ > 1.0e-4f;
        const float twoPiOverFs = 2.0f * 3.14159265f / fs_;
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < chs; ++c) {
                auto& s = ch_[c];
                const float x = in[c][i];
                const float r = std::fabs(x);
                if (r > s.env) s.env += envA_ * (r - s.env);
                else           s.env += envR_ * (r - s.env);

                s.lpIn += lpInA_ * (x - s.lpIn);
                const float lp = s.lpIn;

                // Rising crossing: drives BOTH the sub-octave flip-flop and the
                // fundamental-pitch tracker. Schmitt trigger with envelope-proportional
                // hysteresis (deglitch_): fire when lp rises past +h, re-arm when it
                // falls below −h — so a strong 2nd harmonic (neck humbucker) can't
                // double-trigger the divider and drop the sub for a cycle. Also reject
                // an implied period < 0.6× the tracked one (a residual glitch).
                // deglitch_=0 → h=0 → the old rising-zero-crossing behaviour.
                const float h = deglitch_ * kHystFrac * s.env;
                bool rising = false;
                if      (!s.armed && lp >  h) { rising = true; s.armed = true; }
                else if ( s.armed && lp < -h) { s.armed = false; }
                if (rising && deglitch_ > 0.0f && s.f0 > 0.0f
                    && static_cast<float>(s.sinceCross) < 0.6f * (fs_ / s.f0))
                    rising = false;

                if (rising) {
                    if (s.env > 0.003f && s.sinceCross > 8 && s.sinceCross < static_cast<int>(fs_ / 40.0f)) {
                        const float f0raw = fs_ / static_cast<float>(s.sinceCross);   // 40..~6000 Hz window
                        s.f0 = (s.f0 <= 0.0f) ? f0raw : s.f0 + 0.30f * (f0raw - s.f0);
                    }
                    s.sinceCross = 0;
                    s.flip = -s.flip;
                } else {
                    ++s.sinceCross;
                }
                s.prevLp = lp;

                const float subRaw = s.flip * s.env;
                s.subLP += subLPA_ * (subRaw - s.subLP);
                const float sub = s.subLP;

                // Octave-up: full-wave rectified fundamental (2× freq), DC-blocked.
                // upVoice_ 1 (2026-11-27, a tester: "the octave up didn't sound like an octave
                // up"): the rectifier (upVoice 0, below) passes the played note itself at about -6 dB under the
                // octave (the 350 Hz one-pole leaves the 2nd/3rd harmonics in, and rectifying a
                // mix makes difference tones at f). Now: a 4-pole low-pass that follows the
                // tracked note isolates the fundamental, and squaring its analytic signal
                // (z^2/|z|: the same Hilbert pair as the shimmer) doubles every frequency, so no
                // component lands back on f; |z| keeps the envelope.
                float up;
                if (upVoice_ > 0.5f) {
                    if (s.upFc != s.f0) {   // retune on a new pitch only (a tan per note)
                        s.upFc = s.f0;
                        const float fc = s.f0 > 0.0f ? std::min(std::max(1.25f * s.f0, 60.0f), 1500.0f) : 1000.0f;
                        const float g = std::tan(3.14159265f * fc / fs_);
                        s.upA1 = 1.0f / (1.0f + g * (g + 1.41421356f));
                        s.upA2 = g * s.upA1; s.upA3 = g * s.upA2;
                    }
                    float f = x;
                    for (int k = 0; k < 2; ++k) {   // TPT state-variable low-pass, Q 0.707
                        const float v3 = f - s.upIc2[k];
                        const float v1 = s.upA1 * s.upIc1[k] + s.upA2 * v3;
                        const float v2 = s.upIc2[k] + s.upA2 * s.upIc1[k] + s.upA3 * v3;
                        s.upIc1[k] = 2.0f * v1 - s.upIc1[k];
                        s.upIc2[k] = 2.0f * v2 - s.upIc2[k];
                        f = v2;
                    }
                    const float uI = hilbert(s.apUI, kApI, s.upZ1);
                    const float uQ = hilbert(s.apUQ, kApQ, f);
                    s.upZ1 = f;
                    const float a = std::sqrt(uI * uI + uQ * uQ);
                    up = kUpGain * (uI * uI - uQ * uQ) / (a + 1.0e-6f);
                } else {
                    const float upRaw = std::fabs(lp) * 2.0f;
                    s.upLP += upLPA_ * (upRaw - s.upLP);
                    up = upRaw - s.upLP;
                }

                // Microtonal shimmer via single-sideband frequency shift.
                float shimmer = 0.0f;
                if (doMicro) {
                    // Quadrature (Hilbert) network. The unit delay must sit on the I
                    // path: verified (scratchpad hilbert_phase) that this holds a clean
                    // +90° (Q leads I) across 80 Hz–5 kHz, whereas delaying Q collapses to
                    // ~0° above 2 kHz and flips the shift direction with frequency.
                    const float I = hilbert(s.apI, kApI, s.hilbZ1);
                    const float Q = hilbert(s.apQ, kApQ, x);
                    s.hilbZ1 = x;
                    // Note-tracked shift oscillator. Δ = f0·(ratio−1). Smoothly ramp the
                    // per-sample rotation so pitch changes glide instead of clicking.
                    const float dTarget = s.f0 * (microRatio_ - 1.0f);
                    s.shift += 0.002f * (dTarget - s.shift);
                    const float dTheta = twoPiOverFs * s.shift;
                    s.oscPh += dTheta;
                    if (s.oscPh >  3.14159265f) s.oscPh -= 6.28318531f;
                    if (s.oscPh < -3.14159265f) s.oscPh += 6.28318531f;
                    const float cw = std::cos(s.oscPh), sw = std::sin(s.oscPh);
                    // Upper-sideband select (Q leads I by 90°, so +Q·sin shifts UP by Δ);
                    // negative Δ shifts down.
                    shimmer = I * cw + Q * sw;
                }

                const float y = dry_ * x + down_ * sub * 1.2f + up_ * up * 1.4f + micro_ * shimmer;
                out[c][i] = y + (1.0f - mix_) * (x - y);   // Mix 1: exactly y
            }
            for (int c = chs; c < nch; ++c) if (in[c] != out[c]) out[c][i] = in[c][i];
        }
    }
    void setParameter(const std::string& id, float v) override {
        if (id == "interval") {
            int iv = static_cast<int>(v + 0.5f);
            interval_ = iv < 0 ? 0 : (iv >= kNumIntervals ? kNumIntervals - 1 : iv);
            recomputeRatio();
            return;
        }
        v = std::max(0.0f, std::min(1.0f, v));
        if      (id == "up")       up_ = v;
        else if (id == "down")     down_ = v;
        else if (id == "dry")      dry_ = v;
        else if (id == "micro")    micro_ = v;
        else if (id == "deglitch") deglitch_ = v;   // divider hysteresis (default on)
        else if (id == "upVoice")  upVoice_ = v;    // 1 = clean doubler (default), 0 = the old rectifier
        else if (id == "mix")      mix_ = v;
    }
    float getParameter(const std::string& id) const override {
        if (id == "up")       return up_;
        if (id == "down")     return down_;
        if (id == "dry")      return dry_;
        if (id == "micro")    return micro_;
        if (id == "interval") return static_cast<float>(interval_);
        if (id == "deglitch") return deglitch_;
        if (id == "upVoice")  return upVoice_;
        if (id == "mix")      return mix_;
        return 0.0f;
    }
private:
    static constexpr float kHystFrac = 0.12f;   // Schmitt hysteresis as a fraction of env
    static constexpr float kUpGain   = 1.0f;    // the doubler's level, matched to the rectifier's
    // 2nd-order all-pass: H(z) = (a − z^-2)/(1 − a z^-2).
    struct AP2 { float x1=0, x2=0, y1=0, y2=0; };
    // Polyphase-IIR Hilbert half-band all-pass coefficients (8-pole, ~60 Hz–8 kHz 90°).
    static constexpr float kApI[4] = {0.6923877778f, 0.9360654323f, 0.9882295227f, 0.9987488453f};
    static constexpr float kApQ[4] = {0.4021921162f, 0.8561710882f, 0.9722909546f, 0.9952884791f};

    static inline float ap2(AP2& s, float in, float a) noexcept {
        const float y = a * (in + s.y2) - s.x2;
        s.x2 = s.x1; s.x1 = in; s.y2 = s.y1; s.y1 = y;
        return y;
    }
    static inline float hilbert(AP2 chain[4], const float coef[4], float x) noexcept {
        float y = x;
        for (int k = 0; k < 4; ++k) y = ap2(chain[k], y, coef[k]);
        return y;
    }

    void recomputeRatio() noexcept {
        microRatio_ = std::pow(2.0f, kIntervalCents[interval_] / 1200.0f);
    }

    float fs_ = 48000.0f;
    float up_ = 0.0f, down_ = 0.5f, dry_ = 1.0f, micro_ = 0.0f, mix_ = 1.0f;
    float deglitch_ = 1.0f;   // divider hysteresis ON by default (set 0 for the old divider)
    float upVoice_  = 1.0f;   // octave-up voice: 1 = tracked low-pass + analytic doubler, 0 = rectifier
    int   interval_ = 0;
    float microRatio_ = 1.0f;
    float envA_ = 0.0f, envR_ = 0.0f, lpInA_ = 0.0f, subLPA_ = 0.0f, upLPA_ = 0.0f;
    struct ChannelState {
        float env = 0.0f, lpIn = 0.0f, prevLp = 0.0f, flip = 1.0f, subLP = 0.0f, upLP = 0.0f;
        bool  armed = false;
        int   sinceCross = 0;
        float f0 = 0.0f, shift = 0.0f, oscPh = 0.0f, hilbZ1 = 0.0f;
        AP2   apI[4], apQ[4];
        float upFc = -1.0f, upA1 = 0.0f, upA2 = 0.0f, upA3 = 0.0f, upZ1 = 0.0f;   // octave-up doubler
        float upIc1[2] = {0.0f, 0.0f}, upIc2[2] = {0.0f, 0.0f};
        AP2   apUI[4], apUQ[4];
    };
    ChannelState ch_[2];
};
