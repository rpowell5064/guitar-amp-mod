#pragma once
#include <JuceHeader.h>
#include "GuitarAmpProcessor.h"
#include "OutputLimiter.h"
#include <unordered_map>
#include <atomic>
#include <array>

// Top-level JUCE AudioProcessor.
// Bridges the JUCE AudioProcessorValueTreeState parameter system with the
// underlying GuitarAmpProcessor DSP engine. All audio-thread parameter reads
// use cached std::atomic<float>* pointers for zero-contention access.
class GuitarAmpAudioProcessor : public juce::AudioProcessor,
                                 public juce::AsyncUpdater {
public:
    GuitarAmpAudioProcessor();
    ~GuitarAmpAudioProcessor() override = default;

    // ── AudioProcessor interface ────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Guitar Amp Sim"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int  getNumPrograms() override    { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── Custom API (safe to call from message thread) ───────────────────────
    void loadIR(const juce::File& irFile);
    void loadNeuralModel(const juce::File& f);
    void loadOverdriveNam(const juce::File& f);

    // Reorder reorderable blocks (display positions 0-9).
    // Call from the message thread.
    void reorderBlock(int fromDisplayPos, int toDisplayPos);

    std::vector<int> getLogicalOrder() const;

    // Apply an absolute logical order (10-element permutation of 0-9).
    // Fires onChainReordered asynchronously on the message thread.
    void setChainOrder(const std::vector<int>& order);

    juce::File getIRFile()           const { return currentIRFile;           }
    juce::File getNamFile()          const { return currentNamFile;          }
    juce::File getOverdriveNamFile() const { return currentOverdriveNamFile; }

    // Fired on the message thread after setStateInformation() restores files/state.
    std::function<void()> onFilesRestored;
    std::function<void()> onOverdriveNamRestored;
    std::function<void()> onChainReordered;
    std::function<void()> onEffectActiveChanged;

    // effectActive[i]: i=0→Gate,1→Pitch,2→Drive,3→Amp,4→Cab,5→Mod,6→Delay,7→Reverb,8→EQ,9→Comp
    // false = forced bypass + hidden from chain.
    bool getEffectActive(int origIdx) const;
    void setEffectActive(int origIdx, bool active);
    std::array<bool, 10> getAllEffectActive() const { return effectActive_; }

    // Level meters — written from audio thread, read from UI thread.
    float getInputLevelL()  const noexcept { return inputLevelL.load();  }
    float getInputLevelR()  const noexcept { return inputLevelR.load();  }
    float getOutputLevelL() const noexcept { return outputLevelL.load(); }
    float getOutputLevelR() const noexcept { return outputLevelR.load(); }

    // Returns approximate fraction of real-time spent in processBlock [0,1].
    float getCPULoad() const noexcept { return cpuLoad.load(); }

    // ── Parameter value tree ────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;

private:
    GuitarAmpProcessor dsp;
    OutputLimiter      outputLimiter_;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    // Meters (written audio thread, read UI thread)
    std::atomic<float> inputLevelL{0}, inputLevelR{0};
    std::atomic<float> outputLevelL{0}, outputLevelR{0};

    // Cached raw-parameter pointers for lock-free audio-thread reads.
    // Key = APVTS parameter ID.
    std::unordered_map<juce::String, std::atomic<float>*> rawParams;

    // Last applied enum values so we only call setAmpModel/setDelayType on change.
    int lastAmpModel      = -1; // -1 forces defaults snap on first audio block
    int lastDelayType     = 0;
    int lastOverdriveType = -1;
    int lastModType       = 0;
    int lastTubeType      = -1;
    std::atomic<int> pendingTubeUpdate{-1};
    std::atomic<int> pendingPADefaults{-1};

    std::atomic<float> cpuLoad{0};

    // Default: Gate✓, Pitch✗, Drive✓, Amp✓, Cab✓, Mod✗, Delay✓, Reverb✓, EQ✓, Comp✓
    std::array<bool, 10> effectActive_{{true,false,true,true,true,false,true,true,true,true}};

    juce::File currentIRFile;
    juce::File currentNamFile;
    juce::File currentOverdriveNamFile;

    void cacheRawParamPointers();
    void syncDSPParameters();
    void handleAsyncUpdate() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParamLayout();
    static juce::AudioProcessor::BusesProperties               getBusesProps();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarAmpAudioProcessor)
};
