#pragma once
#include "AudioBlock.h"
#include "AdaaSoftClip.h"
#include "BiquadFilter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Claw — a four-mode noise maker that rides the guitar signal.
//
//   0 Hiss       pick attacks fire bursts of pink/white noise through a resonant band-pass
//   1 Howl       a short delay loop with a band-pass and a clipper inside; self-oscillates
//                once Feed passes ~70 %, and the guitar pushes/pulls the howl
//   2 Butterfly  a Lorenz attractor ring-modulates the guitar; Pitch sets its instability
//                (below the chaotic threshold it sits still until the guitar kicks it)
//   3 Shortwave  a single-sideband heterodyne whistle with AM static riding the signal
//
// Controls (all 0..1): mode, feed, pitch, grit, blend, level, grab (toggle). Grab freezes
// the Howl loop / holds the Hiss burst / lets the attractor free-run / stops the station
// drifting — footswitch-friendly.
//
// A direct AudioBlock at 1x rate: no oversampling, no worker rebuild. Every nonlinearity
// is the first-order ADAA soft clip (AdaaSoftClip.h), the output stage is DC blocker +
// ADAA ceiling at -1 dBFS + a finite-state guard. Everything random comes from one
// xorshift per channel seeded in prepare() (distinct per channel), so a render is
// bit-reproducible — the Hex Forge golden hashes exact output bits.
class ClawNoise : public AudioBlock {
public:
    static constexpr int kNumModes = 4;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(float** in, float** out, int numSamples, int numChannels) override;
    void setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    // Zero the audible memory (loop, burst, attractor, filters) without allocating —
    // what Hex Forge calls when the block re-engages after a switch.
    void clearTails() noexcept;

private:
    // ── primitives (local copies: the block must not drag the drum kit or the reverb in) ──
    struct Rng {                                   // xorshift32, as hexdrums::Rng
        uint32_t s = 0x9E3779B9u;
        void reseed(uint32_t seed) noexcept { s = seed ? seed : 1u; }
        uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float white() noexcept { return static_cast<float>(static_cast<int32_t>(next())) * (1.0f / 2147483648.0f); }
    };
    struct Svf {                                   // TPT state-variable filter (WahBlock topology)
        float ic1 = 0.0f, ic2 = 0.0f;
        // returns the band-pass (unity peak when scaled by k), g = tan(pi fc/fs), k = damping
        float bp(float x, float g, float k) noexcept {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v1 * k;
        }
        void clear() noexcept { ic1 = ic2 = 0.0f; }
    };
    struct DelayLine {                             // as PlateReverbBlock::DelayLine (write + linear read)
        std::vector<float> buf;
        int writeIdx = 0;
        void resize(int len) { buf.assign(len + 8, 0.0f); writeIdx = 0; }
        void clear() noexcept { std::fill(buf.begin(), buf.end(), 0.0f); writeIdx = 0; }
        void write(float v) noexcept { buf[writeIdx] = v; if (++writeIdx >= static_cast<int>(buf.size())) writeIdx = 0; }
        void advance() noexcept { if (++writeIdx >= static_cast<int>(buf.size())) writeIdx = 0; }   // Grab: hold the content, keep cycling
        float readFrac(float delay) const noexcept {
            const int len  = static_cast<int>(buf.size());
            const int d0   = static_cast<int>(delay);
            const float fr = delay - static_cast<float>(d0);
            const int i0   = (writeIdx - d0     + len * 2) % len;
            const int i1   = (writeIdx - d0 - 1 + len * 2) % len;
            return buf[i0] + fr * (buf[i1] - buf[i0]);
        }
    };
    struct AP2 { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };   // 2nd-order all-pass (OctaveBlock's Hilbert network)
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

    struct Ch {
        Rng   rng;
        // shared front end
        float env = 0.0f, envSlow = 0.0f;
        int   refr = 0;
        // Hiss
        float burst = 0.0f, burstTarget = 0.0f; bool burstRising = false;
        float pb0 = 0.0f, pb1 = 0.0f, pb2 = 0.0f;       // Kellett pink state
        Svf   hissBp;
        AdaaSoftClip hissClip;
        // Howl
        DelayLine loop;
        float lenCur = 100.0f;
        Svf   howlBp;
        AdaaSoftClip loopClip;
        float loopOut = 0.0f;
        // Butterfly
        double lx = 1.0, ly = 1.0, lz = 1.0;
        AdaaSoftClip ringClip;
        // Shortwave
        AP2   apI[4], apQ[4];
        float hilbZ1 = 0.0f, carPh = 0.0f, walk = 0.0f, noiseLp = 0.0f, noiseHeld = 0.0f;
        AdaaSoftClip swClip;
        // output
        float blendCur = 0.5f, gainCur = 1.0f;
        BiquadFilter dc;
        AdaaSoftClip ceil;
    };
    void resetChannel(Ch& c, int idx) noexcept;   // full re-init incl. reseed (prepare / blow-up)

    Ch     ch_[2];
    float  fs_ = 48000.0f;
    int    mode_ = 1, modeApplied_ = -1;
    float  feed_ = 0.5f, pitch_ = 0.5f, grit_ = 0.3f, blend_ = 0.5f, level_ = 0.707f;
    bool   grab_ = false;
    // block-rate coefficients
    float  envA_ = 0.0f, envR_ = 0.0f, envS_ = 0.0f;
    int    refrN_ = 1920;
    float  lpNoiseA_ = 0.0f, walkA_ = 0.0f, dcHzUsed_ = 8.0f;
};
