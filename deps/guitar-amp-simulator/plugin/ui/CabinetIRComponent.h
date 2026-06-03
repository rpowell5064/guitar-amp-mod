#pragma once
#include "BlockComponentBase.h"
#include <functional>

// Cabinet / IR loader component.
// Provides a file-browser button to load an IR WAV, plus EQ trim and mix controls.
class CabinetIRComponent : public BlockComponentBase {
public:
    explicit CabinetIRComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

    // Called when user picks a new IR file
    std::function<void(const juce::File&)> onIRLoaded;

    // Update the displayed filename (call after restoring a preset).
    void setIRFilename(const juce::String& name);

private:
    juce::TextButton loadIRButton;
    juce::Label      irFilenameLabel;

    juce::Slider lowCutSlider, highCutSlider, mixSlider;
    juce::Label  lowCutLabel,  highCutLabel,  mixLabel;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> lowCutAttach, highCutAttach, mixAttach;

    std::unique_ptr<juce::FileChooser> fileChooser;

    void openIRBrowser();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CabinetIRComponent)
};
