#include "GuitarAmpProcessor.h"

GuitarAmpProcessor::GuitarAmpProcessor()
    : chain(std::make_unique<SignalChain>(512, 2))
{
    // Build default chain (indices 0-9) --------------------------
    auto gateBlock       = std::make_unique<NoiseGateBlock>();
    auto pitchBlock      = std::make_unique<PitchBlock>();
    auto driveBlock      = std::make_unique<OverdriveBlock>();
    auto ampBlock        = std::make_unique<AmpBlockExtended>();
    auto powerAmpBlock   = std::make_unique<PowerAmpProcessor>();
    auto cabinetBlock    = std::make_unique<CabinetBlock>();
    auto modulationBlock = std::make_unique<ModulationBlock>();
    auto delayBlock      = std::make_unique<DelayBlock>();
    auto reverbBlock     = std::make_unique<PlateReverbBlock>();
    auto speakerEQBlock    = std::make_unique<OutputEQBlock>();
    auto compressorBlock   = std::make_unique<CompressorBlock>();

    // Keep raw pointers before moving into the chain
    gate       = gateBlock.get();
    pitch      = pitchBlock.get();
    drive      = driveBlock.get();
    amp        = ampBlock.get();
    powerAmp   = powerAmpBlock.get();
    cabinet    = cabinetBlock.get();
    modulation = modulationBlock.get();
    delay      = delayBlock.get();
    reverb     = reverbBlock.get();
    speakerEQ  = speakerEQBlock.get();
    compressor = compressorBlock.get();

    chain->addBlock(std::move(gateBlock));        // 0
    chain->addBlock(std::move(pitchBlock));       // 1
    chain->addBlock(std::move(driveBlock));       // 2
    chain->addBlock(std::move(ampBlock));         // 3
    chain->addBlock(std::move(powerAmpBlock));    // 4
    chain->addBlock(std::move(cabinetBlock));     // 5
    chain->addBlock(std::move(modulationBlock));  // 6
    chain->addBlock(std::move(delayBlock));       // 7
    chain->addBlock(std::move(reverbBlock));      // 8
    chain->addBlock(std::move(speakerEQBlock));   // 9
    chain->addBlock(std::move(compressorBlock));  // 10

    registerAllParameters();
}

