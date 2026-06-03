#include "PitchComponent.h"

PitchComponent::PitchComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("WHAMMY", apvts, "pitch_bypass", 1)
{
    modeLabel.setText("MODE", juce::dontSendNotification);
    modeLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    modeLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
    addAndMakeVisible(modeLabel);

    modeSelector.addItem("-2 Octave", 1);
    modeSelector.addItem("-1 Octave", 2);
    modeSelector.addItem("Detune",    3);
    modeSelector.addItem("+1 Octave", 4);
    modeSelector.addItem("+2 Octave", 5);
    modeSelector.setSelectedId(4);   // +1 Octave default
    addAndMakeVisible(modeSelector);
    modeAttach = std::make_unique<CBA>(apvts, "pitch_mode", modeSelector);

    // Expression pedal — horizontal bar (0 = heel/dry, 1 = toe/full shift)
    expressionSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    expressionSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
    addAndMakeVisible(expressionSlider);
    expressionLabel.setText("EXPRESSION", juce::dontSendNotification);
    expressionLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    expressionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
    addAndMakeVisible(expressionLabel);
    expressionAttach = std::make_unique<SA>(apvts, "pitch_expression", expressionSlider);

    configureKnob(mixSlider, mixLabel, "MIX", *this);
    mixAttach = std::make_unique<SA>(apvts, "pitch_mix", mixSlider);

    configureKnob(centsSlider, centsLabel, "TUNE", *this);
    centsAttach = std::make_unique<SA>(apvts, "pitch_cents", centsSlider);
}

void PitchComponent::resized() {
    BlockComponentBase::resized();

    const int kW  = getWidth();
    const int top = kHeaderH + 16;

    // Mode selector row
    modeLabel.setBounds(8, top, 44, 20);
    modeSelector.setBounds(56, top, 180, 24);

    // Expression pedal row
    const int exprY = top + 40;
    expressionLabel.setBounds(8,   exprY + 2, 88, 18);
    expressionSlider.setBounds(100, exprY, kW - 200, 24);

    // Mix and Tune knobs side-by-side
    const int knobTop = exprY + 44;
    const int knobW = 100, knobH = 100;
    const int gap   = 32;
    const int totalW = knobW * 2 + gap;
    const int x0 = (kW - totalW) / 2;
    layoutKnob(mixSlider,   mixLabel,   juce::Rectangle<int>(x0,            knobTop, knobW, knobH));
    layoutKnob(centsSlider, centsLabel, juce::Rectangle<int>(x0 + knobW + gap, knobTop, knobW, knobH));
}
