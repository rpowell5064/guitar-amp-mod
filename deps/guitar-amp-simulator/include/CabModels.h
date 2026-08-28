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
// Bass cabs (2026-08-28, the Blue Liner batch — longer IRs, per-cab macro bands):
//   @bass810     Bass 8x10 Sealed — SVT-style fridge: sealed 58 Hz rolloff, punchy
//                mids, the 500-900 Hz grind, no tweeter (top dies ~4 kHz)
//   @bass410h    Bass 4x10 Horn — ported + horn: deep 35-48 Hz reach, port hump,
//                scooped 800 Hz, crossover notch ~4 kHz, horn top to 12 kHz
//   @bass210     Bass 2x10 — compact, mid-forward, articulate
//   @bass115     Bass 1x15 Flip-Top — B-15-style reflex: round, warm, tubby dip,
//                early dark top (the Motown studio voice)
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

// ── Measured-IR "anatomy" enrichment (2026-07-14) ──────────────────────────────
// A real cab IR differs from a smooth EQ cascade in four audible ways, all of which
// can be synthesized deterministically (seeded — each cab gets a stable fingerprint,
// no sampled data, still license-free):
//   1. CONE-BREAKUP RIPPLE: dozens of narrow ±dB peaks/notches across the mids — the
//      irregular "grain" your ear reads as a speaker instead of a filter curve.
//   2. MULTI-CONE / BAFFLE-EDGE REFLECTIONS: sub-millisecond taps (4 cones at unequal
//      distances to one mic + edge diffraction) — the close-miked 4x12 comb "chunk".
//   3. CLOSED-BACK BOX RETURN: a darker few-ms reflection off the back panel (or, for
//      open-back cabs, a NEGATIVE dipole-cancellation tap).
//   4. BOX AIR MODE: a high-Q low resonance that RINGS in time (the thump breathes).
// The enrichment preserves loudness (L2 renormalized) and, because the ripple
// alternates sign, the MACRO response — presets keep their tone; the texture changes.
struct Enrich {
    uint32_t seed;                       // per-cab fingerprint
    int      nRipple;                    // breakup ripple filter count
    float    ripLo, ripHi, ripDb;        // ripple band + max magnitude (alternating sign)
    int      nComb;                      // sub-ms reflection taps
    float    combMs[3], combG[3];
    float    backMs, backG, backLpHz;    // box return (negative G = open-back cancel)
    float    modeHz, modeDb, modeQ;      // air-mode resonance
    // Per-cab macro-correction band table (2026-08-28, appended for the bass
    // cabs — POSITIONAL-INIT: only append fields at the END). nullptr = the
    // legacy kBands inside enrich() → the six guitar cabs stay bit-identical.
    // Bass cabs point at kBassBands: the legacy table starts at 95 Hz, leaving
    // exactly the band where bass fundamentals live uncorrected.
    const double* bands  = nullptr;
    int           nBands = 0;
};

// Macro-correction centers for bass cabs — extends the loop down to 45 Hz.
inline const double kBassBands[11] = {45.0, 70.0, 110.0, 170.0, 260.0, 400.0,
                                      650.0, 1050.0, 1700.0, 2700.0, 4300.0};

inline uint32_t lcgNext(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
inline float    frand01(uint32_t& s) { return (lcgNext(s) >> 8) / 16777216.0f; }

inline double dftMag(const std::vector<float>& ir, double f, double sr) {
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < ir.size(); ++n) { re += ir[n] * std::cos(w * n); im -= ir[n] * std::sin(w * n); }
    return std::sqrt(re * re + im * im);
}

