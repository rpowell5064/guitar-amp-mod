#include "HorizontalChainComponent.h"

// ─── Color themes ─────────────────────────────────────────────────────────────

juce::Colour HorizontalChainComponent::effectBg(int origIdx) {
    switch (origIdx) {
        case 0: return juce::Colour(0xFF1C2430); // Gate: steel navy
        case 1: return juce::Colour(0xFF2E1414); // Pitch: dark red (Whammy)
        case 2: return juce::Colour(0xFF142214); // Drive: dark green (TS-808)
        case 3: return juce::Colour(0xFF1A1810); // Amp: near black
        case 4: return juce::Colour(0xFF1E1812); // Cabinet: dark wood
        case 5: return juce::Colour(0xFF10182A); // Modulation: navy (Boss)
        case 6: return juce::Colour(0xFF1E150A); // Delay: dark amber (tape)
        case 7: return juce::Colour(0xFF130A22); // Reverb: dark purple
        case 8: return juce::Colour(0xFF081A1A); // EQ: dark teal
        case 9: return juce::Colour(0xFF121A28); // Compressor: dark slate
        default: return juce::Colour(0xFF141428); // Output
    }
}

juce::Colour HorizontalChainComponent::effectAccent(int origIdx,
                                                     juce::AudioProcessorValueTreeState* apvts) {
    if (origIdx == 3 && apvts) {
        auto* p = apvts->getRawParameterValue("amp_model");
        int model = p ? (int)*p : 1;
        switch (model) {
            case 0: return juce::Colour(0xFFBB8844); // Fender: tweed
            case 1: return juce::Colour(0xFFC4952A); // Marshall JCM800: gold
            case 2: return juce::Colour(0xFFCC2222); // EVH 5150: red
            case 3: return juce::Colour(0xFF4499DD); // Neural: blue
            case 4: return juce::Colour(0xFF999999); // Sunn: silver
            case 5: return juce::Colour(0xFFE87722); // Orange Rockerverb: orange
            default: return juce::Colour(0xFFC4952A);
        }
    }
    switch (origIdx) {
        case 0: return juce::Colour(0xFF5A7A90); // Gate: blue-gray
        case 1: return juce::Colour(0xFFCC3333); // Pitch: red
        case 2: return juce::Colour(0xFF4CAF50); // Drive: green
        case 3: return juce::Colour(0xFFC4952A); // Amp: gold (default)
        case 4: return juce::Colour(0xFF8B6845); // Cabinet: brown
        case 5: return juce::Colour(0xFF3366CC); // Modulation: boss blue
        case 6: return juce::Colour(0xFFCC8833); // Delay: amber
        case 7: return juce::Colour(0xFF9955CC); // Reverb: purple
        case 8: return juce::Colour(0xFF33AAAA); // EQ: teal
        case 9: return juce::Colour(0xFF66AADD); // Compressor: steel blue
        default: return juce::Colour(0xFFE94560); // Output: red
    }
}

// ─── Icons ────────────────────────────────────────────────────────────────────

static void icon_Gate(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    g.setColour(c);
    const float bw = b.getWidth(), bh = b.getHeight();
    const float barW = bw * 0.14f, gap = bw * 0.07f;
    const float startX = b.getX() + (bw - 3*barW - 2*gap) * 0.5f;
    const float baseY  = b.getBottom() - bh * 0.08f;
    const float heights[] = { bh*0.4f, bh*0.65f, bh*0.5f };
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle(startX + i*(barW+gap), baseY - heights[i], barW, heights[i], 2.f);
    g.setColour(c.withAlpha(0.45f));
    g.drawHorizontalLine((int)(baseY - bh*0.42f),
                         b.getX() + bw*0.05f, b.getRight() - bw*0.05f);
}

static void icon_Pitch(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    g.setColour(c);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float w = b.getWidth()*0.5f, h = b.getHeight()*0.55f;
    juce::Path p;
    // Arrowhead up
    p.startNewSubPath(cx,       cy - h*0.5f);
    p.lineTo(cx - w*0.25f, cy - h*0.15f);
    p.startNewSubPath(cx,       cy - h*0.5f);
    p.lineTo(cx + w*0.25f, cy - h*0.15f);
    // Stem
    p.startNewSubPath(cx, cy - h*0.5f);
    p.lineTo(cx, cy + h*0.3f);
    // Whammy bar curve
    p.startNewSubPath(cx - w*0.4f, cy + h*0.3f);
    p.quadraticTo(cx, cy + h*0.62f, cx + w*0.4f, cy + h*0.3f);
    g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
}

