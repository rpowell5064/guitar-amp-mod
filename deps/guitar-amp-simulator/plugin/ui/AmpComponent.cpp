#include "AmpComponent.h"

// ─── Per-model chassis art helpers ───────────────────────────────────────────

static void paintChassis_Fender(juce::Graphics& g, juce::Rectangle<int> a) {
    const int x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight();

    g.setColour(juce::Colour(0xFF1A1A1E));
    g.fillRect(a);

    const int gH = h * 62 / 100;
    g.setColour(juce::Colour(0xFF2A2A34));
    g.fillRect(x, y, w, gH);
    g.setColour(juce::Colour(0xFF1E1E28));
    for (int gy = y; gy < y + gH; gy += 4)  g.drawHorizontalLine(gy, (float)x, (float)(x + w));
    for (int gx = x; gx < x + w; gx += 6)   g.drawVerticalLine(gx, (float)y, (float)(y + gH));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x22FFFFFF), (float)(x + w * 0.3f), (float)y,
                                            juce::Colour(0x05FFFFFF), (float)(x + w), (float)y, false));
    g.fillRect(x, y, w, gH);

    const int pY = y + gH, pH = h - gH;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xFF707A88), (float)x, (float)pY,
                                            juce::Colour(0xFF505868), (float)x, (float)(pY + pH), false));
    g.fillRect(x, pY, w, pH);

    const float lx = x + 22.0f, ly = pY + pH * 0.5f, lr = 5.0f;
    g.setColour(juce::Colour(0x440066FF));
    g.fillEllipse(lx - lr * 1.8f, ly - lr * 1.8f, lr * 3.6f, lr * 3.6f);
    g.setColour(juce::Colour(0xFF0088FF));
    g.fillEllipse(lx - lr, ly - lr, lr * 2.0f, lr * 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.fillEllipse(lx - lr * 0.35f, ly - lr * 0.55f, lr * 0.5f, lr * 0.4f);

    g.setColour(juce::Colour(0xFF909AA4));
    g.drawHorizontalLine(y,         (float)x, (float)(x + w));
    g.drawHorizontalLine(y + h - 1, (float)x, (float)(x + w));
}

static void paintChassis_Marshall(juce::Graphics& g, juce::Rectangle<int> a) {
    const int x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight();

    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRect(a);
    g.setColour(juce::Colour(0xFF222222));
    for (int i = -h; i < w + h; i += 8) {
        g.drawLine((float)(x + i), (float)y, (float)(x + i + h), (float)(y + h), 0.6f);
        g.drawLine((float)(x + i + h), (float)y, (float)(x + i), (float)(y + h), 0.6f);
    }

    const int sH = h * 40 / 100, sY = y + h - sH;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xFFB08820), (float)x, (float)sY,
                                            juce::Colour(0xFF806010), (float)x, (float)(sY + sH), false));
    g.fillRect(x, sY, w, sH);
    g.setColour(juce::Colour(0x18000000));
    for (int lx = x; lx < x + w; lx += 3) g.drawVerticalLine(lx, (float)sY, (float)(sY + sH));
    g.setColour(juce::Colour(0xFF1E0C04));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(18.0f).withStyle("Bold Italic")));
    g.drawText("M", x + 18, sY, 28, sH, juce::Justification::centredLeft, false);

    g.setColour(juce::Colour(0xFFC8A030));
    g.drawHorizontalLine(sY,        (float)x, (float)(x + w));
    g.setColour(juce::Colour(0xFF404040));
    g.drawHorizontalLine(y,         (float)x, (float)(x + w));
    g.drawHorizontalLine(y + h - 1, (float)x, (float)(x + w));
}

static void paintChassis_EVH(juce::Graphics& g, juce::Rectangle<int> a) {
    const int x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight();

    g.setColour(juce::Colour(0xFFF0EDE0));
    g.fillRect(a);

    g.setColour(juce::Colour(0xFF1A1A1A));
    const float sk = (float)h;
    const struct { float pos, sw; } stripes[] = {
        {0.000f,0.036f},{0.070f,0.014f},{0.110f,0.065f},{0.210f,0.020f},
        {0.280f,0.048f},{0.360f,0.011f},{0.410f,0.075f},{0.520f,0.013f},
        {0.580f,0.042f},{0.660f,0.020f},{0.710f,0.052f},{0.800f,0.013f},
        {0.850f,0.032f},{0.930f,0.020f}
    };
    for (const auto& s : stripes) {
        const float sx = x + s.pos * w;
        const float sw = s.sw * w;
        juce::Path stripe;
        stripe.addQuadrilateral(sx,        (float)y,
                                 sx + sw,   (float)y,
                                 sx + sw + sk, (float)(y + h),
                                 sx + sk,   (float)(y + h));
        g.fillPath(stripe);
    }

    const int fH = h * 28 / 100, fY = y + h - fH;
    g.setColour(juce::Colour(0xFF141414));
    g.fillRect(x, fY, w, fH);
    g.setColour(juce::Colour(0xFFBBBBBB));
    g.drawHorizontalLine(fY, (float)x, (float)(x + w));
    g.drawHorizontalLine(y,  (float)x, (float)(x + w));
}