inline void enrich(std::vector<float>& ir, double sr, const Enrich& e) {
    const float preL2 = l2norm(ir);
    if (preL2 < 1e-12f) return;
    const std::vector<float> baseIr = ir;   // for the macro-preserving correction below
    // 1) seeded breakup ripple — log-spaced narrow peaks, alternating boost/cut
    uint32_t s = e.seed;
    std::vector<BiquadFilter> rip;
    rip.reserve(static_cast<size_t>(e.nRipple));
    for (int i = 0; i < e.nRipple; ++i) {
        const float t  = (i + 0.15f + 0.7f * frand01(s)) / static_cast<float>(e.nRipple);
        const float f  = e.ripLo * std::pow(e.ripHi / e.ripLo, t);
        const float db = ((i & 1) ? -1.0f : 1.0f) * e.ripDb * (0.35f + 0.65f * frand01(s));
        const float q  = 6.0f + 8.0f * frand01(s);
        BiquadFilter b; b.setCoeffs(Filters::peaking(f, db, q, sr)); rip.push_back(b);
    }
    for (auto& x : ir) for (auto& b : rip) x = b.process(x);
    // 2) box air mode (rings in the time domain)
    if (e.modeDb > 0.01f) {
        BiquadFilter m; m.setCoeffs(Filters::peaking(e.modeHz, e.modeDb, e.modeQ, sr));
        for (auto& x : ir) x = m.process(x);
    }
    // 3) reflections: direct + short taps + (darker) box return
    std::vector<float> out(ir);
    for (int t = 0; t < e.nComb; ++t) {
        const int d = static_cast<int>(e.combMs[t] * 0.001 * sr + 0.5);
        for (size_t i = static_cast<size_t>(d); i < ir.size(); ++i)
            out[i] += e.combG[t] * ir[i - d];
    }
    if (e.backG != 0.0f) {
        const int d = static_cast<int>(e.backMs * 0.001 * sr + 0.5);
        BiquadFilter lp; lp.setCoeffs(Filters::lowpass(e.backLpHz, 0.707, sr));
        for (size_t i = static_cast<size_t>(d); i < ir.size(); ++i)
            out[i] += e.backG * lp.process(ir[i - d]);
    }
    ir.swap(out);
    // loudness renorm FIRST (a uniform gain), so the macro correction below sees — and
    // corrects — the final level relationship against the base voicing.
    {
        const float post = l2norm(ir);
        if (post > 1e-9f) { const float g = preL2 / post; for (auto& v : ir) v *= g; }
    }
    // 4) MACRO-PRESERVING correction: the taps' broad comb nulls and the ripple's random walk
    // shift the smoothed response by several dB (measured: up to -5 dB at 2 kHz), which would
    // re-voice every preset. Measure the 1/3-oct-smoothed drift vs the pre-enrich IR at log band
    // centers and invert it with BROAD (Q~1.1) filters, ITERATED to convergence — the narrow
    // fingerprint and the temporal reflection structure survive; the tuned macro curve returns.
    {
        static const double kBands[] = {95.0, 150.0, 240.0, 400.0, 650.0, 1050.0,
                                        1500.0, 2100.0, 2900.0, 4300.0, 7000.0};
        const double* bands = e.bands ? e.bands : kBands;
        const int     nB    = e.bands ? e.nBands : static_cast<int>(sizeof(kBands) / sizeof(kBands[0]));
        for (int pass = 0; pass < 6; ++pass) {
            std::vector<BiquadFilter> fix;
            for (int bi = 0; bi < nB; ++bi) {
                const double fc = bands[bi];
                double se = 0.0, sb = 0.0;
                for (double m : {0.79, 0.89, 1.0, 1.12, 1.26}) {
                    se += dftMag(ir,     fc * m, sr);
                    sb += dftMag(baseIr, fc * m, sr);
                }
                if (sb < 1e-12) continue;
                const double d = 20.0 * std::log10(se / sb + 1e-15);
                if (std::fabs(d) > 0.2) {
                    BiquadFilter b; b.setCoeffs(Filters::peaking(fc, -0.6 * d, 1.1, sr)); fix.push_back(b);   // damped: undamped dense corrections DIVERGE
                }
            }
            if (fix.empty()) break;
            for (auto& x : ir) for (auto& b : fix) x = b.process(x);
        }
    }
}

