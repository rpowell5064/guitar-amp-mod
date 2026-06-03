#include "OutputEQComponent.h"

// ─── Preset table ─────────────────────────────────────────────────────────────
// Each row: b1freq, b1gain, b2freq, b2gain, b2q, b3freq, b3gain, b3q,
//           b4freq, b4gain, b4q, b5freq, b5gain
struct EQPreset {
    const char* name;
    float b1f, b1g;
    float b2f, b2g, b2q;
    float b3f, b3g, b3q;
    float b4f, b4g, b4q;
    float b5f, b5g;
};

static constexpr EQPreset kPresets[] = {
    // Custom — placeholder, not applied
    { "Custom",      100,  0.0f, 300, 0.0f,1.0f, 1000, 0.0f,1.0f, 4000, 0.0f,1.0f, 8000, 0.0f },

    // Flat — all bands at 0 dB
    { "Flat",        100,  0.0f, 300, 0.0f,1.0f, 1000, 0.0f,1.0f, 4000, 0.0f,1.0f, 8000, 0.0f },

    // FRFR — subtle voicing for full-range flat-response monitoring
    { "FRFR",        100, -1.5f, 350,-1.5f,1.0f, 1200, 0.0f,0.9f, 4500,+1.0f,1.2f, 9000,-1.0f },

    // Guitar Amp — compensate for guitar speaker's LF rolloff and HF cut
    { "Guitar Amp",  100, +2.5f, 280,+2.0f,0.9f, 1000, 0.0f,1.0f, 3000,-3.0f,1.3f, 6000,+6.0f },

    // Headphones — reduce bass buildup and upper-mid harshness
    { "Headphones",  120, -2.5f, 380,-2.0f,0.9f, 1000, 0.0f,1.0f, 3500,-4.0f,1.3f, 8500,+2.5f },
};
static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

static const char* kBandNames[5]  = { "LOW SHELF", "LO-MID", "MID", "HI-MID", "HI SHELF" };
static const char* kFreqParamIds[5] = { "eq_b1_freq","eq_b2_freq","eq_b3_freq","eq_b4_freq","eq_b5_freq" };
static const char* kGainParamIds[5] = { "eq_b1_gain","eq_b2_gain","eq_b3_gain","eq_b4_gain","eq_b5_gain" };
static const char* kQParamIds[3]    = { "eq_b2_q",   "eq_b3_q",   "eq_b4_q" };

// ─── Constructor ──────────────────────────────────────────────────────────────
OutputEQComponent::OutputEQComponent(juce::AudioProcessorValueTreeState& apvts)
    : BlockComponentBase("SPEAKER EQ", apvts, "eq_bypass", 8),
      apvtsRef(apvts)
{
    // Preset selector
    for (int i = 0; i < kNumPresets; ++i)
        presetBox.addItem(kPresets[i].name, i + 1);
    presetBox.setSelectedId(1, juce::dontSendNotification);
    presetBox.onChange = [this] {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx > 0) applyPreset(idx); // 0 = Custom, skip
    };
    addAndMakeVisible(presetBox);

    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centredRight);
    presetLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    presetLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
    addAndMakeVisible(presetLabel);

    // Band controls
    for (int b = 0; b < 5; ++b) {
        auto& bc = bands[b];

        bc.bandLabel.setText(kBandNames[b], juce::dontSendNotification);
        bc.bandLabel.setJustificationType(juce::Justification::centred);
        bc.bandLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
        bc.bandLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFE94560));
        addAndMakeVisible(bc.bandLabel);

        configureKnob(bc.freqSlider, bc.freqLabel, "FREQ",  *this);
        configureKnob(bc.gainSlider, bc.gainLabel, "GAIN",  *this);

        freqAttach[b] = std::make_unique<SA>(apvts, kFreqParamIds[b], bc.freqSlider);
        gainAttach[b] = std::make_unique<SA>(apvts, kGainParamIds[b], bc.gainSlider);

        // Q knob only for the three peak bands (1-3 → bands indices 1,2,3)
        if (b >= 1 && b <= 3) {
            configureKnob(bc.qSlider, bc.qLabel, "Q", *this);
            qAttach[b - 1] = std::make_unique<SA>(apvts, kQParamIds[b - 1], bc.qSlider);
        }
    }
}

// ─── Layout ───────────────────────────────────────────────────────────────────
void OutputEQComponent::resized() {
    BlockComponentBase::resized();

    const int w = getWidth();

    // Preset row
    const int presetY = kHeaderH + 10;
    presetLabel.setBounds(10, presetY, 60, 28);
    presetBox.setBounds(76, presetY, 220, 28);

    // 5 band columns, each 140px wide, centred in the panel
    static constexpr int kColW   = 140;
    static constexpr int kKnobW  = 95;
    static constexpr int kKnobH  = 82;
    static constexpr int kLabelH = 18;
    static constexpr int kRowH   = kKnobH + kLabelH;

    const int totalW  = 5 * kColW;
    const int x0      = (w - totalW) / 2;
    const int bandY   = presetY + 28 + 14; // top of band section

    for (int b = 0; b < 5; ++b) {
        auto& bc     = bands[b];
        const int cx = x0 + b * kColW;
        const int kx = cx + (kColW - kKnobW) / 2; // centre knob in column

        // Band type label
        bc.bandLabel.setBounds(cx, bandY, kColW, 18);

        const int row1Y = bandY + 20;
        const int row2Y = row1Y + kRowH + 4;
        const int row3Y = row2Y + kRowH + 4;

        layoutKnob(bc.freqSlider, bc.freqLabel,
                   juce::Rectangle<int>(kx, row1Y, kKnobW, kRowH));
        layoutKnob(bc.gainSlider, bc.gainLabel,
                   juce::Rectangle<int>(kx, row2Y, kKnobW, kRowH));

        if (b >= 1 && b <= 3)
            layoutKnob(bc.qSlider, bc.qLabel,
                       juce::Rectangle<int>(kx, row3Y, kKnobW, kRowH));
    }
}

// ─── Preset application ───────────────────────────────────────────────────────
void OutputEQComponent::applyPreset(int idx) {
    if (idx < 0 || idx >= kNumPresets) return;
    const auto& p = kPresets[idx];

    // Helper: set a ranged parameter by raw value via normalized conversion
    auto setRaw = [&](const char* id, float raw) {
        if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvtsRef.getParameter(id)))
            param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(raw));
    };

    setRaw("eq_b1_freq", p.b1f);  setRaw("eq_b1_gain", p.b1g);
    setRaw("eq_b2_freq", p.b2f);  setRaw("eq_b2_gain", p.b2g);  setRaw("eq_b2_q", p.b2q);
    setRaw("eq_b3_freq", p.b3f);  setRaw("eq_b3_gain", p.b3g);  setRaw("eq_b3_q", p.b3q);
    setRaw("eq_b4_freq", p.b4f);  setRaw("eq_b4_gain", p.b4g);  setRaw("eq_b4_q", p.b4q);
    setRaw("eq_b5_freq", p.b5f);  setRaw("eq_b5_gain", p.b5g);
}
