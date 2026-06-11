#pragma once
#include "BiquadFilter.h"
#include <vector>
#include <cmath>
#include <cstdint>

// Generates a synthetic Celestion Vintage 30 (4×12, closed-back) close-miked
// with a Shure SM57 — the de-facto standard modern guitar cab tone, used when
// no user IR is loaded.
//
// Approach: a unit impulse filtered through a V30+SM57 frequency-response
// biquad stack, so the IR's magnitude response is exactly the designed voicing
// (smooth, no random notches — a single noise realisation rippled the curve and
// drifted the peaks). This is also a fair model of a close-miked single speaker,
// which has little comb filtering. The cascade's natural ringing supplies the
// short decay; the tail is faded out to avoid a truncation click.
//
// Character — tight closed-back chunk, scooped lower mids, the V30's forward
// upper-mid "honk" ~1.9 kHz, the SM57/cone-breakup bite ~4 kHz, and a steep top
// that dies ~5 kHz (bright but fizz-free) — without requiring an IR file.
//
// Length ~40 ms (scales with sampleRate so the response is SR-independent).
namespace DefaultCabIR {

inline std::vector<float> generate(double sampleRate) {
    const int len = static_cast<int>(sampleRate * 0.040);

    // ── Unit impulse excitation ───────────────────────────────────────────────
    std::vector<float> ir(static_cast<size_t>(len), 0.0f);
    ir[0] = 1.0f;

    // ── V30 + SM57 frequency shaping ─────────────────────────────────────────
    // Closed-back LF rolloff: collapses below ~70 Hz, slight resonant lift to
    // keep the low-end "chunk".
    BiquadFilter hp;
    hp.setCoeffs(Filters::highpass(72.0, 1.0, sampleRate));

    // Closed-back cabinet/speaker resonance: tight thump ~100 Hz (+2.5 dB).
    BiquadFilter thump;
    thump.setCoeffs(Filters::peaking(100.0, 2.5, 1.2, sampleRate));

    // V30 scooped lower mids ~440 Hz (-3 dB) — the classic "scoop".
    BiquadFilter scoop;
    scoop.setCoeffs(Filters::peaking(440.0, -3.0, 1.0, sampleRate));

    // Subtle nasal dip ~750 Hz (-1.5 dB).
    BiquadFilter nasal;
    nasal.setCoeffs(Filters::peaking(750.0, -1.5, 1.6, sampleRate));

    // V30 signature: forward upper-mid "honk"/cut ~1.9 kHz (+4.5 dB).
    BiquadFilter honk;
    honk.setCoeffs(Filters::peaking(1900.0, 4.5, 1.0, sampleRate));

    // SM57 presence + cone-breakup bite ~4 kHz (+3 dB) — the on-axis "edge".
    BiquadFilter bite;
    bite.setCoeffs(Filters::peaking(4000.0, 3.0, 1.8, sampleRate));

    // Steep HF rolloff — two cascaded LP stages (~24 dB/oct). The speaker dies
    // ~5 kHz: bright enough for V30 sizzle, steep enough to kill preamp fizz.
    BiquadFilter lp1, lp2;
    lp1.setCoeffs(Filters::lowpass(5200.0, 0.7, sampleRate));
    lp2.setCoeffs(Filters::lowpass(5200.0, 1.0, sampleRate));

    for (int i = 0; i < len; ++i) {
        float x = ir[i];
        x = hp.process(x);
        x = thump.process(x);
        x = scoop.process(x);
        x = nasal.process(x);
        x = honk.process(x);
        x = bite.process(x);
        x = lp1.process(x);
        x = lp2.process(x);
        ir[i] = x;
    }

    // ── Raised-cosine fade over the last 25% (kill any truncation click) ──────
    const int fade = len / 4;
    for (int i = 0; i < fade; ++i) {
        const float w = 0.5f * (1.0f + std::cos(static_cast<float>(M_PI) * i / fade));
        ir[len - fade + i] *= w;
    }

    // ── Normalise to 50% peak ─────────────────────────────────────────────────
    float peak = 0.0f;
    for (float v : ir) peak = std::max(peak, std::abs(v));
    if (peak > 0.0f) {
        const float scale = 0.5f / peak;
        for (float& v : ir) v *= scale;
    }

    return ir;
}

} // namespace DefaultCabIR
