#pragma once
#include "BlockComponentBase.h"
#include <array>

class NoiseGateComponent : public BlockComponentBase {
public:
    explicit NoiseGateComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    juce::Slider threshSlider, attackSlider, releaseSlider, holdSlider, hystSlider;
    juce::Label  threshLabel,  attackLabel,  releaseLabel,  holdLabel,  hystLabel;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> threshAttach, attackAttach, releaseAttach, holdAttach, hystAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseGateComponent)
};
