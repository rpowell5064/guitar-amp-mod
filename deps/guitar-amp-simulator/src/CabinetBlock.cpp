#include "CabinetBlock.h"
#include <algorithm>
#include <cmath>

void CabinetBlock::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;

    rebuildEQ();
    for (auto& e : eqState_) {
        e.lowCut.reset(); e.highCut.reset();
        e.micLP.reset(); e.micBite.reset(); e.micBody.reset();
        e.distProx.reset(); e.distAir.reset();
    }
    // Room buffers sized for the largest room (Amount = 1) once; lengths follow the knob.
    for (int c = 0; c < kMaxCh; ++c) {
        auto& rs = room_[c];
        static const double kCombMs[RoomState::kCombs] = {25.3, 31.7, 38.1, 44.9};
        const double chScale = (c == 1) ? 1.07 : 1.0;   // stereo decorrelation
        for (int k = 0; k < RoomState::kCombs; ++k) {
            const int maxLen = (int)(kCombMs[k] * 1.4 * chScale * 0.001 * sr) + 4;
            rs.comb[k].assign((size_t)maxLen, 0.0f);
            rs.cw[k] = 0; rs.damp[k] = 0.0f;
        }
        rs.ap.assign((size_t)(0.0061 * 1.07 * sr) + 4, 0.0f);
        rs.aw = 0;
    }
    rebuildRoom();

    dryBuf_.assign(static_cast<size_t>(maxBlock), 0.0f);

    // Prepare all convolver slots with the new block size.
    for (int s = 0; s < kNumSlots; ++s)
        for (int c = 0; c < kMaxCh; ++c)
            convolvers_[s][c].prepare(maxBlock);

    // Rebuild IR FFTs for the new block size, or install a unit impulse.
    if (storedIR_.valid) {
        for (int s = 0; s < kNumSlots; ++s) loadIRIntoSlot(s);
    } else {
        const float impulse = 1.0f;
        for (int s = 0; s < kNumSlots; ++s)
            for (int c = 0; c < kMaxCh; ++c)
                convolvers_[s][c].setIR(&impulse, 1);
    }
    frontSlot_.store(0, std::memory_order_relaxed);
}

void CabinetBlock::setIR(const std::vector<float>& irLeft,
                          const std::vector<float>* irRight) {
    // Save raw IR so prepare() can rebuild FFTs after a sample-rate change.
    storedIR_.ch[0] = irLeft;
    storedIR_.ch[1] = (irRight && !irRight->empty()) ? *irRight : irLeft;
    storedIR_.valid = true;

    // Write to the back slot (audio thread never reads the back slot).
    const int back = 1 - frontSlot_.load(std::memory_order_relaxed);
    loadIRIntoSlot(back);

    // Publish: next audio block sees the new IR.
    frontSlot_.store(back, std::memory_order_release);
}

void CabinetBlock::loadIRIntoSlot(int slot) {
    for (int c = 0; c < kMaxCh; ++c) {
        const auto& src = storedIR_.ch[c];
        convolvers_[slot][c].setIR(src.data(), static_cast<int>(src.size()));
    }
}

// One sample of small-room: 4 damped parallel combs (prime-spaced, size-scaled) -> allpass.
float CabinetBlock::roomTick(RoomState& rs, float x) noexcept {
    float sum = 0.0f;
    for (int k = 0; k < RoomState::kCombs; ++k) {
        float& cell = rs.comb[k][(size_t)rs.cw[k]];
        const float y = cell;
        rs.damp[k] += 0.35f * (y - rs.damp[k]);          // HF dies faster than lows (walls absorb)
        cell = x + roomFb_ * rs.damp[k];
        if (++rs.cw[k] >= rs.clen[k]) rs.cw[k] = 0;
        sum += y;
    }
    sum *= 0.25f;
    float& acell = rs.ap[(size_t)rs.aw];
    const float v   = sum + 0.5f * acell;                 // Schroeder allpass (diffusion)
    const float apy = acell - 0.5f * v;
    acell = v;
    if (++rs.aw >= rs.alen) rs.aw = 0;
    return apy;
}

void CabinetBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

    // Load front slot once — consistent L/R for the whole block, never blocks.
    const int slot    = frontSlot_.load(std::memory_order_acquire);
    const int chCount = std::min(nCh, kMaxCh);

    for (int c = 0; c < chCount; ++c) {
        // Capture dry signal before convolution (handles in-place in==out).
        std::copy(in[c], in[c] + numSamples, dryBuf_.begin());

        // Overlap-add convolution (writes wet to out[c]).
        convolvers_[slot][c].process(in[c], out[c], numSamples);

        // Post-EQ on wet, then wet/dry mix. Mic placement filters run only when moved
        // off the baseline (micActive_), so the default path stays bit-identical.
        if (micActive_) {
            auto& e = eqState_[c];
            for (int i = 0; i < numSamples; ++i) {
                float w = out[c][i];
                w = e.lowCut.process(w);
                w = e.highCut.process(w);
                w = e.micLP.process(w);
                w = e.micBite.process(w);
                w = e.micBody.process(w);
                w = e.distProx.process(w);
                w = e.distAir.process(w);
                if (roomOn_) w += roomMix_ * roomTick(room_[c], w);   // room rides the wet cab signal
                out[c][i] = dryBuf_[i] * (1.0f - mix_) + w * mix_;
            }
        } else if (roomOn_) {
            for (int i = 0; i < numSamples; ++i) {
                float w = out[c][i];
                w = eqState_[c].lowCut.process(w);
                w = eqState_[c].highCut.process(w);
                w += roomMix_ * roomTick(room_[c], w);
                out[c][i] = dryBuf_[i] * (1.0f - mix_) + w * mix_;
            }
        } else {
            for (int i = 0; i < numSamples; ++i) {
                float w = out[c][i];
                w = eqState_[c].lowCut.process(w);
                w = eqState_[c].highCut.process(w);
                out[c][i] = dryBuf_[i] * (1.0f - mix_) + w * mix_;
            }
        }
    }

    for (int c = chCount; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}