static void paintChassis_Neural(juce::Graphics& g, juce::Rectangle<int> a) {
    const int x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight();

    g.setColour(juce::Colour(0xFF070C1A));
    g.fillRect(a);

    const int gs = 14;
    g.setColour(juce::Colour(0xFF0D1E36));
    for (int lx = x; lx < x + w; lx += gs) g.drawVerticalLine(lx, (float)y, (float)(y + h));
    for (int ly = y; ly < y + h; ly += gs) g.drawHorizontalLine(ly, (float)x, (float)(x + w));
    g.setColour(juce::Colour(0xFF1A3A60));
    for (int lx = x + gs; lx < x + w; lx += gs * 2)
        for (int ly = y + gs; ly < y + h; ly += gs * 2)
            g.fillEllipse((float)lx - 1.5f, (float)ly - 1.5f, 3.0f, 3.0f);

    const auto drawTrace = [&](int ty) {
        g.setColour(juce::Colour(0x200088CC));
        g.fillRect(x, ty - 2, w, 6);
        g.setColour(juce::Colour(0xFF0E3A5A));
        g.fillRect(x, ty, w, 2);
    };
    drawTrace(y + h * 35 / 100);
    drawTrace(y + h * 65 / 100);

    g.setColour(juce::Colour(0xFF0D4080));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
    g.drawText("NEURAL  AMP  MODELING", x + 14, y, w - 28, h, juce::Justification::centredLeft, false);

    g.setColour(juce::Colour(0x300055AA));
    g.drawRect(a, 2);
}

static void paintChassis_Sunn(juce::Graphics& g, juce::Rectangle<int> a) {
    const int x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight();

    g.setColour(juce::Colour(0xFF111214));
    g.fillRect(a);

    const int numVents = 4;
    for (int i = 0; i < numVents; ++i) {
        const int vy = y + h * (i + 1) / (numVents + 1);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xFFBB5500), (float)(x + w / 2), (float)vy,
                                                juce::Colour(0xFF3A1800), (float)x, (float)vy, true));
        g.fillRect(x + 10, vy - 4, w - 20, 8);
        g.setColour(juce::Colour(0xFF090B0D));
        g.fillRect(x + 10, vy - 1, w - 20, 3);
    }

    g.setColour(juce::Colour(0xFF606268));
    g.drawHorizontalLine(y,         (float)x, (float)(x + w));
    g.drawHorizontalLine(y + h - 1, (float)x, (float)(x + w));

    for (int i = 0; i < 2; ++i) {
        const float lx = x + w - 28.0f + i * 14.0f;
        const float ly = y + h * 0.5f;
        g.setColour(i == 0 ? juce::Colour(0xFFCC2200) : juce::Colour(0xFFCC6600));
        g.fillEllipse(lx - 3.0f, ly - 3.0f, 6.0f, 6.0f);
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.fillEllipse(lx - 1.0f, ly - 2.0f, 2.0f, 1.5f);
    }
}

static void paintChassis_Orange(juce::Graphics& g, juce::Rectangle<int> a) {
    const int x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight();

    g.setColour(juce::Colour(0xFFDF6A18));
    g.fillRect(a);
    g.setColour(juce::Colour(0xFFC55E14));
    for (int i = -h; i < w + h; i += 12) {
        g.drawLine((float)(x + i), (float)y, (float)(x + i + h), (float)(y + h), 0.8f);
        g.drawLine((float)(x + i + h), (float)y, (float)(x + i), (float)(y + h), 0.8f);
    }

    const int pnH = h * 35 / 100, pnY = y + h - pnH;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xFFBEC2C8), (float)x, (float)pnY,
                                            juce::Colour(0xFF8E9298), (float)x, (float)(pnY + pnH), false));
    g.fillRect(x, pnY, w, pnH);
    g.setColour(juce::Colour(0x15000000));
    for (int lx = x; lx < x + w; lx += 3) g.drawVerticalLine(lx, (float)pnY, (float)(pnY + pnH));

    const int hndW = 12, hndH = h * 65 / 100, hndY = y + (h - hndH) / 2;
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle((float)(x + 4),            (float)hndY, (float)hndW, (float)hndH, 3.0f);
    g.fillRoundedRectangle((float)(x + w - hndW - 4), (float)hndY, (float)hndW, (float)hndH, 3.0f);

    g.setColour(juce::Colour(0xFFA04010));
    g.drawHorizontalLine(y,         (float)x, (float)(x + w));
    g.drawHorizontalLine(y + h - 1, (float)x, (float)(x + w));
    g.setColour(juce::Colour(0xFFD0D4D8));
    g.drawHorizontalLine(pnY, (float)x, (float)(x + w));
}

// ─── Constructor ──────────────────────────────────────────────────────────────

