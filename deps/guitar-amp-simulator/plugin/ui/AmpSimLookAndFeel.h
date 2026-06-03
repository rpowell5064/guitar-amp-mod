#pragma once
#include <JuceHeader.h>

// Neural DSP-inspired dark look and feel.
// Dark navy background, red accent, custom knobs with arc+dot indicator,
// LED toggle buttons, and rounded panels.
class AmpSimLookAndFeel : public juce::LookAndFeel_V4 {
public:
    // ── Colour palette ──────────────────────────────────────────────────────
    static constexpr juce::uint32 ColBackground = 0xFF1A1A2E;
    static constexpr juce::uint32 ColPanel      = 0xFF16213E;
    static constexpr juce::uint32 ColHeader     = 0xFF0F3460;
    static constexpr juce::uint32 ColAccent     = 0xFFE94560;
    static constexpr juce::uint32 ColKnobBody   = 0xFF2D3561;
    static constexpr juce::uint32 ColKnobTrack  = 0xFF3A3A55;
    static constexpr juce::uint32 ColTextBright = 0xFFEEEEEE;
    static constexpr juce::uint32 ColTextDim    = 0xFF7788AA;
    static constexpr juce::uint32 ColBorder     = 0xFF2A2A44;
    static constexpr juce::uint32 ColLedOff     = 0xFF333355;
    static constexpr juce::uint32 ColLedOn      = 0xFFE94560;

    AmpSimLookAndFeel();

    // ── Overrides ───────────────────────────────────────────────────────────
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider&) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool highlighted, bool down) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& bgColour,
                              bool highlighted, bool down) override;

    void drawComboBox(juce::Graphics&, int w, int h,
                      bool isDown, int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;

    void drawLabel(juce::Graphics&, juce::Label&) override;

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
};
