#include "PluginEditor.h"

const std::array<HorizontalChainComponent::EffectInfo,
                 HorizontalChainComponent::kNumEffects>
GuitarAmpEditor::kEffectInfos = {{
    { "Noise Gate",   "GATE",  "gate_bypass"   },
    { "Whammy",       "PITCH", "pitch_bypass"  },
    { "Overdrive",    "DRIVE", "drive_bypass"  },
    { "Amplifier",    "AMP",   "amp_bypass"    },
    { "Cabinet",      "CAB",   "cab_bypass"    },
    { "Modulation",   "MOD",   "mod_bypass"    },
    { "Delay",        "DLY",   "delay_bypass"  },
    { "Plate Reverb", "VERB",  "reverb_bypass" },
    { "Speaker EQ",   "EQ",    "eq_bypass"     },
    { "Compressor",   "COMP",  "comp_bypass"   },
}};

GuitarAmpEditor::GuitarAmpEditor(GuitarAmpAudioProcessor& proc)
    : AudioProcessorEditor(&proc),
      processor(proc),
      presetBrowser(proc),
      chainComp(proc.apvts, kEffectInfos)
{
    setLookAndFeel(&laf);

    // ── Title ─────────────────────────────────────────────────────────────────
    titleLabel.setText("GUITAR AMP SIM", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold")));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFE94560));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // ── Preset browser ────────────────────────────────────────────────────────
    addAndMakeVisible(presetBrowser);

    // ── Meters ────────────────────────────────────────────────────────────────
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);

    inMeterLabel.setText("IN",  juce::dontSendNotification);
    outMeterLabel.setText("OUT", juce::dontSendNotification);
    for (auto* l : { &inMeterLabel, &outMeterLabel }) {
        l->setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
        l->setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
        l->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*l);
    }

    // ── Horizontal effects chain ──────────────────────────────────────────────
    chainComp.onBlockSelected = [this](int origIdx) {
        selectBlock(origIdx);
    };
    chainComp.onBlockDoubleClicked = [this](int origIdx) {
        selectBlock(origIdx);
    };
    chainComp.onBlockMoved = [this](int from, int to) {
        if (from == to) return;
        // from/to are display positions among the active effects only.
        // Map to logical display positions to pass to reorderBlock.
        const auto active = processor.getAllEffectActive();
        // Build active display-pos list
        std::vector<int> activeDisp; // activeDisp[i] = logical display pos of i-th active effect
        for (int dp = 0; dp < 10; ++dp) {
            const int orig = panelOrder_[dp];
            if (active[orig]) activeDisp.push_back(dp);
        }
        if (from >= (int)activeDisp.size() || to >= (int)activeDisp.size()) return;
        processor.reorderBlock(activeDisp[from], activeDisp[to]);
        auto order = processor.getLogicalOrder();
        for (int i = 0; i < 10; ++i) panelOrder_[i] = order[i];
        rebuildChainState();
    };
    chainComp.onBlockRemoved = [this](int origIdx) {
        processor.setEffectActive(origIdx, false);
        rebuildChainState();
    };
    chainComp.onBlockAdded = [this](int origIdx) {
        processor.setEffectActive(origIdx, true);
        rebuildChainState();
        selectBlock(origIdx);
    };
    addAndMakeVisible(chainComp);

    // ── Block components ──────────────────────────────────────────────────────
    buildBlockComponents();

    // ── Bottom bar ────────────────────────────────────────────────────────────
    cpuLabel.setText("CPU: --", juce::dontSendNotification);
    cpuLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    cpuLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(cpuLabel);

    oversampleBox.addItem("1x (Off)", 1);
    oversampleBox.addItem("2x",       2);
    oversampleBox.addItem("4x",       3);
    oversampleBox.setSelectedId(1);
    addAndMakeVisible(oversampleBox);

    selectBlock(selectedOrigIdx_);
    startTimerHz(24);

    setResizable(true, true);
    setResizeLimits(kMinW, kMinH, 1920, 1080);
    setSize(kDefW, kDefH);
}

GuitarAmpEditor::~GuitarAmpEditor() {
    setLookAndFeel(nullptr);
}