void GuitarAmpProcessor::registerAllParameters() {
    // Noise gate
    params.registerParameter("gate.threshold",  -45.0f, -100.0f,  0.0f);
    params.registerParameter("gate.attack",        5.0f,    0.1f, 500.0f);
    params.registerParameter("gate.release",     200.0f,    1.0f, 2000.0f);
    params.registerParameter("gate.hold",        100.0f,    0.0f, 1000.0f);
    params.registerParameter("gate.hysteresis",    5.0f,    0.0f,  24.0f);

    // Pitch block (Whammy pedal)
    params.registerParameter("pitch.mode",       3.0f, 0.0f, 4.0f);   // 0=Down2Oct 1=Down1Oct 2=Detune 3=Up1Oct 4=Up2Oct
    params.registerParameter("pitch.expression", 0.0f, 0.0f, 1.0f);   // 0=heel(dry) 1=toe(full shift)
    params.registerParameter("pitch.mix",        1.0f, 0.0f, 1.0f);
    params.registerParameter("pitch.cents",      0.0f, -50.0f, 50.0f);

    // Overdrive block (TS808 / Life Pedal / NAM)
    params.registerParameter("drive.model",  0.0f, 0.0f, 4.0f);  // 0=TS808, 1=LifePedal, 2=NAM, 3=ProcoRAT, 4=BigMuffPi
    params.registerParameter("drive.drive",  0.0f, 0.0f, 1.0f);
    params.registerParameter("drive.tone",   0.6f, 0.0f, 1.0f);
    params.registerParameter("drive.level",  0.9f, 0.0f, 1.0f);
    params.registerParameter("drive.mix",    1.0f, 0.0f, 1.0f);
    params.registerParameter("drive.octave", 0.3f, 0.0f, 1.0f);  // Life Pedal octave-up level

    // Amp (preamp stage)
    params.registerParameter("amp.gain",      0.65f, 0.0f, 1.0f);
    params.registerParameter("amp.bass",      0.55f, 0.0f, 1.0f);
    params.registerParameter("amp.mid",       0.70f, 0.0f, 1.0f);
    params.registerParameter("amp.treble",    0.65f, 0.0f, 1.0f);
    params.registerParameter("amp.presence",  0.70f, 0.0f, 1.0f);
    params.registerParameter("amp.master",    0.65f, 0.0f, 1.0f);
    params.registerParameter("amp.sag",       0.3f, 0.0f, 1.0f);
    params.registerParameter("amp.namGain",   1.5f, 0.0f, 4.0f);

    // Sunn Model T
    params.registerParameter("sunn.vol1",     0.75f, 0.0f, 1.0f);
    params.registerParameter("sunn.vol2",     0.75f, 0.0f, 1.0f);
    params.registerParameter("sunn.bass1",    0.80f, 0.0f, 1.0f);
    params.registerParameter("sunn.mid1",     0.55f, 0.0f, 1.0f);
    params.registerParameter("sunn.treble1",  0.30f, 0.0f, 1.0f);
    params.registerParameter("sunn.bass2",    0.80f, 0.0f, 1.0f);
    params.registerParameter("sunn.mid2",     0.55f, 0.0f, 1.0f);
    params.registerParameter("sunn.treble2",  0.30f, 0.0f, 1.0f);
    params.registerParameter("sunn.bright",        0.0f, 0.0f, 1.0f);
    params.registerParameter("sunn.bright2",       0.0f, 0.0f, 1.0f);
    params.registerParameter("sunn.channel_link",  0.0f, 0.0f, 2.0f);
    params.registerParameter("sunn.input_pad",     0.0f, 0.0f, 1.0f);

    // EVH 5150 III
    params.registerParameter("evh.channel",    1.0f, 0.0f, 1.0f);  // 1.0 = Red (lead)
    params.registerParameter("evh.resonance",  0.5f, 0.0f, 1.0f);

    // Orange Rockerverb 50
    params.registerParameter("rvb.channel", 0.0f, 0.0f, 1.0f);
    params.registerParameter("rvb.gain",    0.55f, 0.0f, 1.0f);
    params.registerParameter("rvb.bass",    0.35f, 0.0f, 1.0f);
    params.registerParameter("rvb.mid",     0.70f, 0.0f, 1.0f);
    params.registerParameter("rvb.treble",  0.5f,  0.0f, 1.0f);
    params.registerParameter("rvb.master",  0.65f, 0.0f, 1.0f);
    params.registerParameter("rvb.sag",     0.3f, 0.0f, 1.0f);

    // Power amp stage
    params.registerParameter("pamp.tubeType", 1.0f, 0.0f, 3.0f); // 1 = EL34 (JCM800 default)
    params.registerParameter("pamp.presence", 0.55f, 0.0f, 1.0f); // JCM800 calibrated
    params.registerParameter("pamp.depth",    0.18f, 0.0f, 1.0f);
    params.registerParameter("pamp.sag",      0.33f, 0.0f, 1.0f);
    params.registerParameter("pamp.master",   0.62f, 0.0f, 1.0f);
    params.registerParameter("pamp.nfb",      0.42f, 0.0f, 1.0f);
    params.registerParameter("pamp.resonance",0.5f, 0.0f, 1.0f);
    params.registerParameter("pamp.airFeel",  0.0f, 0.0f, 1.0f);

    // Cabinet
    params.registerParameter("cab.lowCutHz",   80.0f,   20.0f, 500.0f);
    params.registerParameter("cab.highCutHz", 16000.0f, 2000.0f, 20000.0f);
    params.registerParameter("cab.mix",         1.0f,    0.0f,   1.0f);

    // Modulation
    params.registerParameter("mod.rate",        0.25f, 0.0f, 1.0f);
    params.registerParameter("mod.depth",       0.55f, 0.0f, 1.0f);
    params.registerParameter("mod.mix",         1.0f,  0.0f, 1.0f);
    params.registerParameter("mod.stereoWidth", 0.0f,  0.0f, 1.0f);
    params.registerParameter("mod.preampOn",    1.0f,  0.0f, 1.0f);
    params.registerParameter("mod.mode",        0.0f,  0.0f, 1.0f);  // 0=Chorus, 1=Vibrato
    params.registerParameter("mod.outputLevel", 0.75f, 0.0f, 1.0f);

    // Delay
    params.registerParameter("delay.type",         0.0f,   0.0f,    2.0f);   // 0=Digital, 1=Tape, 2=Echorec
    params.registerParameter("delay.timeMs",       450.0f, 1.0f,    2000.0f);
    params.registerParameter("delay.feedback",      0.15f, 0.0f,    0.98f);
    params.registerParameter("delay.mix",           0.15f, 0.0f,    1.0f);
    params.registerParameter("delay.lowCutHz",      80.0f, 20.0f,   500.0f);
    params.registerParameter("delay.highCutHz",   8000.0f, 1000.0f, 20000.0f);
    params.registerParameter("delay.stereoWidth",    0.5f, 0.0f,    1.0f);   // Digital
    params.registerParameter("delay.wowDepth",       0.003f, 0.0f,  1.0f);   // Tape/Echorec
    params.registerParameter("delay.flutterDepth",   0.001f, 0.0f,  1.0f);   // Tape/Echorec
    params.registerParameter("delay.saturation",     0.3f, 0.0f,    1.0f);   // Tape
    params.registerParameter("delay.tapeAge",        0.5f, 0.0f,    1.0f);   // Tape
    params.registerParameter("delay.headMask",      15.0f, 1.0f,    15.0f);  // Echorec
    params.registerParameter("delay.noiseLevel",     0.0f, 0.0f,    1.0f);   // Echorec

    // Compressor (VCA / 1176)
    params.registerParameter("comp.type",       0.0f, 0.0f,   1.0f);   // 0=VCA, 1=1176
    params.registerParameter("comp.threshold", -30.0f, -60.0f, 0.0f);  // dBFS
    params.registerParameter("comp.ratio",      1.0f,  0.0f,  4.0f);   // index: 0=2:1…4=Limit
    params.registerParameter("comp.attack",     6.0f,  0.0f, 10.0f);   // 0=slow 10=fast
    params.registerParameter("comp.release",    6.0f,  0.0f, 10.0f);
    params.registerParameter("comp.knee",       6.0f,  0.0f, 10.0f);   // VCA soft-knee dB
    params.registerParameter("comp.makeup",     4.0f,  0.0f, 10.0f);   // 0-+20 dB makeup

    // Speaker EQ
    params.registerParameter("eq.b1.freq",  100.0f,   20.0f, 500.0f);
    params.registerParameter("eq.b1.gain",    0.0f,  -15.0f,  15.0f);
    params.registerParameter("eq.b2.freq",  300.0f,   80.0f, 800.0f);
    params.registerParameter("eq.b2.gain",    0.0f,  -15.0f,  15.0f);
    params.registerParameter("eq.b2.q",       1.0f,    0.3f,   4.0f);
    params.registerParameter("eq.b3.freq", 1000.0f,  200.0f, 4000.0f);
    params.registerParameter("eq.b3.gain",    0.0f,  -15.0f,  15.0f);
    params.registerParameter("eq.b3.q",       1.0f,    0.3f,   4.0f);
    params.registerParameter("eq.b4.freq", 4000.0f,  800.0f, 12000.0f);
    params.registerParameter("eq.b4.gain",    0.0f,  -15.0f,  15.0f);
    params.registerParameter("eq.b4.q",       1.0f,    0.3f,   4.0f);
    params.registerParameter("eq.b5.freq", 8000.0f, 1000.0f, 16000.0f);
    params.registerParameter("eq.b5.gain",    0.0f,  -15.0f,  15.0f);

    // Reverb
    params.registerParameter("reverb.preDelayMs",   5.0f,  0.0f,  100.0f);
    params.registerParameter("reverb.decayTime",    0.8f,  0.1f,   10.0f);
    params.registerParameter("reverb.damping",      0.65f, 0.0f,    1.0f);
    params.registerParameter("reverb.modDepth",     0.2f,  0.0f,    1.0f);
    params.registerParameter("reverb.modRate",      0.5f,  0.01f,   8.0f);
    params.registerParameter("reverb.mix",          0.2f,  0.0f,    1.0f);

    // Route parameter changes to the correct block.
    params.setChangeCallback([this](const std::string& id, float value) {
        // Strip the block prefix and forward to the matching block.
        auto stripPrefix = [&](const std::string& prefix) -> std::string {
            if (id.size() > prefix.size() && id.substr(0, prefix.size()) == prefix)
                return id.substr(prefix.size());
            return {};
        };

        std::string local;
        if      (!(local = stripPrefix("gate.")).empty()   && gate)
            gate->setParameter(local, value);
        else if (!(local = stripPrefix("pitch.")).empty()  && pitch)
            pitch->setParameter(local, value);
        else if (!(local = stripPrefix("drive.")).empty()  && drive)
            drive->setParameter(local, value);
        else if (!(local = stripPrefix("amp.")).empty()    && amp)
            amp->setParameter(local, value);
        else if (!(local = stripPrefix("pamp.")).empty()  && powerAmp)
            powerAmp->setParameter(local, value);
        else if (!(local = stripPrefix("cab.")).empty()    && cabinet)
            cabinet->setParameter(local, value);
        else if (!(local = stripPrefix("mod.")).empty()    && modulation)
            modulation->setParameter(local, value);
        else if (!(local = stripPrefix("delay.")).empty()  && delay)
            delay->setParameter(local, value);
        else if (!(local = stripPrefix("reverb.")).empty() && reverb)
            reverb->setParameter(local, value);
        else if (!(local = stripPrefix("eq.")).empty() && speakerEQ)
            speakerEQ->setParameter(local, value);
        else if (!(local = stripPrefix("comp.")).empty() && compressor)
            compressor->setParameter(local, value);
        else if (!(local = stripPrefix("sunn.")).empty() && amp)
            amp->setParameter(local, value);
        else if (!(local = stripPrefix("rvb.")).empty() && amp)
            amp->setParameter(local, value);
        else if (!(local = stripPrefix("evh.")).empty() && amp)
            amp->setParameter(local, value);
    });
}

