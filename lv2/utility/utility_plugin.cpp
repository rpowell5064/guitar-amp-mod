#include "lv2_util.h"
#include "BiquadFilter.h"
#include "PickupVoicer.h"
#include "PickupLoadSim.h"
#include "HumNotchComb.h"
#include <lv2/core/lv2.h>
#include <algorithm>
#include <cmath>
#include <new>
#ifdef HEXCHAIN_ANAGRAM
#include <cstring>   // enabled=0 passthrough memcpy
#endif

#define UTILITY_URI "https://rpowell5064.github.io/guitaramp-suite/utility"

enum UtilPorts {
    P_IN        = 0,
    P_OUT       = 1,
    P_GAIN_DB   = 2,
    P_PHASE     = 3,
    P_HUM       = 4,
    P_HB_MODEL  = 5,   // single-coil -> humbucker model (enum 0..2)
    P_HB_AMOUNT = 6,   // voicing amount 0..1 (0 = bypass)
    P_HUMBK     = 7,   // humbucker voicing enable (default on -> legacy amount>0 behaviour preserved)
    P_BOOST     = 8,   // clean-boost enable
    P_BOOST_AMT = 9,   // clean-boost amount in dB (0..12)
    P_PICKUP_LOAD = 10, // pickup/cable/input-impedance sim (2026-07-23, default 0 = off)
    P_MAINS     = 11,  // mains frequency for the hum comb: 0 = 60 Hz, 1 = 50 Hz (2026-09-25)
    P_ENABLED,            // lv2:designation lv2:enabled — Input Trim has no Bypass
                          // port, so this IS its bypass.  1 = on, 0 = bypassed
                          // (a plain memcpy passthrough).
#ifdef HEXCHAIN_ANAGRAM
    P_RESET,              // KosmOS: kx:Reset trigger
#endif
    P_GUITAR,             // GUITAR preset (2026-10-05): 0 Default, 1 Telecaster ('59 voicing + 2 dB),
                          // 2 Hot Pickups (a hot modern humbucker tamed back to a PAF). Appended
                          // AFTER the designated enabled port so no existing index moves.
    P_N_PORTS
};

// Hum filter: the shared 6-notch 60 Hz comb (lv2/common/HumNotchComb.h) — upgraded
// 2026-07-14 from the old 50/60 twin-notch to match the Hex Forge Input Trim (the
// user's measured idle floor is 89% 60 Hz harmonics; the comb takes ~15 dB off it).

// Pickup-agnostic clean boost (mirrors the Hex Forge Input Trim OutputBoost): an output
// level boost plus a low-mid "beef" bump (peaking @ 250 Hz, reaching +3 dB at full boost)
// so it fattens single coils and thickens humbuckers. amtDb=0 -> unity + flat = true bypass.
struct OutputBoost {
    BiquadFilter beef;
    float  gain    = 1.0f;
    double curRate = 0.0;
    float  curAmt  = -1.0f;
    void prepare(double sr, float amtDb) noexcept {
        if (sr == curRate && amtDb == curAmt) return;
        curRate = sr; curAmt = amtDb;
        beef.setCoeffs(Filters::peaking(250.0, 3.0 * (amtDb / 12.0), 0.7, sr));
        gain = std::pow(10.0f, amtDb / 20.0f);
    }
    void reset() noexcept { beef.reset(); }
    float process(float x) noexcept { return beef.process(x) * gain; }
};

struct UtilityPlugin {
    HumNotchComb hum;
    PickupVoicer voice;        // single-coil -> humbucker voicing
    PickupVoicer guitar;       // GUITAR preset layer (Default / Telecaster / Hot Pickups)
    PickupLoadSim load;        // pickup loading / input impedance (v24 fidelity)
    OutputBoost  boost;        // clean boost + low-mid beef
    float*       ports[P_N_PORTS] = {};
    float        sr = 44100.0f;
    float        mainsCur = 60.0f;  // mains the comb is tuned to; starts at the prepared 60 Hz so
                                    // the default path re-prepares nothing (bit-identical)
#ifdef HEXCHAIN_ANAGRAM
    bool         resetLatch = false;   // kx:Reset edge detect
#endif
};

