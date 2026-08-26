// Compile-time proof (M0 Stage B4): the Hex Forge engine umbrella builds in a
// TU with NO LV2 headers on the include path or in the source — exactly the
// situation of the JUCE desktop processor. Construction only; the golden
// harness exercises behavior through the LV2 wrapper.
#include "hf_engine_all.h"

int main() {
    auto* p = new (std::nothrow) HexForge;
    if (!p) return 1;
    p->rate = 48000.0;
    const bool ok = (HF_N_PORTS == 461) && !p->primed && p->worker == nullptr;
    delete p;
    printf("engine standalone TU: %s\n", ok ? "OK" : "BAD");
    return ok ? 0 : 1;
}
