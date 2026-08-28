#pragma once
#include "AudioBlock.h"
#include "BiquadFilter.h"
#include "NamModel.h"
#include <array>
#include <string>
#include <vector>

enum class AmpModel {
    FenderDeluxe,      // Clean/edge-of-breakup, warm 6V6 character
    MarshallJCM800,    // Crunch/lead, British EL34 mid-focus
    EVH5150III,        // Modern high-gain, tight and scooped
    NeuralCustom,      // User-loaded NAM model
    SunnModelT,        // High-headroom clean/dirty, 6L6GC
    OrangeRockerverb50,// EL34 British, clean+dirty channels
    FriedmanBEDeluxe,  // Hot-rodded Marshall, 3-channel (Clean/BE/HBE), EL34
    HiwattDR103,       // High-headroom British clean (Gilmour platform), EL34
    VoxAC30,           // Bright Class-A EL84 chime (AC30 Top Boost)
    PeaveyBackstage,   // Solid-state practice combo (Backline Plus / Peavey Backstage Plus)
    MarshallPlexi,     // 1959 Super Lead Plexi — bright, power-amp-driven EL34 crunch
    MesaMarkV,         // Mesa/Boogie Mark V — 9 modes across 3 channels, Simul-Class
    MesaDualRectifier, // Mesa Dual Rectifier — 8 modes across 3 channels, 6L6, variac/rect switches
    PRSMT15,           // PRS MT15 — Clean/Crunch/Lead + bright switch, tight strong-NFB high gain
    AmpegSVT           // Ampeg SVT "Blue Liner" — the suite's first BASS amp: Ultra-Lo/Ultra-Hi,
                       // 3-position mid selector, 6×6550 solid-state-rectified 300 W power
};

// Amp block: gain staging → tone stack → power amp simulation.
//
// In NeuralCustom mode the entire preamp is replaced by a loaded .nam model.
// Tone stack knobs remain active as a post-model EQ.
class AmpBlock : public AudioBlock {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(float** in, float** out, int numSamples, int numChannels) override;
    void setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    void setAmpModel(AmpModel model);
    AmpModel getAmpModel() const noexcept { return currentModel; }

    // Load a .nam model from a file path. Returns true on success.
    bool loadNeuralModel(const std::string& filePath);

    bool hasNeuralModel() const noexcept { return nam.isLoaded(); }

    struct ModelParams {
        int    numStages;
        float  stageGainMin;
        float  stageGainMax;
        double bassFreq;
        double midFreq;
        double trebleFreq;
        double presenceFreq;
        float  midGainRange;
        float  bassGainRange;
        float  trebleGainRange;
        float  presenceGainRange;
        float  powerClipThreshold;
    };

private:
    float gain          = 0.5f;
    float bass          = 0.5f;
    float mid           = 0.5f;
    float treble        = 0.5f;
    float presence      = 0.5f;
    float master        = 0.7f;
    float sag           = 0.3f;
    float namOutputGain = 1.5f; // [0,4] output trim for NAM path; compensates missing power-amp gain

    AmpModel currentModel = AmpModel::MarshallJCM800;
    NamModel nam;

    static constexpr int kMaxCh = 2;

    struct ChannelEQ {
        BiquadFilter bassF, midF, trebleF, presenceF;
        BiquadFilter inputHP;
    };
    std::array<ChannelEQ, kMaxCh> eq;

    float sagEnvelope  = 0.0f;
    float sagDecayCoef = 0.0f;

    ModelParams mp;

    // Scratch buffers for the neural model path — pre-allocated in prepare().
    std::vector<float> namInBuf;
    std::vector<float> namOutBuf;
    std::vector<float> sagFactors;

    void recalcModel();
    void recalcEQ();
    float processPreamp(float x) const noexcept;
    static float saturate(float x, float gain, float thresh) noexcept;
};