AmpComponent::AmpComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("AMPLIFIER", apvts, "amp_bypass", 3),
      apvtsRef(apvts)
{
    // ── Tab toggle ────────────────────────────────────────────────────────────
    preampTabBtn.onClick = [this] {
        showPowerAmp_ = false;
        updateTabUI();
        resized();
    };
    pampTabBtn.onClick = [this] {
        showPowerAmp_ = true;
        updateTabUI();
        resized();
    };
    addAndMakeVisible(preampTabBtn);
    addAndMakeVisible(pampTabBtn);

    // ── Model selector ────────────────────────────────────────────────────────
    modelSelector.addItem("Fender Deluxe",      1);
    modelSelector.addItem("Marshall JCM800",    2);
    modelSelector.addItem("EVH 5150 III",       3);
    modelSelector.addItem("Neural",             4);
    modelSelector.addItem("Sunn Model T",       5);
    modelSelector.addItem("Orange Rockerverb",  6);
    modelSelector.onChange = [this] { updateTabUI(); resized(); };
    addAndMakeVisible(modelSelector);

    modelLabel.setText("MODEL", juce::dontSendNotification);
    modelLabel.setJustificationType(juce::Justification::centred);
    modelLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    modelLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(modelLabel);
    modelAttach = std::make_unique<CBA>(apvts, "amp_model", modelSelector);

    loadModelButton.onClick = [this] { openModelBrowser(); };
    addAndMakeVisible(loadModelButton);

    modelFilenameLabel.setText("No model loaded", juce::dontSendNotification);
    modelFilenameLabel.setJustificationType(juce::Justification::centredLeft);
    modelFilenameLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    modelFilenameLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(modelFilenameLabel);

    pampBypassButton.setButtonText("BYPASS");
    pampBypassButton.setClickingTogglesState(true);
    addAndMakeVisible(pampBypassButton);
    pampBypassLabel.setText("POWER AMP", juce::dontSendNotification);
    pampBypassLabel.setJustificationType(juce::Justification::centred);
    pampBypassLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    pampBypassLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(pampBypassLabel);
    pampBypassAttach = std::make_unique<BA>(apvts, "pamp_bypass", pampBypassButton);

    // ── Preamp knobs ──────────────────────────────────────────────────────────
    configureKnob(gainSlider,    gainLabel,    "GAIN",     *this);
    configureKnob(bassSlider,    bassLabel,    "BASS",     *this);
    configureKnob(midSlider,     midLabel,     "MID",      *this);
    configureKnob(trebleSlider,  trebleLabel,  "TREBLE",   *this);
    configureKnob(presSlider,    presLabel,    "PRESENCE", *this);
    configureKnob(masterSlider,  masterLabel,  "MASTER",   *this);
    configureKnob(sagSlider,     sagLabel,     "SAG",      *this);
    configureKnob(namGainSlider, namGainLabel, "NAM GAIN", *this);

    gainAttach    = std::make_unique<SA>(apvts, "amp_gain",     gainSlider);
    bassAttach    = std::make_unique<SA>(apvts, "amp_bass",     bassSlider);
    midAttach     = std::make_unique<SA>(apvts, "amp_mid",      midSlider);
    trebleAttach  = std::make_unique<SA>(apvts, "amp_treble",   trebleSlider);
    presAttach    = std::make_unique<SA>(apvts, "amp_presence", presSlider);
    masterAttach  = std::make_unique<SA>(apvts, "amp_master",   masterSlider);
    sagAttach     = std::make_unique<SA>(apvts, "amp_sag",      sagSlider);
    namGainAttach = std::make_unique<SA>(apvts, "amp_nam_gain", namGainSlider);

    // ── Model-specific: Sunn Model T ──────────────────────────────────────────
    // Normal channel (Ch1)
    configureKnob(sunnVol1Slider,    sunnVol1Label,    "NORMAL", *this);
    configureKnob(sunnBass1Slider,   sunnBass1Label,   "BASS",   *this);
    configureKnob(sunnMid1Slider,    sunnMid1Label,    "MID",    *this);
    configureKnob(sunnTreble1Slider, sunnTreble1Label, "TREBLE", *this);
    sunnVol1Attach    = std::make_unique<SA>(apvts, "sunn_vol1",    sunnVol1Slider);
    sunnBass1Attach   = std::make_unique<SA>(apvts, "sunn_bass1",   sunnBass1Slider);
    sunnMid1Attach    = std::make_unique<SA>(apvts, "sunn_mid1",    sunnMid1Slider);
    sunnTreble1Attach = std::make_unique<SA>(apvts, "sunn_treble1", sunnTreble1Slider);

    sunnBrightBtn.setButtonText("BRIGHT");
    sunnBrightBtn.setClickingTogglesState(true);
    addAndMakeVisible(sunnBrightBtn);
    sunnBrightLabel.setText("NORMAL BRIGHT", juce::dontSendNotification);
    sunnBrightLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    sunnBrightLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(sunnBrightLabel);
    sunnBrightAttach = std::make_unique<BA>(apvts, "sunn_bright", sunnBrightBtn);

    // Brite channel (Ch2)
    configureKnob(sunnVol2Slider,    sunnVol2Label,    "BRITE",  *this);
    configureKnob(sunnBass2Slider,   sunnBass2Label,   "BASS",   *this);
    configureKnob(sunnMid2Slider,    sunnMid2Label,    "MID",    *this);
    configureKnob(sunnTreble2Slider, sunnTreble2Label, "TREBLE", *this);
    sunnVol2Attach    = std::make_unique<SA>(apvts, "sunn_vol2",    sunnVol2Slider);
    sunnBass2Attach   = std::make_unique<SA>(apvts, "sunn_bass2",   sunnBass2Slider);
    sunnMid2Attach    = std::make_unique<SA>(apvts, "sunn_mid2",    sunnMid2Slider);
    sunnTreble2Attach = std::make_unique<SA>(apvts, "sunn_treble2", sunnTreble2Slider);

    sunnBright2Btn.setButtonText("BRIGHT");
    sunnBright2Btn.setClickingTogglesState(true);
    addAndMakeVisible(sunnBright2Btn);
    sunnBright2Label.setText("BRITE BRIGHT", juce::dontSendNotification);
    sunnBright2Label.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    sunnBright2Label.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(sunnBright2Label);
    sunnBright2Attach = std::make_unique<BA>(apvts, "sunn_bright2", sunnBright2Btn);

    // Channel link
    sunnLinkBox.addItem("Independent", 1);
    sunnLinkBox.addItem("Parallel",    2);
    sunnLinkBox.addItem("Series",      3);
    addAndMakeVisible(sunnLinkBox);
    sunnLinkLabel.setText("CHANNEL LINK", juce::dontSendNotification);
    sunnLinkLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    sunnLinkLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(sunnLinkLabel);
    sunnLinkAttach = std::make_unique<CBA>(apvts, "sunn_channel_link", sunnLinkBox);

    // Input pad
    sunnInputPadBtn.setButtonText("-6dB");
    sunnInputPadBtn.setClickingTogglesState(true);
    addAndMakeVisible(sunnInputPadBtn);
    sunnInputPadLabel.setText("INPUT PAD", juce::dontSendNotification);
    sunnInputPadLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    sunnInputPadLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(sunnInputPadLabel);
    sunnInputPadAttach = std::make_unique<BA>(apvts, "sunn_input_pad", sunnInputPadBtn);

    // ── Model-specific: Orange Rockerverb 50 ──────────────────────────────────
    rvbCleanBtn.setButtonText("CLEAN");
    rvbCleanBtn.setClickingTogglesState(true);
    addAndMakeVisible(rvbCleanBtn);
    rvbCleanLabel.setText("CLEAN CH", juce::dontSendNotification);
    rvbCleanLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    rvbCleanLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(rvbCleanLabel);
    rvbCleanAttach = std::make_unique<BA>(apvts, "rvb_channel", rvbCleanBtn);

    configureKnob(rvbGainSlider,   rvbGainLabel,   "GAIN",    *this);
    configureKnob(rvbBassSlider,   rvbBassLabel,   "BASS",    *this);
    configureKnob(rvbMidSlider,    rvbMidLabel,    "MID",     *this);
    configureKnob(rvbTrebleSlider, rvbTrebleLabel, "TREBLE",  *this);
    configureKnob(rvbMasterSlider, rvbMasterLabel, "MASTER",  *this);
    configureKnob(rvbSagSlider,    rvbSagLabel,    "SAG",     *this);
    rvbGainAttach   = std::make_unique<SA>(apvts, "rvb_gain",   rvbGainSlider);
    rvbBassAttach   = std::make_unique<SA>(apvts, "rvb_bass",   rvbBassSlider);
    rvbMidAttach    = std::make_unique<SA>(apvts, "rvb_mid",    rvbMidSlider);
    rvbTrebleAttach = std::make_unique<SA>(apvts, "rvb_treble", rvbTrebleSlider);
    rvbMasterAttach = std::make_unique<SA>(apvts, "rvb_master", rvbMasterSlider);
    rvbSagAttach    = std::make_unique<SA>(apvts, "rvb_sag",    rvbSagSlider);

    // ── Model-specific: EVH 5150 III ──────────────────────────────────────────
    evhRedBtn.setButtonText("RED (LEAD)");
    evhRedBtn.setClickingTogglesState(true);
    addAndMakeVisible(evhRedBtn);
    evhRedLabel.setText("CHANNEL", juce::dontSendNotification);
    evhRedLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    evhRedLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(evhRedLabel);
    evhRedAttach = std::make_unique<BA>(apvts, "evh_channel", evhRedBtn);

    configureKnob(evhResSlider, evhResLabel, "RESONANCE", *this);
    evhResAttach = std::make_unique<SA>(apvts, "evh_resonance", evhResSlider);

    // ── Power amp controls ────────────────────────────────────────────────────
    tubeTypeBox.addItem("6L6GC", 1);
    tubeTypeBox.addItem("EL34",  2);
    tubeTypeBox.addItem("EL84",  3);
    tubeTypeBox.addItem("KT88",  4);
    addAndMakeVisible(tubeTypeBox);
    tubeTypeLabel.setText("TUBE TYPE", juce::dontSendNotification);
    tubeTypeLabel.setJustificationType(juce::Justification::centred);
    tubeTypeLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    tubeTypeLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(tubeTypeLabel);
    tubeTypeAttach = std::make_unique<CBA>(apvts, "pamp_tube", tubeTypeBox);

    configureKnob(pampPresSlider,   pampPresLabel,   "PRESENCE",  *this);
    configureKnob(pampDepthSlider,  pampDepthLabel,  "DEPTH",     *this);
    configureKnob(pampSagSlider,    pampSagLabel,    "SAG",       *this);
    configureKnob(pampMasterSlider, pampMasterLabel, "MASTER",    *this);
    configureKnob(pampNFBSlider,    pampNFBLabel,    "NFB",       *this);
    configureKnob(pampResSlider,    pampResLabel,    "RESONANCE", *this);

    pampPresAttach   = std::make_unique<SA>(apvts, "pamp_presence",  pampPresSlider);
    pampDepthAttach  = std::make_unique<SA>(apvts, "pamp_depth",     pampDepthSlider);
    pampSagAttach    = std::make_unique<SA>(apvts, "pamp_sag",       pampSagSlider);
    pampMasterAttach = std::make_unique<SA>(apvts, "pamp_master",    pampMasterSlider);
    pampNFBAttach    = std::make_unique<SA>(apvts, "pamp_nfb",       pampNFBSlider);
    pampResAttach    = std::make_unique<SA>(apvts, "pamp_resonance", pampResSlider);

    airFeelBtn.setButtonText("ON");
    airFeelBtn.setClickingTogglesState(true);
    addAndMakeVisible(airFeelBtn);
    airFeelLabel.setText("AIR FEEL", juce::dontSendNotification);
    airFeelLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    airFeelLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    addAndMakeVisible(airFeelLabel);
    airFeelAttach = std::make_unique<BA>(apvts, "pamp_airfeel", airFeelBtn);

    updateTabUI();
}

