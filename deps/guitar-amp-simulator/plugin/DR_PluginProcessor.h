#pragma once
#include <JuceHeader.h>
#include "DeluxeReverbAmpAB763.h"

// ─────────────────────────────────────────────────────────────────────────────
// DR_PluginProcessor — JUCE AudioProcessor for the Fender Deluxe Reverb AB763
// ─────────────────────────────────────────────────────────────────────────────
//
// Owns a single DeluxeReverbAmpAB763 instance and wires all parameters through
// AudioProcessorValueTreeState (APVTS) with 20 ms smoothing.
//
// Parameter IDs and ranges:
//   "volume"          [0, 1]   — Volume knob (input drive, NO GAIN knob)
//   "bass"            [0, 1]   — Tonestack Bass
//   "mid"             [0, 1]   — Tonestack Middle
//   "treble"          [0, 1]   — Tonestack Treble
//   "reverb"          [0, 1]   — Reverb mix (0=dry, 1=wet)
//   "trem_speed"      [0, 1]   — Tremolo speed (1–10 Hz internally)
//   "trem_intensity"  [0, 1]   — Tremolo depth
//   "bright"          [0, 1]   — Bright switch (0=off, 1=on; displayed as toggle)
//   "master"          [0, 1]   — Master volume (non-original, for level matching)
//   "sag"             [0, 1]   — Supply sag depth
//
// All parameters are cached as atomic<float>* for zero-overhead audio-thread
// reads — no APVTS tree queries inside processBlock.
// ─────────────────────────────────────────────────────────────────────────────
class DR_PluginProcessor final : public juce::AudioProcessor {
public:
    DR_PluginProcessor();
    ~DR_PluginProcessor() override = default;

    // ── AudioProcessor interface ──────────────────────────────────────────────
    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Deluxe Reverb AB763"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── APVTS (public so editor can attach sliders) ───────────────────────────
    juce::AudioProcessorValueTreeState apvts;

private:
    DeluxeReverbAmpAB763 amp_;

    // Cached parameter pointers — read atomically on the audio thread.
    std::atomic<float>* pVolume_     = nullptr;
    std::atomic<float>* pBass_       = nullptr;
    std::atomic<float>* pMid_        = nullptr;
    std::atomic<float>* pTreble_     = nullptr;
    std::atomic<float>* pReverb_     = nullptr;
    std::atomic<float>* pTremSpeed_  = nullptr;
    std::atomic<float>* pTremIntens_ = nullptr;
    std::atomic<float>* pBright_     = nullptr;
    std::atomic<float>* pMaster_     = nullptr;
    std::atomic<float>* pSag_        = nullptr;

    // Previous parameter values — detect changes without APVTS listener overhead.
    float prevVolume_    = -1.0f;
    float prevBass_      = -1.0f;
    float prevMid_       = -1.0f;
    float prevTreble_    = -1.0f;
    float prevReverb_    = -1.0f;
    float prevTremSpeed_ = -1.0f;
    float prevTremIntens_= -1.0f;
    float prevBright_    = -1.0f;
    float prevMaster_    = -1.0f;
    float prevSag_       = -1.0f;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DR_PluginProcessor)
};