void GuitarAmpEditor::buildBlockComponents() {
    auto& apvts = processor.apvts;

    gateComp   = std::make_unique<NoiseGateComponent>(apvts);
    pitchComp  = std::make_unique<PitchComponent>(apvts);
    driveComp  = std::make_unique<DriveComponent>(apvts);
    ampComp    = std::make_unique<AmpComponent>(apvts);
    cabComp    = std::make_unique<CabinetIRComponent>(apvts);
    modComp    = std::make_unique<ModulationComponent>(apvts);
    delayComp  = std::make_unique<DelayComponent>(apvts);
    reverbComp = std::make_unique<ReverbComponent>(apvts);
    eqComp       = std::make_unique<OutputEQComponent>(apvts);
    compComp     = std::make_unique<CompressorComponent>(apvts);
    outComp      = std::make_unique<OutputComponent>(apvts);

    driveComp->onNamLoaded = [this](const juce::File& f) {
        processor.loadOverdriveNam(f);
    };
    cabComp->onIRLoaded = [this](const juce::File& f) { processor.loadIR(f); };
    ampComp->onNeuralModelLoaded = [this](const juce::File& f) {
        processor.loadNeuralModel(f);
        if (!processor.getNamFile().existsAsFile())
            ampComp->setNeuralModelFilename("[unsupported] " + f.getFileName());
    };

    presetBrowser.onIRRestored = [this](const juce::File& f) {
        processor.loadIR(f);
        if (cabComp) cabComp->setIRFilename(f.getFileName());
    };
    presetBrowser.onNamRestored = [this](const juce::File& f) {
        processor.loadNeuralModel(f);
        if (ampComp) ampComp->setNeuralModelFilename(f.getFileName());
    };

    processor.onFilesRestored = [this] {
        if (cabComp && processor.getIRFile().existsAsFile())
            cabComp->setIRFilename(processor.getIRFile().getFileName());
        if (ampComp && processor.getNamFile().existsAsFile())
            ampComp->setNeuralModelFilename(processor.getNamFile().getFileName());
    };
    processor.onOverdriveNamRestored = [this] {
        if (driveComp && processor.getOverdriveNamFile().existsAsFile())
            driveComp->setNamFilename(processor.getOverdriveNamFile().getFileName());
    };

    if (processor.getIRFile().existsAsFile())
        cabComp->setIRFilename(processor.getIRFile().getFileName());
    if (processor.getNamFile().existsAsFile())
        ampComp->setNeuralModelFilename(processor.getNamFile().getFileName());
    if (processor.getOverdriveNamFile().existsAsFile())
        driveComp->setNamFilename(processor.getOverdriveNamFile().getFileName());

    // Sync chain order
    {
        auto order = processor.getLogicalOrder();
        for (int i = 0; i < 10; ++i) panelOrder_[i] = order[i];
        rebuildChainState();
    }

    processor.onChainReordered = [this] {
        auto order = processor.getLogicalOrder();
        for (int i = 0; i < 10; ++i) panelOrder_[i] = order[i];
        rebuildChainState();
    };
    processor.onEffectActiveChanged = [this] {
        rebuildChainState();
    };

    addAndMakeVisible(*gateComp);
    addAndMakeVisible(*pitchComp);
    addAndMakeVisible(*driveComp);
    addAndMakeVisible(*ampComp);
    addAndMakeVisible(*cabComp);
    addAndMakeVisible(*modComp);
    addAndMakeVisible(*delayComp);
    addAndMakeVisible(*reverbComp);
    addAndMakeVisible(*eqComp);
    addAndMakeVisible(*compComp);
    addAndMakeVisible(*outComp);
}

void GuitarAmpEditor::rebuildChainState() {
    const auto active = processor.getAllEffectActive();

    // Build active order: walk panelOrder_ (display positions) and include only active effects
    std::vector<int> activeOrder;
    for (int dp = 0; dp < 10; ++dp) {
        const int orig = panelOrder_[dp];
        if (active[orig]) activeOrder.push_back(orig);
    }

    chainComp.setChainState(activeOrder, active);
    chainComp.setSelectedOrigIdx(selectedOrigIdx_);
}

