#include "DelayComponent.h"

DelayComponent::DelayComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("DELAY", apvts, "delay_bypass", 6)
{
    modeSelector.addItem("Digital",        1);
    modeSelector.addItem("Tape",           2);
    modeSelector.addItem("Binson Echorec", 3);
    modeSelector.onChange = [this] { updateModeVisibility(); resized(); };
    addAndMakeVisible(modeSelector);
    modeLabel.setText("MODE", juce::dontSendNotification);
    modeLabel.setJustificationType(juce::Justification::centred);
    modeLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    modeLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(modeLabel);
    modeAttach = std::make_unique<CBA>(apvts, "delay_mode", modeSelector);

    // Shared
    configureKnob(timeSlider, timeLabel, "TIME",     *this);
    configureKnob(fbSlider,   fbLabel,   "FEEDBACK", *this);
    configureKnob(mixSlider,  mixLabel,  "MIX",      *this);
    configureKnob(loSlider,   loLabel,   "LO CUT",   *this);
    configureKnob(hiSlider,   hiLabel,   "HI CUT",   *this);

    timeAttach = std::make_unique<SA>(apvts, "delay_time",     timeSlider);
    fbAttach   = std::make_unique<SA>(apvts, "delay_feedback", fbSlider);
    mixAttach  = std::make_unique<SA>(apvts, "delay_mix",      mixSlider);
    loAttach   = std::make_unique<SA>(apvts, "delay_lowcut",   loSlider);
    hiAttach   = std::make_unique<SA>(apvts, "delay_highcut",  hiSlider);

    // Digital-specific
    configureKnob(stereoSlider, stereoLabel, "STEREO W", *this);
    stereoAttach = std::make_unique<SA>(apvts, "delay_stereowidth", stereoSlider);

    // Tape-specific
    configureKnob(wowSlider,     wowLabel,     "WOW",       *this);
    configureKnob(flutterSlider, flutterLabel, "FLUTTER",   *this);
    configureKnob(satSlider,     satLabel,     "SATURATION",*this);
    configureKnob(ageSlider,     ageLabel,     "TAPE AGE",  *this);

    wowAttach     = std::make_unique<SA>(apvts, "delay_wow",     wowSlider);
    flutterAttach = std::make_unique<SA>(apvts, "delay_flutter", flutterSlider);
    satAttach     = std::make_unique<SA>(apvts, "delay_sat",     satSlider);
    ageAttach     = std::make_unique<SA>(apvts, "delay_tapeage", ageSlider);

    // Echorec-specific — 4 head toggle buttons
    static const char* kHeadNames[] = { "HEAD 1", "HEAD 2", "HEAD 3", "HEAD 4" };
    static const char* kHeadIds[]   = { "delay_head1", "delay_head2",
                                        "delay_head3", "delay_head4" };
    for (int i = 0; i < 4; ++i) {
        headButton[i].setButtonText(juce::String(i + 1));
        headButton[i].setClickingTogglesState(true);
        addAndMakeVisible(headButton[i]);

        headLabel[i].setText(kHeadNames[i], juce::dontSendNotification);
        headLabel[i].setJustificationType(juce::Justification::centredTop);
        headLabel[i].setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        headLabel[i].setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
        addAndMakeVisible(headLabel[i]);

        headAttach[i] = std::make_unique<BA>(apvts, kHeadIds[i], headButton[i]);
    }

    configureKnob(noiseSlider, noiseLabel, "NOISE", *this);
    noiseAttach = std::make_unique<SA>(apvts, "delay_noise", noiseSlider);

    updateModeVisibility();
}

void DelayComponent::updateModeVisibility() {
    const int mode      = modeSelector.getSelectedId();
    const bool isDigital = (mode == 1);
    const bool isTape    = (mode == 2);
    const bool isEchorec = (mode == 3);

    stereoSlider.setVisible(isDigital); stereoLabel.setVisible(isDigital);

    wowSlider.setVisible(isTape);     wowLabel.setVisible(isTape);
    flutterSlider.setVisible(isTape); flutterLabel.setVisible(isTape);
    satSlider.setVisible(isTape);     satLabel.setVisible(isTape);
    ageSlider.setVisible(isTape);     ageLabel.setVisible(isTape);

    for (int i = 0; i < 4; ++i) {
        headButton[i].setVisible(isEchorec);
        headLabel[i].setVisible(isEchorec);
    }
    noiseSlider.setVisible(isEchorec); noiseLabel.setVisible(isEchorec);
}

void DelayComponent::resized() {
    BlockComponentBase::resized();

    const int kW     = getWidth();
    const int topY   = kHeaderH + 10;
    const int selW   = 180, selH = 28;
    modeLabel.setBounds(8, topY, 50, selH);
    modeSelector.setBounds(62, topY, selW, selH);

    const int knobW = 95, knobH = 100;
    const int row1Y = topY + selH + 10;
    const int mode  = modeSelector.getSelectedId();

    // Row 1: time, feedback, mix, lo cut, hi cut (always visible)
    {
        const int totalW = 5 * knobW;
        const int x0     = (kW - totalW) / 2;
        auto c = [&](int i){ return juce::Rectangle<int>(x0 + i * knobW, row1Y, knobW, knobH); };
        layoutKnob(timeSlider, timeLabel, c(0));
        layoutKnob(fbSlider,   fbLabel,   c(1));
        layoutKnob(mixSlider,  mixLabel,  c(2));
        layoutKnob(loSlider,   loLabel,   c(3));
        layoutKnob(hiSlider,   hiLabel,   c(4));
    }

    // Row 2: mode-specific controls
    const int row2Y = row1Y + knobH + 8;

    if (mode == 1) {
        // Digital: stereo width knob
        layoutKnob(stereoSlider, stereoLabel,
                   juce::Rectangle<int>((kW - knobW) / 2, row2Y, knobW, knobH));
    } else if (mode == 2) {
        // Tape: wow, flutter, saturation, tape age
        const int totalW = 4 * knobW;
        const int x0     = (kW - totalW) / 2;
        auto c = [&](int i){ return juce::Rectangle<int>(x0 + i * knobW, row2Y, knobW, knobH); };
        layoutKnob(wowSlider,     wowLabel,     c(0));
        layoutKnob(flutterSlider, flutterLabel, c(1));
        layoutKnob(satSlider,     satLabel,     c(2));
        layoutKnob(ageSlider,     ageLabel,     c(3));
    } else if (mode == 3) {
        // Echorec: 4 head toggle buttons + noise knob
        const int btnW = 64, btnH = 36, labelH = 20;
        const int groupW = 4 * btnW + 16 + knobW; // buttons + gap + noise knob
        const int x0     = (kW - groupW) / 2;
        const int btnY   = row2Y + (knobH - btnH - labelH) / 2;

        for (int i = 0; i < 4; ++i) {
            headButton[i].setBounds(x0 + i * btnW, btnY, btnW - 6, btnH);
            headLabel[i].setBounds(x0 + i * btnW, btnY + btnH + 2, btnW - 6, labelH);
        }
        layoutKnob(noiseSlider, noiseLabel,
                   juce::Rectangle<int>(x0 + 4 * btnW + 16, row2Y, knobW, knobH));
    }
}
