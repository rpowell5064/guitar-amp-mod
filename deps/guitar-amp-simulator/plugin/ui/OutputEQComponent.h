#pragma once
#include "BlockComponentBase.h"

// 5-band parametric EQ panel with speaker preset selector.
// Bands: Low Shelf | Lo-Mid | Mid | Hi-Mid | High Shelf
// Presets: Custom / Flat / FRFR / Guitar Amp / Headphones
class OutputEQComponent : public BlockComponentBase {
public:
    explicit OutputEQComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    juce::AudioProcessorValueTreeState& apvtsRef;

    juce::ComboBox presetBox;
    juce::Label    presetLabel;

    // Per-band knobs: freq, gain, Q.
    // Bands 1 and 5 (shelves) have no Q knob.
    struct BandControls {
        juce::Slider freqSlider, gainSlider, qSlider;
        juce::Label  freqLabel,  gainLabel,  qLabel;
        juce::Label  bandLabel;  // e.g. "LOW SHELF"
    };
    BandControls bands[5];

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> freqAttach[5], gainAttach[5], qAttach[5];

    void applyPreset(int presetIdx);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputEQComponent)
};