// ─── Paint ────────────────────────────────────────────────────────────────────

void AmpComponent::paint(juce::Graphics& g) {
    BlockComponentBase::paint(g);

    const int model = modelSelector.getSelectedItemIndex();
    const juce::Rectangle<int> chassis(0, kHeaderH, getWidth(), kChassisArtH);

    switch (model) {
        case 0: paintChassis_Fender(g, chassis);   break;
        case 1: paintChassis_Marshall(g, chassis); break;
        case 2: paintChassis_EVH(g, chassis);      break;
        case 3: paintChassis_Neural(g, chassis);   break;
        case 4: paintChassis_Sunn(g, chassis);     break;
        case 5: paintChassis_Orange(g, chassis);   break;
        default: break;
    }

    // Tab bar background
    const int tabY = kHeaderH + kChassisArtH;
    g.setColour(juce::Colour(0xFF0D1530));
    g.fillRect(0, tabY, getWidth(), kTabBarH);
    g.setColour(juce::Colour(0xFF1E2A44));
    g.drawHorizontalLine(tabY + kTabBarH - 1, 0.0f, (float)getWidth());
}

// ─── Layout ───────────────────────────────────────────────────────────────────

void AmpComponent::resized() {
    BlockComponentBase::resized();

    const int kW     = getWidth();
    const int tabY   = kHeaderH + kChassisArtH;
    const int tabW   = kW / 2;

    // Tab buttons
    preampTabBtn.setBounds(0,    tabY, tabW,      kTabBarH);
    pampTabBtn.setBounds  (tabW, tabY, kW - tabW, kTabBarH);

    const int contentY = tabY + kTabBarH + 8;

    if (!showPowerAmp_) {
        // ── PREAMP tab layout ─────────────────────────────────────────────────
        const int selW = 220, selH = 28;
        modelLabel.setBounds((kW - selW) / 2 - 60, contentY, 55, selH);
        modelSelector.setBounds((kW - selW) / 2, contentY, selW, selH);

        // Power amp bypass — right of model selector
        const int pampX = (kW - selW) / 2 + selW + 16;
        pampBypassLabel.setBounds (pampX, contentY,      90, 14);
        pampBypassButton.setBounds(pampX, contentY + 14, 90, selH - 14);

        // Model-specific controls row
        bool hasMsc = (modelSelector.getSelectedItemIndex() == 2 ||   // EVH
                       modelSelector.getSelectedItemIndex() == 4 ||   // Sunn
                       modelSelector.getSelectedItemIndex() == 5);    // RVB
        const int mscY = contentY + selH + 10;

        if (modelSelector.getSelectedItemIndex() == 4) {   // Sunn
            const int mscH = 24;
            // Controls row: Normal Bright | Brite Bright | Channel Link (right-aligned)
            sunnBrightLabel.setBounds (16,        mscY,          80, mscH);
            sunnBrightBtn.setBounds   (96,        mscY,          70, mscH);
            sunnBright2Label.setBounds(184,       mscY,          80, mscH);
            sunnBright2Btn.setBounds  (264,       mscY,          70, mscH);
            // Input pad — centred in the gap between bright buttons and channel link
            const int padCx = (334 + (kW - 16 - 148 - 96)) / 2;
            sunnInputPadLabel.setBounds(padCx - 70, mscY,  70, mscH);
            sunnInputPadBtn.setBounds  (padCx,      mscY,  70, mscH);
            const int linkX = kW - 16 - 148;
            sunnLinkLabel.setBounds   (linkX - 96, mscY,         90, mscH);
            sunnLinkBox.setBounds     (linkX,      mscY,        148, mscH);
        } else if (modelSelector.getSelectedItemIndex() == 5) {   // Orange RVB
            rvbCleanLabel.setBounds(16,  mscY, 70, 24);
            rvbCleanBtn.setBounds  (92,  mscY, 80, 24);
        } else if (modelSelector.getSelectedItemIndex() == 2) {   // EVH
            evhRedLabel.setBounds(16,  mscY, 65, 24);
            evhRedBtn.setBounds  (86,  mscY, 110, 24);
        }

        // Neural file loader row
        const bool isNeural = (modelSelector.getSelectedItemIndex() == 3);
        const int neuralRowY = hasMsc ? mscY + 34 : contentY + selH + 10;
        loadModelButton.setBounds(16, neuralRowY, 110, 26);
        modelFilenameLabel.setBounds(136, neuralRowY, kW - 250, 26);

        // Knobs
        const int knobW  = 95, knobH = 100;
        const int knobY  = isNeural ? neuralRowY + 34 : (hasMsc ? mscY + 34 : contentY + selH + 10);

        const bool isSunnKnobs = (modelSelector.getSelectedItemIndex() == 4);
        const bool isEvhKnobs  = (modelSelector.getSelectedItemIndex() == 2);
        const bool isRvbKnobs  = (modelSelector.getSelectedItemIndex() == 5);
        if (isSunnKnobs) {
            // Sunn: two-row layout, knobH=90 to fit in available height
            const int sKnobH = 90;

            // Row 1 — Normal channel: VOL1 BASS1 MID1 TREBLE1 | PRESENCE MASTER SAG
            const int row1W = 7 * knobW;
            const int x1    = (kW - row1W) / 2;
            auto c1 = [&](int i) { return juce::Rectangle<int>(x1 + i * knobW, knobY, knobW, sKnobH); };
            layoutKnob(sunnVol1Slider,    sunnVol1Label,    c1(0));
            layoutKnob(sunnBass1Slider,   sunnBass1Label,   c1(1));
            layoutKnob(sunnMid1Slider,    sunnMid1Label,    c1(2));
            layoutKnob(sunnTreble1Slider, sunnTreble1Label, c1(3));
            layoutKnob(presSlider,        presLabel,        c1(4));
            layoutKnob(masterSlider,      masterLabel,      c1(5));
            layoutKnob(sagSlider,         sagLabel,         c1(6));

            // Row 2 — Brite channel: VOL2 BASS2 MID2 TREBLE2 (4 knobs, centred)
            const int row2Y  = knobY + sKnobH + 8;
            const int row2W  = 4 * knobW;
            const int x2     = (kW - row2W) / 2;
            auto c2 = [&](int i) { return juce::Rectangle<int>(x2 + i * knobW, row2Y, knobW, sKnobH); };
            layoutKnob(sunnVol2Slider,    sunnVol2Label,    c2(0));
            layoutKnob(sunnBass2Slider,   sunnBass2Label,   c2(1));
            layoutKnob(sunnMid2Slider,    sunnMid2Label,    c2(2));
            layoutKnob(sunnTreble2Slider, sunnTreble2Label, c2(3));
        } else if (isRvbKnobs) {
            // RVB: dedicated gain/bass/mid/treble/master/sag + shared presence at pos 4
            const int totalW = 7 * knobW;
            const int x0     = (kW - totalW) / 2;
            auto col = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, knobY, knobW, knobH); };
            layoutKnob(rvbGainSlider,   rvbGainLabel,   col(0));
            layoutKnob(rvbBassSlider,   rvbBassLabel,   col(1));
            layoutKnob(rvbMidSlider,    rvbMidLabel,    col(2));
            layoutKnob(rvbTrebleSlider, rvbTrebleLabel, col(3));
            layoutKnob(presSlider,      presLabel,      col(4));
            layoutKnob(rvbMasterSlider, rvbMasterLabel, col(5));
            layoutKnob(rvbSagSlider,    rvbSagLabel,    col(6));
        } else if (isEvhKnobs) {
            // EVH: 8 knobs — adds RESONANCE after SAG
            const int totalW = 8 * knobW;
            const int x0     = (kW - totalW) / 2;
            auto col = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, knobY, knobW, knobH); };
            layoutKnob(gainSlider,   gainLabel,   col(0));
            layoutKnob(bassSlider,   bassLabel,   col(1));
            layoutKnob(midSlider,    midLabel,    col(2));
            layoutKnob(trebleSlider, trebleLabel, col(3));
            layoutKnob(presSlider,   presLabel,   col(4));
            layoutKnob(masterSlider, masterLabel, col(5));
            layoutKnob(sagSlider,    sagLabel,    col(6));
            layoutKnob(evhResSlider, evhResLabel, col(7));
        } else {
            // All other models: GAIN, BASS, MID, TREBLE, PRESENCE, MASTER, SAG (7 knobs)
            const int totalW = 7 * knobW;
            const int x0     = (kW - totalW) / 2;
            auto col = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, knobY, knobW, knobH); };
            layoutKnob(gainSlider,    gainLabel,    col(0));
            layoutKnob(bassSlider,    bassLabel,    col(1));
            layoutKnob(midSlider,     midLabel,     col(2));
            layoutKnob(trebleSlider,  trebleLabel,  col(3));
            layoutKnob(presSlider,    presLabel,    col(4));
            layoutKnob(masterSlider,  masterLabel,  col(5));
            layoutKnob(sagSlider,     sagLabel,     col(6));
            layoutKnob(namGainSlider, namGainLabel, col(6)); // same slot as SAG; visibility toggled
        }

    } else {
        // ── POWER AMP tab layout ──────────────────────────────────────────────
        const int selW = 180, selH = 28;
        tubeTypeLabel.setBounds((kW - selW) / 2 - 84, contentY, 80, selH);
        tubeTypeBox.setBounds  ((kW - selW) / 2,      contentY, selW, selH);

        // Power amp bypass — right of tube selector
        const int pampX = (kW - selW) / 2 + selW + 16;
        pampBypassLabel.setBounds (pampX, contentY,      90, 14);
        pampBypassButton.setBounds(pampX, contentY + 14, 90, selH - 14);

        // 6 knobs
        const int knobW  = 95, knobH = 100;
        const int knobY  = contentY + selH + 10;
        const int totalW = 6 * knobW;
        const int x0     = (kW - totalW) / 2;

        auto col = [&](int i) { return juce::Rectangle<int>(x0 + i * knobW, knobY, knobW, knobH); };
        layoutKnob(pampPresSlider,   pampPresLabel,   col(0));
        layoutKnob(pampDepthSlider,  pampDepthLabel,  col(1));
        layoutKnob(pampSagSlider,    pampSagLabel,    col(2));
        layoutKnob(pampMasterSlider, pampMasterLabel, col(3));
        layoutKnob(pampNFBSlider,    pampNFBLabel,    col(4));
        layoutKnob(pampResSlider,    pampResLabel,    col(5));

        // Air feel toggle below knobs
        const int afY = knobY + knobH + 12;
        airFeelLabel.setBounds(x0,      afY, 64, 20);
        airFeelBtn.setBounds  (x0 + 68, afY, 60, 24);
    }
}

