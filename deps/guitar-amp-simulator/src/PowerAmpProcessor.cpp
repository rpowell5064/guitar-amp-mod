#include "PowerAmpProcessor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Tube model constants
// ─────────────────────────────────────────────────────────────────────────────

// 6L6GC — Fender-style pentode. Clean, warm, generous headroom.
// Lower driveScale and minimal asymmetry give the characteristic even-order
// "warmth" without aggressive odd-harmonic crunch.
const PowerAmpProcessor::TubeParams PowerAmpProcessor::k6L6GC = {
    /* driveScale  */ 2.5f,
    /* biasShift   */ 0.020f,
    /* dcOffset    */ 0.0f,   // computed in recalcTubeParams
    /* screenComp  */ 0.08f,
    /* cathodeComp */ 0.12f,
    /* outputGain  */ 0.90f,
    /* sagDepth    */ 0.35f,
    /* sagAttackMs */ 12.0f,
    /* sagRelMs    */ 200.0f,
    /* rippleHz    */ 60.0f,  // USA mains
    /* xfmrHPHz   */ 30.0f,
    /* xfmrHPQ    */ 0.60f,
    /* xfmrLPHz   */ 22000.0f,
    /* spkrResHz  */ 95.0f,
    /* spkrResQ   */ 1.4f,
    /* spkrLPHz   */ 6500.0f   // NOTE: spkrPeak/spkrLP are currently never applied
                               // in process() — see PowerAmpProcessor::process.
};

// EL34 — Marshall-style pentode. Asymmetric compression, mid-focus, British sag.
// Higher biasShift and screenComp produce the positive-peak clipping that gives
// the "crunch" character under heavy playing.
const PowerAmpProcessor::TubeParams PowerAmpProcessor::kEL34 = {
    /* driveScale  */ 3.5f,
    /* biasShift   */ 0.050f,
    /* dcOffset    */ 0.0f,
    /* screenComp  */ 0.18f,
    /* cathodeComp */ 0.25f,
    /* outputGain  */ 0.85f,
    /* sagDepth    */ 0.45f,
    /* sagAttackMs */ 8.0f,
    /* sagRelMs    */ 180.0f,
    /* rippleHz    */ 100.0f, // British 50Hz AC → 100Hz rectified
    /* xfmrHPHz   */ 40.0f,  // tighter LF — Marshall transformers ring more
    /* xfmrHPQ    */ 0.80f,
    /* xfmrLPHz   */ 18000.0f,
    /* spkrResHz  */ 85.0f,
    /* spkrResQ   */ 1.6f,
    /* spkrLPHz   */ 5500.0f   // NOTE: spkrPeak/spkrLP are currently never applied.
};

// EL84 — Vox-style pentode. Early breakup, pronounced sag, bright top-end.
// The higher cathodeComp and sagDepth model the EL84's sensitivity to grid bias
// shift under signal — the "vocal" quality of these tubes.
const PowerAmpProcessor::TubeParams PowerAmpProcessor::kEL84 = {
    /* driveScale  */ 3.0f,
    /* biasShift   */ 0.030f,
    /* dcOffset    */ 0.0f,
    /* screenComp  */ 0.15f,
    /* cathodeComp */ 0.35f,
    /* outputGain  */ 0.95f,
    /* sagDepth    */ 0.55f,  // Vox's known for heavy PSU sag
    /* sagAttackMs */ 5.0f,   // faster — smaller filter caps
    /* sagRelMs    */ 80.0f,
    /* rippleHz    */ 100.0f,
    /* xfmrHPHz   */ 35.0f,
    /* xfmrHPQ    */ 0.65f,
    /* xfmrLPHz   */ 22000.0f,
    /* spkrResHz  */ 100.0f,
    /* spkrResQ   */ 1.2f,
    /* spkrLPHz   */ 7000.0f
};

// KT88 — Hiwatt-style beam tetrode. Very clean, stiff supply, huge headroom.
// Minimal asymmetry and compression give the "wall of sound" character —
// the KT88 barely clips even at extreme drive, then falls off hard.
const PowerAmpProcessor::TubeParams PowerAmpProcessor::kKT88 = {
    /* driveScale  */ 2.0f,
    /* biasShift   */ 0.010f,
    /* dcOffset    */ 0.0f,
    /* screenComp  */ 0.05f,
    /* cathodeComp */ 0.08f,
    /* outputGain  */ 1.00f,
    /* sagDepth    */ 0.20f,  // stiff Hiwatt supply
    /* sagAttackMs */ 20.0f,
    /* sagRelMs    */ 350.0f,
    /* rippleHz    */ 100.0f,
    /* xfmrHPHz   */ 25.0f,  // extended low end
    /* xfmrHPQ    */ 0.55f,
    /* xfmrLPHz   */ 20000.0f,
    /* spkrResHz  */ 90.0f,
    /* spkrResQ   */ 1.1f,
    /* spkrLPHz   */ 7500.0f
};

// ─────────────────────────────────────────────────────────────────────────────
// Tube waveshaper — static, no state, safe to call at oversampled rate
// ─────────────────────────────────────────────────────────────────────────────
void PowerAmpProcessor::prepareLtp() noexcept {
    if (sampleRate <= 0.0) return;   // prepare() will run this again with a valid rate
    ltpAtt_ = 1.0f - static_cast<float>(std::exp(-1.0 / (ltpAttMs_ * 1e-3 * sampleRate)));
    ltpRel_ = 1.0f - static_cast<float>(std::exp(-1.0 / (ltpRelMs_ * 1e-3 * sampleRate)));
}

