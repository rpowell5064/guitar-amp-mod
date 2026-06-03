#include "Standalone.h"

// ── AmpSimStandaloneWindow ────────────────────────────────────────────────────

AmpSimStandaloneWindow::AmpSimStandaloneWindow()
    : juce::StandaloneFilterWindow(
          "Guitar Amp Simulator",
          juce::Colour(0xFF1A1A2E),   // background colour
          nullptr,                     // use default PropertySet for settings
          true)                        // take ownership of PropertySet
{
    setUsingNativeTitleBar(true);

    // Set a sensible default window size; the inner component will dictate the
    // actual size once the editor is created.
    centreWithSize(960, 630);
    setResizable(true, false);
    setResizeLimits(800, 480, 1920, 1080);

    setVisible(true);
}

// ── AmpSimApplication ─────────────────────────────────────────────────────────

void AmpSimApplication::initialise(const juce::String& /*commandLine*/) {
    mainWindow.reset(new AmpSimStandaloneWindow());
}

void AmpSimApplication::shutdown() {
    // StandaloneFilterWindow saves device settings when destroyed.
    mainWindow = nullptr;
}

void AmpSimApplication::systemRequestedQuit() {
    quit();
}
