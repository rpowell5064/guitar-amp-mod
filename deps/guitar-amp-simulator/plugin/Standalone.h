#pragma once
#include <JuceHeader.h>

// Standalone application wrapper.
//
// JUCE's StandaloneFilterWindow handles:
//   • Creating and owning the GuitarAmpAudioProcessor
//   • Setting up juce::AudioDeviceManager for native audio I/O
//   • Saving/loading audio device and plugin state from a settings file
//   • Wrapping the AudioProcessorEditor in a resizable desktop window
//
// We subclass it only to customise the window title bar colour and to
// provide the application settings file path.

class AmpSimStandaloneWindow : public juce::StandaloneFilterWindow {
public:
    AmpSimStandaloneWindow();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpSimStandaloneWindow)
};

// ── JUCEApplication ──────────────────────────────────────────────────────────

class AmpSimApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName()    override { return "Guitar Amp Sim"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed()           override { return false; }

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;

private:
    std::unique_ptr<AmpSimStandaloneWindow> mainWindow;
};

// Required JUCE macro — defines main() / WinMain entry point.
START_JUCE_APPLICATION(AmpSimApplication)
