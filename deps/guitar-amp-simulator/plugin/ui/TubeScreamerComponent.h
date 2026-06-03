#pragma once
#include "BlockComponentBase.h"
#include <functional>

// Unified overdrive/distortion panel.
// Supports TS-808 (drive/tone/level/mix), Life Pedal (adds octave), and
// NAM (input/level/mix + a model file loader).
class DriveComponent : public BlockComponentBase {
public:
    explicit DriveComponent(juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

    // Called on the message thread after the user picks a NAM file.
    std::function<void(const juce::File&)> onNamLoaded;

    // Update the displayed NAM filename label (called from PluginEditor on restore).
    void setNamFilename(const juce::String& name);

private:
    // Model selector
    juce::ComboBox modelSelector;
    juce::Label    modelSelectorLabel;

    // Shared knobs
    juce::Slider driveSlider, toneSlider, levelSlider, mixSlider;
    juce::Label  driveLabel,  toneLabel,  levelLabel,  mixLabel;

    // Life Pedal only
    juce::Slider octaveSlider;
    juce::Label  octaveLabel;

    // NAM only
    juce::TextButton namLoadButton { "LOAD NAM MODEL..." };
    juce::Label      namFileLabel;

    using SA  = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<CBA> modelAttach;
    std::unique_ptr<SA>  driveAttach, toneAttach, levelAttach, mixAttach, octaveAttach;

    void updateModeVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriveComponent)
};