// ─── Tab + model UI update ────────────────────────────────────────────────────

void AmpComponent::styleTab(juce::TextButton& btn, bool active) {
    btn.setColour(juce::TextButton::buttonColourId,
                  active ? juce::Colour(0xFF1E3A6E) : juce::Colour(0xFF0D1530));
    btn.setColour(juce::TextButton::textColourOffId,
                  active ? juce::Colours::white : juce::Colour(0xFF6677AA));
}

void AmpComponent::updateTabUI() {
    styleTab(preampTabBtn, !showPowerAmp_);
    styleTab(pampTabBtn,    showPowerAmp_);

    const int model    = modelSelector.getSelectedItemIndex();
    const bool isNeural = (model == 3);
    const bool isSunn   = (model == 4);
    const bool isRVB    = (model == 5);
    const bool isEVH    = (model == 2);

    // Preamp-tab controls
    const bool preamp = !showPowerAmp_;
    modelLabel.setVisible      (preamp);
    modelSelector.setVisible   (preamp);
    loadModelButton.setVisible (preamp && isNeural);
    modelFilenameLabel.setVisible(preamp && isNeural);

    // Shared knobs: hidden for Sunn/RVB (have dedicated controls) and Neural
    const bool useSharedKnobs = !isNeural && !isSunn && !isRVB;
    gainSlider.setVisible   (preamp && useSharedKnobs); gainLabel.setVisible   (preamp && useSharedKnobs);
    bassSlider.setVisible   (preamp && useSharedKnobs); bassLabel.setVisible   (preamp && useSharedKnobs);
    midSlider.setVisible    (preamp && useSharedKnobs); midLabel.setVisible    (preamp && useSharedKnobs);
    trebleSlider.setVisible (preamp && useSharedKnobs); trebleLabel.setVisible (preamp && useSharedKnobs);
    presSlider.setVisible   (preamp && !isNeural); presLabel.setVisible   (preamp && !isNeural);
    masterSlider.setVisible (preamp && useSharedKnobs); masterLabel.setVisible (preamp && useSharedKnobs);
    sagSlider.setVisible    (preamp && useSharedKnobs); sagLabel.setVisible    (preamp && useSharedKnobs);
    namGainSlider.setVisible(preamp && isNeural);  namGainLabel.setVisible(preamp && isNeural);

    // Sunn-specific controls
    sunnVol1Slider.setVisible   (preamp && isSunn); sunnVol1Label.setVisible   (preamp && isSunn);
    sunnBass1Slider.setVisible  (preamp && isSunn); sunnBass1Label.setVisible  (preamp && isSunn);
    sunnMid1Slider.setVisible   (preamp && isSunn); sunnMid1Label.setVisible   (preamp && isSunn);
    sunnTreble1Slider.setVisible(preamp && isSunn); sunnTreble1Label.setVisible(preamp && isSunn);
    sunnBrightBtn.setVisible    (preamp && isSunn); sunnBrightLabel.setVisible (preamp && isSunn);
    sunnVol2Slider.setVisible   (preamp && isSunn); sunnVol2Label.setVisible   (preamp && isSunn);
    sunnBass2Slider.setVisible  (preamp && isSunn); sunnBass2Label.setVisible  (preamp && isSunn);
    sunnMid2Slider.setVisible   (preamp && isSunn); sunnMid2Label.setVisible   (preamp && isSunn);
    sunnTreble2Slider.setVisible(preamp && isSunn); sunnTreble2Label.setVisible(preamp && isSunn);
    sunnBright2Btn.setVisible   (preamp && isSunn); sunnBright2Label.setVisible(preamp && isSunn);
    sunnLinkBox.setVisible      (preamp && isSunn); sunnLinkLabel.setVisible   (preamp && isSunn);
    rvbCleanBtn.setVisible      (preamp && isRVB); rvbCleanLabel.setVisible    (preamp && isRVB);
    rvbGainSlider.setVisible    (preamp && isRVB); rvbGainLabel.setVisible     (preamp && isRVB);
    rvbBassSlider.setVisible    (preamp && isRVB); rvbBassLabel.setVisible     (preamp && isRVB);
    rvbMidSlider.setVisible     (preamp && isRVB); rvbMidLabel.setVisible      (preamp && isRVB);
    rvbTrebleSlider.setVisible  (preamp && isRVB); rvbTrebleLabel.setVisible   (preamp && isRVB);
    rvbMasterSlider.setVisible  (preamp && isRVB); rvbMasterLabel.setVisible   (preamp && isRVB);
    rvbSagSlider.setVisible     (preamp && isRVB); rvbSagLabel.setVisible      (preamp && isRVB);
    evhRedBtn.setVisible      (preamp && isEVH);  evhRedLabel.setVisible    (preamp && isEVH);
    evhResSlider.setVisible   (preamp && isEVH);  evhResLabel.setVisible    (preamp && isEVH);

    // Power-amp-tab controls
    const bool pamp = showPowerAmp_;
    tubeTypeBox.setVisible    (pamp); tubeTypeLabel.setVisible(pamp);
    pampPresSlider.setVisible (pamp); pampPresLabel.setVisible(pamp);
    pampDepthSlider.setVisible(pamp); pampDepthLabel.setVisible(pamp);
    pampSagSlider.setVisible  (pamp); pampSagLabel.setVisible(pamp);
    pampMasterSlider.setVisible(pamp);pampMasterLabel.setVisible(pamp);
    pampNFBSlider.setVisible  (pamp); pampNFBLabel.setVisible(pamp);
    pampResSlider.setVisible  (pamp); pampResLabel.setVisible(pamp);
    airFeelBtn.setVisible     (pamp); airFeelLabel.setVisible(pamp);

    // Power amp bypass + label always visible
    pampBypassButton.setVisible(true);
    pampBypassLabel.setVisible(true);

    // Knob accent colour per model (preamp knobs)
    static constexpr juce::uint32 kArcColors[] = {
        0xFFBB8844,  // 0 Fender
        0xFFC4952A,  // 1 Marshall
        0xFFCC2222,  // 2 EVH
        0xFF44AADD,  // 3 Neural
        0xFF999999,  // 4 Sunn
        0xFFE87722,  // 5 Orange
    };
    const juce::Colour arcCol = (model >= 0 && model < 6)
        ? juce::Colour(kArcColors[model])
        : juce::Colour(kArcColors[1]);

    for (auto* s : { &gainSlider, &bassSlider, &midSlider, &trebleSlider,
                     &presSlider, &masterSlider, &sagSlider, &namGainSlider,
                     &sunnVol1Slider, &sunnBass1Slider, &sunnMid1Slider, &sunnTreble1Slider,
                     &sunnVol2Slider, &sunnBass2Slider, &sunnMid2Slider, &sunnTreble2Slider })
        s->setColour(juce::Slider::rotarySliderFillColourId, arcCol);

    // Power amp knobs use a warm amber accent
    const juce::Colour pampCol(0xFFAA7722);
    for (auto* s : { &pampPresSlider, &pampDepthSlider, &pampSagSlider,
                     &pampMasterSlider, &pampNFBSlider, &pampResSlider })
        s->setColour(juce::Slider::rotarySliderFillColourId, pampCol);

    // Auto-bypass power amp for Neural (NAM captures full signal chain)
    if (auto* param = apvtsRef.getParameter("pamp_bypass"))
        param->setValueNotifyingHost(isNeural ? 1.0f : 0.0f);

    repaint();
}

void AmpComponent::updateModelUI() { updateTabUI(); }

// ─── Neural model file ────────────────────────────────────────────────────────

void AmpComponent::setNeuralModelFilename(const juce::String& filename) {
    modelFilenameLabel.setText(filename, juce::dontSendNotification);
}

void AmpComponent::openModelBrowser() {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Neural Amp Model",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.nam;*.json");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto result = fc.getResult();
            if (result.existsAsFile()) {
                modelFilenameLabel.setText(result.getFileName(), juce::dontSendNotification);
                if (onNeuralModelLoaded) onNeuralModelLoaded(result);
            }
        });
}
