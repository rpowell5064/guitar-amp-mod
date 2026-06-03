#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>
#include <array>

// Horizontal signal chain: active effects displayed as styled pedal blocks
// left-to-right. Output block is always at the far right and cannot be removed.
// Double-click a block → onBlockDoubleClicked. Drag the handle → reorder.
// Click × → onBlockRemoved. Click + → onBlockAdded popup.
class HorizontalChainComponent : public juce::Component {
public:
    static constexpr int kNumEffects = 10; // Gate..Compressor (Output is separate)

    struct EffectInfo {
        juce::String name;       // "Noise Gate"
        juce::String shortName;  // "GATE"
        juce::String bypassParamId;
    };

    HorizontalChainComponent(juce::AudioProcessorValueTreeState& apvts,
                              const std::array<EffectInfo, kNumEffects>& infos);
    ~HorizontalChainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // origIdx 0-9 = effect, 10 = Output
    std::function<void(int origIdx)>      onBlockSelected;
    std::function<void(int origIdx)>      onBlockDoubleClicked;
    std::function<void(int from, int to)> onBlockMoved;    // display positions
    std::function<void(int origIdx)>      onBlockRemoved;
    std::function<void(int origIdx)>      onBlockAdded;

    void setSelectedOrigIdx(int origIdx);

    // activeOrder[displayPos] = origIdx; active[i] = is effect i in the chain.
    void setChainState(const std::vector<int>& activeOrder,
                       const std::array<bool, kNumEffects>& active);

    // Per-effect background and accent colors (public so BlockWidget can use them)
    static juce::Colour effectBg    (int origIdx);
    static juce::Colour effectAccent(int origIdx,
                                     juce::AudioProcessorValueTreeState* apvts = nullptr);

    static void drawIcon(juce::Graphics& g, int origIdx,
                         juce::Rectangle<float> bounds, juce::Colour col,
                         juce::AudioProcessorValueTreeState* apvts = nullptr);

private:
    // ─── Inner block widget ───────────────────────────────────────────────────
    class BlockWidget : public juce::Component {
    public:
        BlockWidget(int origIdx, const juce::String& shortName,
                    juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& bypassParamId, bool isOutput);

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown        (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseDrag        (const juce::MouseEvent& e) override;
        void mouseUp          (const juce::MouseEvent& e) override;

        int  origIdx;
        bool isSelected = false;

        std::function<void()>                        onClicked;
        std::function<void()>                        onDoubleClicked;
        std::function<void(const juce::MouseEvent&)> onDragMove;
        std::function<void(const juce::MouseEvent&)> onDragEnd;
        std::function<void()>                        onRemove;

    private:
        juce::AudioProcessorValueTreeState& apvts_;
        juce::String shortName_;
        bool isOutput_;

        juce::ToggleButton bypassBtn_;
        juce::TextButton   removeBtn_{"x"};
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockWidget)
    };

    // ─── State ────────────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState& apvts_;
    const std::array<EffectInfo, kNumEffects>& infos_;

    std::array<bool, kNumEffects> active_{};
    std::vector<int> activeOrder_;   // display pos → origIdx (active effects only)
    int selectedOrigIdx_ = 3;

    // One widget per active effect + one for Output (always last)
    std::vector<std::unique_ptr<BlockWidget>> widgets_;
    juce::TextButton addButton_;

    // Drag state
    int  dragFromDisplay_  = -1;
    int  dropTarget_       = -1;
    bool isDragging_       = false;

    static constexpr int kMaxBlockW = 110;
    static constexpr int kMinBlockW = 56;
    static constexpr int kArrowW    = 20;
    static constexpr int kPad       = 8;
    static constexpr int kAddW      = 32;

    // Block for Output (always last slot = activeOrder_.size())
    int totalSlots() const noexcept { return (int)activeOrder_.size() + 1; }
    int computeBlockW() const noexcept;
    int blockLeftX(int slot) const noexcept; // slot 0..totalSlots()-1 + slot for Output
    int slotAtX(int x) const noexcept;

    void rebuildWidgets();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HorizontalChainComponent)
};
