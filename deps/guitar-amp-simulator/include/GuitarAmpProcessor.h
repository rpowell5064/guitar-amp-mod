#pragma once
#include "SignalChain.h"
#include "ParameterManager.h"
#include "NoiseGateBlock.h"
#include "PitchBlock.h"
#include "OverdriveBlock.h"
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include "CabinetBlock.h"
#include "ModulationBlock.h"
#include "DelayBlock.h"
#include "PlateReverbBlock.h"
#include "OutputEQBlock.h"
#include "CompressorBlock.h"
#include <memory>
#include <string>
#include <vector>

// Top-level processor.
//
// Default active chain: Input → Gate → Comp → Drive → Amp+PowerAmp → Cabinet → EQ → Delay → Reverb → Output
// Pitch/Mod inactive by default; when re-added: Pitch inserts first, Mod inserts after EQ.
class GuitarAmpProcessor {
public:
    GuitarAmpProcessor();

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void processBlock(float** inputs, float** outputs,
                      int numSamples, int numChannels, double sampleRate);

    void selectAmpModel(AmpModel model);
    void selectTubeType(TubeType type);
    void setDelayType(DelayType type);
    void setParameter(const std::string& id, float value);
    float getParameter(const std::string& id) const;

    // IR loading (call outside the audio callback)
    void loadIR(const std::vector<float>& irL,
                const std::vector<float>* irR = nullptr);

    // NAM model loading for the amp block (call outside the audio callback).
    bool loadNeuralModel(const std::string& filePath);

    // NAM overdrive loading for the drive block (call outside the audio callback).
    bool loadOverdriveNam(const std::string& filePath);

    // Overdrive model selection.
    void selectOverdriveModel(OverdriveType type);
    void selectModulationType(ModulationType type);

    SignalChain& getSignalChain() noexcept { return *chain; }

    // Block reordering.
    // Logical groups (0-9) map to DSP blocks as follows:
    //   0=Gate, 1=Pitch, 2=Drive, 3=Amp+PowerAmp, 4=Cabinet, 5=Modulation, 6=Delay, 7=Reverb, 8=EQ, 9=Compressor
    // fromPos and toPos are display positions (0-9).
    // Call only while the audio thread is suspended.
    void             reorderLogical(int fromPos, int toPos);
    std::vector<int> getLogicalOrder() const;
    void             setLogicalOrder(const std::vector<int>& order);
    void             resetLogicalOrder();

private:
    static constexpr int kNumGroups = 10;

    std::unique_ptr<SignalChain>   chain;
    ParameterManager               params;

    // Chain indices: gate=0, pitch=1, drive=2, amp=3, powerAmp=4, cabinet=5,
    //                modulation=6, delay=7, reverb=8, speakerEQ=9, compressor=10
    NoiseGateBlock*    gate       = nullptr;
    PitchBlock*        pitch      = nullptr;
    OverdriveBlock*    drive      = nullptr;
    AmpBlockExtended*  amp        = nullptr;
    PowerAmpProcessor* powerAmp   = nullptr;
    CabinetBlock*      cabinet    = nullptr;
    ModulationBlock*   modulation = nullptr;
    DelayBlock*        delay      = nullptr;
    PlateReverbBlock*  reverb     = nullptr;
    OutputEQBlock*     speakerEQ  = nullptr;
    CompressorBlock*   compressor = nullptr;

    double currentSampleRate  = 44100.0;
    int    currentMaxBlock    = 512;
    int    currentNumChannels = 2;

    // Logical display order: logicalOrder_[displayPos] = groupIndex (0-9).
    // Slot order determines where inactive effects land when re-added:
    //   Pitch(1) is slot 0 → inserts first; Mod(5) is slot 7 → inserts after EQ(8).
    // Active default visible chain: Gate → Comp → Drive → Amp → Cab → EQ → Delay → Reverb
    int logicalOrder_[kNumGroups] = {1, 0, 9, 2, 3, 4, 8, 5, 6, 7};

    // Rebuild the SignalChain process order from logicalOrder_.
    void applyLogicalOrder();

    void registerAllParameters();
};
