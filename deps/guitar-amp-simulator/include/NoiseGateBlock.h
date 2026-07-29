#pragma once
#include "AudioBlock.h"
#include "BiquadFilter.h"

// Noise gate with configurable threshold, attack, hold, release and hysteresis.
// Uses a fast peak envelope follower to detect signal level, then applies
// a smoothed gain based on a 5-state machine to avoid chattering.
//
// The DETECTOR (only) runs through a fixed 60/120/180/240 Hz notch comb before
// the envelope follower ("hum-reject sidechain"). On this rig the hands-off floor
// is ~89% 60 Hz mains hum (see rig-noise memory); keeping the hum out of the
// detector drops the effective floor the threshold has to clear by ~8-10 dB, so
// the same threshold lets note tails ring far longer with identical rejection
// between notes. The audio path is untouched — the comb never colours the output.
class NoiseGateBlock : public AudioBlock {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(float** in, float** out, int numSamples, int numChannels) override;
    // Keyed variant (2026-07-29, Chime Thirty idle-whine fix): the DETECTOR runs
    // on `key` (mono, e.g. the raw pre-InputTrim input) while the gain gates the
    // audio in `in`. Rationale: the gate thresholds were floor-complianced
    // against the RAW rig floor, but in Hex Forge the gate sits after the Input
    // Trim's pickup voicing + boost (up to ~+20 dB) -- the boosted floor grazed
    // the open threshold and the hysteresis latched the gate open at idle
    // (close = thresh - hyst/2 sat BELOW the boosted floor, so it never
    // re-closed). Keying on the raw input makes thresholds mean what the
    // compliance pass measured, independent of in-chain gain staging.
    void processKeyed(float** in, const float* key, float** out, int numSamples, int numChannels);
    void setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

private:
    // Parameters (all user-facing)
    float thresholdDB  = -60.0f; // dBFS
    float attackMs     =   2.0f; // fast, punchy note onsets without clicking
    float releaseMs    = 250.0f; // long, smooth fade so tails ring out (detection stays fast — see .cpp)
    float holdMs       = 120.0f; // ride through note transitions / palm mutes before releasing
    float hysteresisDB =   8.0f; // dead-band width (wider = no chatter)
    bool  humReject    = true;   // detector-only 60Hz hum-comb (see class note); "humReject" param

    // Derived coefficients (recalculated in prepare/setParameter)
    float envAttack{}, envRelease{};
    float gainAttack{}, gainRelease{};
    int   holdSamplesTotal{};

    // Per-channel state
    static constexpr int kMaxChannels = 2;

    enum class GateState { Closed, Attacking, Open, Holding, Releasing };

    static constexpr int kHumNotches = 4;   // 60/120/180/240 Hz detector comb

    struct ChannelState {
        float        envelope     = 0.0f;
        float        gateGain     = 0.0f;
        int          holdCounter  = 0;
        GateState    state        = GateState::Closed;
        BiquadFilter scNotch[kHumNotches];   // sidechain hum-reject (detector only)
    } ch[kMaxChannels];

    void recalcCoeffs();
    float processSample(float x, float keySample, ChannelState& s) noexcept;
};
