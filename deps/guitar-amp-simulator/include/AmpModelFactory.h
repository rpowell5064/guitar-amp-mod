#pragma once
#include "AmpModelBase.h"
#include "OversamplingWrapper.h"
#include <memory>
#include <string>

// Factory and registry for AmpModelBase implementations.
//
// create()               → raw model (use when embedding inside a custom host block)
// createWithOversampling() → model wrapped in OversamplingWrapper (ready AudioBlock)
//
// No JUCE dependency; model names are plain C-strings for maximum portability.
class AmpModelFactory {
public:
    enum class ModelID : int {
        SunnModelT         = 0,
        OrangeRockerverb50 = 1,
        MarshallJCM800     = 2,
        FenderDeluxe       = 3,
        EVH5150            = 4,
        FriedmanBEDeluxe   = 5,
        HiwattDR103        = 6,
        VoxAC30            = 7,
        PeaveyBackstage    = 8,
        MarshallPlexi1959  = 9,
        MesaMarkV          = 10,
        MesaDualRectifier  = 11,
    };

    static constexpr int kNumModels = 12;

    // Returns ownership of a newly constructed model. Never returns null;
    // unknown IDs fall back to SunnModelT.
    static std::unique_ptr<AmpModelBase> create(ModelID id);

    // Returns the model wrapped in a 4× OversamplingWrapper ready to be
    // inserted directly into the signal chain as an AudioBlock.
    static std::unique_ptr<OversamplingWrapper> createWithOversampling(ModelID id);

    // ── Metadata (no allocation) ──────────────────────────────────────────────

    // Human-readable name, e.g. "Sunn Model T".
    static const char* getModelName(ModelID id) noexcept;

    // Suggested TubeType index for the downstream PowerAmpProcessor.
    // Matches TubeType enum: 0=6L6GC, 1=EL34, 2=EL84, 3=KT88.
    static int recommendedTubeType(ModelID id) noexcept;

    // Null-terminated array of all model name strings; length == kNumModels.
    // Safe to iterate with: for (int i = 0; names[i]; ++i)
    static const char* const* getAllModelNames() noexcept;

    // Parse name string → ModelID. Unknown strings fall back to SunnModelT.
    static ModelID fromString(const std::string& name) noexcept;

private:
    AmpModelFactory() = delete;
};
