#pragma once
#include "AudioBlock.h"
#include "OverdriveFactory.h"
#include "OversamplingWrapper.h"
#include "NamOverdrive.h"
#include <memory>
#include <string>
#include <vector>

// ── OverdriveBlock ─────────────────────────────────────────────────────────
//
// Unified overdrive/distortion AudioBlock.  Supports three model types:
//
//   TubeScreamer808 — 4× oversampled, OversamplingWrapper path
//   LifePedal       — 4× oversampled, OversamplingWrapper path
//   NAM             — block-based at native sample rate, NamOverdrive path
//
// Hot-swap via setType() / setParameter("model", float).  A 10 ms linear
// crossfade is applied so that model switches are click-free at any tempo.
// Stored parameters are re-applied to the new model before the fade begins,
// ensuring parameter continuity across swaps.
//
// Bypass: copyBlock() — fully transparent, no filter state modified.
//
// Parameter routing (id strings received after the "drive." prefix is stripped
// by GuitarAmpProcessor):
//   "model"  → 0=TS808, 1=LifePedal, 2=NAM  (triggers setType)
//   "drive"  → drive [0,1]
//   "tone"   → tone  [0,1]
//   "level"  → level [0,1]
//   "mix"    → mix   [0,1]
//   "octave" → octave [0,1] (LifePedal only; silently ignored by others)
class OverdriveBlock final : public AudioBlock {
public:
    void  prepare(double sampleRate, int maxBlockSize, int nCh) override;
    void  process(float** in, float** out, int numSamples, int nCh) override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    void         setType(OverdriveType newType);
    OverdriveType getType() const noexcept { return type_; }

    // Load a .nam file.  On success silently calls setType(NAM).
    // Returns false if the file fails to load; type is unchanged.
    bool loadNam(const std::string& filePath);

private:
    static constexpr int   kMaxCh     = 2;
    static constexpr float kXfadeMs   = 10.0f;

    OverdriveType type_ = OverdriveType::TubeScreamer808;

    // Active model holders — at most one of os/nam is non-null at any time.
    std::unique_ptr<OversamplingWrapper> osModel_;
    std::unique_ptr<NamOverdrive>        namModel_;

    // Previous model retained during crossfade.
    std::unique_ptr<OversamplingWrapper> oldOs_;
    std::unique_ptr<NamOverdrive>        oldNam_;

    // Crossfade counters (in host-rate samples).
    int xfadeLen_ = 0;
    int xfadePos_ = 0;

    // Scratch buffer for old-model output during the crossfade window.
    std::vector<float> xfadeBuf_[kMaxCh];
    float*             xfadePtrs_[kMaxCh] = {};

    // Stored parameter values — re-applied when a new model is created.
    float pDrive_  = 0.5f;
    float pTone_   = 0.5f;
    float pLevel_  = 0.5f;
    float pMix_    = 1.0f;
    float pOctave_ = 0.3f;
    std::string namFilePath_;

    void applyStoredParams(OversamplingWrapper* m) const noexcept;
    void applyStoredParams(NamOverdrive* m)        const noexcept;

    void processOs (OversamplingWrapper* m, float** in, float** out,
                    int numSamples, int numCh) noexcept;
    void processNam(NamOverdrive* m,         float** in, float** out,
                    int numSamples, int numCh) noexcept;
    void processActive(float** in, float** out, int numSamples, int numCh) noexcept;
    void processOld   (float** in, float** out, int numSamples, int numCh) noexcept;
};
