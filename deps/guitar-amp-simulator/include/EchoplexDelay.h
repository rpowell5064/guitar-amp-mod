#pragma once
#include "DelayBase.h"
#include "EchoplexPreamp.h"
#include <array>

// ── Echoplex EP-3 tape echo (delay type 4) ──────────────────────────────────
//
// The whole instrument, not just a delay line: the input passes the EP-3 JFET
// preamp (EchoplexPreamp, fixed member — the record head hears the JFET, the
// dry path NEVER does: "analog dry path"), the moving head reads back through
// an aging repro chain, and the feedback loop saturates like oxide.
//
//   - Delay: 65–750 ms mechanical EP-3 head range, tap-extendable to 4000 ms.
//   - Transport inertia: time changes glide with a 250 ms 1-pole — moving the
//     head REPITCHES like the real slider, never clicks.
//   - Age (0 = serviced, 1 = thrashed) drives THREE things at once:
//       repro LP   fc = 6000 − 3500·age  Hz   (6 kHz → 2.5 kHz)
//       tape hiss  −90 + 30·age  dBFS         (injected at the record head so
//                                              it CIRCULATES — at fb 0.95 it
//                                              blooms ~+26 dB, like real tape)
//       transport wear = 0.35 + 0.65·age      (scales wow/flutter depth)
//   - Wow: sine LFO whose RATE random-walks in [0.1, 0.3] Hz (bounded ±1 by
//     construction, no variance guesswork — the ambient-reverb-LFO lesson);
//     depth ±0.35 % of the current delay at full wear.
//   - Flutter: 12 Hz random-walk noise minus a 6 Hz 1-pole = 6–12 Hz band;
//     depth ±0.08 % at full wear.
//   - Feedback saturation: tanh(2.5·u)·0.4 — the 1/2.5 normalization keeps
//     small-signal loop gain EXACTLY fb, so the loop is unconditionally
//     stable below fb = 1.0 and self-oscillates musically at the 0.95 cap
//     (a bare tanh(2.5·u) is +8 dB per pass = guaranteed runaway).
//   - Repro LP sits on the READ tap, before both output and feedback, so the
//     FIRST repeat is already one tape-pass dark and repeat N is N passes.
//
// Params: "timeMs" [65,4000] · "feedback" [0,0.95] · "mix" [0,1] ·
//         "age" [0,1] · "pregain" [0,1] → 0..11 dB JFET drive (default 6 dB).
// Deterministic: fixed per-channel LCG seeds, no clocks, no std::random.
class EchoplexDelay final : public DelayBase {
public:
    static constexpr int   kMaxCh      = 2;
    static constexpr float kMaxDelayMs = 4000.0f;

    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  reset()                                              noexcept override;
    void  advanceSmoothing()                                   noexcept override;
    float processSample(float x, int ch)                       noexcept override;
    void  setParameter(const std::string& id, float value)     noexcept override;
    float getParameter(const std::string& id) const            noexcept override;
    const char* delayName() const noexcept override { return "Echoplex EP-3"; }

private:
    float timeMs_   = 310.0f;
    float feedback_ =   0.35f;
    float mix_      =   0.30f;
    float age_      =   0.35f;
    float pregain_  =   0.545f;   // ≈ 6 dB — the classic front-end push

    // Transport inertia 250 ms; fb/mix at the house 5 ms.
    ParamSmoother timeSmoother_, feedbackSmoother_, mixSmoother_;

    EchoplexPreamp preamp_;       // record-path JFET (dry path bypasses it)

    struct ChannelState {
        std::vector<float> buf;
        int      writeIdx  = 0;
        float    ageLPz    = 0.0f;     // repro-chain 1-pole state
        float    wowFreqHz = 0.2f;     // random-walk rate, clamped [0.1, 0.3]
        float    wowPhase  = 0.0f;
        RandomWalk flutWalk;           // 12 Hz LP noise (σ ≈ 1)
        float    flutHPz   = 0.0f;     // minus 6 Hz 1-pole → 6–12 Hz band
        uint32_t seed      = 0x9E3779B9u;  // hiss + wow-rate LCG

        float lcg() noexcept {         // uniform [−1, 1)
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(static_cast<int32_t>(seed)) * (1.0f / 2147483648.0f);
        }
    };
    std::array<ChannelState, kMaxCh> ch_;

    float ageLPCoeff_ = 0.0f;     // e^(−2π·fc/fs) form (TapeDelay convention)
    float noiseLin_   = 3.1623e-5f;   // −90 dB at age 0
    float flutHPa_    = 7.85e-4f;     // 6 Hz 1-pole @ 48 k

    void rebuildAge() noexcept;
    void applyPregain() noexcept;
};