void GuitarAmpProcessor::prepare(double sr, int maxBlock, int nCh) {
    currentSampleRate  = sr;
    currentMaxBlock    = maxBlock;
    currentNumChannels = nCh;
    chain->prepare(sr, maxBlock, nCh);
}

void GuitarAmpProcessor::processBlock(float** inputs, float** outputs,
                                       int numSamples, int numChannels,
                                       double /*sampleRate*/) {
    chain->process(inputs, outputs, numSamples, numChannels);
}

void GuitarAmpProcessor::selectAmpModel(AmpModel model) {
    if (amp) amp->setAmpModel(model);
}

void GuitarAmpProcessor::selectTubeType(TubeType type) {
    if (powerAmp) powerAmp->setTubeType(type);
}

void GuitarAmpProcessor::setDelayType(DelayType type) {
    if (delay) delay->setType(type);
}

void GuitarAmpProcessor::setParameter(const std::string& id, float value) {
    params.setParameter(id, value); // triggers the callback above
}

float GuitarAmpProcessor::getParameter(const std::string& id) const {
    return params.getParameter(id);
}

void GuitarAmpProcessor::loadIR(const std::vector<float>& irL,
                                  const std::vector<float>* irR) {
    if (cabinet) cabinet->setIR(irL, irR);
}

