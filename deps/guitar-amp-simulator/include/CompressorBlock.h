#pragma once
#include "AudioBlock.h"
#include "VCACompressor.h"
#include "FETCompressor1176.h"
#include <array>
#include <algorithm>
#include <cmath>

// Switchable compressor block: VCA feed-forward or 1176-style FET.
//
// Parameter IDs (all set via setParameter / GuitarAmpProcessor "comp." prefix):
//   type      — 0.0 = VCA, 1.0 = 1176
//   threshold — dBFS, -60 to 0  (default -18)
//   ratio     — index 0-4: 0=2:1 1=4:1 2=8:1 3=20:1 4=Limit
//   attack    — 0-10 (10 = fastest)
//   release   — 0-10 (10 = fastest)
//   knee      — 0-10 → 0-12 dB soft-knee width (VCA only)
//   makeup    — 0-10 → 0-+20 dB makeup gain
class CompressorBlock : public AudioBlock {
public:
    enum class Type { VCA = 0, FET_1176 = 1 };

    void prepare(double sr, int maxBlockSize, int nCh) override {
        sampleRate   = sr;
        numChannels  = nCh;
        maxBlockSize_ = maxBlockSize;
        for (auto& v : vca_) { v.prepare(sr); v.setSidechainHP(scHP_); }
        for (auto& f : fet_) { f.prepare(sr); f.setSidechainHP(scHP_); }
    }

    void process(float** in, float** out, int numSamples, int nCh) override {
        if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }
        for (int ch = 0; ch < nCh && ch < kMaxCh; ++ch) {
            for (int i = 0; i < numSamples; ++i) {
                out[ch][i] = (type_ == Type::FET_1176)
                    ? fet_[ch].process(in[ch][i])
                    : vca_[ch].process(in[ch][i]);
            }
        }
    }

    void setParameter(const std::string& id, float v) override {
        if (id == "type") {
            type_ = (v >= 0.5f) ? Type::FET_1176 : Type::VCA;

        } else if (id == "threshold") {
            // VCA: threshold in dBFS
            for (auto& c : vca_) c.setThreshold(v);
            // 1176: threshold controls input drive (lower threshold → more input gain)
            // n01=0 at 0 dBFS (barely clips threshold), n01=1 at -60 dBFS (full drive)
            const float n01 = std::clamp(-v / 60.0f, 0.0f, 1.0f);
            for (auto& c : fet_) c.setInputGain(n01);

        } else if (id == "ratio") {
            static constexpr float kVCA[] = { 2.0f, 4.0f, 8.0f, 20.0f, 1e6f };
            static constexpr int   kFET[] = { 4,    4,    8,    20,    0    };
            const int idx = std::clamp(static_cast<int>(v), 0, 4);
            for (auto& c : vca_) c.setRatio(kVCA[idx]);
            for (auto& c : fet_) c.setRatio(kFET[idx]);

        } else if (id == "attack") {
            // VCA: 10→1ms, 0→100ms (log scale). 1176: inverted by hardware convention.
            const float vcaAtk = 0.001f * std::pow(100.0f, 1.0f - v * 0.1f);
            for (auto& c : vca_) c.setAttack(vcaAtk);
            for (auto& c : fet_) c.setAttack(v * 0.1f);

        } else if (id == "release") {
            // VCA: 10→50ms, 0→1000ms. 1176: same convention.
            const float vcaRel = 0.050f * std::pow(20.0f, 1.0f - v * 0.1f);
            for (auto& c : vca_) c.setRelease(vcaRel);
            for (auto& c : fet_) c.setRelease(v * 0.1f);

        } else if (id == "knee") {
            for (auto& c : vca_) c.setKnee(v * 1.2f); // 0-10 → 0-12 dB

        } else if (id == "makeup") {
            // 0-10 → 0-+20 dB
            const float makeupLin = std::pow(10.0f, v * 2.0f * 0.05f);
            for (auto& c : vca_) c.setMakeupGain(makeupLin);
            for (auto& c : fet_) c.setOutputGain(v * 0.1f);

        } else if (id == "scHP") {
            // Detector-only sidechain high-pass, Hz. 0 = OFF.
            scHP_ = v;
            for (auto& c : vca_) c.setSidechainHP(v);
            for (auto& c : fet_) c.setSidechainHP(v);

        } else if (id == "progRel") {
            // Program-dependent (dual-TC) release for the VCA. 0 = OFF (default).
            // The 1176 path is already program-dependent by design.
            for (auto& c : vca_) c.setProgramRelease(v >= 0.5f);
        }
    }

    float getParameter(const std::string& id) const override {
        if (id == "gr_db") {
            return (type_ == Type::FET_1176)
                ? fet_[0].getGainReductionDb()
                : vca_[0].getGainReductionDb();
        }
        return 0.0f;
    }

private:
    static constexpr int kMaxCh = 2;

    Type type_ = Type::VCA;
    int  maxBlockSize_ = 512;
    // Detector-only sidechain high-pass, ON by default (2026-07-26 Phase-2) so the
    // low string / hum doesn't pump the whole signal. Set "scHP" 0 to disable.
    float scHP_ = 85.0f;

    std::array<VCACompressor,     kMaxCh> vca_;
    std::array<FETCompressor1176, kMaxCh> fet_;
};
