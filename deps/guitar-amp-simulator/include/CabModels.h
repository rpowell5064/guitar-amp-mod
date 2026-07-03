#pragma once
#include "BiquadFilter.h"
#include "DefaultCabIR.h"
#include <vector>
#include <cmath>
#include <string>
#include <cstring>

// ── Extra built-in Hex Forge cabinets ────────────────────────────────────────
//
// License-free SYNTHETIC cab IRs (a unit impulse through a tuned biquad cascade,
// exactly like DefaultCabIR), selected by a sentinel path ("@vox2x12", …). Each
// is LOUDNESS-matched (L2 energy) to the Factory Cab, so switching cabs changes
// TONE, never VOLUME — the generalised fix for the old V30 peak-vs-loudness bug
// (peak-normalising alone leaves cabs with different decay energy at different
// levels; matching L2 makes broadband loudness equal).
//
// Voices (close-miked look-alikes, no captured/sampled data):
//   @vox2x12     Vox-style alnico 2×12 — bright, chimey, forward upper-mids, airy top
//   @american-ob American open-back 2×12 — scooped mids, tight lows, sparkly extended top
//   @greenback   British Greenback 4×12 — warm woody mids, smooth ~5 kHz rolloff
//   @hiwatt      Hiwatt/Fane 4×12 — full-range, tight, flat/hi-fi, extended ends
//   @doom        Big dark 4×12 — huge lows, thick low-mids, early dark top (doom/stoner)
// (@factory / empty / unknown fall back to DefaultCabIR — the V30+SM57.)
namespace CabModels {

// Unit impulse through a biquad cascade → IR (magnitude response == the cascade).
// Raised-cosine fade over the last 25% to kill the truncation click.
inline std::vector<float> renderCascade(double sr, float lenSec, std::vector<BiquadFilter>& stages) {
    const int len = static_cast<int>(sr * lenSec);
    std::vector<float> ir(static_cast<size_t>(len < 1 ? 1 : len), 0.0f);
    if (len < 1) return ir;
    ir[0] = 1.0f;
    for (int i = 0; i < len; ++i) {
        float x = ir[i];
        for (auto& f : stages) x = f.process(x);
        ir[i] = x;
    }
    const int fade = len / 4;
    for (int i = 0; i < fade; ++i) {
        const float w = 0.5f * (1.0f + std::cos(static_cast<float>(M_PI) * i / fade));
        ir[len - fade + i] *= w;
    }
    return ir;
}

inline float l2norm(const std::vector<float>& v) {
    double s = 0.0; for (float x : v) s += double(x) * double(x);
    return static_cast<float>(std::sqrt(s));
}

inline std::vector<float> generate(const std::string& id, double sr) {
    // @factory / empty / unknown → the existing V30+SM57 Factory Cab.
    if (id.empty() || id == "@factory" || id[0] != '@')
        return DefaultCabIR::generate(sr);

    std::vector<BiquadFilter> s;
    auto add = [&](auto coeffs) { BiquadFilter f; f.setCoeffs(coeffs); s.push_back(f); };
    float lenSec = 0.045f;

    if (id == "@vox2x12") {
        // Vox alnico 2×12: bright & chimey. Higher LP corner (extended top), a
        // forward upper-mid/presence push, only a shallow low-mid dip (Vox is
        // mid-present, not scooped), and an alnico "sparkle" bump ~4.5 kHz.
        add(Filters::highpass (100.0, 0.80, sr));
        add(Filters::highshelf(400.0, -3.0, sr));       // gentler dark tilt than the V30
        add(Filters::peaking  (140.0,  2.0, 1.0, sr));  // body
        add(Filters::peaking  (500.0, -2.5, 1.6, sr));  // shallow low-mid dip
        add(Filters::peaking  (900.0,  1.5, 1.0, sr));  // low-mid shoulder
        add(Filters::peaking  (2600.0, 4.0, 1.3, sr));  // the CHIME / presence
        add(Filters::peaking  (4500.0, 2.5, 1.6, sr));  // alnico sparkle
        add(Filters::lowpass  (7600.0, 1.20, sr));      // extended, glassy top
        add(Filters::lowpass  (7800.0, 0.75, sr));
        add(Filters::peaking  (9200.0,-6.0, 2.5, sr));  // tame the very-top fizz
    } else if (id == "@american-ob") {
        // American open-back 2×12 (Jensen/Oxford): scooped mids, tight lows, airy
        // extended top — the blackface/surf/jangle voice.
        add(Filters::highpass (95.0,  0.70, sr));
        add(Filters::highshelf(450.0, -2.5, sr));       // bright, but not ice-picky
        add(Filters::peaking  (105.0,  1.5, 0.9, sr));  // tight body
        add(Filters::peaking  (850.0, -4.5, 1.4, sr));  // the American mid SCOOP
        add(Filters::peaking  (2000.0, 0.8, 1.2, sr));  // low-presence
        add(Filters::peaking  (3400.0, 2.2, 1.3, sr));  // sparkle/bite (tamed)
        add(Filters::lowpass  (7600.0, 1.20, sr));      // airy top, tamed shoulder
        add(Filters::lowpass  (8000.0, 0.75, sr));
        add(Filters::peaking  (9500.0,-6.0, 2.5, sr));
    } else if (id == "@greenback") {
        // Celestion Greenback 4×12: warm woody mids, smooth top (~5 kHz), classic
        // Marshall crunch. Less scoop than the V30; a mid "honk" ~1.6 kHz.
        add(Filters::highpass (86.0,  0.82, sr));
        add(Filters::highshelf(380.0, -5.0, sr));
        add(Filters::peaking  (135.0,  2.2, 1.0, sr));  // body
        add(Filters::peaking  (500.0, -3.0, 2.0, sr));  // mild scoop
        add(Filters::peaking  (1600.0, 3.2, 1.0, sr));  // woody Greenback honk
        add(Filters::peaking  (3200.0,-2.5, 2.0, sr));  // presence dip
        add(Filters::peaking  (4200.0, 1.8, 1.8, sr));  // cone bite
        add(Filters::lowpass  (5100.0, 1.40, sr));      // smooth, warm rolloff
        add(Filters::lowpass  (5100.0, 0.80, sr));
        add(Filters::lowpass  (5300.0, 0.70, sr));
    } else if (id == "@hiwatt") {
        // Hiwatt/Fane 4×12: full-range, tight, flat/hi-fi, extended ends — the
        // Gilmour platform. Least coloured of the set.
        add(Filters::highpass (74.0,  0.75, sr));       // extended, tight low
        add(Filters::highshelf(400.0, -3.5, sr));
        add(Filters::peaking  (120.0,  1.6, 0.9, sr));  // full body, not boomy
        add(Filters::peaking  (620.0, -1.8, 1.4, sr));  // gentle scoop only
        add(Filters::peaking  (1800.0, 1.2, 1.1, sr));  // even mids
        add(Filters::peaking  (3200.0, 2.2, 1.4, sr));  // clear presence
        add(Filters::lowpass  (8000.0, 1.20, sr));      // extended top (Fane is bright)
        add(Filters::lowpass  (8200.0, 0.75, sr));
        add(Filters::peaking  (9800.0,-5.0, 2.5, sr));
    } else if (id == "@doom") {
        // Big dark 4×12 (doom/stoner): huge lows, thick low-mids, dark early top.
        lenSec = 0.055f;                                // longer, boomier tail
        add(Filters::highpass (64.0,  0.90, sr));       // deep low reach
        add(Filters::highshelf(320.0, -7.0, sr));       // strongly dark tilt
        add(Filters::peaking  (95.0,   3.5, 0.9, sr));  // massive body
        add(Filters::peaking  (220.0,  1.5, 1.0, sr));  // thick low-mids
        add(Filters::peaking  (550.0, -1.5, 1.6, sr));  // barely scooped (stays thick)
        add(Filters::peaking  (1400.0, 1.0, 1.2, sr));
        add(Filters::lowpass  (4200.0, 1.40, sr));      // dark — dies early
        add(Filters::lowpass  (4200.0, 0.80, sr));
        add(Filters::lowpass  (4400.0, 0.70, sr));
        add(Filters::peaking  (6500.0,-8.0, 2.0, sr));  // kill any fizz hard
    } else {
        return DefaultCabIR::generate(sr);              // unknown sentinel
    }

    std::vector<float> ir = renderCascade(sr, lenSec, s);

    // ── Loudness-match to the Factory Cab (equal L2 energy = equal volume) ──────
    const std::vector<float> ref = DefaultCabIR::generate(sr);
    const float refL2 = l2norm(ref);
    const float myL2  = l2norm(ir);
    if (myL2 > 1e-9f && refL2 > 0.0f) {
        const float g = refL2 / myL2;
        for (float& v : ir) v *= g;
    }
    return ir;
}

// A '@' path that is NOT the factory sentinel → one of our extra synthetic cabs.
inline bool isSentinel(const char* p) {
    return p && p[0] == '@';
}

} // namespace CabModels
