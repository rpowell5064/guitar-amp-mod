#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>

// Signal chain sidebar: a vertical list of named blocks, each with a bypass
// toggle and a drag handle for reordering.
// Clicking a row fires onBlockSelected(index).
// Dragging a row fires onBlockMoved(fromIndex, toIndex).
class SignalChainComponent : public juce::Component {
public:
    struct BlockEntry {
        juce::String name;
        juce::String bypassParamId;
    };

    explicit SignalChainComponent(juce::AudioProcessorValueTreeState& apvts,
                                   const std::vector<BlockEntry>& entries);

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(int)> onBlockSelected;
    std::function<void(int, int)> onBlockMoved;

    void setSelectedIndex(int idx);
    int  getSelectedIndex() const noexcept { return selectedIdx; }

    // Reorder the first 7 rows (Output row is always last).
    // newOrder[displayPos] = originalRowIndex of the row to place there.
    void setOrder(const std::vector<int>& newOrder);

private:
    static constexpr int kRowH = 38;

    struct RowItem : public juce::Component {
        RowItem(const juce::String& n, int i,
                juce::AudioProcessorValueTreeState& apvts,
                const juce::String& bypassId);

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        juce::String blockName;
        int          index;         // current display position (updated by setOrder)
        int          originalIndex; // position at construction time (never changes)
        bool         isSelected = false;

        juce::ToggleButton bypassBtn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;

        std::function<void(int)> onClicked;
    };

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<std::unique_ptr<RowItem>> rows;
    int selectedIdx = 0;

    // Drag tracking state
    int  dragFromIdx   = -1;
    int  dragCurrentY  = -1;
    bool isDragging    = false;

    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp  (const juce::MouseEvent&) override;
    int  rowAtY   (int y) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalChainComponent)
};
