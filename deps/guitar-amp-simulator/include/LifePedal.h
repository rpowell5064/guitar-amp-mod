#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Sunn O))) Life Pedal — OverdriveBase implementation ──────────────────
//
// Three-stage circuit modelled after the Life Pedal's RAT-style
// distortion core, octave-up rectifier, and clean blend.
//
//   Stage 1 — RAT-style distortion
//     in → preHPF (720 Hz, 1-pole)  ← conditions both clean and dirty paths
//       ↓ [dirty]
//       gain (1–100×, drive-controlled)
//       → tanh(gain·clean) / tanh(gain)   symmetric LED soft-clip
//       → postLPF (5 kHz→500 Hz log sweep, tone=0 bright, tone=1 dark)
//       = distorted
//
//   Stage 2 — Octave-up rectifier (tapped pre-distortion)
//     clean → |·|  full-wave rectify
//           → dcBlock (20 Hz HPF)   remove rectification DC offset
//           → octBPF (1.2 kHz, Q=1.0) isolate octave harmonic
//           = oct
//     dirty = distorted + octave × oct
//
//   Stage 3 — Clean blend + boost
//     clean = preHPF output
//     out = ((1−mix)·clean + mix·dirty) × boost(level)
//     boost: level=0 → 0 dB, level=1 → +20 dB
//
// Parameters: drive [0,1], tone [0,1], level [0,1], octave [0,1], mix [0,1]
//   tone=0 → postLPF at 5 kHz (bright/biting RAT character)
//   tone=1 → postLPF at 500 Hz (warm/dark doom character)
class LifePedal final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                         noexcept override;
    void  advanceSmoothing()                              noexcept override;
    float processSample(float x, int ch)                  noexcept override;
    void  setParameter(const std::string& id, float value) noexcept override;
    float getParameter(const std::string& id) const        noexcept override;

    const char* modelName() const noexcept override { return "Life Pedal"; }

private:
    double oversampledFs_ = 0.0;

    float drive_  = 0.5f;   // [0,1] → pre-clip gain 1–100×
    float tone_   = 0.5f;   // [0,1] → postLPF: 0=5 kHz (bright), 1=500 Hz (dark)
    float level_  = 0.5f;   // [0,1] → output boost 0 dB (×1) to +6 dB (×2)
    float octave_ = 0.3f;   // [0,1] → octave-up level into dirty path
    float mix_    = 1.0f;   // [0,1] → clean/dirty blend

    LinearSmoother driveSmooth_, levelSmooth_, mixSmooth_, octaveSmooth_;
    float driveCur_ = 0.5f, levelCur_ = 0.5f, mixCur_ = 1.0f, octaveCur_ = 0.3f;

    struct ChannelState {
        BiquadFilter preHPF;    // 720 Hz 1-pole (always in series)
        BiquadFilter postLPF;   // tone-swept 1-pole LPF (5 kHz→500 Hz)
        BiquadFilter dcBlock;   // 20 Hz 1-pole HPF (post-rectifier DC removal)
        BiquadFilter octBPF;    // 1.2 kHz BPF Q=1.0 (octave isolation)
    };
    std::array<ChannelState, kMaxCh> ch_;

    void recalcFilters() noexcept;

    // Symmetric soft-clip: tanh(gain·clean) / tanh(gain). ±1 at rail.
    static float softClip(float clean, float gain) noexcept;
};
