#include "AmpModelBase.h"

// The defaults for models without an extras-on path (see AmpModelBase.h).
bool  AmpModelBase::htActive() const noexcept { return false; }
float AmpModelBase::processSampleHt(float x, int channel) noexcept { return processSample(x, channel); }