float PowerAmpProcessor::tubeWaveshaper(float x, const TubeParams& p, float xover,
                                        float duty, float ltpBias) noexcept {
    // Shift input to the cathode bias operating point.
    // A positive biasShift displaces the load-line intersection upward,
    // making positive-half-cycle clipping occur earlier than negative.
    // ltpBias (item #29) rides the SAME offset: a long-tailed-pair's shared
    // tail resistor pulls both grids colder in common-mode as combined drive
    // rises, so it's computed as an EXTRA, level-dependent bias term in
    // process() and simply added here alongside the fixed biasShift.
    const float xs = x + p.biasShift + ltpBias;

    // (duty is handled as STATEFUL flat-top droop in process() — memoryless offsets
    // here were measured inert on already-squared preamp signals: a two-level input
    // has fixed zero-crossings, so no static shaper can shift its duty cycle.)
    (void)duty;

    // Primary soft saturation via hyperbolic tangent.
    float y = std::tanh(p.driveScale * xs);

    // Screen grid compression: the screen draws extra current on positive swings,
    // pulling down the plate voltage faster — creates harder positive-peak rolloff.
    if (y > 0.0f)
        y = y / (1.0f + p.screenComp * y);

    // Cathode bias compression: symmetric 2nd-order softening models the
    // bias shift that occurs as the cathode resistor voltage rises under signal.
    y = y / (1.0f + p.cathodeComp * y * y);

    // Class-AB crossover (item 25): a soft dead-zone near y≈0 where both push-pull
    // tubes sit near cutoff — small-signal gain drops to (1−xover) and the zero
    // crossing gains the characteristic kink (crossover grit, "dirty when quiet").
    // Odd function, f(0)=0, so it adds no DC. xover 0 → bit-identical.
    if (xover > 0.0f) {
        constexpr float kDz = 0.06f;   // crossover dead-zone width
        y -= xover * kDz * std::tanh(y / kDz);
    }

    return (y - p.dcOffset) * p.outputGain;
}

// ─────────────────────────────────────────────────────────────────────────────
// recalcTubeParams: select constant set and pre-compute dcOffset so that
// tubeWaveshaper(0) == 0 after compensation.
// ─────────────────────────────────────────────────────────────────────────────
void PowerAmpProcessor::recalcTubeParams() noexcept {
    switch (tubeType) {
    case TubeType::Tube_6L6GC: tp = k6L6GC; break;
    case TubeType::Tube_EL34:  tp = kEL34;  break;
    case TubeType::Tube_EL84:  tp = kEL84;  break;
    case TubeType::Tube_KT88:  tp = kKT88;  break;
    }
    // Evens desk-loop (phase 2): scale the waveshaper's two intrinsic asymmetry
    // levers. Both are inert on a railed input (two-level theorem), but with the
    // per-amp paDrive reductions the shaper works in its curved region where
    // they generate phase-locked (frequency-robust) even harmonics. 1.0/1.0 =
    // stock constants = bit-identical.
    tp.screenComp *= screenScale_;
    tp.biasShift  *= biasScale_;
    // Compute the raw output at x=0 so we can null the DC offset (at no crossover;
    // any residual DC the crossover adds is removed by the transformer HP downstream).
    tp.dcOffset = 0.0f;
    tp.dcOffset = tubeWaveshaper(0.0f, tp, 0.0f) / tp.outputGain; // before outputGain
    // Verify: tubeWaveshaper(0, tp) should now be ≈ 0.
}

