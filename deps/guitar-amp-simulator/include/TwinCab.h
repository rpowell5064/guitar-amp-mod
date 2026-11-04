#pragma once
#include "PushPullPowerV.h"
#include "CabModels.h"

// The component twins' power stage loaded with the cab the rig runs (2026-11-03): the chosen
// CabModels speaker row against the amp's own (PushPullPowerV::Params::nativeCab). row -1 = a
// cab we have no speaker for (a user IR, No Cab): the amp keeps its own.
namespace evhcomp {
inline void applyCab(PushPullPowerV& pa, bool on, int row) noexcept {
    const SpeakerParams* nat = &CabModels::speakerRowAt(pa.nativeCab());
    const SpeakerParams* ch  = (row < 0 || row == pa.nativeCab()) ? nat : &CabModels::speakerRowAt(row);
    pa.setCabLoad(on, nat, ch);
}
} // namespace evhcomp