static void icon_Drive(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    g.setColour(c);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float w = b.getWidth()*0.8f, h = b.getHeight()*0.4f;
    const float clip = h * 0.62f;
    juce::Path p;
    for (int i = 0; i <= 32; ++i) {
        float t = (float)i / 32.f;
        float x = cx - w*0.5f + t*w;
        float raw = std::sin(t * juce::MathConstants<float>::twoPi) * h;
        float y = cy - juce::jlimit(-clip, clip, raw);
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved));
    g.setColour(c.withAlpha(0.35f));
    g.drawHorizontalLine((int)(cy - clip), cx - w*0.5f, cx + w*0.5f);
    g.drawHorizontalLine((int)(cy + clip), cx - w*0.5f, cx + w*0.5f);
}

static void icon_Amp(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    g.setColour(c);
    const float bw = b.getWidth(), bh = b.getHeight();
    const float chW = bw*0.82f, chH = bh*0.42f;
    const float chX = b.getX() + (bw - chW)*0.5f, chY = b.getY() + bh*0.42f;
    // Chassis
    g.drawRoundedRectangle(chX, chY, chW, chH, 3.f, 1.8f);
    // Tubes
    const float tubeW = bw*0.1f, tubeH = bh*0.3f;
    for (int i = 0; i < 3; ++i) {
        float tx = chX + chW*0.22f + i*(chW*0.25f) - tubeW*0.5f;
        float ty = chY - tubeH;
        g.drawRoundedRectangle(tx, ty, tubeW, tubeH, tubeW*0.4f, 1.4f);
        g.setColour(c.withAlpha(0.55f));
        g.fillEllipse(tx + tubeW*0.2f, ty + tubeH*0.2f, tubeW*0.6f, tubeW*0.6f);
        g.setColour(c);
    }
    // Knobs row
    for (int i = 0; i < 4; ++i) {
        float kx = chX + chW*0.13f + i*(chW*0.24f);
        float ky = chY + chH*0.55f;
        g.fillEllipse(kx - 3.f, ky - 3.f, 6.f, 6.f);
    }
}

static void icon_Cabinet(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    g.setColour(c);
    const float bw = b.getWidth(), bh = b.getHeight();
    const float boxW = bw*0.78f, boxH = bh*0.72f;
    const float bx = b.getX() + (bw - boxW)*0.5f;
    const float by = b.getY() + (bh - boxH)*0.5f;
    g.drawRoundedRectangle(bx, by, boxW, boxH, 3.f, 1.6f);
    // Speaker rings
    const float cx = b.getCentreX(), cy = b.getCentreY();
    for (float r : { boxH*0.08f, boxH*0.18f, boxH*0.3f })
        g.drawEllipse(cx - r, cy - r, r*2.f, r*2.f, 1.2f);
    g.fillEllipse(cx - 4.f, cy - 4.f, 8.f, 8.f);
}

