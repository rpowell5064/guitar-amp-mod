#include "CabinetIRComponent.h"

CabinetIRComponent::CabinetIRComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("CABINET / IR", apvts, "cab_bypass", 4)
{
    loadIRButton.setButtonText("LOAD IR");
    loadIRButton.onClick = [this] { openIRBrowser(); };
    addAndMakeVisible(loadIRButton);

    irFilenameLabel.setText("No IR loaded", juce::dontSendNotification);
    irFilenameLabel.setJustificationType(juce::Justification::centredLeft);
    irFilenameLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    irFilenameLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(irFilenameLabel);

    configureKnob(lowCutSlider,  lowCutLabel,  "LOW CUT",  *this);
    configureKnob(highCutSlider, highCutLabel, "HIGH CUT", *this);
    configureKnob(mixSlider,     mixLabel,     "MIX",      *this);

    lowCutAttach  = std::make_unique<SA>(apvts, "cab_lowcut",  lowCutSlider);
    highCutAttach = std::make_unique<SA>(apvts, "cab_highcut", highCutSlider);
    mixAttach     = std::make_unique<SA>(apvts, "cab_mix",     mixSlider);
}

void CabinetIRComponent::resized() {
    BlockComponentBase::resized();

    const int kW  = getWidth();
    const int row1Y = kHeaderH + 12;

    loadIRButton.setBounds(16, row1Y, 90, 28);
    irFilenameLabel.setBounds(116, row1Y, kW - 220, 28);

    const int knobW  = 110, knobH = 110;
    const int top    = row1Y + 50;
    const int totalW = 3 * knobW;
    const int x0     = (kW - totalW) / 2;

    auto col = [&](int i) {
        return juce::Rectangle<int>(x0 + i * knobW, top, knobW, knobH);
    };
    layoutKnob(lowCutSlider,  lowCutLabel,  col(0));
    layoutKnob(highCutSlider, highCutLabel, col(1));
    layoutKnob(mixSlider,     mixLabel,     col(2));
}

void CabinetIRComponent::setIRFilename(const juce::String& filename) {
    irFilenameLabel.setText(filename, juce::dontSendNotification);
}

void CabinetIRComponent::openIRBrowser() {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Impulse Response",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.aiff;*.flac");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto result = fc.getResult();
            if (result.existsAsFile()) {
                irFilenameLabel.setText(result.getFileName(), juce::dontSendNotification);
                if (onIRLoaded) onIRLoaded(result);
            }
        });
}
