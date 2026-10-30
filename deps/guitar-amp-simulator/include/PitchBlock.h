#pragma once
#include "AudioBlock.h"
#include <array>
#include <vector>

// Whammy-style pitch effects block.
//
// Algorithm: two-grain granular pitch shifting with Hann-windowed COLA
// (Constant Overlap-Add) at 50% overlap.  Two grains at half-period phase
// offset sum to a constant window value of 1.0 at all times, so there is
// no amplitude modulation artifact.
//
// Aligned grain restarts (WSOLA-style, "align", default on)
//   COLA keeps the *window* sum flat, but each restarted grain jumps to an
//   arbitrary point in the waveform, so during the crossfade it partially
//   cancels the grain it replaces.  That cancellation recurs at the grain
//   restart rate (2 / grain length = 40 Hz), heard as a periodic warble that
//   moves with the pedal.  With align on, each restart searches one input
//   period (period from the same zero-crossing tracker the octave block uses)
//   for the start offset whose waveform best matches the grain still playing
//   (normalised cross-correlation, as in WSOLA: Verhelst & Roelands 1993), so
//   the crossfade sums in phase.  The search only moves the start towards the
//   write head (never adds delay) and is skipped at unity ratio, so the heel
//   position is bit-identical either way.  align = 0 is the unaligned shifter.
//
// Expression pedal
//   expression = 0.0 → heel position: no pitch shift (ratio = 1.0, unity)
//   expression = 1.0 → toe  position: full target shift for selected mode
//   Intermediate values sweep linearly between 1.0 and the target ratio.
//
//   Pi Stomp note: bind the expression pedal ADC output to "pitch.expression"
//   to get hardware whammy control on the physical pedal.
//
// Parameters
//   "mode"        [0, 4]  WhammyMode integer
//   "expression"  [0, 1]  expression pedal position (0=heel, 1=toe)
//   "mix"         [0, 1]  dry/wet blend
//   "cents"       [-50, 50] fine-tune offset
//   "align"       {0, 1}  aligned grain restarts (default 1)
class PitchBlock final : public AudioBlock {
public:
    // Whammy modes (integer values match "pitch_mode" APVTS choice index)
    enum Mode {
        Down2Oct = 0,   // -2 octaves  (ratio 0.25)
        Down1Oct = 1,   // -1 octave   (ratio 0.50)
        Detune   = 2,   // -1 semitone (ratio ~0.9439)
        Up1Oct   = 3,   // +1 octave   (ratio 2.00)
        Up2Oct   = 4,   // +2 octaves  (ratio 4.00)
    };

    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

private:
    static constexpr int   kMaxCh      = 2;
    static constexpr int   kNumGrains  = 2;
    static constexpr int   kBufBits    = 16;
    static constexpr int   kBufSize    = 1 << kBufBits;   // 65536 samples (~1.5 s @ 44.1 kHz)
    static constexpr int   kBufMask    = kBufSize - 1;
    static constexpr float kGrainMs    = 50.0f;            // grain length in ms
    static constexpr float kTwoPi      = 6.28318530718f;

    // Grain-alignment search budget (per grain restart, per channel).
    static constexpr int   kCorrTaps   = 64;     // max taps per lag (window decimated to fit)
    static constexpr int   kCoarseLags = 32;     // coarse lag grid over one period, then halving refine
    static constexpr float kHystFrac   = 0.12f;  // tracker Schmitt hysteresis (as OctaveBlock)

    // Target pitch ratios indexed by Mode.
    // Detune = 2^(-1/12) ≈ 0.94387 (one semitone down).
    static constexpr float kModeRatios[5] = {
        0.25000f,   // Down2Oct
        0.50000f,   // Down1Oct
        0.94387f,   // Detune  (one semitone down)
        2.00000f,   // Up1Oct
        4.00000f,   // Up2Oct
    };

    int   mode_       = Up1Oct;
    float expression_ = 0.0f;
    float mix_        = 1.0f;
    float cents_      = 0.0f;   // fine-tune offset [-50, +50 cents]
    bool  align_      = true;   // WSOLA-style aligned grain restarts

    float pitchRatio_   = 1.0f;   // recomputed whenever mode or expression changes
    float phaseInc_     = 0.0f;   // 1.0f / grainSamples_
    int   grainSamples_ = 2205;

    std::array<std::vector<float>, kMaxCh> buf_;
    int writeHead_[kMaxCh] = {};

    struct GrainState {
        float readHead = 0.0f;
        float phase    = 0.0f;
    };
    std::array<std::array<GrainState, kNumGrains>, kMaxCh> grains_{};

    // Input period tracker for the alignment search: the OctaveBlock estimator
    // (350 Hz one-pole isolates the fundamental; Schmitt-triggered rising
    // crossings with envelope-proportional hysteresis; smoothed f0).
    struct Tracker {
        float env = 0.0f, lp = 0.0f, f0 = 0.0f;
        bool  armed = false;
        int   sinceCross = 0;
    };
    Tracker trk_[kMaxCh];
    float envA_ = 0.0f, envR_ = 0.0f, lpA_ = 0.0f;
    int   maxLag_ = 1102;   // longest searchable period (40 Hz, capped at grain/2)
    int   minWin_ = 132;    // shortest correlation window (3 ms)
    int   maxWin_ = 551;    // longest correlation window (grain/4)
    int   defLag_ = 441;    // search span while the tracker has no estimate (10 ms)

    void  recomputeRatio() noexcept;
    void  resetGrains()    noexcept;
    void  trackPeriod(float x, int ch) noexcept;
    float alignedStart(int ch, float nominal, float ref) const noexcept;
    float processOneSample(float x, int ch) noexcept;
};