static void icon_Modulation(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float w = b.getWidth()*0.82f, h = b.getHeight()*0.3f;
    for (int wave = 0; wave < 2; ++wave) {
        g.setColour(wave == 0 ? c : c.withAlpha(0.5f));
        juce::Path p;
        float phase = wave == 0 ? 0.f : 0.7f;
        float amp   = wave == 0 ? h : h*0.65f;
        for (int i = 0; i <= 24; ++i) {
            float t = (float)i / 24.f;
            float x = cx - w*0.5f + t*w;
            float y = cy + wave*h*0.3f
                      - std::sin(t * juce::MathConstants<float>::twoPi * 1.5f + phase) * amp;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.strokePath(p, juce::PathStrokeType(wave == 0 ? 1.8f : 1.2f,
                                              juce::PathStrokeType::curved));
    }
}

static void icon_Delay(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float sp = b.getWidth() * 0.22f;
    const float alphas[] = { 1.0f, 0.55f, 0.25f };
    const float sizes[]  = { 9.0f, 6.5f,  4.0f  };
    for (int i = 0; i < 3; ++i) {
        g.setColour(c.withAlpha(alphas[i]));
        float x = cx - sp + i*sp;
        float s = sizes[i];
        g.fillEllipse(x - s*0.5f, cy - s*0.5f, s, s);
    }
    g.setColour(c.withAlpha(0.22f));
    g.drawLine(cx - sp + 5.f, cy, cx - sp + sp*0.55f, cy, 1.f);
    g.drawLine(cx + 5.f,      cy, cx + sp*0.55f,       cy, 1.f);
}

static void icon_Reverb(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    const float cx = b.getCentreX(), cy = b.getCentreY() + b.getHeight()*0.05f;
    const float radii[]  = { b.getHeight()*0.11f, b.getHeight()*0.22f, b.getHeight()*0.33f };
    const float alphas[] = { 0.9f, 0.55f, 0.28f };
    for (int i = 0; i < 3; ++i) {
        g.setColour(c.withAlpha(alphas[i]));
        float r = radii[i];
        juce::Path arc;
        arc.addArc(cx - r, cy - r, r*2.f, r*2.f,
                   -juce::MathConstants<float>::pi * 0.6f,
                    juce::MathConstants<float>::pi * 0.6f, true);
        g.strokePath(arc, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved));
    }
    g.setColour(c);
    g.fillEllipse(cx - 3.f, cy - 3.f, 6.f, 6.f);
}

