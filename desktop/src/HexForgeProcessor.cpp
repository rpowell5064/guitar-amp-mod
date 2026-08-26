#include "HexForgeProcessor.h"

// ── Parameter layout from the generated table ─────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout HexForgeProcessor::makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kHfNumDesktopParams; ++i) {
        const HfDesktopParam& d = kHfDesktopParams[i];
        if (d.cls != HFD_PARAM && d.cls != HFD_SETTING) continue;
        const bool automatable = (d.cls == HFD_PARAM);
        const juce::ParameterID pid { d.id, 1 };
        if (d.kind[0] == 't' && d.kind[1] == '\0') {
            layout.add(std::make_unique<juce::AudioParameterBool>(
                pid, d.name, d.df > 0.5f,
                juce::AudioParameterBoolAttributes().withAutomatable(automatable)));
        } else {
            const bool stepped = (d.kind[0] == 'e' || d.kind[0] == 'i') && d.kind[1] == '\0';
            juce::NormalisableRange<float> range(d.mn, d.mx, stepped ? 1.0f : 0.0f);
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                pid, d.name, range, d.df,
                juce::AudioParameterFloatAttributes()
                    .withAutomatable(automatable)
                    .withLabel(d.kind[1] != '\0' ? d.kind : "")));   // "db"/"ms"/"hz"
        }
    }
    return layout;
}

HexForgeProcessor::HexForgeProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "HexForge", makeLayout()) {
    // Seed hostVals with defaults and cache the APVTS atomics per port.
    for (int i = 0; i < kHfNumDesktopParams; ++i) {
        const HfDesktopParam& d = kHfDesktopParams[i];
        hostVals[(size_t) d.port] = d.df;
        if (d.cls == HFD_PARAM || d.cls == HFD_SETTING)
            rows.push_back({ d.port, apvts.getRawParameterValue(d.id) });
    }
    bypassParam = apvts.getParameter("bypass");

    // Out-of-band preset-store backup lives in the per-user app-data dir
    // (the LV2 build keeps $HOME/.config/hexchain). Must be set BEFORE
    // hfEngineInit — it reads the backup at construction.
    const auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("HexChain");
    hfplat::setConfigDir(dir.getFullPathName().toStdString());
}

HexForgeProcessor::~HexForgeProcessor() {
    worker.stop();
    destroyEngine();
}

void HexForgeProcessor::destroyEngine() {
    if (!eng) return;
    worker.stop();
    // Mirror hf_cleanup, plus the members it historically leaves to process
    // teardown (amp2 + pendings) — a desktop instance must free everything.
    delete eng->amp;
    delete eng->amp2;
    delete eng->pendAmp;
    delete eng->pendAmp2;
    for (auto* nm : eng->pendNam) delete nm;
    delete eng->ampNam; delete eng->drNam; delete eng->cabNam;
    delete eng->amp2Nam; delete eng->dr2Nam;
    delete eng;
    eng = nullptr;
}

void HexForgeProcessor::rebuildEngine(double rate) {
    destroyEngine();
    auto* p = new (std::nothrow) HexForge;
    if (!p) return;
    p->worker = &worker;
    p->host = &hostBridge;
    if (!hfEngineInit(p, rate)) {
        delete p;
        return;
    }
    // Wire every control port at the LV2 "connect_port" level: engine hostPorts
    // point into hostVals; hfPrime() then redirects param ports onto eff[].
    for (int i = HF_BYPASS; i < HF_N_PORTS; ++i) {
        if (i == HF_MIDI_IN) continue;
        p->hostPorts[i] = &hostVals[(size_t) i];
    }
    eng = p;
    worker.start(eng);
    applyPendingState();
}

void HexForgeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    if (!eng || eng->rate != sampleRate)
        rebuildEngine(sampleRate);
    inCopy.setSize(2, juce::jmax(16, samplesPerBlock), false, false, true);
    setLatencySamples(0);   // OLA convolver is zero-latency; no other lookahead
}

bool HexForgeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    return true;
}

void HexForgeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    DenormalGuard denormalGuard;
    const int n = buffer.getNumSamples();
    if (eng == nullptr || n == 0) return;

    // 1) Finished worker replies land exactly like LV2 work_response.
    worker.drainResponses(eng);

    // 2) UI file/preset commands, applied on the audio thread like the atom loop.
    {
        const juce::SpinLock::ScopedTryLockType tl(pathLock);
        if (tl.isLocked() && !pendingPaths.empty()) {
            for (const PathCmd& c : pendingPaths) {
                switch (c.prop) {
                    case HFP_IR_FILE:  schedPath(eng, eng->irPath,     c.path, W_CAB_IR,   0); break;
                    case HFP_AMP_NAM:  schedPath(eng, eng->ampNamPath, c.path, W_NAM_LOAD, 0); break;
                    case HFP_DR_NAM:   schedPath(eng, eng->drNamPath,  c.path, W_NAM_LOAD, 1); break;
                    case HFP_CAB_NAM:  schedPath(eng, eng->cabNamPath, c.path, W_NAM_LOAD, 2); break;
                    case HFP_AMP2_NAM: schedPath(eng, eng->amp2NamPath, c.path, W_NAM_LOAD, 3); break;
                    case HFP_DR2_NAM:  schedPath(eng, eng->dr2NamPath, c.path, W_NAM_LOAD, 4); break;
                    case HFP_IR2_FILE:
                        std::strncpy(eng->ir2Path, c.path, kPathMax - 1);
                        eng->ir2Path[kPathMax - 1] = '\0';
                        eng->lastCab2Model = -1;   // run() re-schedules from the new path
                        break;
                }
            }
            pendingPaths.clear();
        }
    }

    // 3) APVTS -> host port values (the engine's live-edit detect does the rest).
    for (const ParamRow& r : rows)
        hostVals[(size_t) r.port] = r.value->load(std::memory_order_relaxed);

    // 4) Engine input must not alias the output buffer (the keyed gates re-read
    //    the raw input mid-chain), so feed it a copy.
    if (inCopy.getNumSamples() < n) inCopy.setSize(2, n, false, false, true);
    const int nIn = getTotalNumInputChannels() >= 2 ? 2 : 1;   // bus, not buffer
    inCopy.copyFrom(0, 0, buffer, 0, 0, n);
    inCopy.copyFrom(1, 0, buffer, (nIn >= 2 && buffer.getNumChannels() >= 2) ? 1 : 0, 0, n);

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() >= 2 ? buffer.getWritePointer(1) : nullptr;
    static thread_local std::vector<float> monoScratch;
    if (outR == nullptr) {   // mono-out host: engine still writes stereo
        monoScratch.resize((size_t) n);
        outR = monoScratch.data();
    }
    auto setPort = [this](int i, float* ptr) {
        eng->hostPorts[i] = ptr;
        if (!eng->primed || !isParamPort(i)) eng->ports[i] = ptr;
    };
    setPort(HF_IN_L, inCopy.getWritePointer(0));
    setPort(HF_IN_R, inCopy.getWritePointer(1));
    setPort(HF_OUT_L, outL);
    setPort(HF_OUT_R, outR);

    hfPrime(eng);
    eng->uiNotify = false;   // no notify channel yet (webview lands in M2)
    hfEngineRun(eng, (uint32_t) n, nullptr, 0);

    if (buffer.getNumChannels() == 1 && outR == monoScratch.data())
        for (int i = 0; i < n; ++i) outL[i] = 0.5f * (outL[i] + outR[i]);
}

// ── Programs = the 128-slot preset store ──────────────────────────────────────
int HexForgeProcessor::getCurrentProgram() {
    return eng ? eng->curBank * kSlots + eng->curSlot : 0;
}
void HexForgeProcessor::setCurrentProgram(int index) {
    requestPreset(index);
}
const juce::String HexForgeProcessor::getProgramName(int index) {
    const int b = index / kSlots, s = index % kSlots;
    juce::String tag = juce::String(b + 1) + juce::String::charToString((juce::juce_wchar)('A' + s));
    if (eng && b < kBanks && s < kSlots) {
        const Preset& pr = eng->presets[b][s];
        if (pr.used && pr.name[0]) return tag + " " + juce::String(juce::CharPointer_UTF8(pr.name));
    }
    return tag;
}
void HexForgeProcessor::requestPreset(int flatIndex) {
    if (flatIndex < 0 || flatIndex >= kBanks * kSlots) return;
    hostVals[HF_PS_GOTO] = (float) flatIndex;   // engine edge-detects the change
}
void HexForgeProcessor::requestFileLoad(int hfProp, const juce::String& path) {
    PathCmd c {};
    c.prop = hfProp;
    std::snprintf(c.path, sizeof(c.path), "%s", path.toRawUTF8());
    const juce::SpinLock::ScopedLockType l(pathLock);
    pendingPaths.push_back(c);
}

