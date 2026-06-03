#pragma once
#include "BlockComponentBase.h"
#include "LevelMeter.h"

class OutputComponent : public BlockComponentBase {
public:
    explicit OutputComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

    // Call from the UI timer to push fresh levels into the meter.
    void setLevels(float l, float r) { meter.setLevels(l, r); }

private:
    juce::Slider levelSlider;
    juce::Label  levelLabel;
    LevelMeter   meter;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> levelAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputComponent)
};