static void icon_EQ(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    g.setColour(c);
    const float bw = b.getWidth(), bh = b.getHeight();
    const float barW  = bw * 0.11f;
    const float areaW = bw * 0.78f;
    const float baseY = b.getCentreY() + bh * 0.28f;
    const float maxH  = bh * 0.58f;
    const float hts[] = { 0.42f, 0.72f, 1.0f, 0.68f, 0.38f };
    const float startX = b.getX() + (bw - areaW)*0.5f;
    for (int i = 0; i < 5; ++i) {
        float x = startX + i * (areaW / 4.0f);
        float h = maxH * hts[i];
        g.fillRoundedRectangle(x, baseY - h, barW, h, 1.5f);
    }
    // Curve overlay
    juce::Path curve;
    for (int j = 0; j <= 20; ++j) {
        float t = (float)j / 20.f;
        float x = startX + t*areaW;
        float sh = 0.5f + 0.45f * std::sin(t * juce::MathConstants<float>::pi + 0.4f);
        float y = baseY - maxH * sh;
        if (j == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    g.setColour(c.withAlpha(0.55f));
    g.strokePath(curve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
}

static void icon_Compressor(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float w = b.getWidth() * 0.62f, h = b.getHeight() * 0.52f;
    // Knee/threshold point (slightly left of centre)
    const float kx = cx - w * 0.12f, ky = cy + h * 0.12f;
    // Transfer function: 1:1 below knee, compressed above
    juce::Path curve;
    curve.startNewSubPath(cx - w * 0.5f, cy + h * 0.5f);
    curve.lineTo(kx, ky);
    curve.lineTo(cx + w * 0.5f, cy - h * 0.15f);
    g.setColour(c);
    g.strokePath(curve, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    // 1:1 reference line
    juce::Path ref;
    ref.startNewSubPath(cx - w * 0.5f, cy + h * 0.5f);
    ref.lineTo(cx + w * 0.5f, cy - h * 0.5f);
    g.setColour(c.withAlpha(0.28f));
    g.strokePath(ref, juce::PathStrokeType(1.1f));
    // Threshold marker
    g.setColour(c.withAlpha(0.4f));
    g.drawLine(kx, cy + h * 0.5f, kx, cy - h * 0.5f, 1.0f);
}

static void icon_Output(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    g.setColour(c);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float h = b.getHeight() * 0.38f, w = b.getWidth() * 0.32f;
    // Speaker body
    juce::Path spk;
    spk.addRoundedRectangle(cx - w*0.5f, cy - h*0.35f, w*0.45f, h*0.7f, 1.5f);
    spk.startNewSubPath(cx - w*0.05f, cy - h*0.35f);
    spk.lineTo(cx + w*0.5f, cy - h*0.65f);
    spk.lineTo(cx + w*0.5f, cy + h*0.65f);
    spk.lineTo(cx - w*0.05f, cy + h*0.35f);
    spk.closeSubPath();
    g.fillPath(spk);
    // Sound waves
    g.setColour(c.withAlpha(0.55f));
    for (float r : { h*0.55f, h*0.85f }) {
        juce::Path arc;
        arc.addArc(cx + w*0.5f - r, cy - r, r*2.f, r*2.f,
                   -juce::MathConstants<float>::pi * 0.38f,
                    juce::MathConstants<float>::pi * 0.38f, true);
        g.strokePath(arc, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved));
    }
}

void HorizontalChainComponent::drawIcon(juce::Graphics& g, int origIdx,
                                         juce::Rectangle<float> bounds,
                                         juce::Colour col,
                                         juce::AudioProcessorValueTreeState* /*apvts*/) {
    switch (origIdx) {
        case 0:  icon_Gate      (g, bounds, col); break;
        case 1:  icon_Pitch     (g, bounds, col); break;
        case 2:  icon_Drive     (g, bounds, col); break;
        case 3:  icon_Amp       (g, bounds, col); break;
        case 4:  icon_Cabinet   (g, bounds, col); break;
        case 5:  icon_Modulation(g, bounds, col); break;
        case 6:  icon_Delay     (g, bounds, col); break;
        case 7:  icon_Reverb    (g, bounds, col); break;
        case 8:  icon_EQ         (g, bounds, col); break;
        case 9:  icon_Compressor (g, bounds, col); break;
        default: icon_Output     (g, bounds, col); break;
    }
}

// ─── BlockWidget ──────────────────────────────────────────────────────────────

HorizontalChainComponent::BlockWidget::BlockWidget(
        int idx, const juce::String& shortName,
        juce::AudioProcessorValueTreeState& apvts,
        const juce::String& bypassParamId, bool isOutput)
    : origIdx(idx), apvts_(apvts), shortName_(shortName), isOutput_(isOutput)
{
    if (bypassParamId.isNotEmpty()) {
        bypassBtn_.setClickingTogglesState(true);
        bypassBtn_.setButtonText({});
        addAndMakeVisible(bypassBtn_);
        bypassAttach_ = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts_, bypassParamId, bypassBtn_);
    }

    if (!isOutput_) {
        removeBtn_.setButtonText("x");
        addAndMakeVisible(removeBtn_);
        removeBtn_.onClick = [this] { if (onRemove) onRemove(); };
    }

    bypassBtn_.onClick = [this] { repaint(); };
}

void HorizontalChainComponent::BlockWidget::paint(juce::Graphics& g) {
    const bool bypassed = bypassBtn_.getToggleState();
    const juce::Colour bg     = HorizontalChainComponent::effectBg(origIdx);
    const juce::Colour accent = HorizontalChainComponent::effectAccent(
        origIdx, isOutput_ ? nullptr : &apvts_);

    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    // Background gradient
    juce::ColourGradient grad(bg.brighter(0.18f), 0.f, 0.f,
                              bg.darker(0.12f),  0.f, (float)getHeight(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 6.0f);

    // Border
    if (isSelected) {
        g.setColour(accent.withAlpha(0.85f));
        g.drawRoundedRectangle(bounds, 6.0f, 2.0f);
        g.setColour(accent.withAlpha(0.12f));
        g.fillRoundedRectangle(bounds, 6.0f);
    } else {
        g.setColour(juce::Colour(0xFF2C3555));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }

    // Top accent stripe
    {
        juce::Path stripe;
        stripe.addRoundedRectangle(bounds.getX(), bounds.getY(),
                                   bounds.getWidth(), 3.5f,
                                   6.f, 6.f, true, true, false, false);
        g.setColour(bypassed ? accent.withAlpha(0.18f) : accent);
        g.fillPath(stripe);
    }

    // Icon
    const int nameH = 20;
    const int btnH  = isOutput_ ? 0 : 18;
    juce::Rectangle<float> iconRect(
        bounds.getX() + 6,
        bounds.getY() + 8,
        bounds.getWidth() - 12,
        bounds.getHeight() - nameH - btnH - 12);

    drawIcon(g, origIdx, iconRect,
             bypassed ? accent.withAlpha(0.22f) : accent,
             isOutput_ ? nullptr : &apvts_);

    // Short name
    g.setColour(bypassed ? juce::Colour(0xFF3A4A5A) : juce::Colour(0xFFCCDDEE));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f).withStyle("Bold")));
    g.drawFittedText(shortName_,
        juce::Rectangle<int>(2, getHeight() - nameH, getWidth() - 4, nameH - 2),
        juce::Justification::centred, 1);
}