void CabinetBlock::setParameter(const std::string& id, float v) {
    if      (id == "lowCutHz")  { lowCutHz_  = v; rebuildEQ(); }
    else if (id == "highCutHz") { highCutHz_ = v; rebuildEQ(); }
    else if (id == "mix")       { mix_ = std::clamp(v, 0.0f, 1.0f); }
    else if (id == "micpos")    { micPos_  = std::clamp(v, 0.0f, 1.0f); rebuildEQ(); }
    else if (id == "micdist")   { micDist_ = std::clamp(v, 0.0f, 1.0f); rebuildEQ(); }
    else if (id == "roomon")    { roomOn_  = v > 0.5f; }
    else if (id == "roommix")   { roomMix_ = std::clamp(v, 0.0f, 1.0f); }
    else if (id == "roomamt")   { const float a = std::clamp(v, 0.0f, 1.0f);
                                  if (a != roomAmt_) { roomAmt_ = a; rebuildRoom(); } }
}

float CabinetBlock::getParameter(const std::string& id) const {
    if (id == "lowCutHz")  return lowCutHz_;
    if (id == "highCutHz") return highCutHz_;
    if (id == "mix")       return mix_;
    if (id == "micpos")    return micPos_;
    if (id == "micdist")   return micDist_;
    if (id == "roomon")    return roomOn_ ? 1.0f : 0.0f;
    if (id == "roommix")   return roomMix_;
    if (id == "roomamt")   return roomAmt_;
    return 0.0f;
}

void CabinetBlock::rebuildRoom() {
    // Amount 0 -> tight booth (~0.12 s), 1 -> live room (~0.45 s): comb lengths scale
    // 0.6..1.4x of the base primes, feedback rises with size.
    const double scale = 0.6 + 0.8 * (double)roomAmt_;
    roomFb_ = 0.42f + 0.33f * roomAmt_;
    static const double kCombMs[RoomState::kCombs] = {25.3, 31.7, 38.1, 44.9};
    for (int c = 0; c < kMaxCh; ++c) {
        auto& rs = room_[c];
        const double chScale = (c == 1) ? 1.07 : 1.0;
        for (int k = 0; k < RoomState::kCombs; ++k) {
            if (rs.comb[k].empty()) continue;
            int len = (int)(kCombMs[k] * scale * chScale * 0.001 * sampleRate);
            rs.clen[k] = std::clamp(len, 1, (int)rs.comb[k].size() - 1);
        }
        if (!rs.ap.empty()) {
            int len = (int)(0.0061 * chScale * sampleRate);
            rs.alen = std::clamp(len, 1, (int)rs.ap.size() - 1);
        }
    }
}

void CabinetBlock::rebuildEQ() {
    const BiquadCoeffs lc = Filters::highpass(static_cast<double>(lowCutHz_),  0.707, sampleRate);
    const BiquadCoeffs hc = Filters::lowpass (static_cast<double>(highCutHz_), 0.707, sampleRate);
    // Mic placement morphs (one-sided from the voiced baseline; see header).
    const double p = static_cast<double>(micPos_), d = static_cast<double>(micDist_);
    micActive_ = (p > 0.001f || d > 0.001f);
    // position: cap-edge -> cone edge. HF corner slides 12 kHz -> 3.3 kHz (perceptually even
    // via a log sweep), the 4.2 kHz bite recedes up to -4.5 dB, low-mid body up to +1.5 dB.
    const BiquadCoeffs plp = Filters::lowpass (12000.0 * std::pow(3300.0 / 12000.0, p), 0.707, sampleRate);
    const BiquadCoeffs pbt = Filters::peaking (4200.0, -4.5 * p, 1.2, sampleRate);
    const BiquadCoeffs pbd = Filters::lowshelf(260.0,   1.5 * p,      sampleRate);
    // distance: close -> ~30 cm. Proximity bass falls away (-4.5 dB shelf @130), slight air loss.
    const BiquadCoeffs dpx = Filters::lowshelf (130.0, -4.5 * d, sampleRate);
    const BiquadCoeffs dar = Filters::highshelf(6500.0, -2.5 * d, sampleRate);
    for (auto& e : eqState_) {
        e.lowCut.setCoeffs(lc);
        e.highCut.setCoeffs(hc);
        e.micLP.setCoeffs(plp);
        e.micBite.setCoeffs(pbt);
        e.micBody.setCoeffs(pbd);
        e.distProx.setCoeffs(dpx);
        e.distAir.setCoeffs(dar);
    }
}
