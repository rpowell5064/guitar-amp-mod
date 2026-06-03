#pragma once
#include "BlockComponentBase.h"

class CompressorComponent : public BlockComponentBase {
public:
    explicit CompressorComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    juce::ComboBox typeSelector;
    juce::Label    typeSelectorLabel;

    juce::ComboBox ratioSelector;
    juce::Label    ratioSelectorLabel;

    juce::Slider thresholdSlider, attackSlider, releaseSlider,
                 kneeSlider,     makeupSlider;
    juce::Label  thresholdLabel,  attackLabel,  releaseLabel,
                 kneeLabel,       makeupLabel;

    using SA  = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<CBA> typeAttach, ratioAttach;
    std::unique_ptr<SA>  thresholdAttach, attackAttach, releaseAttach,
                         kneeAttach, makeupAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorComponent)
};
