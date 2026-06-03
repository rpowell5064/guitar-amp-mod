#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

// Stereo LED-style level meter.
// The owning component writes levels from whatever thread; this component
// reads them on a 30 Hz Timer tick and repaints.
class LevelMeter : public juce::Component, private juce::Timer {
public:
    LevelMeter()  { startTimerHz(30); }
    ~LevelMeter() { stopTimer(); }

    void setLevels(float l, float r) noexcept {
        targetL.store(l);
        targetR.store(r);
    }

    void paint(juce::Graphics& g) override {
        const int w  = getWidth();
        const int h  = getHeight();
        const int bw = (w - 2) / 2;

        g.fillAll(juce::Colour(0xFF111122));
        drawBar(g, 0,      0, bw, h, displayL);
        drawBar(g, bw + 2, 0, bw, h, displayR);
    }

private:
    std::atomic<float> targetL{0}, targetR{0};
    float displayL = 0, displayR = 0;

    void timerCallback() override {
        // Instant attack, 300ms approx peak-hold/decay
        const float decay = 0.85f;
        displayL = std::max(targetL.load(), displayL * decay);
        displayR = std::max(targetR.load(), displayR * decay);
        repaint();
    }

    static void drawBar(juce::Graphics& g, int x, int y, int w, int h, float peak) {
        const float db   = 20.0f * std::log10(std::max(peak, 1e-9f));
        const float norm = juce::jmap(db, -60.0f, 0.0f, 0.0f, 1.0f);
        const int   fill = (int)(h * std::clamp(norm, 0.0f, 1.0f));

        // Background
        g.setColour(juce::Colour(0xFF1A1A2E));
        g.fillRect(x, y, w, h);

        if (fill == 0) return;

        // Green zone (lower 70%)
        const int greenTop = h - (int)(h * 0.7f);
        const int yGreen   = std::max(y + h - fill, y + greenTop);
        const int hGreen   = std::min(fill, h - greenTop);
        if (hGreen > 0) {
            g.setColour(juce::Colour(0xFF00CC44));
            g.fillRect(x, yGreen, w, hGreen);
        }
        // Yellow zone (next 20%)
        const int yellowTop = h - (int)(h * 0.9f);
        if (fill > h - greenTop) {
            const int hYellow = std::min(fill - (h - greenTop), (int)(h * 0.2f));
            g.setColour(juce::Colour(0xFFCCCC00));
            g.fillRect(x, y + yellowTop, w, hYellow);
        }
        // Red zone (top 10%)
        if (fill > h - yellowTop) {
            const int hRed = std::min(fill - (h - yellowTop), (int)(h * 0.1f));
            g.setColour(juce::Colour(0xFFCC2200));
            g.fillRect(x, y, w, hRed);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};
