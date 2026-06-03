#include "AmpBlock.h"
#include <cmath>
#include <algorithm>

static const AmpBlock::ModelParams kFenderDeluxe = {
    /* numStages         */ 2,
    /* stageGainMin/Max  */ 2.0f,  8.0f,
    /* bassFreq          */ 100.0,
    /* midFreq           */ 500.0,
    /* trebleFreq        */ 3000.0,
    /* presenceFreq      */ 5000.0,
    /* midGainRange      */ 10.0f,
    /* bassGainRange     */ 18.0f,
    /* trebleGainRange   */ 18.0f,
    /* presenceGainRange */  8.0f,
    /* powerClipThresh   */ 0.85f
};

static const AmpBlock::ModelParams kMarshallJCM800 = {
    /* numStages         */ 3,
    /* stageGainMin/Max  */ 4.0f, 18.0f,
    /* bassFreq          */  80.0,
    /* midFreq           */ 700.0,
    /* trebleFreq        */ 4000.0,
    /* presenceFreq      */ 6000.0,
    /* midGainRange      */ 20.0f,
    /* bassGainRange     */ 16.0f,
    /* trebleGainRange   */ 20.0f,
    /* presenceGainRange */ 14.0f,
    /* powerClipThresh   */ 0.75f
};

// Neutral EQ used with neural model path — tone stack remains active post-NAM.
static const AmpBlock::ModelParams kNeuralNeutral = {
    /* numStages         */ 1,
    /* stageGainMin/Max  */ 1.0f, 1.0f,
    /* bassFreq          */ 100.0,
    /* midFreq           */ 600.0,
    /* trebleFreq        */ 3500.0,
    /* presenceFreq      */ 6000.0,
    /* midGainRange      */ 15.0f,
    /* bassGainRange     */ 15.0f,
    /* trebleGainRange   */ 15.0f,
    /* presenceGainRange */ 10.0f,
    /* powerClipThresh   */ 1.0f
};

static const AmpBlock::ModelParams kEVH5150III = {
    /* numStages         */ 4,
    /* stageGainMin/Max  */ 6.0f, 30.0f,
    /* bassFreq          */  60.0,
    /* midFreq           */ 400.0,
    /* trebleFreq        */ 5000.0,
    /* presenceFreq      */ 8000.0,
    /* midGainRange      */ 24.0f,
    /* bassGainRange     */ 18.0f,
    /* trebleGainRange   */ 24.0f,
    /* presenceGainRange */ 18.0f,
    /* powerClipThresh   */ 0.65f
};

float AmpBlock::saturate(float x, float stageGain, float thresh) noexcept {
    const float driven = x * stageGain;
    return std::tanh(driven * thresh) / thresh;
}

void AmpBlock::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;
    sagDecayCoef = static_cast<float>(std::exp(-1.0 / (0.08 * sr)));
    sagEnvelope  = 0.0f;

    namInBuf.resize(static_cast<size_t>(maxBlock));
    namOutBuf.resize(static_cast<size_t>(maxBlock));
    sagFactors.resize(static_cast<size_t>(maxBlock));

    if (nam.isLoaded())
        nam.reset(sr, maxBlock);

    recalcModel();
    for (auto& c : eq) {
        c.bassF.reset(); c.midF.reset();
        c.trebleF.reset(); c.presenceF.reset();
        c.inputHP.reset();
    }
}

void AmpBlock::setAmpModel(AmpModel model) {
    if (model == AmpModel::NeuralCustom && nam.isLoaded())
        nam.reset(sampleRate, maxBlockSize);
    currentModel = model;
    recalcModel();
}

bool AmpBlock::loadNeuralModel(const std::string& filePath) {
    const bool ok = nam.loadFromFile(filePath);
    if (ok && sampleRate > 0.0 && maxBlockSize > 0)
        nam.reset(sampleRate, maxBlockSize);
    return ok;
}

void AmpBlock::setParameter(const std::string& id, float v) {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "gain")     { gain     = c; }
    else if (id == "bass")     { bass     = c; recalcEQ(); return; }
    else if (id == "mid")      { mid      = c; recalcEQ(); return; }
    else if (id == "treble")   { treble   = c; recalcEQ(); return; }
    else if (id == "presence") { presence = c; recalcEQ(); return; }
    else if (id == "master")   { master   = c; }
    else if (id == "sag")      { sag      = c; }
    else if (id == "namGain")  { namOutputGain = std::clamp(v, 0.0f, 8.0f); }
}

float AmpBlock::getParameter(const std::string& id) const {
    if (id == "gain")     return gain;
    if (id == "bass")     return bass;
    if (id == "mid")      return mid;
    if (id == "treble")   return treble;
    if (id == "presence") return presence;
    if (id == "master")   return master;
    if (id == "sag")      return sag;
    if (id == "namGain")  return namOutputGain;
    return 0.0f;
}