static LV2_Handle util_instantiate(const LV2_Descriptor*, double rate,
                                    const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) UtilityPlugin;
    if (!p) return nullptr;
    p->sr = static_cast<float>(rate);
    p->hum.prepare(rate);
    p->load.prepare(rate);
    p->ports[P_ENABLED] = nullptr;   // null-checked in run (hosts connect every port first)
    p->ports[P_GUITAR]  = nullptr;
#ifdef HEXCHAIN_ANAGRAM
    p->ports[P_RESET]   = nullptr;
#endif
    return p;
}

static void util_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<UtilityPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void util_run(LV2_Handle h, uint32_t n) {
    auto*       p        = static_cast<UtilityPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): reconstruct every stateful processor so a freshly
    // paired dual-mono partner starts identical (see anagram/ANAGRAM-NOTES.md).
    // voice/boost re-prepare lazily below (reconstruction clears their guards).
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) {
            p->resetLatch = true;
            p->hum   = HumNotchComb{};  p->hum.prepare(p->sr, p->mainsCur);
            p->load  = PickupLoadSim{}; p->load.prepare(p->sr);
            p->voice = PickupVoicer{};
            p->guitar = PickupVoicer{};
            p->boost = OutputBoost{};
        }
    } else p->resetLatch = false;
#endif
    // lv2:designation lv2:enabled: Input Trim has no stock Bypass port, so this
    // IS its bypass.  Inverted sense — enabled=0 is a plain passthrough.
    if (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f) {
        if (p->ports[P_OUT] != p->ports[P_IN])
            std::memcpy(p->ports[P_OUT], p->ports[P_IN], sizeof(float) * n);
        return;
    }
    const float gainLin  = std::pow(10.0f, *p->ports[P_GAIN_DB] / 20.0f);
    const float sign     = (*p->ports[P_PHASE] > 0.5f) ? -1.0f : 1.0f;
    const bool  humOn    = *p->ports[P_HUM] > 0.5f;
    const float mainsHz  = (p->ports[P_MAINS] && *p->ports[P_MAINS] > 0.5f) ? 50.0f : 60.0f;
    if (mainsHz != p->mainsCur) { p->mainsCur = mainsHz; p->hum.prepare(p->sr, mainsHz); }   // 50/60 Hz mains
    const float hbAmount = *p->ports[P_HB_AMOUNT];
    const bool  hbOn     = *p->ports[P_HUMBK] > 0.5f;   // enable toggle (default on -> legacy amount>0 behaviour)
    const int   hbModel  = static_cast<int>(*p->ports[P_HB_MODEL] + 0.5f);
    const bool  boostOn  = *p->ports[P_BOOST] > 0.5f;
    const float scale    = gainLin * sign;
    const float* src     = p->ports[P_IN];
    float*       dst     = p->ports[P_OUT];

    // Recompute voicing/boost only when their params change (cheap, guarded inside).
    const int   guitar   = p->ports[P_GUITAR] ? std::min(2, std::max(0, static_cast<int>(*p->ports[P_GUITAR] + 0.5f))) : 0;
    if (guitar)  p->guitar.prepare(p->sr, PickupVoicer::kGuitarBase + guitar, 1.0f);
    if (hbOn)    p->voice.prepare(p->sr, hbModel, hbAmount);
    if (boostOn) p->boost.prepare(p->sr, *p->ports[P_BOOST_AMT]);
    p->load.set(p->ports[P_PICKUP_LOAD] ? *p->ports[P_PICKUP_LOAD] : 0.0f);

    // Chain (matches the Hex Forge Input Trim order): hum -> GUITAR preset -> humbucker voice -> boost -> gain/phase.
    for (uint32_t i = 0; i < n; ++i) {
        float x = src[i];
        x = p->load.process(x);   // pickup loading: physically first (guitar/cable interface)
        if (humOn)   x = p->hum.process(x);
        if (guitar)  x = p->guitar.process(x);   // GUITAR preset layer (which instrument is plugged in)
        if (hbOn)    x = p->voice.process(x);
        if (boostOn) x = p->boost.process(x);
        dst[i] = x * scale;
    }
}

static void util_cleanup(LV2_Handle h) { delete static_cast<UtilityPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(UTILITY_URI,
    util_instantiate, util_connect_port,
    nullptr, util_run, nullptr, util_cleanup, nullptr)
