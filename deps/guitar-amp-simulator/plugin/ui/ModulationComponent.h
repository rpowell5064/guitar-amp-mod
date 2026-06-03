#pragma once
#include "BlockComponentBase.h"

class ModulationComponent : public BlockComponentBase {
public:
    explicit ModulationComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    juce::ComboBox modelSelector;
    juce::Label    modelLabel;
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<CBA> modelAttach;

    // Shared knobs (all models)
    juce::Slider rateSlider, depthSlider, mixSlider, stereoWidthSlider;
    juce::Label  rateLabel,  depthLabel,  mixLabel,  stereoWidthLabel;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> rateAttach, depthAttach, mixAttach, stereoWidthAttach;

    // CE-2 Chorus specific
    juce::ToggleButton preampBtn;
    juce::Label        preampLabel;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<BA> preampAttach;

    // Uni-Vibe specific
    juce::ToggleButton vibratoBtn;
    juce::Label        vibratoLabel;
    std::unique_ptr<BA> vibratoAttach;

    juce::Slider outLevelSlider;
    juce::Label  outLevelLabel;
    std::unique_ptr<SA> outLevelAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationComponent)
};