// Per-cab fingerprints. Depths are deliberately conservative — the goal is "measured
// IR" texture under the SAME macro voicing, not a different-sounding cab.
inline const Enrich* enrichFor(const std::string& id) {
    // closed 4x12s: 3 cone/edge taps + a back-panel return; open-backs: fewer taps,
    // a negative dipole tap; ripple depth follows how "grainy" the real speaker is.
    static const Enrich kFactory = { 0xC0FFEE01u, 18, 850.0f, 6200.0f, 4.5f,
        3, {0.22f, 0.47f, 0.83f}, {0.30f, -0.22f, 0.16f}, 2.9f, 0.14f, 1800.0f, 118.0f, 1.9f, 7.0f };
    static const Enrich kVox     = { 0x5EEDA702u, 18, 650.0f, 7000.0f, 4.0f,
        2, {0.26f, 0.61f, 0.0f}, {0.24f, -0.18f, 0.0f}, 1.6f, -0.16f, 1200.0f, 132.0f, 2.2f, 5.0f };
    static const Enrich kAmerOB  = { 0x0BEAC403u, 16, 600.0f, 7400.0f, 3.8f,
        1, {0.31f, 0.0f, 0.0f}, {0.20f, 0.0f, 0.0f}, 1.9f, -0.18f, 1000.0f, 105.0f, 2.0f, 5.0f };
    static const Enrich kGreenbk = { 0x6B33D804u, 19, 800.0f, 5000.0f, 5.0f,
        3, {0.24f, 0.52f, 0.90f}, {0.31f, -0.23f, 0.17f}, 3.1f, 0.16f, 1500.0f, 110.0f, 2.0f, 6.0f };
    static const Enrich kHiwatt  = { 0xFA4EFA05u, 16, 850.0f, 7600.0f, 3.5f,
        3, {0.20f, 0.44f, 0.78f}, {0.26f, -0.19f, 0.14f}, 2.7f, 0.12f, 2000.0f, 100.0f, 2.2f, 6.0f };
    static const Enrich kDoom    = { 0xD0053B06u, 16, 700.0f, 3500.0f, 4.8f,
        3, {0.28f, 0.58f, 1.02f}, {0.32f, -0.24f, 0.18f}, 3.6f, 0.20f,  900.0f,  88.0f, 2.2f, 6.0f };
    // Bass cabs (2026-08-28): all use kBassBands so the macro correction covers
    // the fundamental range (legacy table starts at 95 Hz). 8x10 = 8 unequal
    // cones → dense combs + sealed back return + 60 Hz box mode; 410h = ported
    // 46 Hz sharp-Q port ring; 115 = single cone → sparse combs, reflex ring.
    static const Enrich kBass810  = { 0xBA55810Au, 20, 400.0f, 3500.0f, 4.5f,
        3, {0.30f, 0.66f, 1.10f}, {0.33f, -0.24f, 0.18f}, 3.4f, 0.18f, 900.0f, 60.0f, 2.2f, 7.0f,
        kBassBands, 11 };
    static const Enrich kBass410h = { 0xBA55410Bu, 18, 350.0f, 6000.0f, 4.0f,
        3, {0.28f, 0.60f, 0.14f}, {0.28f, -0.20f, 0.12f}, 3.0f, 0.10f, 700.0f, 46.0f, 2.5f, 9.0f,
        kBassBands, 11 };
    static const Enrich kBass210  = { 0xBA55210Cu, 16, 500.0f, 5000.0f, 3.8f,
        2, {0.22f, 0.48f, 0.0f},  {0.24f, -0.18f, 0.0f},  2.2f, 0.12f, 1100.0f, 75.0f, 2.0f, 6.0f,
        kBassBands, 11 };
    static const Enrich kBass115  = { 0xBA55B15Du, 14, 300.0f, 2800.0f, 4.2f,
        1, {0.35f, 0.0f, 0.0f},   {0.18f, 0.0f, 0.0f},    3.8f, 0.16f, 700.0f, 50.0f, 2.6f, 8.0f,
        kBassBands, 11 };
    if (id.empty() || id == "@factory") return &kFactory;
    if (id == "@vox2x12")     return &kVox;
    if (id == "@american-ob") return &kAmerOB;
    if (id == "@greenback")   return &kGreenbk;
    if (id == "@hiwatt")      return &kHiwatt;
    if (id == "@doom")        return &kDoom;
    if (id == "@bass810")     return &kBass810;
    if (id == "@bass410h")    return &kBass410h;
    if (id == "@bass210")     return &kBass210;
    if (id == "@bass115")     return &kBass115;
    return nullptr;
}