void AmpBlock::recalcModel() {
    switch (currentModel) {
    case AmpModel::FenderDeluxe:   mp = kFenderDeluxe;   break;
    case AmpModel::MarshallJCM800: mp = kMarshallJCM800; break;
    case AmpModel::EVH5150III:     mp = kEVH5150III;     break;
    case AmpModel::NeuralCustom:   mp = kNeuralNeutral;  break;
    }
    recalcEQ();
}

void AmpBlock::recalcEQ() {
    auto knobToDB = [](float knob, float range) {
        return static_cast<double>((knob - 0.5f) * range);
    };

    const double bassDB     = knobToDB(bass,     mp.bassGainRange);
    const double midDB      = knobToDB(mid,      mp.midGainRange);
    const double trebleDB   = knobToDB(treble,   mp.trebleGainRange);
    const double presenceDB = knobToDB(presence, mp.presenceGainRange);

    const BiquadCoeffs bC = Filters::lowshelf (mp.bassFreq,     bassDB,   sampleRate);
    const BiquadCoeffs mC = Filters::peaking  (mp.midFreq,      midDB,  0.7, sampleRate);
    const BiquadCoeffs tC = Filters::highshelf(mp.trebleFreq,   trebleDB,   sampleRate);
    const BiquadCoeffs pC = Filters::highshelf(mp.presenceFreq, presenceDB, sampleRate);
    const BiquadCoeffs hC = Filters::highpass (20.0, 0.707, sampleRate);

    for (auto& c : eq) {
        c.bassF.setCoeffs(bC);
        c.midF.setCoeffs(mC);
        c.trebleF.setCoeffs(tC);
        c.presenceF.setCoeffs(pC);
        c.inputHP.setCoeffs(hC);
    }
}

float AmpBlock::processPreamp(float x) const noexcept {
    const float stageGain = mp.stageGainMin +
                            gain * (mp.stageGainMax - mp.stageGainMin);
    const float perStage = stageGain / static_cast<float>(mp.numStages);
    float y = x;
    for (int s = 0; s < mp.numStages; ++s)
        y = saturate(y, perStage, mp.powerClipThreshold);
    return y;
}

void AmpBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

    const int chCount = std::min(nCh, kMaxCh);

    // ── Neural model path ─────────────────────────────────────────────────────
    if (currentModel == AmpModel::NeuralCustom) {
        if (!nam.isLoaded()) { copyBlock(in, out, numSamples, nCh); return; }

        // Logarithmic gain: gain=0 → –20 dB, gain=0.5 → +10 dB, gain=1 → +40 dB.
        const float preGain = std::pow(10.0f, gain * 3.0f - 1.0f);

        // Pass 1: DC-block + preGain → namInBuf; capture per-sample sag factor.
        for (int i = 0; i < numSamples; ++i) {
            const float rms = std::abs(in[0][i]);
            sagEnvelope = sagDecayCoef * sagEnvelope + (1.0f - sagDecayCoef) * rms;
            sagFactors[i] = 1.0f / (1.0f + sag * sagEnvelope * 3.0f);
            namInBuf[i]   = eq[0].inputHP.process(in[0][i]) * preGain;
        }

        // Pass 2: NAM inference on the whole block.
        nam.processBuffer(namInBuf.data(), namOutBuf.data(), numSamples);

        // Pass 3: post-model master + sag + tone stack (same mono source → all channels).
        for (int i = 0; i < numSamples; ++i) {
            const float y = namOutBuf[i] * master * sagFactors[i] * namOutputGain;
            for (int c = 0; c < chCount; ++c) {
                float s = eq[c].bassF.process(y);
                s = eq[c].midF.process(s);
                s = eq[c].trebleF.process(s);
                s = eq[c].presenceF.process(s);
                out[c][i] = s;
            }
        }
        for (int c = chCount; c < nCh; ++c)
            if (in[c] != out[c])
                for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
        return;
    }

    // ── Analog model path ─────────────────────────────────────────────────────
    for (int i = 0; i < numSamples; ++i) {
        float rmsApprox = 0.0f;
        for (int c = 0; c < chCount; ++c)
            rmsApprox += std::abs(in[c][i]);
        rmsApprox /= static_cast<float>(chCount);

        sagEnvelope = sagDecayCoef * sagEnvelope + (1.0f - sagDecayCoef) * rmsApprox;
        const float sagFactor = 1.0f / (1.0f + sag * sagEnvelope * 3.0f);

        for (int c = 0; c < chCount; ++c) {
            auto& e = eq[c];
            float s = e.inputHP.process(in[c][i]);
            s = processPreamp(s);
            s = e.bassF.process(s);
            s = e.midF.process(s);
            s = e.trebleF.process(s);
            s *= sagFactor;
            s  = std::tanh(s * master * 2.0f);
            s  = e.presenceF.process(s);
            out[c][i] = s;
        }
    }

    for (int c = chCount; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}
