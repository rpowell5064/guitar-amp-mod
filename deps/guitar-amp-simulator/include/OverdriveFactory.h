#pragma once
#include "OverdriveBase.h"
#include "NamOverdrive.h"
#include "OversamplingWrapper.h"
#include <memory>
#include <string>

enum class OverdriveType {
    TubeScreamer808 = 0,
    LifePedal       = 1,
    NAM             = 2,
    ProcoRAT        = 3,
    BigMuffPi       = 4,
    DS1             = 5,
    Klon            = 6,
    SuperOverdriveSD1 = 7,
    DOD250          = 8
};

class OverdriveFactory {
public:
    // Bare model — no oversampling.  Caller owns the result.
    static std::unique_ptr<OverdriveBase> create(OverdriveType type);

    // Model wrapped in 4× OversamplingWrapper.
    // Valid for TubeScreamer808 and LifePedal only; returns nullptr for NAM
    // (NAM must go through the direct block-processing path in OverdriveBlock).
    static std::unique_ptr<OversamplingWrapper> createOversampled(OverdriveType type, bool eco = false);

    // Load a .nam file and return a ready-to-use NamOverdrive.
    // Returns nullptr if the file fails to load.
    static std::unique_ptr<NamOverdrive> createNam(const std::string& filePath);

    static const char*  modelName(OverdriveType type) noexcept;
    static OverdriveType fromIndex(int index)          noexcept;
};
