#include "OutputComponent.h"

OutputComponent::OutputComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("OUTPUT", apvts, "") // empty = no bypass toggle
{

    configureKnob(levelSlider, levelLabel, "OUTPUT LEVEL", *this);
    levelSlider.setTextValueSuffix(" dB");
    levelAttach = std::make_unique<SA>(apvts, "output_level", levelSlider);

    addAndMakeVisible(meter);
}

void OutputComponent::resized() {
    // Skip BlockComponentBase::resized() so the invisible bypass button isn't placed.
    const int kW = getWidth();
    const int knobW = 120, knobH = 110;
    const int cx    = (kW - knobW) / 2;
    const int top   = kHeaderH + 20;

    layoutKnob(levelSlider, levelLabel, juce::Rectangle<int>(cx, top, knobW, knobH));

    // Stereo meters below the knob
    const int meterW = 60, meterH = 140;
    meter.setBounds((kW - meterW) / 2, top + knobH + 16, meterW, meterH);
}
