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
float PowerAmpProcessor::tubeWaveshaper(float x, const TubeParams& p, float xover,
                                        float duty) noexcept {
    // Shift input to the cathode bias operating point.
    // A positive biasShift displaces the load-line intersection upward,
    // making positive-half-cycle clipping occur earlier than negative.
    const float xs = x + p.biasShift;

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
    const double aaFC = sr * 0.45;  // anti-alias cutoff at 0.45 * native Nyquist

    // 4th-order Butterworth anti-alias filter (two biquad stages, Butterworth Q cascade).
    // Q1 = 1.3066, Q2 = 0.5412 — standard Butterworth pole pair decomposition for N=4.
    const BiquadCoeffs aaC0 = Filters::lowpass(aaFC, 1.3066, osr);
    const BiquadCoeffs aaC1 = Filters::lowpass(aaFC, 0.5412, osr);
    for (int c = 0; c < kMaxCh; ++c) {
        upAA[c].s0.setCoeffs(aaC0);   upAA[c].s1.setCoeffs(aaC1);
        downAA[c].s0.setCoeffs(aaC0); downAA[c].s1.setCoeffs(aaC1);
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

    // Post-saturation sag-VCA envelope: slow-ish 2.5 ms attack lets the pick
    // transient overshoot before the VCA clamps (= bloom); 13 ms release sets the
    // recovery τ (matches the JCM800 capture's ~13 ms).
    bloomVcaAttCoef = static_cast<float>(std::exp(-1.0 / (0.0025 * sr)));
    cplAtt = 1.0f - static_cast<float>(std::exp(-1.0 / (0.015 * sr)));   // coupling env: 15 ms up
    cplRel = 1.0f - static_cast<float>(std::exp(-1.0 / (0.250 * sr)));   // 250 ms down
    bloomVcaRelCoef = static_cast<float>(std::exp(-1.0 / (0.0130 * sr)));

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

    // Flat-top droop rate for the duty mechanism (~8 ms coupling tilt, at 2x OS rate).
    dutyCoef = 1.0f - std::exp(-1.0f / (float)(sr * 2.0 * 0.008));

    for (int c = 0; c < kMaxCh; ++c) {
        osBuf[c].assign(static_cast<size_t>(maxBlock) * 2, 0.0f);
        upAA[c].reset();
        downAA[c].reset();
        nfbHP[c].reset();
        xfmrHP[c].reset(); xfmrLP[c].reset();
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
        dutyEnv[c]      = 0.0f;
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
    else if (id == "master")   { masterVol = c01; }
    else if (id == "nfb")      { nfbAmount = c01; needFilters = true; }
    else if (id == "resonance"){ resonance = c01; needFilters = true; }
    else if (id == "coupling") { coupling  = c01; }
    else if (id == "airFeel")  { airFeelOn = v > 0.5f; }
    else if (id == "xover")    { xoverDepth_ = c01; }       // class-AB crossover (Phase-2)
    else if (id == "duty")     { duty_ = c01; }             // push-pull duty asymmetry (even harmonics)
    else if (id == "padrive")  { paDrive_  = std::clamp(v, 0.25f, 8.0f); }  // PA distortion drive
    else if (id == "pamakeup") { paMakeup_ = std::clamp(v, 0.1f,  4.0f); }  // PA level restore
    else if (id == "fluxOT")   { fluxOT_   = v > 0.5f; }    // flux-domain OT saturation (Phase-2)

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
    if (id == "padrive")  return paDrive_;
    if (id == "pamakeup") return paMakeup_;
    if (id == "fluxOT")   return fluxOT_ ? 1.0f : 0.0f;
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
        case 0: return { 0.58f,  0.10f,  0.08f,  0.82f,  0.74f,  0.15f }; // Fender Deluxe Reverb AB763
        case 1: return { 0.62f,  0.55f,  0.18f,  0.42f,  0.33f,  0.36f }; // Marshall JCM800 2203
        case 2: return { 0.38f,  0.63f,  0.72f,  0.61f,  0.29f,  0.00f }; // EVH 5150 III
        case 3: return { 0.50f,  0.50f,  0.50f,  0.50f,  0.50f,  0.00f }; // NAM neutral
        case 4: return { 0.71f,  0.44f,  0.82f,  0.19f,  0.21f,  0.00f }; // Sunn Model T (own 6550)
        case 5: return { 0.54f,  0.32f,  0.66f,  0.28f,  0.47f,  0.15f }; // Orange Rockerverb 100 MKII
        // NOTE (2026-07-26): duty 0.45 was tried here for the dimed capture's even-rich
        // profile (h2 17/h4 13/h6 11%) and measured NO effect — the PA contributes so
        // little distortion vs the preamp (35% THD) that PA evens dilute to ~nothing.
        // The evens gap needs the PA driven as a first-class distortion contributor
        // (gain-staging re-architecture + full preset re-level) — supervised project.
        case 6: return { 0.60f,  0.50f,  0.30f,  0.40f,  0.35f,  0.36f }; // Friedman BE-Deluxe (Beardo BE) — EL34; bloomVca 0.36 = HBE bloom matches NAM exactly (tested: lower over-sags nothing, just loses HBE bloom)
        case 7: return { 0.55f,  0.45f,  0.55f,  0.30f,  0.25f,  0.05f }; // Mesa Dual Rectifier (Diamond Plate) — 6L6, low NFB (Vintage baseline; Modern modes get a host-side nfb≈0.05 override), deep lows, TIGHT supply (capture bloom only 4 dB); variac/rect feel lives in the model's own sag VCA
        case 8: return { 0.55f,  0.50f,  0.45f,  0.60f,  0.15f,  0.05f }; // PRS MT15 (Tremont 15) — STRONG NFB / tight damping, minimal sag (percussive recovery); 6L6 pair on the real amp
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
    // the power tubes. We use ch0 as the sidechain.
    // Result is stored temporarily in osBuf[0] (first numSamples elements)
    // as a gain factor, then used during the per-channel loop.
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
        // Small ripple from the rectified mains rides on the droop.
        const float ripple = 0.003f * std::sin(ripplePhase); // -50 dBFS
        const float vSupply = 1.0f
                            - sagAmount * tp.sagDepth * sagEnv
                            + ripple * (1.0f - sagAmount * sagEnv);
        sagGain[i] = std::max(0.25f, vSupply); // floor at 25% to avoid silence

        ripplePhase += rippleStep;
        if (ripplePhase >= static_cast<float>(2.0 * M_PI))
            ripplePhase -= static_cast<float>(2.0 * M_PI);
    }

    // ── Per-channel processing ────────────────────────────────────────────────
    for (int ch = 0; ch < chCount; ++ch) {
        float* osPtr = osBuf[ch].data();

        // Step 1: apply sag gain reduction and upsample 2×.
        // Zero-insertion upsampling: for each native sample feed [x, 0] through
        // the anti-alias LP filter running at 2× rate. Multiply by 2 to
        // compensate for the signal energy halved by the zero stuffing.
        {
            int osIdx = 0;
            for (int i = 0; i < numSamples; ++i) {
                const float saggedSample = in[ch][i] * sagGain[i];
                osPtr[osIdx++] = 2.0f * upAA[ch].process(saggedSample);
                osPtr[osIdx++] = 2.0f * upAA[ch].process(0.0f);
            }
        }

        // Step 2: tube waveshaper at 2× oversampled rate.
        // Running at 2× rate reduces aliasing from the nonlinear waveshaper
        // to components above the original Nyquist, which are then filtered out.
        {
            const int osLen = numSamples * 2;
            for (int i = 0; i < osLen; ++i)
                osPtr[i] = tubeWaveshaper(paDrive_ * osPtr[i], tp, xoverDepth_, duty_) * paMakeup_;
            // Push-pull asymmetric flat-top droop (duty v2, stateful): the + half
            // droops toward its coupling network while the − half doesn't — the
            // positive flat-top TILTS, adding the even harmonics (h2/h4/h6) real
            // push-pull stages show. Zero-crossings untouched; DC-free by symmetry
            // of the droop state (removed downstream by the xfmr HP regardless).
            if (duty_ > 0.0f) {
                float& e = dutyEnv[ch];
                const float k = dutyCoef;
                for (int i = 0; i < osLen; ++i) {
                    const float pos = osPtr[i] > 0.0f ? osPtr[i] : 0.0f;
                    e += k * (pos - e);
                    osPtr[i] -= duty_ * e * (osPtr[i] > 0.0f ? 1.0f : 0.0f);
                }
            }
        }

        // Step 3: downsample 2× — LP filter at OS rate then decimate.
        // Process both even and odd OS samples through the filter to keep
        // filter state coherent; output only the even-indexed (original) samples.
        {
            int osIdx = 0;
            for (int i = 0; i < numSamples; ++i) {
                const float s0 = downAA[ch].process(osPtr[osIdx++]);
                /*discard*/      downAA[ch].process(osPtr[osIdx++]);
                out[ch][i] = s0;
            }
        }

        // Step 4: negative feedback loop.
        // A high-pass-filtered portion of the output feeds back negatively,
        // reducing distortion and output impedance. The presence knob raises
        // the HP cutoff so NFB attenuates only the very high end — the classic
        // "presence" feel of a well-tuned power amp.
        {
            const float nfbScale = nfbAmount * 0.35f; // max ~35% feedback depth
            for (int i = 0; i < numSamples; ++i) {
                const float fb  = nfbHP[ch].process(nfbPrev[ch]) * nfbScale;
                const float y   = out[ch][i] - fb;
                out[ch][i]      = y;
                nfbPrev[ch]     = y;
            }
        }

        // Step 5: output transformer model.
        // HP models LF rolloff / resonance from primary inductance.
        // LP models HF rolloff from leakage inductance + winding capacitance.
        // A soft tanh saturation models iron-core saturation at high levels.
        for (int i = 0; i < numSamples; ++i) {
            float s = xfmrHP[ch].process(out[ch][i]);
            s       = xfmrLP[ch].process(s);
            if (!fluxOT_) {
                // Original: instantaneous-voltage soft clip with a hysteresis lag.
                xfmrSatState[ch] = 0.97f * xfmrSatState[ch] + 0.03f * s;
                s = std::tanh(s + 0.15f * xfmrSatState[ch]); // soft asymmetric limit
                s /= std::tanh(1.15f); // normalise so unity gain at small signal
            } else {
                // Flux-domain core saturation (item 26): integrate → saturate flux →
                // differentiate. Self-inverting, so it is EXACTLY s until the flux
                // clips; LF (which accumulates far more flux) grinds well before HF.
                const float a    = fluxPole_;
                const float flux = a * fluxState[ch] + s;                       // ∫ (leaky)
                const float sat  = std::tanh(flux * fluxDrive_) / fluxDrive_;   // flux limit
                s                = sat - a * fluxSatPrev[ch];                   // d/dt (inverse)
                fluxState[ch]    = flux;
                fluxSatPrev[ch]  = sat;
            }
            out[ch][i] = s;
        }

        // Step 6: speaker-impedance coupling ("coupling" 0..1, default 0 = OFF,
        // bit-identical). Shaped path = the cone-resonance peak (spkrPeak — gain
        // rides the Resonance knob, frequency per tube type) + a fixed voice-coil
        // HF-rise shelf. The blend amount follows a drive envelope: at idle ~35%
        // of the dialed coupling, rising to 100% as the stage is pushed — the
        // damping collapse that makes a cranked amp thump and shimmer.
        if (coupling > 0.0f) {
            for (int i = 0; i < numSamples; ++i) {
                const float x = out[ch][i];
                float z = spkrPeak[ch].process(x);
                z = cplShelf[ch].process(z);
                const float a = std::fabs(x);
                cplEnv[ch] += (a > cplEnv[ch] ? cplAtt : cplRel) * (a - cplEnv[ch]);
                float dr = cplEnv[ch] * 1.6f; if (dr > 1.0f) dr = 1.0f;
                const float amt = coupling * (0.35f + 0.65f * dr);
                out[ch][i] = x + amt * (z - x);
            }
        }

        // Step 7: presence and depth EQ.
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
