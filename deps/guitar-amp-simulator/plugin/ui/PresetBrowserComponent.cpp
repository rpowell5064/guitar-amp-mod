#include "PresetBrowserComponent.h"
#include "PluginProcessor.h"

juce::File PresetBrowserComponent::getPresetDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("GuitarAmpSim").getChildFile("Presets");
}

PresetBrowserComponent::PresetBrowserComponent(GuitarAmpAudioProcessor& proc)
    : processor(proc)
{
    titleLabel.setText("PRESET", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7788AA));
    titleLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(titleLabel);

    presetList.setTextWhenNothingSelected("-- Default --");
    addAndMakeVisible(presetList);

    saveButton.onClick = [this] { saveCurrentPreset(); };
    loadButton.onClick = [this] { loadSelectedPreset(); };
    addAndMakeVisible(saveButton);
    addAndMakeVisible(loadButton);

    refreshPresetList();
}

void PresetBrowserComponent::resized() {
    auto area = getLocalBounds();
    titleLabel.setBounds(area.removeFromLeft(55));
    saveButton.setBounds(area.removeFromRight(54));
    area.removeFromRight(4);
    loadButton.setBounds(area.removeFromRight(54));
    area.removeFromRight(4);
    presetList.setBounds(area);
}

void PresetBrowserComponent::refreshPresetList() {
    presetList.clear();
    presetFiles.clear();

    const auto dir = getPresetDirectory();
    if (dir.isDirectory()) {
        for (const auto& f : juce::RangedDirectoryIterator(dir, false, "*.xml"))
            presetFiles.push_back(f.getFile());
    }

    for (int i = 0; i < (int)presetFiles.size(); ++i)
        presetList.addItem(presetFiles[i].getFileNameWithoutExtension(), i + 1);
}

void PresetBrowserComponent::saveCurrentPreset() {
    auto* dlg = new juce::AlertWindow("Save Preset", "Enter preset name:", juce::MessageBoxIconType::NoIcon);
    dlg->addTextEditor("name", "New Preset");
    dlg->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    dlg->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetBrowserComponent> safeThis(this);
    dlg->enterModalState(true,
        juce::ModalCallbackFunction::create([safeThis, dlg](int result) {
            if (result == 1) {
                const juce::String name = dlg->getTextEditorContents("name").trim();
                if (name.isNotEmpty() && safeThis != nullptr) {
                    const auto dir     = safeThis->getPresetDirectory();
                    const auto filesDir = dir.getChildFile(name + "_files");
                    dir.createDirectory();

                    auto state = safeThis->processor.apvts.copyState();

                    // Save chain order.
                    {
                        auto order = safeThis->processor.getLogicalOrder();
                        juce::String s;
                        for (int i = 0; i < 8; ++i) { if (i) s += ","; s += juce::String(order[i]); }
                        state.setProperty("chainOrder", s, nullptr);
                    }

                    // Copy the IR file and record a preset-relative path in the XML.
                    const auto irFile = safeThis->processor.getIRFile();
                    if (irFile.existsAsFile()) {
                        filesDir.createDirectory();
                        const auto destIR = filesDir.getChildFile(
                            "cab_ir" + irFile.getFileExtension());
                        irFile.copyFileTo(destIR);
                        state.setProperty("irFile",
                            name + "_files/" + destIR.getFileName(), nullptr);
                    }

                    // Copy the NAM model file similarly.
                    const auto namFile = safeThis->processor.getNamFile();
                    if (namFile.existsAsFile()) {
                        filesDir.createDirectory();
                        const auto destNam = filesDir.getChildFile(
                            "amp_model" + namFile.getFileExtension());
                        namFile.copyFileTo(destNam);
                        state.setProperty("namFile",
                            name + "_files/" + destNam.getFileName(), nullptr);
                    }

                    auto xml = state.createXml();
                    if (xml) xml->writeToFile(dir.getChildFile(name + ".xml"), {});
                    safeThis->refreshPresetList();
                }
            }
            delete dlg;
        }), true);
}

void PresetBrowserComponent::loadSelectedPreset() {
    const int idx = presetList.getSelectedItemIndex();
    if (idx < 0 || idx >= (int)presetFiles.size()) return;

    auto xml = juce::XmlDocument::parse(presetFiles[idx]);
    if (!xml || !xml->hasTagName(processor.apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml(*xml);

    // Extract non-APVTS properties before replacing state.
    const juce::String irRelPath      = tree.getProperty("irFile",     "").toString();
    const juce::String namRelPath     = tree.getProperty("namFile",    "").toString();
    const juce::String chainOrderStr  = tree.getProperty("chainOrder", "").toString();

    processor.apvts.replaceState(tree);

    const auto presetDir = getPresetDirectory();

    if (irRelPath.isNotEmpty()) {
        const auto irFile = presetDir.getChildFile(irRelPath);
        if (irFile.existsAsFile() && onIRRestored)
            onIRRestored(irFile);
    }

    if (namRelPath.isNotEmpty()) {
        const auto namFile = presetDir.getChildFile(namRelPath);
        if (namFile.existsAsFile() && onNamRestored)
            onNamRestored(namFile);
    }

    // Restore chain order (reset to default if absent or invalid).
    {
        bool loaded = false;
        if (chainOrderStr.isNotEmpty()) {
            auto tokens = juce::StringArray::fromTokens(chainOrderStr, ",", "");
            if (tokens.size() == 8) {
                std::vector<int> order;
                bool valid = true;
                for (const auto& t : tokens) {
                    int v = t.trim().getIntValue();
                    if (v < 0 || v > 7) { valid = false; break; }
                    order.push_back(v);
                }
                if (valid) { processor.setChainOrder(order); loaded = true; }
            }
        }
        if (!loaded) processor.setChainOrder({0, 1, 2, 3, 4, 5, 6, 7});
    }
}
