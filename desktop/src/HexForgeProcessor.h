// ─────────────────────────────────────────────────────────────────────────────
// Hex Forge — desktop (JUCE) processor, M1.
//
// Wraps the SAME engine source the LV2 plugin compiles
// (lv2/hexforge/engine/hf_engine_all.h) behind a juce::AudioProcessor:
//   * APVTS built from the generated table (desktop/generated/hexforge_params.h)
//     — ids are the LV2 symbols; PARAM rows automatable, SETTING rows not.
//   * hostVals[] plays the role of the LV2 host port buffers: APVTS values are
//     copied in each block and the engine's own live-edit detection (lastPort)
//     keeps preset-recall vs knob-move semantics identical to the device.
//   * DesktopWorker reproduces the LV2 worker contract: schedule() from the
//     audio thread → SPSC ring → worker thread runs hfWork() → replies ring
//     back → drained at the top of processBlock into hfWorkResponse(). The
//     engine's mute-ramp zero-point apply is untouched.
//   * State = live eff[] + global settings + the 7 file paths + the preset
//     store blob (same hfSerialize/hfDeserialize/migratePorts as the device).
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <JuceHeader.h>
#include "hf_engine_all.h"
#include "hexforge_params.h"
#include "HfResampler.h"

// ── Worker thread: LV2 worker semantics on a juce::Thread ─────────────────────
class DesktopWorker final : public HfWorkerIface, private juce::Thread {
public:
    DesktopWorker() : juce::Thread("HexForge Worker") {}
    ~DesktopWorker() override { stop(); }

    void start(HexForge* engine) { eng = engine; startThread(juce::Thread::Priority::normal); }
    void stop() { stopThread(4000); eng = nullptr; }

    // Audio thread (and the engine's work_response bodies): enqueue a request.
    bool schedule(const void* msg, uint32_t size) override {
        if (size != sizeof(WorkMsg)) return false;
        int s1, n1, s2, n2;
        reqFifo.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 < 1) return false;
        std::memcpy(&reqBuf[(size_t) s1], msg, sizeof(WorkMsg));
        reqFifo.finishedWrite(1);
        notify();
        return true;
    }

    // Audio thread: deliver any finished replies into the engine.
    void drainResponses(HexForge* p) {
        for (;;) {
            int s1, n1, s2, n2;
            respFifo.prepareToRead(1, s1, n1, s2, n2);
            if (n1 < 1) return;
            hfWorkResponse(p, &respBuf[(size_t) s1]);
            respFifo.finishedRead(1);
        }
    }

private:
    void run() override {
        while (!threadShouldExit()) {
            bool worked = false;
            for (;;) {
                int s1, n1, s2, n2;
                reqFifo.prepareToRead(1, s1, n1, s2, n2);
                if (n1 < 1) break;
                WorkMsg m;
                std::memcpy(&m, &reqBuf[(size_t) s1], sizeof(WorkMsg));
                reqFifo.finishedRead(1);
                if (eng != nullptr)
                    hfWork(eng, &m, &DesktopWorker::respondShim, this);
                worked = true;
            }
            if (!worked) wait(200);
        }
    }
    static void respondShim(void* handle, uint32_t size, const void* data) {
        auto* self = static_cast<DesktopWorker*>(handle);
        if (size != sizeof(WorkMsg)) return;
        int s1, n1, s2, n2;
        self->respFifo.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 < 1) return;   // ring full: drop (matches LV2 ERR_NO_SPACE behavior)
        std::memcpy(&self->respBuf[(size_t) s1], data, sizeof(WorkMsg));
        self->respFifo.finishedWrite(1);
    }

    static constexpr int kRing = 64;
    HexForge* eng = nullptr;
    juce::AbstractFifo reqFifo { kRing }, respFifo { kRing };
    std::array<WorkMsg, kRing> reqBuf {}, respBuf {};
};

// ── Host notifications → the webview bridge (M2) ─────────────────────────────
// The engine pushes notification strings on the AUDIO thread; the editor's
// timer drains them on the message thread and forwards them to the page as
// mod-ui-style parameter 'change' events (keyed by URI fragment). Entries
// coalesce by key — the icon script replays full snapshots, so latest wins.
class DesktopHost final : public HfHostIface {
public:
    HexForge* eng = nullptr;    // set by the processor after hfEngineInit

