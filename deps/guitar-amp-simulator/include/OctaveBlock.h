#pragma once
#include "AudioBlock.h"
#include <cmath>
#include <algorithm>

// Octave block — analog-style (Boss OC-2 voicing) sub-octave + octave-up generator
// blended with dry. The sub-octave is a flip-flop divider (a square at half the input
// frequency, gated by the input envelope); the octave-up is a full-wave rectifier of
// the isolated fundamental. Both track MONOPHONICALLY — clean and singing on single
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
//   dry (dry blend), micro (shimmer voice level), interval (0..5 = 24-TET step).
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

                // Rising zero-crossing: drives BOTH the sub-octave flip-flop and the
                // fundamental-pitch tracker for the shimmer.
                const bool rising = (lp > 0.0f && s.prevLp <= 0.0f);
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
                const float upRaw = std::fabs(lp) * 2.0f;
                s.upLP += upLPA_ * (upRaw - s.upLP);
                const float up = upRaw - s.upLP;

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

                out[c][i] = dry_ * x + down_ * sub * 1.2f + up_ * up * 1.4f + micro_ * shimmer;
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
        if      (id == "up")    up_ = v;
        else if (id == "down")  down_ = v;
        else if (id == "dry")   dry_ = v;
        else if (id == "micro") micro_ = v;
    }
    float getParameter(const std::string& id) const override {
        if (id == "up")       return up_;
        if (id == "down")     return down_;
        if (id == "dry")      return dry_;
        if (id == "micro")    return micro_;
        if (id == "interval") return static_cast<float>(interval_);
        return 0.0f;
    }
private:
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
    float up_ = 0.0f, down_ = 0.5f, dry_ = 1.0f, micro_ = 0.0f;
    int   interval_ = 0;
    float microRatio_ = 1.0f;
    float envA_ = 0.0f, envR_ = 0.0f, lpInA_ = 0.0f, subLPA_ = 0.0f, upLPA_ = 0.0f;
    struct ChannelState {
        float env = 0.0f, lpIn = 0.0f, prevLp = 0.0f, flip = 1.0f, subLP = 0.0f, upLP = 0.0f;
        int   sinceCross = 0;
        float f0 = 0.0f, shift = 0.0f, oscPh = 0.0f, hilbZ1 = 0.0f;
        AP2   apI[4], apQ[4];
    };
    ChannelState ch_[2];
};
