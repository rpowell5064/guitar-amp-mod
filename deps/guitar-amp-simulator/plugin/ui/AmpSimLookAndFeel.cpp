#include "AmpSimLookAndFeel.h"
#include <cmath>

AmpSimLookAndFeel::AmpSimLookAndFeel() {
    // Global colour overrides for all JUCE widgets
    setColour(juce::ResizableWindow::backgroundColourId,   juce::Colour(ColBackground));
    setColour(juce::Slider::thumbColourId,                 juce::Colour(ColAccent));
    setColour(juce::Slider::rotarySliderFillColourId,      juce::Colour(ColAccent));
    setColour(juce::Slider::trackColourId,                 juce::Colour(ColKnobTrack));
    setColour(juce::Slider::backgroundColourId,            juce::Colour(ColPanel));
    setColour(juce::Slider::textBoxTextColourId,           juce::Colour(ColTextDim));
    setColour(juce::Slider::textBoxOutlineColourId,        juce::Colour(ColBorder));
    setColour(juce::Slider::textBoxBackgroundColourId,     juce::Colour(ColPanel));
    setColour(juce::Label::textColourId,                   juce::Colour(ColTextDim));
    setColour(juce::ComboBox::backgroundColourId,          juce::Colour(ColPanel));
    setColour(juce::ComboBox::textColourId,                juce::Colour(ColTextBright));
    setColour(juce::ComboBox::outlineColourId,             juce::Colour(ColBorder));
    setColour(juce::ComboBox::arrowColourId,               juce::Colour(ColAccent));
    setColour(juce::PopupMenu::backgroundColourId,         juce::Colour(ColHeader));
    setColour(juce::PopupMenu::textColourId,               juce::Colour(ColTextBright));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(ColAccent));
    setColour(juce::TextButton::buttonColourId,            juce::Colour(ColHeader));
    setColour(juce::TextButton::buttonOnColourId,          juce::Colour(ColAccent));
    setColour(juce::TextButton::textColourOffId,           juce::Colour(ColTextBright));
    setColour(juce::TextButton::textColourOnId,            juce::Colours::white);
    setColour(juce::ToggleButton::textColourId,            juce::Colour(ColTextDim));
}

// ── Rotary knob ───────────────────────────────────────────────────────────────
void AmpSimLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                          int x, int y, int width, int height,
                                          float sliderPos,
                                          float startAngle, float endAngle,
                                          juce::Slider& slider) {
    const float cx  = x + width  * 0.5f;
    const float cy  = y + height * 0.5f;
    const float r   = std::min(width, height) * 0.5f - 4.0f;
    const float rIn = r * 0.78f;

    // ── Body shadow
    g.setColour(juce::Colour(0x44000000));
    g.fillEllipse(cx - r - 1, cy - r + 2, (r + 1) * 2.0f, (r + 1) * 2.0f);

    // ── Body gradient
    juce::ColourGradient bodyGrad(juce::Colour(0xFF3D4A7A), cx, cy - r,
                                   juce::Colour(0xFF1A2040), cx, cy + r, false);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);

    // ── Outer ring
    g.setColour(juce::Colour(ColBorder));
    g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);

    // ── Track arc (full range)
    {
        juce::Path track;
        track.addCentredArc(cx, cy, rIn, rIn, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(ColKnobTrack));
        g.strokePath(track, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    // ── Value arc (accent colour)
    const float valueAngle = startAngle + sliderPos * (endAngle - startAngle);
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, rIn, rIn, 0.0f, startAngle, valueAngle, true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.strokePath(arc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    // ── Indicator dot (white, at arc end)
    {
        const float dotR  = r * 0.12f;
        const float halfPi = juce::MathConstants<float>::halfPi;
        const float dotX  = cx + rIn * std::cos(valueAngle - halfPi);
        const float dotY  = cy + rIn * std::sin(valueAngle - halfPi);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    // ── Inner highlight
    juce::ColourGradient shine(juce::Colours::white.withAlpha(0.08f), cx, cy - r * 0.5f,
                                juce::Colours::transparentBlack, cx, cy + r * 0.5f, true);
    g.setGradientFill(shine);
    g.fillEllipse(cx - r * 0.7f, cy - r * 0.7f, r * 1.4f, r * 1.4f);
}

// ── LED toggle button ─────────────────────────────────────────────────────────
void AmpSimLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                                          bool highlighted, bool /*down*/) {
    const float w    = (float)btn.getWidth();
    const float h    = (float)btn.getHeight();
    const bool  on   = btn.getToggleState();
    const float cr   = 4.0f;

    // Background pill
    const juce::Colour bg = on ? juce::Colour(ColAccent).darker(0.2f)
                                : juce::Colour(ColPanel);
    g.setColour(highlighted ? bg.brighter(0.15f) : bg);
    g.fillRoundedRectangle(0, 0, w, h, cr);

    g.setColour(juce::Colour(ColBorder));
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, cr, 1.0f);

    // LED circle on the left side
    const float ledSize = h - 10.0f;
    const float ledX    = 5.0f;
    const float ledY    = (h - ledSize) * 0.5f;
    if (on) {
        g.setColour(juce::Colour(ColAccent).withAlpha(0.35f));
        g.fillEllipse(ledX - 2, ledY - 2, ledSize + 4, ledSize + 4);
    }
    g.setColour(on ? juce::Colour(ColLedOn) : juce::Colour(ColLedOff));
    g.fillEllipse(ledX, ledY, ledSize, ledSize);

    // Specular on LED
    if (on) {
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.fillEllipse(ledX + ledSize * 0.2f, ledY + ledSize * 0.1f,
                       ledSize * 0.35f, ledSize * 0.25f);
    }

    // Label text
    g.setColour(on ? juce::Colours::white : juce::Colour(ColTextDim));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f).withStyle("Bold")));
    g.drawFittedText(btn.getButtonText(),
        juce::Rectangle<int>((int)(ledX + ledSize + 5), 0,
                              (int)(w - ledX - ledSize - 10), (int)h),
        juce::Justification::centredLeft, 1);
}

