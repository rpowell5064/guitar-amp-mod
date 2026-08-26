#include "HexForgeProcessor.h"
#include "BinaryData.h"

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
        portBySym[d.id] = d.port;
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
    hostBridge.eng = nullptr;
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
    hostBridge.eng = p;
    worker.start(eng);
    applyPendingState();
}

void HexForgeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    // The engine ALWAYS runs at 48 kHz (factory preset levels were measured
    // there and NAM captures don't resample); other host rates are wrapped.
    if (!eng)
        rebuildEngine(48000.0);
    const int blk = juce::jmax(16, samplesPerBlock);
    inCopy.setSize(2, blk, false, false, true);
    srcActive = std::abs(sampleRate - 48000.0) > 1.0;
    if (srcActive) {
        srcIn.prepare(sampleRate, 48000.0, blk);
        const int cap48 = (int) std::ceil(blk * 48000.0 / sampleRate) + 16;
        srcOut.prepare(48000.0, sampleRate, cap48);
        src48In.setSize(2, cap48, false, false, true);
        src48Out.setSize(2, cap48, false, false, true);
        srcFifoL.assign((size_t) (blk * 2 + 64), 0.0f);
        srcFifoR.assign((size_t) (blk * 2 + 64), 0.0f);
        srcFifoLen = kSrcPrime;   // zeros — absorbs per-block ±1 count jitter
        setLatencySamples((int) std::lround(srcIn.groupDelayInputSamples()
                                            + srcOut.groupDelayInputSamples() * sampleRate / 48000.0)
                          + kSrcPrime);
    } else {
        setLatencySamples(0);   // OLA convolver is zero-latency; no other lookahead
    }
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
                        // "@nocab" bypasses Cab 2 (LV2 atom-loop parity, -2 = user
                        // override active); anything else re-loads next run().
                        eng->lastCab2Model = (std::strcmp(c.path, "@nocab") == 0) ? -2 : -1;
                        break;
                }
            }
            pendingPaths.clear();
        }
    }

    // 2b) UI preset rename (#ps_name patch) — LV2 atom-loop parity.
    if (haveRename.exchange(false)) {
        juce::String nm;
        {
            const juce::SpinLock::ScopedLockType l(renameLock);
            nm = pendingRename;
        }
        Preset& pr = eng->presets[eng->curBank][eng->curSlot];
        std::snprintf(pr.name, sizeof(pr.name), "%s", nm.toRawUTF8());
        for (char* ch = pr.name; *ch; ++ch) if (*ch == '|') *ch = ' ';
        pr.used = true;
        if (eng->uiNotify) eng->host->emitIndex();
        hfWriteBackup(eng);
    }

    // 2c) Editor (re)attached: replay the patch:Get response — current files,
    // preset name, index list, knob snapshot — through the notify queue.
    if (uiResync.exchange(false) && uiAttached.load()) {
        eng->host->fileSet(HFP_IR_FILE, eng->irPath);
        eng->host->fileSet(HFP_AMP_NAM, eng->ampNamPath);
        eng->host->fileSet(HFP_DR_NAM, eng->drNamPath);
        eng->host->fileSet(HFP_CAB_NAM, eng->cabNamPath);
        eng->host->fileSet(HFP_AMP2_NAM, eng->amp2NamPath);
        eng->host->fileSet(HFP_IR2_FILE, eng->ir2Path);
        eng->host->fileSet(HFP_DR2_NAM, eng->dr2NamPath);
        eng->host->stringSet(HFP_PS_NAME, eng->presets[eng->curBank][eng->curSlot].name);
        eng->host->emitIndex();
        eng->host->emitApply();
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
    eng->uiNotify = uiAttached.load(std::memory_order_relaxed);

    if (!srcActive) {
        setPort(HF_IN_L, inCopy.getWritePointer(0));
        setPort(HF_IN_R, inCopy.getWritePointer(1));
        setPort(HF_OUT_L, outL);
        setPort(HF_OUT_R, outR);
        hfPrime(eng);
        hfEngineRun(eng, (uint32_t) n, nullptr, 0);
    } else {
        // host → 48k → engine → 48k → host (see HfResampler.h)
        if (src48In.getNumSamples() < (int) std::ceil(n * 48000.0 / getSampleRate()) + 16) {
            const int cap = (int) std::ceil(n * 48000.0 / getSampleRate()) + 16;
            src48In.setSize(2, cap, false, false, true);
            src48Out.setSize(2, cap, false, false, true);
        }
        const int m = srcIn.process(inCopy.getReadPointer(0), inCopy.getReadPointer(1), n,
                                    src48In.getWritePointer(0), src48In.getWritePointer(1));
        setPort(HF_IN_L, src48In.getWritePointer(0));
        setPort(HF_IN_R, src48In.getWritePointer(1));
        setPort(HF_OUT_L, src48Out.getWritePointer(0));
        setPort(HF_OUT_R, src48Out.getWritePointer(1));
        hfPrime(eng);
        hfEngineRun(eng, (uint32_t) m, nullptr, 0);
        if ((int) srcFifoL.size() < srcFifoLen + n + 8) {
            srcFifoL.resize((size_t) (srcFifoLen + n + 8));
            srcFifoR.resize((size_t) (srcFifoLen + n + 8));
        }
        const int o = srcOut.process(src48Out.getReadPointer(0), src48Out.getReadPointer(1), m,
                                     srcFifoL.data() + srcFifoLen, srcFifoR.data() + srcFifoLen);
        srcFifoLen += o;
        const int take = juce::jmin(srcFifoLen, n);
        std::memcpy(outL, srcFifoL.data(), (size_t) take * sizeof(float));
        std::memcpy(outR, srcFifoR.data(), (size_t) take * sizeof(float));
        if (take < n) {   // startup only, covered by the reported latency
            std::memset(outL + take, 0, (size_t) (n - take) * sizeof(float));
            std::memset(outR + take, 0, (size_t) (n - take) * sizeof(float));
        }
        std::memmove(srcFifoL.data(), srcFifoL.data() + take, (size_t) (srcFifoLen - take) * sizeof(float));
        std::memmove(srcFifoR.data(), srcFifoR.data() + take, (size_t) (srcFifoLen - take) * sizeof(float));
        srcFifoLen -= take;
    }

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
void HexForgeProcessor::requestPortSet(const juce::String& symbol, float value) {
    if (auto* rp = apvts.getParameter(symbol)) {   // PARAM/SETTING → through the host
        rp->setValueNotifyingHost(rp->convertTo0to1(value));
        return;
    }
    auto it = portBySym.find(symbol);              // INTERNAL/COMMAND → straight in
    if (it != portBySym.end()) hostVals[(size_t) it->second] = value;
}
int HexForgeProcessor::propForUri(const juce::String& uri) {
    if (uri.endsWith("#irfile"))  return HFP_IR_FILE;
    if (uri.endsWith("#ampnam"))  return HFP_AMP_NAM;
    if (uri.endsWith("#drnam"))   return HFP_DR_NAM;
    if (uri.endsWith("#cabnam"))  return HFP_CAB_NAM;
    if (uri.endsWith("#amp2nam")) return HFP_AMP2_NAM;
    if (uri.endsWith("#ir2file")) return HFP_IR2_FILE;
    if (uri.endsWith("#dr2nam"))  return HFP_DR2_NAM;
    return -1;
}
void HexForgeProcessor::requestParamSet(const juce::String& uri, const juce::String& value) {
    const int prop = propForUri(uri);
    if (prop < 0) {
        if (uri.endsWith("#ps_name")) requestPresetRename(value);
        return;
    }
    juce::String v = value;
    if (prop == HFP_IR_FILE && v == "@factory") v = juce::String();   // clear to the built-in
    if (prop == HFP_IR2_FILE && v == "@builtin") v = juce::String();  // defer to rb_cab
    requestFileLoad(prop, v);
}
void HexForgeProcessor::requestPresetRename(const juce::String& name) {
    {
        const juce::SpinLock::ScopedLockType l(renameLock);
        pendingRename = name;
    }
    haveRename.store(true);
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

// ── Editor: the device modgui in a webview (M2) ───────────────────────────────
// The page is the pre-rendered Mustache icon + the device's unmodified
// script-hexforge.js, driven by hexforge-desktop-bridge.js (the mod-ui shim).
// Dev serving straight from the repo (HF_DESKTOP_RESOURCES_DIR / HF_MODGUI_DIR)
// so UI edits are a browser-reload away; packaging embeds them later (M3).
namespace {
static const char* kHfUriStr = "https://rpowell5064.github.io/guitaramp-suite/hexforge";

juce::String mimeFor(const juce::String& ext) {
    if (ext == ".html") return "text/html";
    if (ext == ".css")  return "text/css";
    if (ext == ".js")   return "text/javascript";
    if (ext == ".png")  return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg")  return "image/svg+xml";
    return "application/octet-stream";
}

class WebEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit WebEditor(HexForgeProcessor& p)
        : juce::AudioProcessorEditor(p), proc(p),
          resDir(HF_DESKTOP_RESOURCES_DIR), modguiDir(HF_MODGUI_DIR) {
        auto opts =
            juce::WebBrowserComponent::Options {}
#if JUCE_WINDOWS
                .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
                .withWinWebView2Options(
                    juce::WebBrowserComponent::Options::WinWebView2 {}.withUserDataFolder(
                        juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("HexForgeWebView2")))
#endif  // macOS/Linux: JUCE's default backend (WKWebView / webkit2gtk)
                .withNativeIntegrationEnabled()
                .withResourceProvider([this](const auto& url) { return provide(url); })
                .withEventListener("hfMsg", [this](const juce::var& v) { onMsg(v); });
        web = std::make_unique<juce::WebBrowserComponent>(opts);
        addAndMakeVisible(*web);
        web->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
        setResizable(true, true);
        setSize(1500, 900);   // pedal is 1440px wide + page padding
        startTimerHz(30);
        lastOut.assign((size_t) kHfNumDesktopParams, -1.0e9f);
    }
    ~WebEditor() override { proc.uiAttached.store(false); }
    void resized() override { web->setBounds(getLocalBounds()); }

private:
    std::optional<juce::WebBrowserComponent::Resource> provide(const juce::String& url) {
        juce::String path = url.startsWith("/") ? url.substring(1) : url;
        if (path.contains("?")) path = path.upToFirstOccurrenceOf("?", false, false);
        if (path.isEmpty() || path == "index.html") path = "icon-hexforge-desktop.html";
        // Dev mode: the repo checkout is present — serve the live files so UI
        // edits are a reload away.
        if (modguiDir.isDirectory()) {
            juce::File f;
            if (path.startsWith("resources/"))   f = modguiDir.getChildFile(path.substring(10));
            else if (path.startsWith("modgui/")) f = modguiDir.getChildFile(path.substring(7));
            else                                 f = resDir.getChildFile(path);
            juce::MemoryBlock mb;
            if (f.existsAsFile() && f.loadFileAsData(mb)) {
                const auto* b = static_cast<const std::byte*>(mb.getData());
                return juce::WebBrowserComponent::Resource {
                    std::vector<std::byte>(b, b + mb.getSize()),
                    mimeFor(f.getFileExtension()) };
            }
        }
        // Embedded assets (BinaryData flattens paths — basenames are unique).
        const juce::String base = path.fromLastOccurrenceOf("/", false, false);
        static const auto embedded = [] {
            std::map<juce::String, std::pair<const char*, int>> m;
            for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
                const char* name = BinaryData::namedResourceList[i];
                int size = 0;
                const char* data = BinaryData::getNamedResource(name, size);
                m[BinaryData::getNamedResourceOriginalFilename(name)] = { data, size };
            }
            return m;
        }();
        const auto it = embedded.find(base);
        if (it == embedded.end()) return std::nullopt;
        const auto* b = reinterpret_cast<const std::byte*>(it->second.first);
        return juce::WebBrowserComponent::Resource {
            std::vector<std::byte>(b, b + it->second.second),
            mimeFor("." + base.fromLastOccurrenceOf(".", false, false)) };
    }

    void pushBatch(const juce::StringArray& calls) {
        if (calls.isEmpty()) return;
        juce::String js;
        for (const auto& c : calls) js << "window.hfFromNative(" << c << ");";
        web->evaluateJavascript(js);
    }
    static juce::String jsonObj(std::initializer_list<std::pair<const char*, juce::var>> fields) {
        auto* o = new juce::DynamicObject();
        for (const auto& f : fields) o->setProperty(f.first, f.second);
        return juce::JSON::toString(juce::var(o), true);
    }

    void sendInit() {
        juce::Array<juce::var> ports;
        auto* eng = proc.engine();
        for (int i = 0; i < kHfNumDesktopParams; ++i) {
            const HfDesktopParam& d = kHfDesktopParams[i];
            if (d.cls == HFD_OUTPUT) continue;
            const float v = (eng && isParamPort(d.port)) ? eng->eff[d.port]
                                                         : proc.portValue(d.port);
            juce::Array<juce::var> pair; pair.add(d.id); pair.add(v);
            ports.add(juce::var(pair));
        }
        juce::Array<juce::var> params;
        const char* frag[7] = { "#irfile", "#ir2file", "#ampnam", "#drnam",
                                "#cabnam", "#amp2nam", "#dr2nam" };
        const char* val[7] = {
            eng ? eng->irPath : "",   eng ? eng->ir2Path : "",  eng ? eng->ampNamPath : "",
            eng ? eng->drNamPath : "", eng ? eng->cabNamPath : "",
            eng ? eng->amp2NamPath : "", eng ? eng->dr2NamPath : "",
        };
        for (int k = 0; k < 7; ++k) {
            juce::Array<juce::var> pr;
            pr.add(juce::String(kHfUriStr) + frag[k]);
            pr.add(juce::String(juce::CharPointer_UTF8(val[k])));
            params.add(juce::var(pr));
        }
        pushBatch({ jsonObj({ { "t", "init" }, { "ports", juce::var(ports) },
                              { "parameters", juce::var(params) } }) });
    }

    void onMsg(const juce::var& v) {
        const juce::String t = v.getProperty("t", {}).toString();
        if (t == "ready") {
            sendInit();
        } else if (t == "started") {
            proc.uiAttached.store(true);
            proc.uiResync.store(true);
            // Tell the page how much room the display offers; beyond it the
            // bridge zooms the pedal down instead of growing a scrollbar.
            if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
                const auto area = disp->userArea;
                pushBatch({ jsonObj({ { "t", "avail" },
                                      { "w", area.getWidth() - 24 },
                                      { "h", area.getHeight() - 90 } }) });
            }
        } else if (t == "size") {
            // Auto-fit: the pedal grows when the tuner / cal wizard opens —
            // follow it (clamped to the display) so no scrollbar appears.
            const int w = (int) (double) v.getProperty("w", 1500);
            const int h = (int) (double) v.getProperty("h", 900);
            if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
                const auto area = disp->userArea;
                setSize(juce::jlimit(720, area.getWidth() - 16, w),
                        juce::jlimit(420, area.getHeight() - 80, h));
            } else {
                setSize(w, h);
            }
        } else if (t == "set") {
            proc.requestPortSet(v.getProperty("sym", {}).toString(),
                                (float) (double) v.getProperty("val", 0.0));
        } else if (t == "patch") {
            proc.requestParamSet(v.getProperty("uri", {}).toString(),
                                 v.getProperty("val", {}).toString());
        } else if (t == "paramset") {
            proc.requestParamSet(v.getProperty("uri", {}).toString(),
                                 v.getProperty("val", {}).toString());
        } else if (t == "browse") {
            const juce::String uri = v.getProperty("uri", {}).toString();
            const bool nam = uri.contains("nam");
            chooser = std::make_unique<juce::FileChooser>(
                nam ? "Select a NAM capture" : "Select an impulse response",
                juce::File(), nam ? "*.nam" : "*.wav");
            chooser->launchAsync(juce::FileBrowserComponent::openMode
                                     | juce::FileBrowserComponent::canSelectFiles,
                                 [this, uri](const juce::FileChooser& fc) {
                                     const auto f = fc.getResult();
                                     if (!f.existsAsFile()) return;
                                     proc.requestParamSet(uri, f.getFullPathName());
                                     pushBatch({ jsonObj({ { "t", "param" }, { "uri", uri },
                                                           { "val", f.getFullPathName() } }) });
                                 });
        }
    }

    void timerCallback() override {
        juce::StringArray calls;
        // Engine notify strings (meters/tuner/cal/ps_*/file paths) → param events.
        for (auto& e : proc.hostQueue().drain())
            calls.add(jsonObj({ { "t", "param" }, { "uri", juce::String(kHfUriStr) + e.first },
                                { "val", e.second } }));
        // Monitored output ports (cpu badges, clip LED, bank/slot mirrors).
        for (int i = 0; i < kHfNumDesktopParams; ++i) {
            const HfDesktopParam& d = kHfDesktopParams[i];
            if (d.cls != HFD_OUTPUT) continue;
            const float v = proc.portValue(d.port);
            if (std::abs(v - lastOut[(size_t) i]) < 0.004f) continue;
            lastOut[(size_t) i] = v;
            calls.add(jsonObj({ { "t", "port" }, { "sym", d.id }, { "val", v } }));
        }
        pushBatch(calls);
    }

    HexForgeProcessor& proc;
    juce::File resDir, modguiDir;
    std::unique_ptr<juce::WebBrowserComponent> web;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<float> lastOut;
};
} // namespace

juce::AudioProcessorEditor* HexForgeProcessor::createEditor() {
    return new WebEditor(*this);
}

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new HexForgeProcessor();
}
