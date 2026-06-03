#pragma once
#include "BlockComponentBase.h"

class ReverbComponent : public BlockComponentBase {
public:
    explicit ReverbComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    juce::Slider preDelaySlider, decaySlider, dampSlider,
                 modDSlider, modRSlider, mixSlider;
    juce::Label  preDelayLabel,  decayLabel,  dampLabel,
                 modDLabel,      modRLabel,   mixLabel;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> preDelayAttach, decayAttach, dampAttach,
                        modDAttach, modRAttach, mixAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbComponent)
};