// ── Text buttons (Load, Save, etc.) ───────────────────────────────────────────
void AmpSimLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                                               const juce::Colour& /*bgColour*/,
                                               bool highlighted, bool down) {
    const float cr   = 4.0f;
    const float w    = (float)btn.getWidth();
    const float h    = (float)btn.getHeight();
    juce::Colour col = juce::Colour(ColHeader);
    if (btn.getToggleState()) col = juce::Colour(ColAccent).darker(0.1f);
    if (highlighted)          col = col.brighter(0.12f);
    if (down)                 col = col.darker(0.15f);

    g.setColour(col);
    g.fillRoundedRectangle(0, 0, w, h, cr);
    g.setColour(juce::Colour(ColBorder));
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, cr, 1.0f);
}

// ── ComboBox ──────────────────────────────────────────────────────────────────
void AmpSimLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h,
                                      bool /*isDown*/,
                                      int btnX, int /*btnY*/, int btnW, int /*btnH*/,
                                      juce::ComboBox& box) {
    const float cr = 4.0f;
    g.setColour(juce::Colour(ColPanel));
    g.fillRoundedRectangle(0, 0, (float)w, (float)h, cr);
    g.setColour(juce::Colour(ColBorder));
    g.drawRoundedRectangle(0.5f, 0.5f, (float)w - 1, (float)h - 1, cr, 1.0f);

    // Arrow
    const float cx = btnX + btnW * 0.5f;
    const float cy = h * 0.5f;
    juce::Path arrow;
    arrow.addTriangle(cx - 5, cy - 3, cx + 5, cy - 3, cx, cy + 3);
    g.setColour(juce::Colour(ColAccent));
    g.fillPath(arrow);
    (void)box;
}

// ── Labels ────────────────────────────────────────────────────────────────────
void AmpSimLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    if (!label.isBeingEdited()) {
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(getLabelFont(label));
        g.drawFittedText(label.getText(),
                         label.getLocalBounds().reduced(1),
                         label.getJustificationType(),
                         juce::jmax(1, (int)((float)label.getHeight() / g.getCurrentFont().getHeight())),
                         label.getMinimumHorizontalScale());
    }
}

juce::Font AmpSimLookAndFeel::getLabelFont(juce::Label& l) {
    return juce::Font(juce::FontOptions{}.withHeight(l.getFont().getHeight()));
}

juce::Font AmpSimLookAndFeel::getComboBoxFont(juce::ComboBox& b) {
    return juce::Font(juce::FontOptions{}.withHeight((float)b.getHeight() * 0.55f));
}

// ── Linear slider (used for low/high cut displays) ─────────────────────────────
void AmpSimLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                                          float sliderPos, float, float,
                                          juce::Slider::SliderStyle style,
                                          juce::Slider& slider) {
    if (style == juce::Slider::LinearHorizontal) {
        const float trackY  = y + h * 0.5f;
        const float trackH  = 4.0f;

        // Track background
        g.setColour(juce::Colour(ColKnobTrack));
        g.fillRoundedRectangle((float)x, trackY - trackH * 0.5f, (float)w, trackH, 2.0f);

        // Track fill
        g.setColour(juce::Colour(ColAccent));
        g.fillRoundedRectangle((float)x, trackY - trackH * 0.5f,
                                sliderPos - x, trackH, 2.0f);

        // Thumb
        const float thumbR = 7.0f;
        g.setColour(juce::Colour(ColAccent).brighter(0.2f));
        g.fillEllipse(sliderPos - thumbR, trackY - thumbR, thumbR * 2, thumbR * 2);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawEllipse(sliderPos - thumbR, trackY - thumbR, thumbR * 2, thumbR * 2, 1.0f);
    } else {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, 0, 1, style, slider);
    }
}
