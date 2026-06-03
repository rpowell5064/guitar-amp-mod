#include "CompressorComponent.h"

CompressorComponent::CompressorComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("COMPRESSOR", apvts, "comp_bypass", 9)
{
    typeSelector.addItem("VCA",   1);
    typeSelector.addItem("1176",  2);
    typeSelector.setSelectedId(1);
    typeSelectorLabel.setText("TYPE", juce::dontSendNotification);
    typeSelectorLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    typeSelectorLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
    typeSelectorLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(typeSelector);
    addAndMakeVisible(typeSelectorLabel);
    typeAttach = std::make_unique<CBA>(apvts, "comp_type", typeSelector);

    ratioSelector.addItem("2:1",   1);
    ratioSelector.addItem("4:1",   2);
    ratioSelector.addItem("8:1",   3);
    ratioSelector.addItem("20:1",  4);
    ratioSelector.addItem("Limit", 5);
    ratioSelector.setSelectedId(2);
    ratioSelectorLabel.setText("RATIO", juce::dontSendNotification);
    ratioSelectorLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    ratioSelectorLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
    ratioSelectorLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(ratioSelector);
    addAndMakeVisible(ratioSelectorLabel);
    ratioAttach = std::make_unique<CBA>(apvts, "comp_ratio", ratioSelector);

    configureKnob(thresholdSlider, thresholdLabel, "THRESHOLD", *this);
    thresholdSlider.setTextValueSuffix(" dB");
    thresholdAttach = std::make_unique<SA>(apvts, "comp_threshold", thresholdSlider);

    configureKnob(attackSlider,  attackLabel,  "ATTACK",  *this);
    configureKnob(releaseSlider, releaseLabel, "RELEASE", *this);
    configureKnob(kneeSlider,    kneeLabel,    "KNEE",    *this);
    configureKnob(makeupSlider,  makeupLabel,  "MAKEUP",  *this);

    attackAttach  = std::make_unique<SA>(apvts, "comp_attack",  attackSlider);
    releaseAttach = std::make_unique<SA>(apvts, "comp_release", releaseSlider);
    kneeAttach    = std::make_unique<SA>(apvts, "comp_knee",    kneeSlider);
    makeupAttach  = std::make_unique<SA>(apvts, "comp_makeup",  makeupSlider);
}

void CompressorComponent::resized() {
    BlockComponentBase::resized();

    const int kW       = getWidth();
    const int kH       = getHeight();
    const int top      = kHeaderH + 16;
    const int selectorH = 24;
    const int labelH   = 16;
    const int knobW    = 110, knobH = 110;

    // Type and Ratio selectors side by side
    const int selectorW = 140;
    const int gap       = 24;
    const int totalSel  = selectorW * 2 + gap;
    int sx = (kW - totalSel) / 2;

    typeSelectorLabel.setBounds(sx, top, selectorW, labelH);
    typeSelector.setBounds(sx, top + labelH + 2, selectorW, selectorH);

    sx += selectorW + gap;
    ratioSelectorLabel.setBounds(sx, top, selectorW, labelH);
    ratioSelector.setBounds(sx, top + labelH + 2, selectorW, selectorH);

    // Knobs row below selectors
    const int knobsTop = top + labelH + selectorH + 24;
    const int totalW   = knobW * 5;
    int kx = (kW - totalW) / 2;

    layoutKnob(thresholdSlider, thresholdLabel, {kx,            knobsTop, knobW, knobH});
    layoutKnob(attackSlider,    attackLabel,    {kx + knobW,    knobsTop, knobW, knobH});
    layoutKnob(releaseSlider,   releaseLabel,   {kx + knobW*2,  knobsTop, knobW, knobH});
    layoutKnob(kneeSlider,      kneeLabel,      {kx + knobW*3,  knobsTop, knobW, knobH});
    layoutKnob(makeupSlider,    makeupLabel,    {kx + knobW*4,  knobsTop, knobW, knobH});

    (void)kH;
}