void HorizontalChainComponent::BlockWidget::resized() {
    const int sz = 14;
    // Bypass LED: bottom-left
    if (!isOutput_)
        bypassBtn_.setBounds(4, getHeight() - sz - 4, sz, sz);
    // Remove X: top-right
    if (!isOutput_)
        removeBtn_.setBounds(getWidth() - sz - 2, 2, sz, sz);
}

void HorizontalChainComponent::BlockWidget::mouseDown(const juce::MouseEvent& e) {
    if (onClicked) onClicked();
    (void)e;
}

void HorizontalChainComponent::BlockWidget::mouseDoubleClick(const juce::MouseEvent& e) {
    if (onDoubleClicked) onDoubleClicked();
    (void)e;
}

void HorizontalChainComponent::BlockWidget::mouseDrag(const juce::MouseEvent& e) {
    if (onDragMove) onDragMove(e.getEventRelativeTo(getParentComponent()));
}

void HorizontalChainComponent::BlockWidget::mouseUp(const juce::MouseEvent& e) {
    if (onDragEnd) onDragEnd(e.getEventRelativeTo(getParentComponent()));
}

// ─── HorizontalChainComponent ─────────────────────────────────────────────────

HorizontalChainComponent::HorizontalChainComponent(
        juce::AudioProcessorValueTreeState& apvts,
        const std::array<EffectInfo, kNumEffects>& infos)
    : apvts_(apvts), infos_(infos)
{
    active_.fill(true);
    for (int i = 0; i < kNumEffects; ++i) activeOrder_.push_back(i);

    addButton_.setButtonText("+");
    addButton_.onClick = [this] {
        juce::PopupMenu menu;
        bool anyInactive = false;
        for (int i = 0; i < kNumEffects; ++i) {
            if (!active_[i]) {
                menu.addItem(i + 1, infos_[i].name);
                anyInactive = true;
            }
        }
        if (!anyInactive) return;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addButton_),
            [this](int result) {
                if (result >= 1 && result <= kNumEffects) {
                    const int origIdx = result - 1;
                    if (onBlockAdded) onBlockAdded(origIdx);
                }
            });
    };
    addAndMakeVisible(addButton_);

    rebuildWidgets();
}

HorizontalChainComponent::~HorizontalChainComponent() = default;

int HorizontalChainComponent::computeBlockW() const noexcept {
    const int N = totalSlots(); // active effects + Output
    const int arrows = N - 1;
    const int avail = getWidth() - kPad*2 - kAddW - 4;
    const int computed = (avail - arrows * kArrowW) / std::max(1, N);
    return juce::jlimit(kMinBlockW, kMaxBlockW, computed);
}

int HorizontalChainComponent::blockLeftX(int slot) const noexcept {
    const int bw = computeBlockW();
    return kPad + slot * (bw + kArrowW);
}

int HorizontalChainComponent::slotAtX(int x) const noexcept {
    const int bw = computeBlockW();
    const int slot = (x - kPad) / (bw + kArrowW);
    return juce::jlimit(0, totalSlots() - 1, slot);
}

