#pragma once
#include "AudioBlock.h"
#include "DR_SpringReverb.h"
#include "StudioReverbs.h"
#include <vector>
#include <array>

// Plate reverb using a Schroeder / Moorer-style network:
//   input → pre-delay → 4 series allpass diffusers
//         → 4 parallel modulated comb filters (with damping LP)
//         → stereo output (L = sum of combs 0,2, R = sum of combs 1,3)
//
// Modulation on comb filter delay times creates the lush, smeared density
// characteristic of real plate transducers.
class PlateReverbBlock : public AudioBlock {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(float** in, float** out, int numSamples, int numChannels) override;
    void setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;
    // Clear every tail buffer (2026-07-23: seamless preset/effect switching —
    // a bypassed reverb otherwise holds its tank and replays it on re-engage).
    void reset() noexcept {
        auto clear = [](DelayLine& d) { std::fill(d.buf.begin(), d.buf.end(), 0.0f); d.writeIdx = 0; };
        clear(preDelay);
        for (auto& a : ap) clear(a.dl);
        for (auto& c : combs) { clear(c.dl); c.lastLP = 0.0f; c.shelfZ = 0.0f; }
        clearVelvet();
        for (auto& a : smearAp) clear(a.dl);
        clear(bloomLine);
        bloomLP = inHP = loopHP = 0.0f;
        // Re-seed the comb LFO phases (2026-07-25): free-running phases made every
        // recall land on a different interference pattern — audible tail variance
        // (and ±1 dB measurement jitter on wet ambient presets). Deterministic now.
        for (int i = 0; i < kNumComb; ++i) lfoPhase[i] = lfoPhase0[i];
        studioPlate.clear();
        hall.clear();
    }

private:
    // Parameters
    float preDelayMs = 10.0f;
    float decayTime  =  1.5f; // seconds (RT60)
    float damping    =  0.3f; // 0=bright, 1=dark
    float modDepth   =  0.5f; // LFO depth on comb delays (normalised)
    float modRate    =  0.8f; // Hz
    float mix        =  0.3f;
    bool  dense      = false; // Density switch (see kNumComb comment)
    // Type switch (2026-07-23): 0 = plate tank (everything above), 1 = SPRING —
    // the Accutronics 4EB3 three-spring model already in the codebase (built for
    // the Deluxe Reverb amp), exposed as a block type. Mono tank (authentic),
    // decay/damping/mix map onto the spring's own controls; predelay/mod are
    // plate-only. Default plate = bit-identical.
    bool  springOn   = false;
    float drip       = 0.5f;   // spring dispersion "drip" [0,1] ON by default (item 31); 0 = off
    DR_SpringReverb spring;
    std::vector<float> springBuf;
    // Type 2 (2026-07-25): HEX AMBIENT — Cloudburst/SLO-family ambient tail.
    // Same tank members re-lengthened (prime-staggered ~1.6x plate), plus:
    //   * two long SMEAR allpasses in front (attack softening / diffused wash),
    //   * a band-limited BLOOM recirculation (comb sum -> LP -> ~90 ms line ->
    //     back into the diffusers) so density GROWS over time,
    //   * slow incommensurate per-comb mod (non-pitchy, no chorus wobble),
    //   * loop HP (no mud build-up) + mono-safe width expansion.
    // `bloom` (0..1) drives smear g, recirc gain, mod depth and width together.
    bool  ambientOn  = false;
    float bloom      = 0.5f;
    // monoSum (2026-09-09): the host rig collapses L+R downstream. The plate's
    // L/R tails are incoherent (odd/even comb split), so a plain 0.5*(L+R) sum
    // drops the wet ~3 dB against the correlated dry — every preset reads drier
    // in mono than it was voiced. When on, emit the energy-preserving correlated
    // wet 0.7071*(wetL+wetR) on both channels instead; the downstream sum then
    // leaves the wet/dry balance where stereo ears hear it. OFF = bit-identical.
    bool  monoSum    = false;
    // Plate tank voicing (2026-10-30, types 0 classic + dense only; ambient
    // already has its own analogs — loop HP and kAmbLFORate):
    //   lfShelf   — Jot absorbent filters: each comb's scalar loop gain becomes a
    //               first-order low shelf so the LF T60 is ~0.6x the mid T60
    //               (tight plate low end, no comb-bank LF hang), anchored so the
    //               1 kHz RT60 = decayTime exactly (presets keep their length).
    //   lfoSpread — incommensurate per-comb LFO rates (modRate x kLFORatio[k])
    //               instead of one shared rate: the comb taps never realign, so
    //               the tail has no periodic swirl at 1/modRate.
    //   velvet    — 15-pulse velvet-noise FIR in front of the allpass chain
    //               (sparse input diffusion, ~10 ms, energy-normalised).
    // All three OFF = bit-identical to the pre-2026-10-30 tank. Defaults:
    // lfoSpread ON (wet level moves < 0.1 dB, 1 kHz RT60 within ±2 %);
    // lfShelf OFF — removing LF tail energy is its point, and that re-levels the
    // wet by 0.3-1.5 dB on full-range program, so it is a per-preset choice;
    // velvet OFF — flat on noise, but in the full chain it moved short-window
    // preset loudness by up to ±1.6 dB, so it waits for a by-ear preset pass.
    bool  lfShelf    = false;
    bool  lfoSpread  = true;
    bool  velvet     = false;
    // Types 3/4 (2026-10-18): STUDIO PLATE (Dattorro figure-of-eight tank, mono
    // send -> decorrelated stereo) and HALL (8-line Hadamard FDN, true stereo
    // send). Own tanks (StudioReverbs.h), so types 0-2 stay bit-identical;
    // Decay = RT60, Damping matched to the classic tank's HF loss per second.
    bool  studioOn   = false;
    bool  hallOn     = false;
    studiorev::DattorroPlate studioPlate;
    studiorev::FdnHall       hall;
    void processStudio(float** in, float** out, int numSamples, int nCh) noexcept;