inline std::vector<float> generate(const std::string& id, double sr, bool enriched = true) {
    // @factory / empty / unknown → the V30+SM57 Factory Cab, now with the measured-IR
    // enrichment (same macro voicing + loudness; adds the real-speaker micro-structure).
    if (id.empty() || id == "@factory" || id[0] != '@') {
        std::vector<float> ir = DefaultCabIR::generate(sr);
        if (enriched) {
            if (const Enrich* e = enrichFor("@factory")) enrich(ir, sr, *e);
            // loudness anchor: match the plain Factory Cab exactly (the correction stage
            // inside enrich() can add net energy)
            const std::vector<float> ref = DefaultCabIR::generate(sr);
            const float g = l2norm(ir) > 1e-9f ? l2norm(ref) / l2norm(ir) : 1.0f;
            for (float& v : ir) v *= g;
        }
        return ir;
    }

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
    } else if (id == "@bass810") {
        // Bass 8x10 Sealed (SVT fridge): sealed "infinite baffle", 58 Hz-5 kHz
        // ±3 dB, punchy mid-forward, the famous 500-900 Hz grind, no tweeter.
        // Longer IR — a 45 Hz cycle is 22 ms and the fade eats the last 25%.
        lenSec = 0.10f;
        add(Filters::highpass (58.0,  0.65, sr));       // sealed 12 dB/oct alignment
        add(Filters::peaking  (100.0,  2.5, 0.9, sr));  // punch
        add(Filters::peaking  (250.0,  1.5, 1.0, sr));  // thickness
        add(Filters::peaking  (700.0,  3.5, 0.8, sr));  // THE grind (500-900 Hz)
        add(Filters::peaking  (1600.0, 1.2, 1.2, sr));  // bite
        add(Filters::lowpass  (4000.0, 1.20, sr));      // ten-inch top death (no tweeter)
        add(Filters::lowpass  (4200.0, 0.70, sr));
        add(Filters::peaking  (6000.0,-8.0, 2.0, sr));
    } else if (id == "@bass410h") {
        // Bass 4x10 Horn (ported + 1" horn ~4 kHz crossover): 48 Hz-18 kHz,
        // usable to 28 Hz, port hump, modern scoop, horn sizzle.
        lenSec = 0.11f;                                 // deepest reach of the set
        add(Filters::highpass (35.0,  0.90, sr));       // ported 24 dB/oct below tuning
        add(Filters::highpass (48.0,  0.70, sr));
        add(Filters::peaking  (55.0,   2.5, 1.4, sr));  // port hump
        add(Filters::peaking  (800.0, -3.0, 1.0, sr));  // the HLF scoop
        add(Filters::peaking  (4000.0,-3.0, 2.5, sr));  // crossover notch
        add(Filters::peaking  (7000.0, 4.0, 0.8, sr));  // horn presence
        add(Filters::lowpass  (12000.0, 0.75, sr));     // horn top
    } else if (id == "@bass210") {
        // Bass 2x10: compact, mid-forward, articulate — tight studio voice.
        lenSec = 0.10f;
        add(Filters::highpass (62.0,  0.80, sr));
        add(Filters::peaking  (120.0,  2.0, 0.9, sr));  // tight body
        add(Filters::peaking  (900.0,  2.5, 1.0, sr));  // articulation
        add(Filters::peaking  (2500.0, 2.0, 1.4, sr));  // definition
        add(Filters::lowpass  (5000.0, 1.20, sr));
        add(Filters::lowpass  (5200.0, 0.70, sr));
        add(Filters::peaking  (7000.0,-6.0, 2.5, sr));
    } else if (id == "@bass115") {
        // Bass 1x15 Flip-Top (B-15 reflex): round, warm, tubby dip, early dark
        // top — the Motown studio voice.
        lenSec = 0.10f;
        add(Filters::highpass (45.0,  0.80, sr));       // reflex reach
        add(Filters::peaking  (80.0,   3.0, 0.8, sr));  // round body
        add(Filters::peaking  (250.0,  2.0, 1.0, sr));  // warmth
        add(Filters::peaking  (1200.0,-2.0, 1.2, sr));  // the "tubby" dip
        add(Filters::lowpass  (3200.0, 1.30, sr));      // early dark top (doom-style triple)
        add(Filters::lowpass  (3200.0, 0.80, sr));
        add(Filters::lowpass  (3400.0, 0.70, sr));
        add(Filters::peaking  (5000.0,-8.0, 2.0, sr));
    } else {
        return DefaultCabIR::generate(sr);              // unknown sentinel
    }

    std::vector<float> ir = renderCascade(sr, lenSec, s);

    // Measured-IR micro-structure (loudness- and macro-preserving; see Enrich above).
    if (enriched) if (const Enrich* e = enrichFor(id)) enrich(ir, sr, *e);

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
