#include "NoiseGateComponent.h"

NoiseGateComponent::NoiseGateComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("NOISE GATE", apvts, "gate_bypass", 0)
{
    configureKnob(threshSlider,  threshLabel,  "THRESHOLD", *this);
    configureKnob(attackSlider,  attackLabel,  "ATTACK",    *this);
    configureKnob(releaseSlider, releaseLabel, "RELEASE",   *this);
    configureKnob(holdSlider,    holdLabel,    "HOLD",      *this);
    configureKnob(hystSlider,    hystLabel,    "HYSTERESIS",*this);

    threshAttach  = std::make_unique<SA>(apvts, "gate_threshold",  threshSlider);
    attackAttach  = std::make_unique<SA>(apvts, "gate_attack",     attackSlider);
    releaseAttach = std::make_unique<SA>(apvts, "gate_release",    releaseSlider);
    holdAttach    = std::make_unique<SA>(apvts, "gate_hold",       holdSlider);
    hystAttach    = std::make_unique<SA>(apvts, "gate_hysteresis", hystSlider);
}

void NoiseGateComponent::resized() {
    BlockComponentBase::resized();

    const int kW  = getWidth();
    const int top = kHeaderH + 20;
    const int knobW = 100, knobH = 100;
    const int totalW = 5 * knobW;
    int x = (kW - totalW) / 2;

    auto col = [&](int i) {
        return juce::Rectangle<int>(x + i * knobW, top, knobW, knobH);
    };
    layoutKnob(threshSlider,  threshLabel,  col(0));
    layoutKnob(attackSlider,  attackLabel,  col(1));
    layoutKnob(releaseSlider, releaseLabel, col(2));
    layoutKnob(holdSlider,    holdLabel,    col(3));
    layoutKnob(hystSlider,    hystLabel,    col(4));
}
