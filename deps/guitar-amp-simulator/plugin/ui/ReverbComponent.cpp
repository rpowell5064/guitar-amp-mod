#include "ReverbComponent.h"

ReverbComponent::ReverbComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("PLATE REVERB", apvts, "reverb_bypass", 7)
{
    configureKnob(preDelaySlider, preDelayLabel, "PRE-DELAY", *this);
    configureKnob(decaySlider,    decayLabel,    "DECAY",     *this);
    configureKnob(dampSlider,     dampLabel,     "DAMPING",   *this);
    configureKnob(modDSlider,     modDLabel,     "MOD DEPTH", *this);
    configureKnob(modRSlider,     modRLabel,     "MOD RATE",  *this);
    configureKnob(mixSlider,      mixLabel,      "MIX",       *this);

    preDelayAttach = std::make_unique<SA>(apvts, "reverb_predelay", preDelaySlider);
    decayAttach    = std::make_unique<SA>(apvts, "reverb_decay",    decaySlider);
    dampAttach     = std::make_unique<SA>(apvts, "reverb_damping",  dampSlider);
    modDAttach     = std::make_unique<SA>(apvts, "reverb_moddepth", modDSlider);
    modRAttach     = std::make_unique<SA>(apvts, "reverb_modrate",  modRSlider);
    mixAttach      = std::make_unique<SA>(apvts, "reverb_mix",      mixSlider);
}

void ReverbComponent::resized() {
    BlockComponentBase::resized();

    const int kW     = getWidth();
    const int top    = kHeaderH + 20;
    const int knobW  = 100, knobH = 100;
    const int totalW = 6 * knobW;
    const int x0     = (kW - totalW) / 2;

    auto col = [&](int i){ return juce::Rectangle<int>(x0 + i * knobW, top, knobW, knobH); };
    layoutKnob(preDelaySlider, preDelayLabel, col(0));
    layoutKnob(decaySlider,    decayLabel,    col(1));
    layoutKnob(dampSlider,     dampLabel,     col(2));
    layoutKnob(modDSlider,     modDLabel,     col(3));
    layoutKnob(modRSlider,     modRLabel,     col(4));
    layoutKnob(mixSlider,      mixLabel,      col(5));
}
