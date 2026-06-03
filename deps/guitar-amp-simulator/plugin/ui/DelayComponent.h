#pragma once
#include "BlockComponentBase.h"

// Delay panel — Digital, Tape, and Binson Echorec modes.
// Shows shared controls (time, feedback, mix, lo cut, hi cut) plus
// mode-specific controls that swap when the mode selector changes.
class DelayComponent : public BlockComponentBase {
public:
    explicit DelayComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    // Mode selector
    juce::ComboBox modeSelector;
    juce::Label    modeLabel;

    // Shared knobs
    juce::Slider timeSlider, fbSlider, mixSlider, loSlider, hiSlider;
    juce::Label  timeLabel,  fbLabel,  mixLabel,  loLabel,  hiLabel;

    // Digital-only
    juce::Slider stereoSlider;
    juce::Label  stereoLabel;

    // Tape-only
    juce::Slider wowSlider, flutterSlider, satSlider, ageSlider;
    juce::Label  wowLabel,  flutterLabel,  satLabel,  ageLabel;

    // Echorec-only
    juce::ToggleButton headButton[4];
    juce::Label        headLabel[4];
    juce::Slider       noiseSlider;
    juce::Label        noiseLabel;

    using SA  = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA  = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<CBA> modeAttach;
    std::unique_ptr<SA>  timeAttach, fbAttach, mixAttach, loAttach, hiAttach,
                         stereoAttach, wowAttach, flutterAttach, satAttach, ageAttach,
                         noiseAttach;
    std::unique_ptr<BA>  headAttach[4];

    void updateModeVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DelayComponent)
};