// ── State ─────────────────────────────────────────────────────────────────────
// Envelope: magic, version, port count, live eff[] snapshot, SETTING values,
// curBank/curSlot, the 7 live file paths, then the preset-store blob (same
// hfSerialize/hfDeserialize/migratePorts as the device + backup file).
static constexpr uint32_t kDeskMagic = 0x48584631;   // "HXF1"

void HexForgeProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (!eng) {   // not prepared yet: pass through whatever we were given
        if (havePendingState) destData = pendingState;
        return;
    }
    juce::MemoryOutputStream mo(destData, false);
    mo.writeInt((int) kDeskMagic);
    mo.writeInt(1);
    mo.writeInt(HF_N_PORTS);
    for (int i = 0; i < HF_N_PORTS; ++i)
        mo.writeFloat(isParamPort(i) ? eng->eff[i] : hostVals[(size_t) i]);
    mo.writeInt(eng->curBank);
    mo.writeInt(eng->curSlot);
    const char* paths[7] = { eng->irPath, eng->ir2Path, eng->ampNamPath, eng->drNamPath,
                             eng->cabNamPath, eng->amp2NamPath, eng->dr2NamPath };
    for (const char* s : paths) mo.writeString(juce::String(juce::CharPointer_UTF8(s)));
    std::vector<uint8_t> blob;
    hfSerialize(eng, blob);
    mo.writeInt((int) blob.size());
    mo.write(blob.data(), blob.size());
}

void HexForgeProcessor::setStateInformation(const void* data, int sizeInBytes) {
    pendingState.replaceAll(data, (size_t) sizeInBytes);
    havePendingState = true;
    if (eng) applyPendingState();
}

void HexForgeProcessor::applyPendingState() {
    if (!havePendingState || !eng) return;
    juce::MemoryInputStream mi(pendingState, false);
    if ((uint32_t) mi.readInt() != kDeskMagic) return;
    if (mi.readInt() != 1) return;
    const int np = mi.readInt();
    std::vector<float> vals((size_t) juce::jmax(np, (int) HF_N_PORTS), 0.0f);
    for (int i = 0; i < np; ++i) vals[(size_t) i] = mi.readFloat();
    const int cb = mi.readInt(), cs = mi.readInt();
    juce::String paths[7];
    for (auto& s : paths) s = mi.readString();
    const int blobSize = mi.readInt();
    std::vector<uint8_t> blob((size_t) juce::jmax(blobSize, 0));
    if (blobSize > 0) mi.read(blob.data(), blobSize);

    // Preset store first (its own version + migration), then the live sound.
    if (!blob.empty()) hfDeserialize(eng, blob.data(), blob.size());
    eng->curBank = juce::jlimit(0, kBanks - 1, cb);
    eng->curSlot = juce::jlimit(0, kSlots - 1, cs);
    eng->pendingRecall = false;   // the restored live eff[] IS the sound
    if (np == HF_N_PORTS) {
        for (int i = 0; i < HF_N_PORTS; ++i) {
            if (isParamPort(i)) {
                eng->eff[i] = vals[(size_t) i];
            } else if (i >= HF_BYPASS && i != HF_MIDI_IN) {
                hostVals[(size_t) i] = vals[(size_t) i];
            }
        }
        syncApvtsFromEff();
    }
    // Re-load the 7 current files through the normal audio-thread path.
    static const int kProp[7] = { HFP_IR_FILE, HFP_IR2_FILE, HFP_AMP_NAM, HFP_DR_NAM,
                                  HFP_CAB_NAM, HFP_AMP2_NAM, HFP_DR2_NAM };
    for (int k = 0; k < 7; ++k)
        if (paths[k].isNotEmpty()) requestFileLoad(kProp[k], paths[k]);
    havePendingState = false;
}

