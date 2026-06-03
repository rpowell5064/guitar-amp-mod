#include "TubeScreamerComponent.h"

DriveComponent::DriveComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("OVERDRIVE", apvts, "drive_bypass", 2)
{
    // Model selector
    modelSelector.addItem("TS-808",       1);
    modelSelector.addItem("Life Pedal",  2);
    modelSelector.addItem("NAM",         3);
    modelSelector.addItem("ProCo RAT",  4);
    modelSelector.addItem("Big Muff Pi", 5);
    modelSelector.onChange = [this] { updateModeVisibility(); resized(); };
    addAndMakeVisible(modelSelector);
    modelSelectorLabel.setText("MODEL", juce::dontSendNotification);
    modelSelectorLabel.setJustificationType(juce::Justification::centred);
    modelSelectorLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    modelSelectorLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(modelSelectorLabel);
    modelAttach = std::make_unique<CBA>(apvts, "drive_model", modelSelector);

    // Shared knobs
    configureKnob(driveSlider, driveLabel, "DRIVE", *this);
    configureKnob(toneSlider,  toneLabel,  "TONE",  *this);
    configureKnob(levelSlider, levelLabel, "LEVEL", *this);
    configureKnob(mixSlider,   mixLabel,   "MIX",   *this);

    driveAttach = std::make_unique<SA>(apvts, "drive_drive", driveSlider);
    toneAttach  = std::make_unique<SA>(apvts, "drive_tone",  toneSlider);
    levelAttach = std::make_unique<SA>(apvts, "drive_level", levelSlider);
    mixAttach   = std::make_unique<SA>(apvts, "drive_mix",   mixSlider);

    // Life Pedal octave
    configureKnob(octaveSlider, octaveLabel, "OCTAVE", *this);
    octaveAttach = std::make_unique<SA>(apvts, "drive_octave", octaveSlider);

    // NAM file loader
    namLoadButton.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Load NAM Model", juce::File{}, "*.nam");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc) {
                const auto files = fc.getResults();
                if (files.isEmpty()) return;
                const juce::File f = files[0];
                namFileLabel.setText(f.getFileName(), juce::dontSendNotification);
                if (onNamLoaded) onNamLoaded(f);
            });
    };
    addAndMakeVisible(namLoadButton);

    namFileLabel.setText("No model loaded", juce::dontSendNotification);
    namFileLabel.setJustificationType(juce::Justification::centredLeft);
    namFileLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    namFileLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
    addAndMakeVisible(namFileLabel);

    updateModeVisibility();
}

void DriveComponent::setNamFilename(const juce::String& name) {
    namFileLabel.setText(name, juce::dontSendNotification);
}

void DriveComponent::updateModeVisibility() {
    const int mode        = modelSelector.getSelectedId();
    const bool isTS808    = (mode == 1);
    const bool isLife     = (mode == 2);
    const bool isNam      = (mode == 3);
    const bool isRAT      = (mode == 4);
    const bool isBigMuff  = (mode == 5);

    toneSlider.setVisible(isTS808 || isLife || isRAT || isBigMuff);
    toneLabel .setVisible(isTS808 || isLife || isRAT || isBigMuff);
    // Rename tone label contextually: "FILTER" for RAT (treble-cut), "TONE" for all others.
    toneLabel.setText(isRAT ? "FILTER" : "TONE", juce::dontSendNotification);

    mixSlider.setVisible(isTS808 || isLife || isNam);
    mixLabel .setVisible(isTS808 || isLife || isNam);

    octaveSlider.setVisible(isLife); octaveLabel.setVisible(isLife);
    namLoadButton.setVisible(isNam);
    namFileLabel .setVisible(isNam);
}

void DriveComponent::resized() {
    BlockComponentBase::resized();

    const int kW   = getWidth();
    const int topY = kHeaderH + 10;
    const int selW = 160, selH = 28;
    modelSelectorLabel.setBounds(8, topY, 52, selH);
    modelSelector.setBounds(64, topY, selW, selH);

    const int knobW = 110, knobH = 110;
    const int row1Y = topY + selH + 12;
    const int mode  = modelSelector.getSelectedId();

    if (mode == 1) {
        // TS-808: drive, tone, level, mix
        const int totalW = 4 * knobW;
        const int x0     = (kW - totalW) / 2;
        auto c = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, row1Y, knobW, knobH); };
        layoutKnob(driveSlider, driveLabel, c(0));
        layoutKnob(toneSlider,  toneLabel,  c(1));
        layoutKnob(levelSlider, levelLabel, c(2));
        layoutKnob(mixSlider,   mixLabel,   c(3));
    } else if (mode == 2) {
        // Life Pedal: drive, tone, octave, level, mix
        const int totalW = 5 * knobW;
        const int x0     = (kW - totalW) / 2;
        auto c = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, row1Y, knobW, knobH); };
        layoutKnob(driveSlider,  driveLabel,  c(0));
        layoutKnob(toneSlider,   toneLabel,   c(1));
        layoutKnob(octaveSlider, octaveLabel, c(2));
        layoutKnob(levelSlider,  levelLabel,  c(3));
        layoutKnob(mixSlider,    mixLabel,    c(4));
    } else if (mode == 4) {
        // ProCo RAT: distortion (drive), filter (tone), volume (level)
        const int totalW = 3 * knobW;
        const int x0     = (kW - totalW) / 2;
        auto c = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, row1Y, knobW, knobH); };
        layoutKnob(driveSlider, driveLabel, c(0));
        layoutKnob(toneSlider,  toneLabel,  c(1));
        layoutKnob(levelSlider, levelLabel, c(2));
    } else if (mode == 5) {
        // Big Muff Pi: sustain (drive), tone, volume (level)
        const int totalW = 3 * knobW;
        const int x0     = (kW - totalW) / 2;
        auto c = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, row1Y, knobW, knobH); };
        layoutKnob(driveSlider, driveLabel, c(0));
        layoutKnob(toneSlider,  toneLabel,  c(1));
        layoutKnob(levelSlider, levelLabel, c(2));
    } else if (mode == 3) {
        // NAM: load button + filename, then drive (input), level, mix
        const int btnH = 30, btnW = 200, lblH = 20;
        namLoadButton.setBounds((kW - btnW) / 2, row1Y, btnW, btnH);
        namFileLabel.setBounds((kW - btnW) / 2, row1Y + btnH + 4, btnW, lblH);

        const int knobRowY = row1Y + btnH + lblH + 12;
        const int totalW   = 3 * knobW;
        const int x0       = (kW - totalW) / 2;
        auto c = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, knobRowY, knobW, knobH); };
        layoutKnob(driveSlider, driveLabel, c(0));
        layoutKnob(levelSlider, levelLabel, c(1));
        layoutKnob(mixSlider,   mixLabel,   c(2));
    }
}