    // ---- Internal delay-line wrappers ----
    struct DelayLine {
        std::vector<float> buf;
        int writeIdx = 0;
        int nominalLen = 0; // base length in samples

        void resize(int len) { buf.assign(len + 8, 0.0f); nominalLen = len; writeIdx = 0; }

        void write(float v) noexcept {
            buf[writeIdx] = v;
            if (++writeIdx >= static_cast<int>(buf.size())) writeIdx = 0;
        }

        float read(int delay) const noexcept {
            const int len = static_cast<int>(buf.size());
            const int idx = (writeIdx - delay + len * 2) % len;
            return buf[idx];
        }

        // Linear interpolation read for fractional delay
        float readFrac(float delay) const noexcept {
            const int len  = static_cast<int>(buf.size());
            const int d0   = static_cast<int>(delay);
            const float fr = delay - static_cast<float>(d0);
            const int i0   = (writeIdx - d0     + len * 2) % len;
            const int i1   = (writeIdx - d0 - 1 + len * 2) % len;
            return buf[i0] + fr * (buf[i1] - buf[i0]);
        }

        // 4-point Hermite read (2026-07-14): the tank combs are LFO-modulated, and a linear
        // moving tap dulls/grains the tail; Hermite keeps damping the only HF loss. Needs
        // delay in [1, len-3] — the tank mod excursion is a few samples inside the +8 slack.
        float readFracHermite(float delay) const noexcept {
            const int len  = static_cast<int>(buf.size());
            const int d0   = static_cast<int>(delay);
            const float fr = delay - static_cast<float>(d0);
            const float xm1 = buf[(writeIdx - d0 + 1 + len * 2) % len];
            const float x0  = buf[(writeIdx - d0     + len * 2) % len];
            const float x1  = buf[(writeIdx - d0 - 1 + len * 2) % len];
            const float x2  = buf[(writeIdx - d0 - 2 + len * 2) % len];
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * fr + c2) * fr + c1) * fr + x0;
        }
    };

    // Schroeder allpass: H(z) = (z^{-N} - g) / (1 - g*z^{-N})
    struct AllpassDelay {
        DelayLine dl;
        float g = 0.7f;

        void resize(int len) { dl.resize(len); }

        float process(float x) noexcept {
            const float d = dl.read(dl.nominalLen);
            const float v = x + g * d;
            dl.write(v);
            return d - g * v;
        }
    };

    // Feedback comb with per-filter damping (1-pole LP in feedback path)
    struct CombDelay {
        DelayLine dl;
        float feedback  = 0.7f;
        float damping   = 0.3f;
        float lastLP    = 0.0f;

        void resize(int len) { dl.resize(len); }

        float process(float x, float modDelaySamples) noexcept {
            const float out = dl.readFracHermite(modDelaySamples);
            // 1-pole LP for high-frequency damping
            lastLP = out * (1.0f - damping) + lastLP * damping;
            dl.write(x + feedback * lastLP);
            return out;
        }

        // lfShelf variant: the scalar feedback is replaced by a first-order low
        // shelf H(z) = (b0 + b1 z^-1)/(1 + a1 z^-1) (TDF-II, state shelfZ) whose
        // DC/Nyquist gains are the per-comb LF/HF loop gains (Jot absorbent filter).
        float shelfB0 = 0.0f, shelfB1 = 0.0f, shelfA1 = 0.0f, shelfZ = 0.0f;

        float processShelf(float x, float modDelaySamples) noexcept {
            const float out = dl.readFracHermite(modDelaySamples);
            lastLP = out * (1.0f - damping) + lastLP * damping;
            const float y = shelfB0 * lastLP + shelfZ;
            shelfZ = shelfB1 * lastLP - shelfA1 * y;
            dl.write(x + y);
            return out;
        }
    };

    // Pre-delay line (stereo input summed to mono before reverb network)
    DelayLine preDelay;

    // Density switch (2026-07-23, OPT-IN): 0 = the classic 4-comb/4-AP tank every
    // preset was voiced on (bit-identical); 1 = a dense 8-comb/6-AP plate — lusher,
    // less metallic tail. Densifying was measured as a blanket change and REJECTED
    // (it re-leveled ~50 presets by up to 3 dB), so it ships as a per-preset switch.
    // Arrays are sized for the dense tank; classic mode uses the first 4/4.
    static constexpr int kNumAP   = 6;
    static constexpr int kNumComb = 8;
    static constexpr int kClassicAP   = 4;
    static constexpr int kClassicComb = 4;

    AllpassDelay ap[kNumAP];
    CombDelay    combs[kNumComb];

    // Per-comb LFO state (+ the prepare()-seeded initial phases reset() returns to)
    float lfoPhase[kNumComb]  = {};
    float lfoPhase0[kNumComb] = {};

    // Scaled delay lengths (set in prepare())
    int apLengths[kNumAP]     = {};
    int combLengths[kNumComb] = {};

    // Velvet input diffuser: pulse delays (samples, scaled in prepare()) + a
    // power-of-two ring (mask wrap: 15 taps/sample, no modulo)
    static constexpr int kNumVelvet = 15;
    int                velvetPos[kNumVelvet] = {};
    std::vector<float> velvetBuf;
    int                velvetW = 0, velvetMask = 0;
    void clearVelvet() noexcept { std::fill(velvetBuf.begin(), velvetBuf.end(), 0.0f); velvetW = 0; }

    // ── Hex Ambient extras ────────────────────────────────────────────────
    int ambApLengths[kNumAP]     = {};   // longer diffuser set (prime-staggered)
    int ambCombLengths[kNumComb] = {};   // longer tail set (~x1.6 plate)
    AllpassDelay smearAp[2];             // long pre-diffusers (attack smear)
    int          smearLengths[2] = {};
    DelayLine    bloomLine;              // ~90 ms recirculation line
    float        bloomLP  = 0.0f;        // recirc band-limit state (dark bloom)
    float        inHP     = 0.0f;        // wet-send high-pass state (~100 Hz)
    float        loopHP   = 0.0f;        // in-loop high-pass state (~110 Hz, anti-mud)
    float        ambFeedback[kNumComb] = {};   // ambient RT60 set (decay x2.2, longer lines)

    void recalcFeedback();
    void recalcDamping();
    void syncSpring();
};
