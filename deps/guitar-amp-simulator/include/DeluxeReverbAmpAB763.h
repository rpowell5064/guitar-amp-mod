#pragma once
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "BiquadFilter.h"
#include "DR_SpringReverb.h"
#include "DR_Tremolo.h"
#include "DR_PowerAmpSection.h"
#include "DR_OutputTransformer.h"
#include "DR_SpeakerCabinet.h"

// ─────────────────────────────────────────────────────────────────────────────
// DeluxeReverbAmpAB763 — complete component model of the 1965 Fender Deluxe
//                        Reverb (AB763 circuit revision, Vibrato channel)
// ─────────────────────────────────────────────────────────────────────────────
//
// AB763 signal path (Vibrato channel):
//
//   Input
//     │
//     ├─[35 Hz HPF: grid coupling cap]
//     │
//     ├─[V1A: 12AX7 first gain stage]   ← kFenderV1 LUT model
//     ├─[40 Hz HPF: inter-stage coupling]
//     │
//     ├─[V1B: 12AX7 second gain stage]  ← kFenderV2 LUT model
//     ├─[40 Hz HPF: output coupling]
//     │  (V1A+V1B run at 2× oversampled rate)
//     │
//     ├─[ToneStack: Bass / Middle / Treble — Fender mid-scoop passive network]
//     │
//     ├─[Volume: 1 MΩ pot, with optional Bright cap (220 pF across wiper)]
//     │
//     ├─[Reverb send → DR_SpringReverb → Reverb return] (native rate)
//     │
//     ├─[DR_Tremolo: opto-coupler LFO amplitude modulator] (native rate)
//     │
//     ├─[DR_PowerAmpSection:            ← 2× oversampled rate
//     │    DR_PhaseInverter (V4 LTP)
//     │    DR_PowerTube6V6 × 2 (V5, V6)
//     │    NFB loop (22 dB, shelved HP)
//     │    Sag (5AR4 tube rectifier model)]
//     │
//     ├─[DR_OutputTransformer: 125A1A]  (native rate)
//     │
//     └─[DR_SpeakerCabinet: Jensen C12N parametric EQ]
//
// OVERSAMPLING
//   Two stages are oversampled at 2× the native rate to suppress aliasing from
//   the nonlinear tube models:
//     • Preamp (V1A + V1B): most sensitive to aliasing at moderate drive.
//     • Power amp (PI + 6V6): high signal level near saturation.
//   Everything else (tonestack, reverb, tremolo, xfmr, cab) runs at native rate.
//   Toggle ENABLE_OVERSAMPLING below to disable for even lower CPU on Pi Zero.
//
// NO GAIN KNOB — consistent with the original AB763 circuit.
// Drive comes entirely from Volume, tube operating points, and signal level.
// ─────────────────────────────────────────────────────────────────────────────

// Set to 0 to disable oversampling (saves ~30% CPU — use on Pi Zero / Pi 1).
#ifndef DR_ENABLE_OVERSAMPLING
#define DR_ENABLE_OVERSAMPLING 1
#endif

class DeluxeReverbAmpAB763 final {
public:
    // ── Setup ─────────────────────────────────────────────────────────────────
    // nativeSampleRate: host sample rate (44.1, 48, 88.2, 96 kHz).
    // maxBlockSize:     maximum samples per processBlock call.
    void prepare(double nativeSampleRate, int maxBlockSize);
    void reset()  noexcept;

    // ── Per-block processing (mono) ───────────────────────────────────────────
    // in and out may alias (in-place processing is safe).
    void processBlock(const float* in, float* out, int numSamples) noexcept;

    // ── Parameters (all [0, 1] unless stated) ────────────────────────────────

    // Volume: input level to V1A.  0=silent, 1=full drive.  NO GAIN KNOB.
    // Smooth ramping is applied internally (20 ms ramp at native rate).
    void setVolume       (float v) noexcept;

    // Tonestack controls (0.5 = noon).
    void setBass         (float v) noexcept;
    void setMid          (float v) noexcept;
    void setTreble       (float v) noexcept;

