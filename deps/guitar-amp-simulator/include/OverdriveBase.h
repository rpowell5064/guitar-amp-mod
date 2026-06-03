#pragma once
#include "AmpModelBase.h"

// Abstract base class for all overdrive/distortion models.
//
// Inherits AmpModelBase so every model is directly usable with
// OversamplingWrapper without any additional glue.
//
// Conventional parameter IDs: "drive" [0,1], "tone" [0,1],
// "level" [0,1], "mix" [0,1].  Extended parameters (e.g. "octave")
// are model-specific and go through setParameter/getParameter.
//
// All processing is at the OVERSAMPLED sample rate (OversamplingWrapper
// multiplies the host rate by kFactor before calling prepare()).
class OverdriveBase : public AmpModelBase {
public:
    const char* modelName()           const noexcept override { return "OverdriveBase"; }
    int         recommendedTubeType() const noexcept override { return -1; }
};
