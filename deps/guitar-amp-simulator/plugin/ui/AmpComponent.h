#pragma once
#include "BlockComponentBase.h"
#include <functional>

// Amp front-panel component.
//
// Two tabs share the content area below the chassis art:
//   PREAMP   — model selector, shared knobs (gain/bass/mid/treble/presence/master/sag),
//              model-specific controls (Sunn bright+channel-link, RVB clean, EVH blue ch)
//   POWER AMP — tube-type selector + 6 power-amp knobs + air-feel toggle
//
// The power-amp bypass toggle lives in the header strip (always visible).
class AmpComponent : public BlockComponentBase {
public:
    explicit AmpComponent(juce::AudioProcessorValueTreeState& apvts);
    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int kChassisArtH = 62;
    static constexpr int kTabBarH     = 26;

    // Called when the user picks a new NAM model file.
    std::function<void(const juce::File&)> onNeuralModelLoaded;

    // Update the displayed filename (call after loading a model from a preset).
    void setNeuralModelFilename(const juce::String& name);

private:
    juce::AudioProcessorValueTreeState& apvtsRef;

    // ── Tab toggle ─────────────────────────────────────────────────────────────
    juce::TextButton preampTabBtn { "PREAMP" };
    juce::TextButton pampTabBtn   { "POWER AMP" };
    bool showPowerAmp_ = false;

    // ── Model selector (PREAMP tab) ────────────────────────────────────────────
    juce::ComboBox modelSelector;
    juce::Label    modelLabel;

    juce::TextButton            loadModelButton { "LOAD MODEL" };
    juce::Label                 modelFilenameLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::ToggleButton pampBypassButton;
    juce::Label        pampBypassLabel;

    // ── Preamp knobs ───────────────────────────────────────────────────────────
    juce::Slider gainSlider, bassSlider, midSlider, trebleSlider,
                 presSlider, masterSlider, sagSlider, namGainSlider;
    juce::Label  gainLabel,  bassLabel,  midLabel,  trebleLabel,
                 presLabel,  masterLabel, sagLabel,  namGainLabel;

    // ── Model-specific controls ────────────────────────────────────────────────
    // Sunn Model T — Normal channel (Ch1)
    juce::Slider       sunnVol1Slider, sunnBass1Slider, sunnMid1Slider, sunnTreble1Slider;
    juce::Label        sunnVol1Label,  sunnBass1Label,  sunnMid1Label,  sunnTreble1Label;
    juce::ToggleButton sunnBrightBtn;
    juce::Label        sunnBrightLabel;
    // Sunn Model T — Brite channel (Ch2)
    juce::Slider       sunnVol2Slider, sunnBass2Slider, sunnMid2Slider, sunnTreble2Slider;
    juce::Label        sunnVol2Label,  sunnBass2Label,  sunnMid2Label,  sunnTreble2Label;
    juce::ToggleButton sunnBright2Btn;
    juce::Label        sunnBright2Label;
    // Sunn Model T — channel link + input pad
    juce::ComboBox     sunnLinkBox;
    juce::Label        sunnLinkLabel;
    juce::ToggleButton sunnInputPadBtn;
    juce::Label        sunnInputPadLabel;
    // Orange Rockerverb 50
    juce::ToggleButton rvbCleanBtn;
    juce::Label        rvbCleanLabel;
    juce::Slider       rvbGainSlider,   rvbBassSlider,   rvbMidSlider,
                       rvbTrebleSlider, rvbMasterSlider, rvbSagSlider;
    juce::Label        rvbGainLabel,    rvbBassLabel,    rvbMidLabel,
                       rvbTrebleLabel,  rvbMasterLabel,  rvbSagLabel;
    // EVH 5150 III
    juce::ToggleButton evhRedBtn;
    juce::Label        evhRedLabel;
    juce::Slider       evhResSlider;
    juce::Label        evhResLabel;

    // ── Power amp controls (POWER AMP tab) ────────────────────────────────────
    juce::ComboBox tubeTypeBox;
    juce::Label    tubeTypeLabel;
    juce::Slider   pampPresSlider, pampDepthSlider,  pampSagSlider,
                   pampMasterSlider, pampNFBSlider,  pampResSlider;
    juce::Label    pampPresLabel,  pampDepthLabel,   pampSagLabel,
                   pampMasterLabel, pampNFBLabel,    pampResLabel;
    juce::ToggleButton airFeelBtn;
    juce::Label        airFeelLabel;

    // ── Attachments ────────────────────────────────────────────────────────────
    using SA  = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA  = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<CBA> modelAttach;
    std::unique_ptr<BA>  pampBypassAttach;
    std::unique_ptr<SA>  gainAttach, bassAttach, midAttach, trebleAttach,
                         presAttach, masterAttach, sagAttach, namGainAttach;

    std::unique_ptr<SA>  sunnVol1Attach,  sunnBass1Attach,  sunnMid1Attach,  sunnTreble1Attach;
    std::unique_ptr<BA>  sunnBrightAttach;
    std::unique_ptr<SA>  sunnVol2Attach,  sunnBass2Attach,  sunnMid2Attach,  sunnTreble2Attach;
    std::unique_ptr<BA>  sunnBright2Attach;
    std::unique_ptr<CBA> sunnLinkAttach;
    std::unique_ptr<BA>  sunnInputPadAttach;
    std::unique_ptr<BA>  rvbCleanAttach;
    std::unique_ptr<SA>  rvbGainAttach, rvbBassAttach, rvbMidAttach,
                         rvbTrebleAttach, rvbMasterAttach, rvbSagAttach;
    std::unique_ptr<BA>  evhRedAttach;
    std::unique_ptr<SA>  evhResAttach;

    std::unique_ptr<CBA> tubeTypeAttach;
    std::unique_ptr<SA>  pampPresAttach, pampDepthAttach, pampSagAttach,
                         pampMasterAttach, pampNFBAttach, pampResAttach;
    std::unique_ptr<BA>  airFeelAttach;

    // ── Helpers ────────────────────────────────────────────────────────────────
    void updateTabUI();        // show/hide all controls based on showPowerAmp_ + model
    void updateModelUI();      // accent colours + chassis repaint (called from updateTabUI)
    void openModelBrowser();

    static void styleTab(juce::TextButton& btn, bool active);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpComponent)
};
