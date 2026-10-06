#pragma once
#include "BiquadFilter.h"
#include <cmath>

// ── Single-coil -> humbucker voicing (Input Trim) ─────────────────────────────
// Re-voices a Tele-style single coil toward one of three target bridge humbuckers
// (HB Model port). Each model is a recipe of up to 5 dB-based biquad bands plus an
// output level match (a humbucker is hotter, so it also drives the chain harder).
// `amount` (0..1) scales every dB toward 0, so amount=0 (or toggle off) is a true
// bypass for every model. Curves derived + verified against modeled pickup
// responses in tools/pickup_voicing.py (Texas Special bridge source; max err
// '59 Bucker 1.53 / Norse Hammer 2.19 / Modern Flux 1.66 dB over 80 Hz..8 kHz).
//
// Shared by the Hex Forge Input Trim block and the standalone Input Trim
// (utility) plugin — keep this the single copy so the tuned curves never drift.
struct PickupVoicer {
    enum { kMaxBands = 5 };
    enum { kGuitarBase = 2 };   // recipe(kGuitarBase + g) = the Input Trim GUITAR preset g (1 Single Coil, 2 Hot Pickups)
    enum BandKind { OFF, PEAK, LOSHELF, HISHELF };
    struct Band   { BandKind kind; double fc, dB, Q; };
    struct Recipe { Band band[kMaxBands]; double levelDb; };

    // model 0='59 Bucker (PAF), 1=Norse Hammer (Ragnarok), 2=Modern Flux (Fishman Modern)
    static const Recipe& recipe(int model) noexcept {
        static const Recipe kR[kGuitarBase + 3] = {   // 3 HB models + the 2 GUITAR presets
            // '59 Bucker — warm, mid-forward, smooth rolled-off top
            {{ {PEAK,2000.0,+2.5,0.9}, {PEAK,4500.0,-5.0,1.8}, {HISHELF,4000.0,-13.0,0.0},
               {OFF,0,0,0}, {OFF,0,0,0} }, 4.0},
            // Norse Hammer — hot ceramic: tight bass, chunky low-mids, present, dark smooth top
            {{ {LOSHELF,90.0,-2.5,0.0}, {PEAK,340.0,+2.0,0.8}, {PEAK,2000.0,+2.5,1.0},
               {PEAK,4500.0,-5.0,1.8}, {HISHELF,3800.0,-15.0,0.0} }, 7.0},
            // Modern Flux — active hi-fi: tight bass, mid scoop, broad presence dip, extended top
            {{ {LOSHELF,95.0,-2.0,0.0}, {PEAK,450.0,-3.0,1.0}, {PEAK,4800.0,-8.0,1.1},
               {HISHELF,7000.0,+1.0,0.0}, {OFF,0,0,0} }, 5.5},
            // ── GUITAR presets (Input Trim "Guitar" selector, 2026-10-05; kGuitarBase + 1/2) ──
            // Single Coil — the '59 Bucker voicing with 2 dB more level, so a single-coil bridge pickup
            // lands where a humbucker guitar does instead of reading thin.
            {{ {PEAK,2000.0,+2.5,0.9}, {PEAK,4500.0,-5.0,1.8}, {HISHELF,4000.0,-13.0,0.0},
               {OFF,0,0,0}, {OFF,0,0,0} }, 6.0},
            // Hot Pickups — a hot modern bridge humbucker (ceramic / active) brought back to a
            // PAF: the ideal |H_paf| / |H_hot| correction fitted to 0.84 dB over 80 Hz-8 kHz
            // (tools/guitar_presets.py): the crisp extended top rolled off, a touch of 3 kHz
            // body back, and the output pulled down to vintage level (-4 dB: a hot bridge
            // pickup sits 4-6 dB above a PAF; the pure model match says -1.4).
            {{ {LOSHELF,85.0,-0.8,0.0}, {PEAK,2100.0,+0.9,2.0}, {PEAK,3000.0,+2.1,1.4},
               {PEAK,5000.0,-2.4,0.7}, {HISHELF,6800.0,-3.3,0.0} }, -4.0},
        };
        return kR[(model < 0 || model > kGuitarBase + 2) ? 0 : model];
    }

    BiquadFilter f[kMaxBands];
    int    nActive  = 0;
    float  gain     = 1.0f;
    double curRate  = 0.0;
    int    curModel = -1;
    float  curAmt   = -1.0f;   // force a recompute on the first prepare

    void prepare(double sr, int model, float amount) noexcept {
        if (sr == curRate && model == curModel && amount == curAmt) return;
        curRate = sr; curModel = model; curAmt = amount;
        const Recipe& r = recipe(model);
        nActive = 0;
        for (int i = 0; i < kMaxBands; ++i) {
            const Band& b = r.band[i];
            if (b.kind == OFF) continue;
            const double g = b.dB * amount;
            BiquadCoeffs c;
            switch (b.kind) {
                case PEAK:    c = Filters::peaking  (b.fc, g, b.Q, sr); break;
                case LOSHELF: c = Filters::lowshelf (b.fc, g,      sr); break;
                case HISHELF: c = Filters::highshelf(b.fc, g,      sr); break;
                default:      break;
            }
            f[nActive++].setCoeffs(c);
        }
        gain = std::pow(10.0f, static_cast<float>(r.levelDb * amount) / 20.0f);
    }
    void reset() noexcept { for (auto& bq : f) bq.reset(); }
    float process(float x) noexcept {
        for (int i = 0; i < nActive; ++i) x = f[i].process(x);
        return x * gain;
    }
};
