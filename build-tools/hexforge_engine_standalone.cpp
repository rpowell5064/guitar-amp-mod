// Compile-time proof (M0 Stage B4): the Hex Forge engine umbrella builds in a
// TU with NO LV2 headers on the include path or in the source — exactly the
// situation of the JUCE desktop processor. Construction only; the golden
// harness exercises behavior through the LV2 wrapper.
//
// Also hosts the desktop resampler verification (M3): Smart App Control kept
// cloud-blocking a dedicated hf_resampler_test.exe (crash-tainted lineage),
// and this binary's family has clean reputation — so the checks ride along.
#include "hf_engine_all.h"

#define main hfResamplerTestMain
#include "hf_resampler_test.cpp"
#undef main

int main() {
    auto* p = new (std::nothrow) HexForge;
    if (!p) return 1;
    p->rate = 48000.0;
    const bool ok = (HF_N_PORTS == 461) && !p->primed && p->worker == nullptr;
    delete p;
    printf("engine standalone TU: %s\n", ok ? "OK" : "BAD");
    if (!ok) return 1;
    return hfResamplerTestMain();
}