void HorizontalChainComponent::rebuildWidgets() {
    for (auto& w : widgets_) removeChildComponent(w.get());
    widgets_.clear();

    // One widget per active effect (in display order)
    for (int dp = 0; dp < (int)activeOrder_.size(); ++dp) {
        const int origIdx = activeOrder_[dp];
        const auto& info  = infos_[origIdx];
        auto w = std::make_unique<BlockWidget>(
            origIdx, info.shortName, apvts_, info.bypassParamId, false);

        w->isSelected = (origIdx == selectedOrigIdx_);

        w->onClicked = [this, origIdx] {
            setSelectedOrigIdx(origIdx);
            if (onBlockSelected) onBlockSelected(origIdx);
        };
        w->onDoubleClicked = [this, origIdx] {
            if (onBlockDoubleClicked) onBlockDoubleClicked(origIdx);
        };
        w->onDragMove = [this](const juce::MouseEvent& e) {
            if (!isDragging_ && e.mouseWasDraggedSinceMouseDown()) {
                isDragging_ = true;
            }
            if (isDragging_) {
                dropTarget_ = slotAtX(e.x);
                repaint();
            }
        };
        w->onDragEnd = [this, dp](const juce::MouseEvent& e) {
            if (isDragging_) {
                const int toSlot = slotAtX(e.x);
                if (toSlot != dp && toSlot < (int)activeOrder_.size()
                    && onBlockMoved)
                    onBlockMoved(dp, toSlot);
                isDragging_  = false;
                dragFromDisplay_ = -1;
                dropTarget_  = -1;
                repaint();
            }
        };
        w->onRemove = [this, origIdx] {
            if (onBlockRemoved) onBlockRemoved(origIdx);
        };

        addAndMakeVisible(*w);
        widgets_.push_back(std::move(w));
    }

    // Output block (always last, fixed)
    {
        auto w = std::make_unique<BlockWidget>(kNumEffects, "OUT", apvts_, juce::String{}, true);
        w->isSelected = (selectedOrigIdx_ == kNumEffects);
        w->onClicked  = [this] {
            setSelectedOrigIdx(kNumEffects);
            if (onBlockSelected) onBlockSelected(kNumEffects);
        };
        w->onDoubleClicked = [this] {
            if (onBlockDoubleClicked) onBlockDoubleClicked(kNumEffects);
        };
        addAndMakeVisible(*w);
        widgets_.push_back(std::move(w));
    }

    resized();
    repaint();
}

void HorizontalChainComponent::setChainState(const std::vector<int>& activeOrder,
                                               const std::array<bool, kNumEffects>& active) {
    activeOrder_ = activeOrder;
    active_ = active;
    rebuildWidgets();
}

void HorizontalChainComponent::setSelectedOrigIdx(int origIdx) {
    selectedOrigIdx_ = origIdx;
    for (auto& w : widgets_) {
        w->isSelected = (w->origIdx == origIdx);
        w->repaint();
    }
}

void HorizontalChainComponent::paint(juce::Graphics& g) {
    // Background
    g.fillAll(juce::Colour(0xFF0C1220));

    // Top divider line
    g.setColour(juce::Colour(0xFF1E2A40));
    g.fillRect(0, 0, getWidth(), 2);

    // Draw arrows between blocks
    const int bw  = computeBlockW();
    const int bh  = getHeight();
    const int cnt = (int)widgets_.size();

    g.setColour(juce::Colour(0xFF3A4A60));
    for (int i = 0; i < cnt - 1; ++i) {
        const int ax = blockLeftX(i) + bw;
        const int ay = bh / 2;
        const int mw = kArrowW;

        // Arrow line
        g.drawLine((float)(ax + 3), (float)ay, (float)(ax + mw - 6), (float)ay, 1.5f);
        // Arrowhead
        juce::Path arrow;
        arrow.startNewSubPath((float)(ax + mw - 8), (float)(ay - 4));
        arrow.lineTo((float)(ax + mw - 3), (float)ay);
        arrow.lineTo((float)(ax + mw - 8), (float)(ay + 4));
        g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::mitered,
                                                  juce::PathStrokeType::butt));
    }

    // Drag drop indicator
    if (isDragging_ && dropTarget_ >= 0 && dropTarget_ < (int)activeOrder_.size()) {
        const int dx = blockLeftX(dropTarget_) - kArrowW / 2;
        g.setColour(juce::Colour(0xFFE94560));
        g.fillRoundedRectangle((float)(dx - 2), 4.f, 4.f, (float)(bh - 8), 2.f);
    }
}

void HorizontalChainComponent::resized() {
    const int bw = computeBlockW();
    const int bh = getHeight() - 4;

    for (int i = 0; i < (int)widgets_.size(); ++i)
        widgets_[i]->setBounds(blockLeftX(i), 2, bw, bh);

    // Plus button after last block
    const int lastRight = widgets_.empty() ? kPad : blockLeftX((int)widgets_.size() - 1) + bw;
    addButton_.setBounds(lastRight + 6, bh / 2 - 13, kAddW, 26);
}
