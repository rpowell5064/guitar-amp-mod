#pragma once
#include <JuceHeader.h>

class GuitarAmpAudioProcessor;

// Compact preset browser for the top bar.
// Lists *.xml files found in the user's preset directory, supports save and load.
class PresetBrowserComponent : public juce::Component {
public:
    explicit PresetBrowserComponent(GuitarAmpAudioProcessor& proc);
    void resized() override;
    void refreshPresetList();

    // Fired after a preset is loaded that contains an IR or NAM file.
    std::function<void(const juce::File&)> onIRRestored;
    std::function<void(const juce::File&)> onNamRestored;

private:
    GuitarAmpAudioProcessor& processor;

    juce::ComboBox presetList;
    juce::TextButton saveButton{"SAVE"};
    juce::TextButton loadButton{"LOAD"};
    juce::Label      titleLabel;

    std::vector<juce::File> presetFiles;

    static juce::File getPresetDirectory();
    void saveCurrentPreset();
    void loadSelectedPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowserComponent)
};