// ─────────────────────────────────────────────────────────────────────────────
// recalcFilters: recompute all biquad coefficients from current params + tp
// ─────────────────────────────────────────────────────────────────────────────
void PowerAmpProcessor::recalcFilters() {
    const double sr   = sampleRate;
    const double osr  = sr * 2.0;   // oversampled rate

    // 8th-order Butterworth anti-alias filter (2026-07-27, replaces the old 4th-order
    // pair — see the OsFilter comment in the header). Prewarped bilinear design,
    // identical construction to OversamplingWrapper::computeAACoeffs (factor=2 here,
    // vs 4 there); kCutoffFrac=0.90 matches the old aaFC = sr*0.45 target exactly
    // (0.45*sr = 0.90 * (sr/2) = 0.90 * native Nyquist).
    constexpr double kCutoffFrac = 0.90;
    const double K  = std::tan(M_PI * kCutoffFrac / (2.0 * 2.0));  // factor_ = 2
    const double K2 = K * K;
    auto makeSOS = [&](double Q) -> BiquadCoeffs {
        const double D = K2 + K / Q + 1.0;
        return { K2 / D, 2.0 * K2 / D, K2 / D, 2.0 * (K2 - 1.0) / D, (K2 - K / Q + 1.0) / D };
    };
    // Pole Q's: Q_k = 1/(2*cos((2k+1)*pi/16)), k = 0..3 -> {0.5098, 0.6013, 0.9000, 2.5629}
    const BiquadCoeffs sos[4] = {
        makeSOS(1.0 / (2.0 * std::cos(1.0 * M_PI / 16.0))),
        makeSOS(1.0 / (2.0 * std::cos(3.0 * M_PI / 16.0))),
        makeSOS(1.0 / (2.0 * std::cos(5.0 * M_PI / 16.0))),
        makeSOS(1.0 / (2.0 * std::cos(7.0 * M_PI / 16.0))),
    };
    for (int c = 0; c < kMaxCh; ++c) {
        upAA[c].s0.setCoeffs(sos[0]);   upAA[c].s1.setCoeffs(sos[1]);
        upAA[c].s2.setCoeffs(sos[2]);   upAA[c].s3.setCoeffs(sos[3]);
        downAA[c].s0.setCoeffs(sos[0]); downAA[c].s1.setCoeffs(sos[1]);
        downAA[c].s2.setCoeffs(sos[2]); downAA[c].s3.setCoeffs(sos[3]);
    }

    // NFB high-pass: presence knob raises the cutoff, reducing NFB in the upper
    // mids/highs — same mechanism as a presence capacitor in a real amp's NFB loop.
    const double nfbCutoff = 400.0 + presence * 3200.0; // 400–3600 Hz
    const BiquadCoeffs nfbC = Filters::highpass(nfbCutoff, 0.707, sr);
    for (int c = 0; c < kMaxCh; ++c)
        nfbHP[c].setCoeffs(nfbC);

    // Output transformer: high-pass with slight resonance models the LF
    // leakage inductance and core saturation onset; low-pass models winding
    // capacitance / leakage inductance on the HF side.
    const BiquadCoeffs xfmrHPC = Filters::highpass(tp.xfmrHPHz, tp.xfmrHPQ, sr);
    const BiquadCoeffs xfmrLPC = Filters::lowpass(std::min(static_cast<double>(tp.xfmrLPHz), sr * 0.45), 0.707, sr);
    for (int c = 0; c < kMaxCh; ++c) {
        xfmrHP[c].setCoeffs(xfmrHPC);
        xfmrLP[c].setCoeffs(xfmrLPC);
    }

    // Gentle pre-saturation LP (2026-07-27, low-risk aliasing mitigation — see the
    // header comment): fixed at 0.72 * native Nyquist, well above any tone-shaping
    // role, purely to trim the alias-prone top octave feeding the nonlinearity.
    const BiquadCoeffs preSatLPC = Filters::lowpass(sr * 0.36, 0.707, sr);
    for (int c = 0; c < kMaxCh; ++c)
        preSatLP[c].setCoeffs(preSatLPC);

    // Speaker impedance: peaking at cone resonance (scaled by resonance knob),
    // plus LP for voice-coil inductance.  The amplitude of the resonance peak
    // is controlled by the damping factor (inversely proportional to NFB amount).
    const double spkrPeakDB = 3.0 + resonance * 5.0;   // +3 to +8 dB
    const BiquadCoeffs spkrPeakC = Filters::peaking(tp.spkrResHz, spkrPeakDB,
                                                     tp.spkrResQ, sr);
    const BiquadCoeffs spkrLPC   = Filters::lowpass(tp.spkrLPHz,  0.707, sr);
    for (int c = 0; c < kMaxCh; ++c) {
        spkrPeak[c].setCoeffs(spkrPeakC);
        spkrLP[c].setCoeffs(spkrLPC);
        cplShelf[c].setCoeffs(Filters::highshelf(2400.0, 4.5, sr));   // voice-coil rise
    }

    // Presence EQ: high-shelf around 2–6 kHz, amount ±8 dB centred at 0.5.
    const double presDB = (presence - 0.5) * 16.0;
    const BiquadCoeffs presC = Filters::highshelf(2500.0, presDB, sr);
    for (int c = 0; c < kMaxCh; ++c)
        presEQ[c].setCoeffs(presC);

    // Depth EQ: low-shelf around 80–200 Hz.  The depth knob adds LF body.
    const double depthDB = (depth - 0.5) * 12.0;
    const BiquadCoeffs depthC = Filters::lowshelf(120.0, depthDB, sr);
    for (int c = 0; c < kMaxCh; ++c)
        depthEQ[c].setCoeffs(depthC);

    // LF bloom LP filter at ~150 Hz — extracts low-frequency energy for
    // the dynamic "air push" enhancement.
    const BiquadCoeffs bloomC = Filters::lowpass(150.0, 0.707, sr);
    for (int c = 0; c < kMaxCh; ++c)
        bloomLP[c].setCoeffs(bloomC);

    // Sag envelope time constants.
    sagAttackCoef = static_cast<float>(std::exp(-1.0 / (tp.sagAttackMs   * 0.001 * sr)));
    sagRelCoef    = static_cast<float>(std::exp(-1.0 / (tp.sagReleaseMs  * 0.001 * sr)));

    // Bloom envelope smoothing (~100ms).
    bloomEnvCoef = static_cast<float>(std::exp(-1.0 / (0.100 * sr)));

    // Flux-domain OT integrator pole — corner ~25 Hz (just below the OT LF rolloff)
    // so the guitar LF band accumulates flux and saturates before the highs (item 26).
    fluxPole_ = static_cast<float>(std::exp(-2.0 * M_PI * 25.0 / sr));

    // LTP tail-coupling envelope (item #29): fast, tail-resistor-scale time
    // constants — much quicker than sag's power-supply-scale envelope, since a
    // real tail resistor's RC is dominated by a small bypass cap, not the B+
    // reservoir. 2 ms attack lets the envelope track pick transients; 8 ms
    // release keeps it from chasing individual cycles at guitar fundamentals.
    ltpAtt_ = 1.0f - static_cast<float>(std::exp(-1.0 / (ltpAttMs_ * 1e-3 * sr)));
    ltpRel_ = 1.0f - static_cast<float>(std::exp(-1.0 / (ltpRelMs_ * 1e-3 * sr)));

    // Post-saturation sag-VCA envelope: slow-ish 2.5 ms attack lets the pick
    // transient overshoot before the VCA clamps (= bloom); 13 ms release sets the
    // recovery τ (matches the JCM800 capture's ~13 ms).
    bloomVcaAttCoef = static_cast<float>(std::exp(-1.0 / (0.0025 * sr)));
    cplAtt = 1.0f - static_cast<float>(std::exp(-1.0 / (0.015 * sr)));   // coupling env: 15 ms up
    cplRel = 1.0f - static_cast<float>(std::exp(-1.0 / (0.250 * sr)));   // 250 ms down
    bloomVcaRelCoef = static_cast<float>(std::exp(-1.0 / (bloomVcaRelMs * 0.001 * sr)));

    // Early reflection tap lengths in samples (1.7ms / 4.1ms / 8.3ms).
    erTap[0] = std::min(static_cast<int>(0.0017 * sr), kERBufLen - 1);
    erTap[1] = std::min(static_cast<int>(0.0041 * sr), kERBufLen - 1);
    erTap[2] = std::min(static_cast<int>(0.0083 * sr), kERBufLen - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// prepare
// ─────────────────────────────────────────────────────────────────────────────
void PowerAmpProcessor::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;

    // Dual-corner coupling corners for the duty mechanism (at 2x OS rate), tuned in
    // tools/evens_harness vs the Rockerverb capture: fast HP 600 Hz (curves the +
    // flat-top), slow HP 3 Hz (− half ≈ untouched), sharp 4 kHz sign selector.
    const double osr = sr * 2.0;
    dcKA   = static_cast<float>(1.0 - std::exp(-2.0 * M_PI * 600.0  / osr));
    dcKB   = static_cast<float>(1.0 - std::exp(-2.0 * M_PI *   3.0  / osr));
    dcKSgn = static_cast<float>(1.0 - std::exp(-2.0 * M_PI * 4000.0 / osr));

    for (int c = 0; c < kMaxCh; ++c) {
        upAA[c].reset();
        downAA[c].reset();
        nfbHP[c].reset();
        xfmrHP[c].reset(); xfmrLP[c].reset(); preSatLP[c].reset();
        spkrPeak[c].reset(); spkrLP[c].reset();
        cplShelf[c].reset(); cplEnv[c] = 0.0f;
        presEQ[c].reset(); depthEQ[c].reset();
        bloomLP[c].reset();
        nfbPrev[c]      = 0.0f;
        xfmrSatState[c] = 0.0f;
        fluxState[c]    = 0.0f;
        fluxSatPrev[c]  = 0.0f;
        bloomEnv[c]     = 0.0f;
        bloomVcaEnv[c]  = 0.0f;
        dcLpA[c] = 0.0f; dcLpB[c] = 0.0f; dcSgn[c] = 0.0f;
        ltpEnv[c] = 0.0f;
    }
    sagEnv    = 0.0f;
    ripplePhase = 0.0f;
    erWritePos  = 0;
    std::memset(erBuf, 0, sizeof(erBuf));

    recalcTubeParams();
    recalcFilters();
}

// ─────────────────────────────────────────────────────────────────────────────
// setTubeType
// ─────────────────────────────────────────────────────────────────────────────
void PowerAmpProcessor::setTubeType(TubeType type) noexcept {
    tubeType = type;
    recalcTubeParams();
    // recalcFilters() uses tp.xfmrHPHz etc., so must follow recalcTubeParams.
    recalcFilters();
}

// ─────────────────────────────────────────────────────────────────────────────
// setParameter / getParameter
// ─────────────────────────────────────────────────────────────────────────────
void PowerAmpProcessor::setParameter(const std::string& id, float v) {
    const float c01 = std::clamp(v, 0.0f, 1.0f);
    bool needFilters = false;

    if      (id == "tubeType") { setTubeType(static_cast<TubeType>(static_cast<int>(v))); return; }
    else if (id == "presence") { presence  = c01; needFilters = true; }
    else if (id == "depth")    { depth     = c01; needFilters = true; }
    else if (id == "sag")      { sagAmount = c01; }
    else if (id == "bloomvca") { bloomVcaDepth = c01; }
    else if (id == "bloomrelms") {
        bloomVcaRelMs = std::clamp(v, 5.0f, 300.0f);
        if (sampleRate > 0.0)
            bloomVcaRelCoef = static_cast<float>(std::exp(-1.0 / (bloomVcaRelMs * 0.001 * sampleRate)));
    }
    else if (id == "master")   { masterVol = c01; }
    else if (id == "nfb")      { nfbAmount = c01; needFilters = true; }
    else if (id == "resonance"){ resonance = c01; needFilters = true; }
    else if (id == "coupling") { coupling  = c01; }
    else if (id == "airFeel")  { airFeelOn = v > 0.5f; }
    else if (id == "xover")    { xoverDepth_ = c01; }       // class-AB crossover (Phase-2)
    else if (id == "duty")     { duty_ = c01; }             // push-pull duty asymmetry (even harmonics)
    else if (id == "evengen")  { evenGen_ = c01; }          // post-distortion even-harmonic exciter
    else if (id == "padrive")  { paDrive_  = std::clamp(v, 0.25f, 8.0f); }  // PA distortion drive
    else if (id == "pamakeup") { paMakeup_ = std::clamp(v, 0.1f,  4.0f); }  // PA level restore
    else if (id == "fluxOT")   { fluxOT_   = v > 0.5f; }    // flux-domain OT saturation (Phase-2)
    else if (id == "fluxshear"){ fluxShear_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    else if (id == "ripplesag"){ rippleSagCoupling_ = std::max(0.0f, v); }  // item #27, 0 = off
    else if (id == "ltptail")  { ltpTail_ = std::max(0.0f, v); }            // item #29, 0 = off
    else if (id == "pascreen") { screenScale_ = std::max(0.0f, v); recalcTubeParams(); }  // evens desk-loop
    else if (id == "pabias")   { biasScale_   = std::max(0.0f, v); recalcTubeParams(); }  // evens desk-loop
    else if (id == "ltpatt")   { ltpAttMs_ = std::max(0.05f, v); prepareLtp(); }  // evens desk-loop (ms)
    else if (id == "ltprel")   { ltpRelMs_ = std::max(0.05f, v); prepareLtp(); }  // evens desk-loop (ms)

    if (needFilters)
        recalcFilters();
}

float PowerAmpProcessor::getParameter(const std::string& id) const {
    if (id == "tubeType") return static_cast<float>(static_cast<int>(tubeType));
    if (id == "presence") return presence;
    if (id == "depth")    return depth;
    if (id == "sag")      return sagAmount;
    if (id == "bloomvca") return bloomVcaDepth;
    if (id == "master")   return masterVol;
    if (id == "nfb")      return nfbAmount;
    if (id == "resonance")return resonance;
    if (id == "coupling") return coupling;
    if (id == "airFeel")  return airFeelOn ? 1.0f : 0.0f;
    if (id == "xover")    return xoverDepth_;
    if (id == "duty")     return duty_;
    if (id == "evengen")  return evenGen_;
    if (id == "padrive")  return paDrive_;
    if (id == "pamakeup") return paMakeup_;
    if (id == "fluxOT")   return fluxOT_ ? 1.0f : 0.0f;
    if (id == "fluxshear") return fluxShear_;
    if (id == "ripplesag")return rippleSagCoupling_;
    if (id == "ltptail")  return ltpTail_;
    return 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// getDefaultsForModel — per-amp calibrated power-amp defaults
// ─────────────────────────────────────────────────────────────────────────────
PowerAmpProcessor::AmpDefaults
PowerAmpProcessor::getDefaultsForModel(int idx) noexcept {
    //                           master  presence  depth   nfb     sag    bloomVca
    // bloomVca = post-saturation sag-VCA depth, tuned per amp vs the JFE captures:
    //   JCM800 wants bloom (NAM 1.53 dB) + recovery (13 ms); Fender's pushed 6V6 sags
    //   hard (driven comp gap) but stays clean when quiet (env-gated); Rockerverb a
    //   touch; EVH already blooms in its own preamp (matches), so 0 — no double-count.
    switch (idx) {
        // rippleSagCoupling deliberately left at 0 (item #27 rollout, 2026-07-28): this
        // row is SHARED across Fender/Hiwatt/Vox (all tube) AND Peavey Backline (SOLID-
        // STATE, kCanonical idx 9 -> row 0) -- a solid-state supply doesn't have the
        // tube-rectifier ripple-modulated "ghost note" character, so adding it here
        // would incorrectly color Backline. Needs the row split (see the existing
        // "kCanonical rows are SHARED" note) before Fender/Hiwatt/Vox can get their own
        // ripple value without also hitting the solid-state amp.
        case 0: return { 0.58f,  0.10f,  0.08f,  0.82f,  0.74f,  0.15f }; // Fender Deluxe Reverb AB763
        // rippleSagCoupling 0.02 (item #27 pilot, 2026-07-28): current-dependent mains
        // ripple, JCM800 only for now -- offline harness can't score "sounds more
        // authentic" (the AM sidebands are inharmonic to the test tone, invisible to
        // the FR/THD/harmonic/feel sections; confirmed byte-identical there), so this
        // needs the user's ears before going wider. See AMP-REVOICE-NOTES.md.
        // ltpTail 0.15 (item #29 pilot, 2026-07-28): JCM800 is one of the three amps
        // PhaseInverter.cpp documents as genuinely LTP-topology (kMarshall_LTP) --
        // the natural pilot for the dynamic tail-coupling mechanism.
        case 1: return { 0.62f,  0.55f,  0.18f,  0.42f,  0.33f,  0.36f,  0.0f, 1.0f, 1.0f, 0.02f, 0.15f }; // Marshall JCM800 2203
        // rippleSagCoupling 0.012 (item #27 rollout, 2026-07-28): EVH already has its
        // OWN internal sag/bloom (bloomVca=0 above to avoid double-counting that), so
        // kept modest here too -- the PA-level ripple is meant to layer a subtle mains
        // texture on top, not compete with EVH's already-tuned dynamics.
        // ltpTail 0.12 (item #29 rollout, 2026-07-28): one of PhaseInverter.cpp's three
        // documented LTP amps (kEVH_LTP); kept slightly below JCM800's pilot value for
        // the same "don't compete with EVH's already-heavily-tuned dynamics" reason.
        // EVH paDrive 0.30 / paMakeup 1.25 (2026-07-29, user: EVH reads weakest
        // post-overhauls): the model's hot output was driving the shared PA so
        // deep into saturation that it crushed ~80% of any post-limiter EQ AND
        // the dynamics -- measured: backing the PA waveshaper drive off moved
        // EVERY metric toward the head-only Red capture simultaneously (2k FR
        // -7.7->-4.5, 5k -5.7->-1.9, THD@1k 58->72 toward the real 93, THD@110
        // 30->24 toward 16, bloom -2.37->-1.72). Makeup restores loudness
        // (loudness-neutral pair); sag/NFB/ripple character all still active.
        case 2: return { 0.38f,  0.63f,  0.72f,  0.61f,  0.29f,  0.00f,  0.0f, 0.30f, 1.25f, 0.012f, 0.12f, false }; // EVH 5150 III (fluxOT OFF -- see AmpDefaults.fluxOT)
        case 3: return { 0.50f,  0.50f,  0.50f,  0.50f,  0.50f,  0.00f }; // NAM neutral -- left at 0: user-supplied captures may already carry real ripple color, or may not; don't guess
        case 4: return { 0.71f,  0.44f,  0.82f,  0.19f,  0.21f,  0.00f }; // Sunn Model T (own 6550) -- PA is bypassed for Sunn, so rippleSagCoupling here is inert either way
        // rippleSagCoupling 0.022 (item #27 rollout): highest sag (0.47) of the EL34
        // rows besides Fender -> proportionally the most ripple depth.
        // ltpTail 0.18 (item #29 rollout, 2026-07-28): the third of PhaseInverter.cpp's
        // documented LTP amps (kOrange_LTP); highest of the three, matching Rockerverb
        // also having the highest sag/bloomVca of the group.
        // Rockerverb paDrive/paMakeup/ltpTail re-tuned 2026-07-29 (PA evens
        // phase 2, AMP-REVOICE-NOTES.md): the railed waveshaper was re-squaring
        // away the even harmonics the preamp already generates. paDrive 0.4
        // keeps the shaper in its curved region; ltpTail 3.0 turns the LTP
        // grid-bias envelope ripple into the h2 generator (hits the dimed
        // capture's h2 at BOTH 111/223 Hz); paMakeup restores loudness parity.
        case 5: return { 0.54f,  0.32f,  0.66f,  0.28f,  0.47f,  0.15f,  0.0f, 0.4f, 1.60f, 0.022f, 3.0f, true, 0.40f }; // Orange Rockerverb 100 MKII [2026-08-04: paMakeup 1.75->1.60 to cancel the even-exciter's +0.8 dB -> loudness-neutral, no preset re-level needed] [2026-08-03: duty NO-OP (dimed preamp swamps PA evens) -> post-distortion even exciter 0.40 instead; h2 matched + h4/h6 doubled (partial -- extreme h4/h6~20 still wants PA-first-class re-arch); +0.9 dB -> re-level presets]
        // NOTE (2026-07-26): duty 0.45 was tried here for the dimed capture's even-rich
        // profile (h2 17/h4 13/h6 11%) and measured NO effect — the PA contributes so
        // little distortion vs the preamp (35% THD) that PA evens dilute to ~nothing.
        // The evens gap needs the PA driven as a first-class distortion contributor
        // (gain-staging re-architecture + full preset re-level) — supervised project.
        // rippleSagCoupling 0.018 (item #27 rollout): EL34, moderate sag (0.35).
        case 6: return { 0.60f,  0.50f,  0.30f,  0.40f,  0.35f,  0.36f,  0.0f, 2.5f, 1.0f, 0.018f, 0.0f, true, 0.30f }; // Friedman BE-Deluxe [2026-08-04 "more gain": paDrive 1.0->2.5 flattens the saturation curve (soft playing 40->44% THD = more consistently driven) at stable level. THD CEILING ~48% (both preamp softLimit + PA waveshaper are smooth clips); the capture's flat 53% needs power-amp CROSSOVER = the bigger project] (Beardo BE) — EL34; bloomVca 0.36 = HBE bloom matches NAM exactly (tested: lower over-sags nothing, just loses HBE bloom)
        // rippleSagCoupling 0.010 (item #27 rollout): TIGHT supply (bloom only 4 dB
        // vs the capture) -> proportionally less ripple depth than JCM800/Rockerverb.
        case 7: return { 0.55f,  0.45f,  0.55f,  0.30f,  0.25f,  0.05f,  0.15f, 1.0f, 1.0f, 0.010f }; // Mesa Dual Rectifier (Diamond Plate) [2026-08-03 duty 0.15: PA distorts here, evens rise toward capture] — 6L6, low NFB (Vintage baseline; Modern modes get a host-side nfb≈0.05 override), deep lows, TIGHT supply (capture bloom only 4 dB); variac/rect feel lives in the model's own sag VCA
        // rippleSagCoupling 0.006 (item #27 rollout): tightest/most percussive amp in
        // the suite (sag 0.15) -> smallest ripple depth, so it doesn't fight the
        // deliberately tight/snappy character.
        case 8: return { 0.55f,  0.50f,  0.45f,  0.60f,  0.15f,  0.05f,  0.30f, 1.0f, 1.0f, 0.006f }; // PRS MT15 (Tremont 15) — STRONG NFB / tight damping, minimal sag (percussive recovery); 6L6 pair on the real amp
        // Vox AC30 split off the shared clean row 0 (2026-07-29, LF-THD round 2):
        // same voicing fields as row 0, but paDrive 0.3 + fluxOT OFF -- the
        // shared PA's railed shaper + flux-OT grind were the "second re-clipper"
        // that re-distorted any LF the model restored (THD@110 45% vs the
        // capture's 24; probes: paDrive 0.4+flux-off = 23.9 exact at -12,
        // paDrive 0.2 = 26.0 exact at -6, so 0.3 splits the rungs). An AC30 has
        // NO negative feedback and a single-ended-feeling cathode-biased EL84
        // class-A output -- flux-off + low drive is also the physically right
        // shape. paMakeup restores loudness parity (measured).
        // sag 0.74 -> 0.50 (same session): the un-railed shaper passes supply
        // wobble the old rail used to clamp -- bloom grew +2.0 -> +5.5 dB vs
        // the capture. Unlike Rockerverb, the sag lever WORKS here (measured
        // monotone); 0.50 restores the pre-change bloom (+1.85 vs +2.03) with
        // THD unchanged, keeping the famous AC30 springy sag at its prior level.
        // paDrive 0.3->0.45 / paMakeup 1.11->1.07 (same day, idle-noise re-fit):
        // the chimePk cut (see VoxAC30Model.cpp) removed 10 kHz energy that had
        // been acting as a hidden PA-sag exciter, un-compressing the whole
        // measurement -- drive re-raised to put THD@110 back astride the
        // capture (27.3/36.5 vs 24.2/26.1) with the re-trimmed model EQ.
        // nfb 0.82 -> 0.05 (same day, THE ACTUAL WHINE FIX): with flux OT off
        // and paDrive 0.45, the NFB loop SELF-OSCILLATES at 5749 Hz (-21 dBFS!)
        // in the full plugin context -- reproduced with PURE SILENCE input on
        // preset recall ("goes off just landing on the preset, nothing plugged
        // in"). The 0.82 was inherited from the shared Fender row; a real AC30
        // has NO global negative feedback at all, so near-zero is also the
        // physically correct topology. Re-measured vs capture after.
        case 9: return { 0.58f,  0.10f,  0.08f,  0.05f,  0.50f,  0.15f,  0.12f, 0.45f, 1.07f, 0.0f, 0.0f, false }; // Vox AC30 Top Boost
        default: return { 0.50f, 0.50f,  0.50f,  0.50f,  0.50f,  0.00f };
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// process — main audio callback
// ─────────────────────────────────────────────────────────────────────────────
void PowerAmpProcessor::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

    const int chCount = std::min(nCh, kMaxCh);

    // ── Pre-compute sag per native-rate sample (mono sidechain from ch0) ──────
    // The supply droops based on the RMS-like envelope of the signal entering
    // the power tubes. We use ch0 as the sidechain. Result is stored as a gain
    // factor in sagGain[], applied per-sample in the per-channel loop below.
    static thread_local std::vector<float> sagGain;
    if (static_cast<int>(sagGain.size()) < numSamples)
        sagGain.resize(static_cast<size_t>(numSamples));

    // Power supply ripple phase step per native sample.
    const float rippleStep = static_cast<float>(2.0 * M_PI * tp.rippleHz / sampleRate);

    for (int i = 0; i < numSamples; ++i) {
        // Envelope follows the absolute value of ch0 input.
        const float absIn = std::abs(in[0][i]);
        if (absIn > sagEnv)
            sagEnv = sagAttackCoef * sagEnv + (1.0f - sagAttackCoef) * absIn;
        else
            sagEnv = sagRelCoef * sagEnv + (1.0f - sagRelCoef) * absIn;

        // Supply voltage factor: drops proportionally to sag envelope.
        // Small ripple from the rectified mains rides on the droop. Item #27
        // (2026-07-28): rectifier ripple physically grows with current draw, so
        // add extra depth proportional to the sag envelope on top of the fixed
        // quiescent term -- rippleSagCoupling_ 0 (default, every amp) keeps this
        // identical to the old fixed -50 dBFS term.
        const float rippleAmt = 0.003f + rippleSagCoupling_ * sagEnv; // base -50 dBFS + current-dependent growth
        const float ripple = rippleAmt * std::sin(ripplePhase);
        const float vSupply = 1.0f
                            - sagAmount * tp.sagDepth * sagEnv
                            + ripple * (1.0f - sagAmount * sagEnv);
        sagGain[i] = std::max(0.25f, vSupply); // floor at 25% to avoid silence

        ripplePhase += rippleStep;
        if (ripplePhase >= static_cast<float>(2.0 * M_PI))
            ripplePhase -= static_cast<float>(2.0 * M_PI);
    }

    // ── Per-channel processing ────────────────────────────────────────────────
    // Item #24 (2026-07-28): NFB now wraps the WHOLE power stage — sag/upsample,
    // tube waveshaper, downsample, output transformer, and speaker-impedance
    // coupling all sit INSIDE the loop, closing sample-by-sample around the
    // nonlinearity instead of correcting the already-clipped output after the
    // fact. This is a genuine restructure (was 5 separate whole-block passes;
    // is now 1 unified per-sample loop, since the feedback for sample i needs
    // sample i-1's FULLY processed output before sample i's upsample/waveshaper
    // can run). All the per-sample stateful math (duty dual-corner, flux OT,
    // speaker-coupling envelope) is UNCHANGED and still processed in the exact
    // same relative order — only the NFB tap POINT moved, from after the
    // waveshaper to before it, with the feedback source now the fully-processed
    // output (including the speaker-impedance tap, physically the transformer-
    // secondary/speaker-facing signal a real amp's loop actually derives from).
    // Presence/depth EQ (Step 7) stays OUTSIDE the loop, same as before — a
    // real amp's tone controls sit downstream of the power section, not inside
    // its feedback path.
    const float nfbScale = nfbAmount * 0.35f; // max ~35% feedback depth (unchanged)
    for (int ch = 0; ch < chCount; ++ch) {
        for (int i = 0; i < numSamples; ++i) {
            // NFB: subtract a HP-filtered portion of the PREVIOUS sample's fully-
            // processed output from this sample's drive, before the nonlinearity.
            const float fb    = nfbHP[ch].process(nfbPrev[ch]) * nfbScale;
            const float drive = in[ch][i] * sagGain[i] - fb;

            // LTP tail coupling (item #29): fast envelope of the drive signal,
            // a proxy for "how hard both grids are swinging together" absent a
            // literal two-path PI split. Bias grows colder (more negative) as
            // the envelope rises — the shared tail resistor's common-mode
            // voltage pulling both grids down together under heavier drive.
            const float driveAbs = std::fabs(drive);
            ltpEnv[ch] += (driveAbs > ltpEnv[ch] ? ltpAtt_ : ltpRel_) * (driveAbs - ltpEnv[ch]);
            const float ltpBias = -ltpTail_ * 0.15f * ltpEnv[ch];

            // Step 1: 2× upsample (zero-insertion + AA LP), one native sample at
            // a time — same math as before, just no longer a separate whole-
            // block pass.
            float osA = 2.0f * upAA[ch].process(drive);
            float osB = 2.0f * upAA[ch].process(0.0f);

            // Step 2: tube waveshaper at 2× oversampled rate.
            osA = tubeWaveshaper(paDrive_ * osA, tp, xoverDepth_, duty_, ltpBias) * paMakeup_;
            osB = tubeWaveshaper(paDrive_ * osB, tp, xoverDepth_, duty_, ltpBias) * paMakeup_;
            // Dual-corner asymmetric coupling (duty mechanism) — unchanged math,
            // now applied inline to this sample's two OS sub-samples in the same
            // relative order (osA then osB) as the old whole-block pass.
            if (duty_ > 0.0f) {
                float& la = dcLpA[ch]; float& lb = dcLpB[ch]; float& sg = dcSgn[ch];
                float osv[2] = { osA, osB };
                for (int k = 0; k < 2; ++k) {
                    const float x = osv[k];
                    la += dcKA   * (x - la);
                    lb += dcKB   * (x - lb);
                    sg += dcKSgn * ((x > 0.0f ? 1.0f : 0.0f) - sg);
                    const float mixed = sg * (x - la) + (1.0f - sg) * (x - lb);
                    osv[k] = x + duty_ * (mixed - x);
                }
                osA = osv[0]; osB = osv[1];
            }

            // Step 3: downsample 2× — LP filter at OS rate then decimate.
            const float s0 = downAA[ch].process(osA);
            /*discard*/      downAA[ch].process(osB);
            float y = s0;

            // Step 5: output transformer model.
            // HP models LF rolloff / resonance from primary inductance.
            // LP models HF rolloff from leakage inductance + winding capacitance.
            // A soft tanh saturation models iron-core saturation at high levels.
            {
                float s = xfmrHP[ch].process(y);
                s       = xfmrLP[ch].process(s);
                s       = preSatLP[ch].process(s);
                if (!fluxOT_) {
                    xfmrSatState[ch] = 0.97f * xfmrSatState[ch] + 0.03f * s;
                    s = std::tanh(s + 0.15f * xfmrSatState[ch]);
                    s /= std::tanh(1.15f);
                } else {
                    const float a    = fluxPole_;
                    const float flux = a * fluxState[ch] + s;
                    // Shear blend (see fluxShear_ in the header): the linear
                    // term keeps d(sat)/d(flux) >= fluxShear_ so the
                    // differentiate step below can never cancel to silence in
                    // deep saturation; small-signal slope stays exactly 1.
                    const float nl   = std::tanh(flux * fluxDrive_) / fluxDrive_;
                    const float sat  = fluxShear_ * flux + (1.0f - fluxShear_) * nl;
                    s                = sat - a * fluxSatPrev[ch];
                    fluxState[ch]    = flux;
                    fluxSatPrev[ch]  = sat;
                }
                y = s;
            }

            // Step 6: speaker-impedance coupling ("coupling" 0..1, default 0 =
            // OFF, bit-identical). Shaped path = cone-resonance peak + voice-
            // coil HF-rise shelf, blended in following a drive envelope.
            if (coupling > 0.0f) {
                float z = spkrPeak[ch].process(y);
                z = cplShelf[ch].process(z);
                const float a = std::fabs(y);
                cplEnv[ch] += (a > cplEnv[ch] ? cplAtt : cplRel) * (a - cplEnv[ch]);
                float dr = cplEnv[ch] * 1.6f; if (dr > 1.0f) dr = 1.0f;
                const float amt = coupling * (0.35f + 0.65f * dr);
                y = y + amt * (z - y);
            }

            // NFB feedback tap: the fully-processed output (post transformer +
            // speaker coupling) is what closes the loop for the NEXT sample.
            nfbPrev[ch] = y;
            // Post-distortion even-harmonic exciter (see AmpDefaults.evenDepth): |y| is
            // rich in h2/h4/h6; adding a DC-blocked copy restores the even-order warmth
            // of a heavily driven push-pull output. AFTER the NFB tap, so it colors the
            // output without feeding the loop. evenGen_ = 0 -> bit-identical.
            if (evenGen_ > 0.0f) {
                const float ar = std::fabs(y);
                rectLp_[ch] += 0.001f * (ar - rectLp_[ch]);   // slow mean -> DC removal
                y += evenGen_ * (ar - rectLp_[ch]);
            }
            out[ch][i]  = y;
        }

        // Step 7: presence and depth EQ — downstream of the power section,
        // outside the NFB loop (unchanged from before).
        for (int i = 0; i < numSamples; ++i) {
            float s    = presEQ[ch].process(out[ch][i]);
            s          = depthEQ[ch].process(s);
            out[ch][i] = s;
        }
    }

    // ── Amp-in-the-room (early reflections + LF bloom) ───────────────────────
    if (airFeelOn) {
        // Early reflection gains: -30 / -36 / -42 dBFS relative to dry signal.
        // These are room first-reflections that give the "pushing air" sensation
        // without adding audible reverb character.
        static constexpr float kERGain[3] = { 0.032f, 0.016f, 0.008f };

        for (int i = 0; i < numSamples; ++i) {
            for (int ch = 0; ch < chCount; ++ch) {
                // Write dry signal into ER delay line.
                erBuf[ch][erWritePos] = out[ch][i];

                // Read delayed taps and mix in at very low level.
                float er = 0.0f;
                for (int t = 0; t < 3; ++t) {
                    const int readPos = (erWritePos - erTap[t] + kERBufLen) % kERBufLen;
                    er += erBuf[ch][readPos] * kERGain[t];
                }
                out[ch][i] += er;

                // LF bloom: dynamic low-frequency enhancement that models a
                // speaker cone moving significant air volume.
                const float absS = std::abs(out[ch][i]);
                bloomEnv[ch] = bloomEnvCoef * bloomEnv[ch] + (1.0f - bloomEnvCoef) * absS;
                const float bloomMix   = bloomEnv[ch] * 0.05f; // max 5% blend
                const float bloomSig   = bloomLP[ch].process(out[ch][i]);
                out[ch][i] += (bloomSig - out[ch][i]) * bloomMix;
            }
            // Advance circular buffer write position once per native-rate sample.
            erWritePos = (erWritePos + 1) % kERBufLen;
        }
    }

    // ── Post-saturation sag VCA (per-amp bloom + recovery) ───────────────────
    // Applied AFTER the waveshaper so the limiter can't re-normalise it away (the
    // pre-saturation sag above is masked — verified with nam_compare). Pre-master so
    // the depth tracks tube-side level, not the volume knob. Env-gated: stays clean
    // when quiet, compresses + blooms when driven. Depth 0 ⇒ no-op (EVH/Sunn/NAM).
    if (bloomVcaDepth > 0.0f) {
        for (int ch = 0; ch < chCount; ++ch)
            for (int i = 0; i < numSamples; ++i) {
                const float a = std::abs(out[ch][i]);
                float& e = bloomVcaEnv[ch];
                e = (a > e) ? (bloomVcaAttCoef * e + (1.0f - bloomVcaAttCoef) * a)
                            : (bloomVcaRelCoef * e + (1.0f - bloomVcaRelCoef) * a);
                out[ch][i] *= std::max(1.0f - bloomVcaDepth * e, 0.3f); // floor ≈ -10 dB
            }
    }

    // ── Master volume ─────────────────────────────────────────────────────────
    for (int ch = 0; ch < chCount; ++ch)
        for (int i = 0; i < numSamples; ++i)
            out[ch][i] *= masterVol;

    // ── Copy-through any extra channels beyond kMaxCh ─────────────────────────
    for (int ch = chCount; ch < nCh; ++ch)
        if (in[ch] != out[ch])
            std::copy(in[ch], in[ch] + numSamples, out[ch]);
}
