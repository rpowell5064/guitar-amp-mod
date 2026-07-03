#pragma once
#include "AudioBlock.h"

// Noise gate with configurable threshold, attack, hold, release and hysteresis.
// Uses a fast peak envelope follower to detect signal level, then applies
// a smoothed gain based on a 5-state machine to avoid chattering.
class NoiseGateBlock : public AudioBlock {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(float** in, float** out, int numSamples, int numChannels) override;
    void setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

private:
    // Parameters (all user-facing)
    float thresholdDB  = -60.0f; // dBFS
    float attackMs     =   2.0f; // fast, punchy note onsets without clicking
    float releaseMs    = 250.0f; // long, smooth fade so tails ring out (detection stays fast — see .cpp)
    float holdMs       = 120.0f; // ride through note transitions / palm mutes before releasing
    float hysteresisDB =   8.0f; // dead-band width (wider = no chatter)

    // Derived coefficients (recalculated in prepare/setParameter)
    float envAttack{}, envRelease{};
    float gainAttack{}, gainRelease{};
    int   holdSamplesTotal{};

    // Per-channel state
    static constexpr int kMaxChannels = 2;

    enum class GateState { Closed, Attacking, Open, Holding, Releasing };

    struct ChannelState {
        float     envelope     = 0.0f;
        float     gateGain     = 0.0f;
        int       holdCounter  = 0;
        GateState state        = GateState::Closed;
    } ch[kMaxChannels];

    void recalcCoeffs();
    float processSample(float x, ChannelState& s) noexcept;
};
