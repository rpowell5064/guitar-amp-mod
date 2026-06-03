#pragma once
#include "BlockComponentBase.h"

class PitchComponent : public BlockComponentBase {
public:
    explicit PitchComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    juce::ComboBox modeSelector;
    juce::Label    modeLabel;
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<CBA> modeAttach;

    // Expression pedal — horizontal slider
    // Pi Stomp: bind ADC output to "pitch.expression" for hardware whammy control
    juce::Slider expressionSlider;
    juce::Label  expressionLabel;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> expressionAttach;

    juce::Slider mixSlider;
    juce::Label  mixLabel;
    std::unique_ptr<SA> mixAttach;

    juce::Slider centsSlider;  // fine-tune [-50, +50 cents]
    juce::Label  centsLabel;
    std::unique_ptr<SA> centsAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchComponent)
};