    static const char* suffixFor(int prop) {
        switch (prop) {
            case HFP_PS_NAME:  return "#ps_name";
            case HFP_METERS:   return "#meters";
            case HFP_TUNER:    return "#tuner";
            case HFP_CAL:      return "#cal";
            case HFP_IR_FILE:  return "#irfile";
            case HFP_AMP_NAM:  return "#ampnam";
            case HFP_DR_NAM:   return "#drnam";
            case HFP_CAB_NAM:  return "#cabnam";
            case HFP_AMP2_NAM: return "#amp2nam";
            case HFP_IR2_FILE: return "#ir2file";
            case HFP_DR2_NAM:  return "#dr2nam";
        }
        return "";
    }
    void push(const char* suffix, const char* s) {
        const juce::SpinLock::ScopedLockType l(lock);
        for (auto& e : queue)
            if (e.first == suffix) { e.second = s; return; }
        queue.emplace_back(suffix, s);
    }
    void stringSet(int prop, const char* s) override { push(suffixFor(prop), s); }
    void fileSet(int prop, const char* path) override { push(suffixFor(prop), path); }
    void emitIndex() override {
        if (!eng) return;
        char buf[6144];
        hfIndexString(eng, buf, (int) sizeof(buf));
        push("#ps_index", buf);
    }
    void emitApply() override {
        if (!eng) return;
        char buf[6144];
        hfApplyString(eng, buf, (int) sizeof(buf));
        push("#ps_apply", buf);
    }
    void statusDump() override {}
    // Message thread: take everything queued.
    std::vector<std::pair<juce::String, juce::String>> drain() {
        const juce::SpinLock::ScopedLockType l(lock);
        auto out = std::move(queue);
        queue.clear();
        return out;
    }
private:
    juce::SpinLock lock;
    std::vector<std::pair<juce::String, juce::String>> queue;
};

// ── The processor ─────────────────────────────────────────────────────────────
class HexForgeProcessor final : public juce::AudioProcessor {
public:
    HexForgeProcessor();
    ~HexForgeProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Hex Forge"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return kBanks * kSlots; }
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // UI entry points (message thread): queued and applied on the audio thread,
    // mirroring the LV2 atom-loop path.
    void requestFileLoad(int hfProp, const juce::String& path);
    void requestPreset(int flatIndex);   // 0..127
    // Webview bridge entry points (message thread):
    void requestPortSet(const juce::String& symbol, float value);
    void requestParamSet(const juce::String& uri, const juce::String& value);   // patch:Set path semantics
    void requestPresetRename(const juce::String& name);                          // #ps_name patch
    static int propForUri(const juce::String& uri);                              // -1 if unknown

    bool engineReady() const { return eng != nullptr; }
    double engineRate() const { return eng ? eng->rate : 0.0; }
    HexForge* engine() const { return eng; }               // message-thread reads only
    DesktopHost& hostQueue() { return hostBridge; }
    float portValue(int port) const { return hostVals[(size_t) port]; }

    std::atomic<bool> uiAttached { false };   // editor webview live → engine pushes notifies
    std::atomic<bool> uiResync { false };     // one-shot: replay files/name/index/apply (patch:Get)

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void rebuildEngine(double rate);
    void destroyEngine();
    void applyPendingState();
    void syncApvtsFromEff();

    HexForge* eng = nullptr;
    DesktopWorker worker;
    DesktopHost hostBridge;

    // Host-side port values (the LV2 "port buffers"): engine hostPorts[] point here.
    std::array<float, HF_N_PORTS> hostVals {};

    struct ParamRow { int port; std::atomic<float>* value; };
    std::vector<ParamRow> rows;                 // PARAM + SETTING rows
    std::map<juce::String, int> portBySym;      // every control-port symbol → port index
    juce::AudioProcessorParameter* bypassParam = nullptr;

    juce::SpinLock renameLock;                  // #ps_name patch from the UI
    juce::String pendingRename;
    std::atomic<bool> haveRename { false };

    juce::AudioBuffer<float> inCopy;            // engine input must not alias output

    // ── Sample-rate wrapper (M3): the engine ALWAYS runs at 48 kHz (preset
    // levels + NAM captures live there); other host rates get a resampler
    // pair around it, with the filter delay + FIFO prime reported as latency.
    static constexpr int kSrcPrime = 8;         // output-FIFO priming (jitter head-room)
    bool srcActive = false;
    HfPolyResampler srcIn, srcOut;              // host→48k, 48k→host
    juce::AudioBuffer<float> src48In, src48Out; // 48k-domain engine I/O
    std::vector<float> srcFifoL, srcFifoR;      // host-rate output FIFO
    int srcFifoLen = 0;

    // UI → audio-thread command hand-off (SPSC-ish: single writer = message thread)
    struct PathCmd { int prop; char path[kPathMax]; };
    juce::SpinLock pathLock;
    std::vector<PathCmd> pendingPaths;          // guarded by pathLock, drained per block

    juce::MemoryBlock pendingState;             // state that arrived before the engine
    bool havePendingState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HexForgeProcessor)
};