void GuitarAmpEditor::selectBlock(int origIdx) {
    selectedOrigIdx_ = origIdx;
    chainComp.setSelectedOrigIdx(origIdx);

    gateComp->setVisible  (origIdx == 0);
    pitchComp->setVisible (origIdx == 1);
    driveComp->setVisible (origIdx == 2);
    ampComp->setVisible   (origIdx == 3);
    cabComp->setVisible   (origIdx == 4);
    modComp->setVisible   (origIdx == 5);
    delayComp->setVisible (origIdx == 6);
    reverbComp->setVisible(origIdx == 7);
    eqComp->setVisible    (origIdx == 8);
    compComp->setVisible  (origIdx == 9);
    outComp->setVisible   (origIdx == 10);
}

void GuitarAmpEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xFF1A1A2E));

    // Top bar background
    g.setColour(juce::Colour(0xFF0F1A30));
    g.fillRect(0, 0, getWidth(), kTopH);

    // Bottom bar background
    g.setColour(juce::Colour(0xFF0F1A30));
    g.fillRect(0, getHeight() - kBotH, getWidth(), kBotH);

    // Accent stripe under top bar
    g.setColour(juce::Colour(0xFFE94560));
    g.fillRect(0, kTopH - 2, getWidth(), 2);

    // Divider above chain
    g.setColour(juce::Colour(0xFF0F1A30));
    g.fillRect(0, getHeight() - kBotH - kChainH, getWidth(), 1);
}

void GuitarAmpEditor::resized() {
    const int w      = getWidth();
    const int h      = getHeight();
    const int panelH = h - kTopH - kChainH - kBotH;

    // ── Top bar ───────────────────────────────────────────────────────────────
    const int meterW = 34, meterH = kTopH - 8;
    int mx = w - 6;

    mx -= meterW;
    outputMeter.setBounds(mx, 4, meterW, meterH);
    outMeterLabel.setBounds(mx, 4, meterW, 14);
    mx -= 4;
    mx -= meterW;
    inputMeter.setBounds(mx, 4, meterW, meterH);
    inMeterLabel.setBounds(mx, 4, meterW, 14);
    mx -= 8;

    titleLabel.setBounds(8, 0, 180, kTopH);
    presetBrowser.setBounds(192, 10, mx - 200, kTopH - 20);

    // ── Settings panel (full width) ───────────────────────────────────────────
    const juce::Rectangle<int> panelArea(0, kTopH, w, panelH);
    gateComp->setBounds  (panelArea);
    pitchComp->setBounds (panelArea);
    driveComp->setBounds (panelArea);
    ampComp->setBounds   (panelArea);
    cabComp->setBounds   (panelArea);
    modComp->setBounds   (panelArea);
    delayComp->setBounds (panelArea);
    reverbComp->setBounds(panelArea);
    eqComp->setBounds    (panelArea);
    compComp->setBounds  (panelArea);
    outComp->setBounds   (panelArea);

    // ── Horizontal chain ──────────────────────────────────────────────────────
    chainComp.setBounds(0, kTopH + panelH, w, kChainH);

    // ── Bottom bar ────────────────────────────────────────────────────────────
    const int botY = h - kBotH;
    cpuLabel.setBounds(8, botY + 6, 100, kBotH - 8);
    oversampleBox.setBounds(w - 150, botY + 4, 140, kBotH - 8);
}

void GuitarAmpEditor::timerCallback() {
    inputMeter.setLevels(processor.getInputLevelL(),  processor.getInputLevelR());
    outputMeter.setLevels(processor.getOutputLevelL(), processor.getOutputLevelR());

    if (outComp && outComp->isVisible())
        outComp->setLevels(processor.getOutputLevelL(), processor.getOutputLevelR());

    const int cpuPct = juce::jlimit(0, 99, (int)(processor.getCPULoad() * 100.0f));
    cpuLabel.setText("CPU: " + juce::String(cpuPct) + "%", juce::dontSendNotification);

    // Refresh amp block in chain (accent color follows amp_model selection)
    chainComp.repaint();
}
