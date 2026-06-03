#pragma once
#include <JuceHeader.h>
#include "HorizontalChainComponent.h"

// Shared base for all DSP block UI panels.
// Draws a full-width dark panel, a coloured header that matches the block's
// accent colour in the horizontal chain, and a bypass toggle.
class BlockComponentBase : public juce::Component {
public:
    explicit BlockComponentBase(const juce::String& blockName,
                                 juce::AudioProcessorValueTreeState& apvts,
                                 const juce::String& bypassParamId,
                                 int origIdx = -1) // origIdx drives the accent colour
        : name(blockName), origIdx_(origIdx)
    {
        if (bypassParamId.isNotEmpty()) {
            bypassButton.setButtonText("BYPASS");
            bypassButton.setClickingTogglesState(true);
            addAndMakeVisible(bypassButton);
            bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                apvts, bypassParamId, bypassButton);
        }
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xFF16213E));

        const auto hdr = getLocalBounds().removeFromTop(kHeaderH);

        // Header background — slightly lighter than body
        g.setColour(juce::Colour(0xFF0F2248));
        g.fillRect(hdr);

        // Accent stripe at bottom of header (matches chain block colour)
        const juce::Colour accent = origIdx_ >= 0
            ? HorizontalChainComponent::effectAccent(origIdx_)
            : juce::Colour(0xFFE94560);
        g.setColour(accent);
        g.fillRect(0, kHeaderH - 3, getWidth(), 3);

        // Title
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(16.0f).withStyle("Bold")));
        g.drawFittedText(name,
            juce::Rectangle<int>(12, 0, getWidth() - 110, kHeaderH),
            juce::Justification::centredLeft, 1);
    }

    void resized() override {
        bypassButton.setBounds(getWidth() - 90, 8, 82, kHeaderH - 16);
    }

protected:
    static constexpr int kHeaderH = 44;

    static void configureKnob(juce::Slider& s, juce::Label& l,
                               const juce::String& labelText,
                               juce::Component& parent) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        parent.addAndMakeVisible(s);

        l.setText(labelText, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centredTop);
        l.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        l.setColour(juce::Label::textColourId, juce::Colour(0xFF8899AA));
        parent.addAndMakeVisible(l);
    }

    static void layoutKnob(juce::Slider& s, juce::Label& l,
                            juce::Rectangle<int> col) {
        const int labelH = 20;
        l.setBounds(col.getX(), col.getBottom() - labelH, col.getWidth(), labelH);
        s.setBounds(col.getX() + 4, col.getY(), col.getWidth() - 8,
                    col.getHeight() - labelH - 2);
    }

    juce::String name;
    int origIdx_ = -1;
    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;
};