void HexForgeProcessor::syncApvtsFromEff() {
    // Push engine values into APVTS (UI + host view) and align lastPort/hostVals
    // so the live-edit detector doesn't read the sync as a knob move.
    for (int i = 0; i < kHfNumDesktopParams; ++i) {
        const HfDesktopParam& d = kHfDesktopParams[i];
        if (d.cls != HFD_PARAM && d.cls != HFD_SETTING) continue;
        const float v = isParamPort(d.port) ? eng->eff[d.port] : hostVals[(size_t) d.port];
        if (auto* rp = apvts.getParameter(d.id))
            rp->setValueNotifyingHost(rp->convertTo0to1(v));
        hostVals[(size_t) d.port] = v;
        if (isParamPort(d.port)) eng->lastPort[d.port] = v;
    }
}

// ── Editor: generic parameter panel + a minimal M1 tool strip ─────────────────
namespace {
class M1Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit M1Editor(HexForgeProcessor& p)
        : juce::AudioProcessorEditor(p), proc(p), generic(p) {
        addAndMakeVisible(presetBox);
        addAndMakeVisible(loadNam);
        addAndMakeVisible(loadIr);
        addAndMakeVisible(rateNote);
        addAndMakeVisible(generic);
        refreshPresets();
        presetBox.onChange = [this] {
            const int id = presetBox.getSelectedId();
            if (id > 0) proc.requestPreset(id - 1);
        };
        loadNam.setButtonText("Load Amp NAM...");
        loadNam.onClick = [this] { pickFile(HFP_AMP_NAM, "*.nam"); };
        loadIr.setButtonText("Load Cab IR...");
        loadIr.onClick = [this] { pickFile(HFP_IR_FILE, "*.wav"); };
        rateNote.setJustificationType(juce::Justification::centredLeft);
        setResizable(true, true);
        setSize(760, 640);
        startTimerHz(2);
    }
    void resized() override {
        auto r = getLocalBounds();
        auto top = r.removeFromTop(34).reduced(4);
        presetBox.setBounds(top.removeFromLeft(240));
        loadNam.setBounds(top.removeFromLeft(140).reduced(2, 0));
        loadIr.setBounds(top.removeFromLeft(140).reduced(2, 0));
        rateNote.setBounds(top);
        generic.setBounds(r);
    }
private:
    void timerCallback() override {
        const double sr = proc.engineRate();
        rateNote.setText(sr > 0 && std::abs(sr - 48000.0) > 1.0
                             ? juce::String("Note: presets are voiced at 48 kHz (host: ")
                                   + juce::String(sr / 1000.0, 1) + " kHz)"
                             : juce::String(),
                         juce::dontSendNotification);
        const int cur = proc.getCurrentProgram();
        if (cur + 1 != presetBox.getSelectedId())
            presetBox.setSelectedId(cur + 1, juce::dontSendNotification);
    }
    void refreshPresets() {
        presetBox.clear(juce::dontSendNotification);
        for (int i = 0; i < proc.getNumPrograms(); ++i)
            presetBox.addItem(proc.getProgramName(i), i + 1);
        presetBox.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    }
    void pickFile(int prop, const juce::String& pattern) {
        chooser = std::make_unique<juce::FileChooser>("Select file", juce::File(), pattern);
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                             [this, prop](const juce::FileChooser& fc) {
                                 const auto f = fc.getResult();
                                 if (f.existsAsFile())
                                     proc.requestFileLoad(prop, f.getFullPathName());
                             });
    }
    HexForgeProcessor& proc;
    juce::ComboBox presetBox;
    juce::TextButton loadNam, loadIr;
    juce::Label rateNote;
    juce::GenericAudioProcessorEditor generic;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

juce::AudioProcessorEditor* HexForgeProcessor::createEditor() {
    return new M1Editor(*this);
}

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new HexForgeProcessor();
}