    // Bright switch: adds a 220 pF cap across the Volume pot, boosting the top
    // end slightly.  Modelled as a shelved high-frequency boost.
    void setBright       (bool on) noexcept;

    // Reverb mix (0 = dry, 1 = wet).
    void setReverb       (float v) noexcept;

    // Tremolo Speed and Intensity.
    void setTremSpeed    (float v) noexcept;
    void setTremIntensity(float v) noexcept;

    // Master volume (non-original post-stage attenuator for recording use).
    // Documented as a modern addition — NOT in the original AB763.
    void setMasterVol    (float v) noexcept;

    // Sag depth (0 = stiff / new capacitors, 1 = maximum 5AR4 sag).
    void setSag          (float v) noexcept;

private:
    // ── Preamp (V1A + V1B) ───────────────────────────────────────────────────
    // Runs at oversampled rate when DR_ENABLE_OVERSAMPLING=1.
    BiquadFilter    inputHP_;    // 35 Hz HPF — input coupling cap C1
    TriodeComponent v1a_;        // First triode of V1 (12AX7)
    BiquadFilter    inter12HP_;  // 40 Hz HPF — inter-stage coupling cap C2
    TriodeComponent v1b_;        // Second triode of V1 (12AX7)
    BiquadFilter    outCoupHP_;  // 40 Hz HPF — output coupling to tonestack

    // ── Tonestack + Volume ────────────────────────────────────────────────────
    ToneStackComponent tonestack_;  // Fender mid-scoop passive network
    BiquadFilter       brightHP_;   // bright cap: HP shelved boost when bright switch on
    bool               bright_     = false;

    // Volume smoother (1-pole LP for click-free control changes).
    float volumeCurr_  = 0.5f;
    float volumeTarget_= 0.5f;
    float volumeCoef_  = 0.0f;  // 1-pole LP coeff for 20 ms ramp

    // ── Effects (native rate) ─────────────────────────────────────────────────
    DR_SpringReverb springReverb_;
    DR_Tremolo      tremolo_;

    // ── Power amp (oversampled rate) ──────────────────────────────────────────
    DR_PowerAmpSection powerAmp_;

    // ── Post-amp (native rate) ────────────────────────────────────────────────
    DR_OutputTransformer outputXfmr_;
    DR_SpeakerCabinet    cab_;

    // ── Oversampling (2×) ─────────────────────────────────────────────────────
    // Two cascaded biquad AA filters per stage (4th-order Butterworth at 0.45×fs).
    // Q values from the 4th-order Butterworth factorisation: 0.5412 and 1.3066.
    struct TwoXOS {
        BiquadFilter up0, up1;    // anti-alias on upsample path (at 2×fs)
        BiquadFilter dn0, dn1;    // anti-alias on downsample path (at 2×fs)

        void prepare(double fs_native) noexcept {
            const double fc = fs_native * 0.45;
            const double fs2 = fs_native * 2.0;
            up0.setCoeffs(Filters::lowpass(fc, 0.5412, fs2));
            up1.setCoeffs(Filters::lowpass(fc, 1.3066, fs2));
            dn0.setCoeffs(Filters::lowpass(fc, 0.5412, fs2));
            dn1.setCoeffs(Filters::lowpass(fc, 1.3066, fs2));
        }
        void reset() noexcept { up0.reset(); up1.reset(); dn0.reset(); dn1.reset(); }

        // Upsample one native-rate sample → two oversampled samples (os[0], os[1]).
        void upsample(float x, float& os0, float& os1) noexcept {
            os0 = up1.process(up0.process(x * 2.0f));
            os1 = up1.process(up0.process(0.0f));
        }
        // Downsample two oversampled samples → one native-rate sample.
        float downsample(float os0, float os1) noexcept {
            dn1.process(dn0.process(os0));
            return dn1.process(dn0.process(os1));
        }
    };

    TwoXOS preampOS_;   // oversampling for V1A + V1B
    TwoXOS powerOS_;    // oversampling for Phase Inverter + 6V6 push-pull

    double nativeFs_ = 48000.0;
};
