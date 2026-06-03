#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/AmpSimLookAndFeel.h"
#include "ui/LevelMeter.h"
#include "ui/HorizontalChainComponent.h"
#include "ui/PresetBrowserComponent.h"
#include "ui/NoiseGateComponent.h"
#include "ui/PitchComponent.h"
#include "ui/TubeScreamerComponent.h"
#include "ui/AmpComponent.h"
#include "ui/CabinetIRComponent.h"
#include "ui/ModulationComponent.h"
#include "ui/DelayComponent.h"
#include "ui/ReverbComponent.h"
#include "ui/OutputEQComponent.h"
#include "ui/OutputComponent.h"
#include "ui/CompressorComponent.h"

// Main plugin window.
//
// Layout (default 960 × 600):
//  ┌──────────────────────────────────────────────────────────────────┐
//  │  [PRESET BROWSER]                      [IN METER] [OUT METER]   │  50px
//  ├──────────────────────────────────────────────────────────────────┤
//  │                                                                  │
//  │              Block settings panel (full width)                   │  ~400px
//  │                                                                  │
//  ├──────────────────────────────────────────────────────────────────┤
//  │  [GATE] → [DRIVE] → [AMP] → [CAB] → [DLY] → [VERB] →  [+]     │  122px
//  ├──────────────────────────────────────────────────────────────────┤
//  │  CPU: X%   [Oversample ▼]                                        │  28px
//  └──────────────────────────────────────────────────────────────────┘
class GuitarAmpEditor : public juce::AudioProcessorEditor,
                        private juce::Timer {
public:
    explicit GuitarAmpEditor(GuitarAmpAudioProcessor& processor);
    ~GuitarAmpEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    GuitarAmpAudioProcessor& processor;
    AmpSimLookAndFeel laf;

    // ── Top bar ───────────────────────────────────────────────────────────
    juce::Label            titleLabel;
    PresetBrowserComponent presetBrowser;
    LevelMeter             inputMeter;
    LevelMeter             outputMeter;
    juce::Label            inMeterLabel, outMeterLabel;

    // ── Horizontal effects chain ──────────────────────────────────────────
    HorizontalChainComponent chainComp;

    // ── Block panels (only one visible at a time) ─────────────────────────
    std::unique_ptr<NoiseGateComponent>  gateComp;
    std::unique_ptr<PitchComponent>      pitchComp;
    std::unique_ptr<DriveComponent>      driveComp;
    std::unique_ptr<AmpComponent>        ampComp;
    std::unique_ptr<CabinetIRComponent>  cabComp;
    std::unique_ptr<ModulationComponent> modComp;
    std::unique_ptr<DelayComponent>      delayComp;
    std::unique_ptr<ReverbComponent>     reverbComp;
    std::unique_ptr<OutputEQComponent>   eqComp;
    std::unique_ptr<CompressorComponent> compComp;
    std::unique_ptr<OutputComponent>     outComp;

    // ── Bottom bar ────────────────────────────────────────────────────────
    juce::Label    cpuLabel;
    juce::ComboBox oversampleBox;

    // panelOrder_[displayPos] = origIdx for the 10 reorderable effects
    int panelOrder_[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int selectedOrigIdx_ = 3; // default → Amp

    static const std::array<HorizontalChainComponent::EffectInfo,
                             HorizontalChainComponent::kNumEffects> kEffectInfos;

    void buildBlockComponents();
    void selectBlock(int origIdx); // origIdx 0-9 = effect, 10 = Output
    void rebuildChainState();       // push current order+active state to chainComp
    void timerCallback() override;

    static constexpr int kTopH  = 50;
    static constexpr int kChainH= 122;
    static constexpr int kBotH  = 28;
    static constexpr int kMinW  = 800;
    static constexpr int kMinH  = 480;
    static constexpr int kDefW  = 960;
    static constexpr int kDefH  = 600;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarAmpEditor)
};
