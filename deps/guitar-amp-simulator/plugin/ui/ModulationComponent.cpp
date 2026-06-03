#include "ModulationComponent.h"

ModulationComponent::ModulationComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("MODULATION", apvts, "mod_bypass", 5)
{
    modelLabel.setText("MODEL", juce::dontSendNotification);
    modelLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    modelLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
    addAndMakeVisible(modelLabel);

    modelSelector.addItem("Boss CE-2 Chorus", 1);
    modelSelector.addItem("Uni-Vibe",         2);
    modelSelector.setSelectedId(1);
    addAndMakeVisible(modelSelector);
    modelAttach = std::make_unique<CBA>(apvts, "mod_type", modelSelector);

    // ── Shared knobs ──────────────────────────────────────────────────────
    configureKnob(rateSlider,        rateLabel,        "RATE",         *this);
    configureKnob(depthSlider,       depthLabel,       "DEPTH",        *this);
    configureKnob(mixSlider,         mixLabel,         "MIX",          *this);
    configureKnob(stereoWidthSlider, stereoWidthLabel, "STEREO WIDTH", *this);

    rateAttach        = std::make_unique<SA>(apvts, "mod_rate",        rateSlider);
    depthAttach       = std::make_unique<SA>(apvts, "mod_depth",       depthSlider);
    mixAttach         = std::make_unique<SA>(apvts, "mod_mix",         mixSlider);
    stereoWidthAttach = std::make_unique<SA>(apvts, "mod_stereowidth", stereoWidthSlider);

    // ── CE-2 Chorus: preamp toggle ────────────────────────────────────────
    preampBtn.setButtonText("PREAMP");
    preampBtn.setClickingTogglesState(true);
    addAndMakeVisible(preampBtn);
    preampLabel.setText("INPUT PREAMP (CE-2)", juce::dontSendNotification);
    preampLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    preampLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
    addAndMakeVisible(preampLabel);
    preampAttach = std::make_unique<BA>(apvts, "mod_preamp", preampBtn);

    // ── Uni-Vibe: vibrato mode + output level ─────────────────────────────
    vibratoBtn.setButtonText("VIBRATO");
    vibratoBtn.setClickingTogglesState(true);
    addAndMakeVisible(vibratoBtn);
    vibratoLabel.setText("VIBRATO MODE (Uni-Vibe)", juce::dontSendNotification);
    vibratoLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    vibratoLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
    addAndMakeVisible(vibratoLabel);
    vibratoAttach = std::make_unique<BA>(apvts, "mod_vibratomode", vibratoBtn);

    configureKnob(outLevelSlider, outLevelLabel, "OUT LEVEL", *this);
    outLevelAttach = std::make_unique<SA>(apvts, "mod_outlevel", outLevelSlider);
}

void ModulationComponent::resized() {
    BlockComponentBase::resized();

    const int kW    = getWidth();
    const int top   = kHeaderH + 16;

    // Model selector row
    modelLabel.setBounds(8, top, 50, 20);
    modelSelector.setBounds(62, top, 200, 24);

    // 4 shared knobs + output level knob in one row
    const int knobTop = top + 40;
    const int knobW   = 100, knobH = 100;
    const int totalW  = 5 * knobW;
    const int x0      = (kW - totalW) / 2;

    auto col = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, knobTop, knobW, knobH); };
    layoutKnob(rateSlider,        rateLabel,        col(0));
    layoutKnob(depthSlider,       depthLabel,       col(1));
    layoutKnob(mixSlider,         mixLabel,         col(2));
    layoutKnob(stereoWidthSlider, stereoWidthLabel, col(3));
    layoutKnob(outLevelSlider,    outLevelLabel,    col(4));

    // Toggle row below knobs
    const int toggleY = knobTop + knobH + 16;
    preampBtn.setBounds   (x0,        toggleY, 90, 24);
    preampLabel.setBounds (x0 + 96,   toggleY + 4, 160, 18);
    vibratoBtn.setBounds  (x0 + 280,  toggleY, 90, 24);
    vibratoLabel.setBounds(x0 + 376,  toggleY + 4, 180, 18);
}
