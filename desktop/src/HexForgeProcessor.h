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

// ── Host notifications: cached for the (future, M2) webview bridge ────────────
class DesktopHost final : public HfHostIface {
public:
    void stringSet(int prop, const char* s) override {
        const juce::SpinLock::ScopedLockType l(lock);
        latest[prop] = s;
    }
    void fileSet(int, const char*) override {}
    void emitIndex() override {}
    void emitApply() override {}
    void statusDump() override {}
    juce::String get(int prop) const {
        const juce::SpinLock::ScopedLockType l(lock);
        auto it = latest.find(prop);
        return it != latest.end() ? it->second : juce::String();
    }
private:
    mutable juce::SpinLock lock;
    std::map<int, juce::String> latest;
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

    bool engineReady() const { return eng != nullptr; }
    double engineRate() const { return eng ? eng->rate : 0.0; }

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
    juce::AudioProcessorParameter* bypassParam = nullptr;

    juce::AudioBuffer<float> inCopy;            // engine input must not alias output

    // UI → audio-thread command hand-off (SPSC-ish: single writer = message thread)
    struct PathCmd { int prop; char path[kPathMax]; };
    juce::SpinLock pathLock;
    std::vector<PathCmd> pendingPaths;          // guarded by pathLock, drained per block

    juce::MemoryBlock pendingState;             // state that arrived before the engine
    bool havePendingState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HexForgeProcessor)
};
