#include "SignalChainComponent.h"

// ── RowItem ───────────────────────────────────────────────────────────────────

SignalChainComponent::RowItem::RowItem(const juce::String& n, int i,
                                        juce::AudioProcessorValueTreeState& apvts,
                                        const juce::String& bypassId)
    : blockName(n), index(i), originalIndex(i)
{
    if (bypassId.isNotEmpty()) {
        bypassBtn.setButtonText({});
        bypassBtn.setClickingTogglesState(true);
        addAndMakeVisible(bypassBtn);
        bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, bypassId, bypassBtn);
    }
}

void SignalChainComponent::RowItem::paint(juce::Graphics& g) {
    const bool on = !bypassBtn.getToggleState(); // bypass=false → block is ON

    if (isSelected) {
        g.setColour(juce::Colour(0xFF252550));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 4.0f);
    }

    // Left accent stripe when active
    if (on) {
        g.setColour(juce::Colour(0xFFE94560));
        g.fillRect(0, 4, 3, getHeight() - 8);
    }

    // Drag handle dots (3 columns of 2)
    g.setColour(juce::Colour(0xFF445566));
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col)
            g.fillEllipse(8.0f + col * 5.0f, 11.0f + row * 5.0f, 3.0f, 3.0f);

    // Block name
    g.setColour(isSelected ? juce::Colours::white : juce::Colour(0xFFAABBCC));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)
                          .withStyle(isSelected ? "Bold" : "Regular")));
    g.drawFittedText(blockName,
        juce::Rectangle<int>(22, 0, getWidth() - 50, getHeight()),
        juce::Justification::centredLeft, 1);
}

void SignalChainComponent::RowItem::resized() {
    const int btnSize = getHeight() - 10;
    bypassBtn.setBounds(getWidth() - btnSize - 4, 5, btnSize, btnSize);
}

void SignalChainComponent::RowItem::mouseDown(const juce::MouseEvent& e) {
    if (onClicked) onClicked(index);
    (void)e;
}

void SignalChainComponent::RowItem::mouseDrag(const juce::MouseEvent& e) {
    if (auto* parent = getParentComponent())
        parent->mouseDrag(e.getEventRelativeTo(parent));
}

void SignalChainComponent::RowItem::mouseUp(const juce::MouseEvent& e) {
    if (auto* parent = getParentComponent())
        parent->mouseUp(e.getEventRelativeTo(parent));
}

void SignalChainComponent::RowItem::mouseMove(const juce::MouseEvent& e) {
    setMouseCursor(e.x < 22 ? juce::MouseCursor::UpDownResizeCursor
                             : juce::MouseCursor::NormalCursor);
}

void SignalChainComponent::RowItem::mouseExit(const juce::MouseEvent&) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

// ── SignalChainComponent ──────────────────────────────────────────────────────

SignalChainComponent::SignalChainComponent(juce::AudioProcessorValueTreeState& apvts,
                                             const std::vector<BlockEntry>& entries)
    : apvts(apvts)
{
    for (int i = 0; i < (int)entries.size(); ++i) {
        auto row = std::make_unique<RowItem>(entries[i].name, i,
                                              apvts, entries[i].bypassParamId);
        row->onClicked = [this](int idx) {
            setSelectedIndex(idx);
            if (onBlockSelected) onBlockSelected(idx);
        };
        addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }
    setSelectedIndex(0);
}

void SignalChainComponent::setSelectedIndex(int idx) {
    selectedIdx = idx;
    for (auto& r : rows)
        r->isSelected = (r->index == idx);
    repaint();
}

void SignalChainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xFF0D1528));

    // Separator lines between rows
    g.setColour(juce::Colour(0xFF1E2A3A));
    for (int i = 1; i < (int)rows.size(); ++i)
        g.drawHorizontalLine(i * kRowH, 0.0f, (float)getWidth());

    // Drag ghost line
    if (isDragging && dragCurrentY >= 0) {
        const int snapY = rowAtY(dragCurrentY) * kRowH;
        g.setColour(juce::Colour(0xFFE94560));
        g.drawHorizontalLine(snapY, 4.0f, (float)getWidth() - 4.0f);
    }
}

void SignalChainComponent::resized() {
    for (int i = 0; i < (int)rows.size(); ++i)
        rows[i]->setBounds(0, i * kRowH, getWidth(), kRowH);
}

int SignalChainComponent::rowAtY(int y) const noexcept {
    return juce::jlimit(0, (int)rows.size() - 1, y / kRowH);
}

void SignalChainComponent::mouseDrag(const juce::MouseEvent& e) {
    // Begin drag when user presses the drag-handle area (left 22px)
    if (!isDragging && e.mouseWasDraggedSinceMouseDown()) {
        const int row = rowAtY(e.getMouseDownY());
        if (e.getMouseDownX() < 22) {
            dragFromIdx = row;
            isDragging  = true;
        }
    }
    if (isDragging) {
        dragCurrentY = e.y;
        repaint();
    }
}

void SignalChainComponent::mouseUp(const juce::MouseEvent& e) {
    if (isDragging) {
        const int toIdx = rowAtY(e.y);
        if (toIdx != dragFromIdx && onBlockMoved)
            onBlockMoved(dragFromIdx, toIdx);
        isDragging   = false;
        dragFromIdx  = -1;
        dragCurrentY = -1;
        repaint();
    }
}

void SignalChainComponent::setOrder(const std::vector<int>& newOrder) {
    // newOrder has 8 entries for the reorderable rows; Output row is always last.
    if (rows.size() < 9 || newOrder.size() != 8) return;

    auto outputRow = std::move(rows.back());
    rows.pop_back();

    std::vector<std::unique_ptr<RowItem>> reordered;
    reordered.reserve(9);
    for (int origIdx : newOrder) {
        for (auto& r : rows) {
            if (r && r->originalIndex == origIdx) {
                reordered.push_back(std::move(r));
                break;
            }
        }
    }
    reordered.push_back(std::move(outputRow));
    rows = std::move(reordered);

    // Update each row's display index and re-seat the click handler.
    for (int i = 0; i < (int)rows.size(); ++i) {
        rows[i]->index = i;
        rows[i]->isSelected = (i == selectedIdx);
    }

    resized();
    repaint();
}