bool GuitarAmpProcessor::loadNeuralModel(const std::string& filePath) {
    if (!amp) return false;
    return amp->loadNeuralModel(filePath);
}

bool GuitarAmpProcessor::loadOverdriveNam(const std::string& filePath) {
    if (!drive) return false;
    return drive->loadNam(filePath);
}

void GuitarAmpProcessor::selectOverdriveModel(OverdriveType type) {
    if (drive) drive->setType(type);
}

void GuitarAmpProcessor::selectModulationType(ModulationType type) {
    if (modulation) modulation->setType(type);
}

// ─── Block reordering ─────────────────────────────────────────────────────────
//
// Logical group → DSP block index mapping (Amp and PowerAmp always move together):
//   Group 0: [0]    Gate
//   Group 1: [1]    Pitch
//   Group 2: [2]    Drive
//   Group 3: [3, 4] Amp + PowerAmp
//   Group 4: [5]    Cabinet
//   Group 5: [6]    Modulation
//   Group 6: [7]    Delay
//   Group 7: [8]    Reverb
//   Group 8: [9]    EQ
//   Group 9: [10]   Compressor

static const int kGroupDsp[10][2] = {
    {0, -1}, {1, -1}, {2, -1}, {3,  4}, {5, -1}, {6, -1}, {7, -1}, {8, -1}, {9, -1}, {10, -1}
};

void GuitarAmpProcessor::applyLogicalOrder() {
    std::vector<int> flat;
    flat.reserve(11);
    for (int displayPos = 0; displayPos < kNumGroups; ++displayPos) {
        const int group = logicalOrder_[displayPos];
        flat.push_back(kGroupDsp[group][0]);
        if (kGroupDsp[group][1] >= 0)
            flat.push_back(kGroupDsp[group][1]);
    }
    chain->setOrder(flat);
}

void GuitarAmpProcessor::reorderLogical(int fromPos, int toPos) {
    if (fromPos == toPos || fromPos < 0 || toPos < 0 ||
        fromPos >= kNumGroups || toPos >= kNumGroups) return;
    const int moving = logicalOrder_[fromPos];
    if (fromPos < toPos)
        for (int i = fromPos; i < toPos; ++i) logicalOrder_[i] = logicalOrder_[i + 1];
    else
        for (int i = fromPos; i > toPos; --i) logicalOrder_[i] = logicalOrder_[i - 1];
    logicalOrder_[toPos] = moving;
    applyLogicalOrder();
}

std::vector<int> GuitarAmpProcessor::getLogicalOrder() const {
    return {logicalOrder_, logicalOrder_ + kNumGroups};
}

void GuitarAmpProcessor::setLogicalOrder(const std::vector<int>& order) {
    if (static_cast<int>(order.size()) != kNumGroups) return;
    for (int i = 0; i < kNumGroups; ++i) logicalOrder_[i] = order[i];
    applyLogicalOrder();
}

void GuitarAmpProcessor::resetLogicalOrder() {
    for (int i = 0; i < kNumGroups; ++i) logicalOrder_[i] = i;
    applyLogicalOrder();
}
