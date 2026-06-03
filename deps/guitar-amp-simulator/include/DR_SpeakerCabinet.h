#pragma once
#include "BiquadFilter.h"
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// DR_SpeakerCabinet — Jensen C12N 12" parametric speaker model
// ─────────────────────────────────────────────────────────────────────────────
//
// The stock Deluxe Reverb uses a Jensen C12N (or equivalent Eminence) in an
// open-back pine cabinet.  The frequency response has been approximated from
// published impulse response measurements and the C12N data sheet:
//
//   1. LF highpass (cone resonance / open-back rolloff) — HP at 85 Hz, Q = 0.9.
//      The open-back cabinet removes the bass extension a closed-back would add.
//
//   2. Lower-mid rise (+2.5 dB at 250 Hz) — voice-coil inductance interaction.
//
//   3. Mid-range peak (+1.5 dB at 1.3 kHz, Q = 0.8) — cone breakup onset.
//
//   4. Presence peak (+3 dB at 3.2 kHz, Q = 1.8) — the C12N "sparkle/cut".
//      This peak is what makes the Jensen sound "forward" in a mix.
//
//   5. HF lowpass (voice-coil inductance rolloff) — LP at 6.5 kHz, 2nd order.
//      The C12N rolls off quite early compared to ceramic speakers.
//
// No IR convolution — fully parametric.  Swap SpeakerCabinet for a
// CabinetBlock IR convolver if higher accuracy is needed (at CPU cost).
//
// Pluggable design: call prepare() with a different sampleRate to rebuild
// all coefficients; no other changes required.
// ─────────────────────────────────────────────────────────────────────────────
class DR_SpeakerCabinet final {
public:
    void prepare(double sampleRate) noexcept;
    void reset()  noexcept;

    float processSample(float x) noexcept;
    void  processBlock (float* data, int numSamples) noexcept;

private:
    BiquadFilter lfHP_;       // cone resonance / open-back LF rolloff
    BiquadFilter lmRise_;     // lower-mid body (+2.5 dB @ 250 Hz)
    BiquadFilter midPeak_;    // cone breakup (+1.5 dB @ 1.3 kHz)
    BiquadFilter presPeak_;   // C12N presence peak (+3 dB @ 3.2 kHz)
    BiquadFilter hfLP_;       // voice-coil inductance HF rolloff (2nd order)
};
