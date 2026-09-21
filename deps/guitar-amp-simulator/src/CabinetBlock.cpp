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
        e.mic2LP.reset(); e.mic2Bite.reset(); e.mic2Body.reset();
        e.stHP.reset(); e.stLP.reset(); e.conA.reset(); e.conB.reset();
        e.compEnv = 0.0f; e.compRef = 0.0f;
        e.ribRing.fill(0.0f); e.distRing.fill(0.0f); e.dlyW = 0;   // item #41 rings
    }
    compAtt_  = 1.0f - std::exp(-1.0f / (0.030f * (float)sr));  // 30 ms attack
    compRel_  = 1.0f - std::exp(-1.0f / (0.180f * (float)sr));  // 180 ms release
    compSlow_ = 1.0f - std::exp(-1.0f / (1.500f * (float)sr));  // sliding reference (~1.5 s)

    // Item #40: LF-band envelope (fast, tracks note-to-note bass dynamics) vs its
    // own slower reference, and the thermal envelope (seconds-scale "how hot right
    // now" vs a tens-of-seconds "normal operating level" baseline).
    spkLfAtt_     = 1.0f - std::exp(-1.0f / (0.010f  * (float)sr));  // 10 ms
    spkLfRel_     = 1.0f - std::exp(-1.0f / (0.060f  * (float)sr));  // 60 ms
    spkLfRefCoef_ = 1.0f - std::exp(-1.0f / (1.200f  * (float)sr));  // ~1.2 s reference
    spkThAtt_     = 1.0f - std::exp(-1.0f / (2.500f  * (float)sr));  // 2.5 s (heating)
    spkThRel_     = 1.0f - std::exp(-1.0f / (6.000f  * (float)sr));  // 6 s (cooling, slower)
    spkThRefCoef_ = 1.0f - std::exp(-1.0f / (25.00f  * (float)sr));  // ~25 s baseline
    for (auto& s : spkState_) {
        s.compEnv = 0.0f; s.compRef = 0.0f;
        s.lfSplit.setCoeffs(Filters::lowpass1pole(120.0, sr));
        s.lfFastEnv = 0.0f; s.lfRef = 0.0f;
        s.hfShelf.setCoeffs(Filters::highshelf(3000.0, -3.0, sr));  // fixed max thermal-hot cut
        s.thermalEnv = 0.0f; s.thermalRef = 0.0f;
        s.lfSplit.reset(); s.hfShelf.reset();
    }
    spkNeedPrep_ = true;             // physical speaker model: re-derive for the new rate
    spkPrepareIfNeeded();
    for (auto& m : mic2_) m.reset();
    // Room buffers sized for the largest room (Amount = 1) once; lengths follow the knob.
    for (int c = 0; c < kMaxCh; ++c) {
        auto& rs = room_[c];
        static const double kCombMs[RoomState::kCombs] = {25.3, 31.7, 38.1, 44.9, 19.7, 52.3};
        const double chScale = (c == 1) ? 1.07 : 1.0;   // stereo decorrelation
        for (int k = 0; k < RoomState::kCombs; ++k) {
            const int maxLen = (int)(kCombMs[k] * 1.4 * chScale * 0.001 * sr) + 4;
            rs.comb[k].assign((size_t)maxLen, 0.0f);
            rs.cw[k] = 0; rs.damp[k] = 0.0f;
        }
        rs.ap.assign((size_t)(0.0061 * 1.07 * sr) + 4, 0.0f);
        rs.ap2.assign((size_t)(0.0089 * 1.07 * sr) + 4, 0.0f);
        rs.aw = 0; rs.aw2 = 0;
    }
    for (auto& sp : space_) { sp.ring.assign((size_t)SpaceState::kRing, 0.0f); sp.w = 0; sp.lp1.reset(); sp.lp2.reset(); }
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

void CabinetBlock::setSpeakerParams(const SpeakerParams& sp) {
    const int back = 1 - spkParamFront_.load(std::memory_order_relaxed);
    spkParams_[back] = sp;
    spkParamFront_.store(back, std::memory_order_release);
}

// Audio thread: (re)derive the speaker model when a new row was published or
// the source resistance / sample rate changed. Pure arithmetic, no allocation.
void CabinetBlock::spkPrepareIfNeeded() noexcept {
    const int front = spkParamFront_.load(std::memory_order_acquire);
    if (front == spkParamSeen_ && !spkNeedPrep_) return;
    spkParamSeen_ = front; spkNeedPrep_ = false;
    SpeakerParams sp = spkParams_[front];
    sp.rout = spkRout_;
    for (auto& m : spk_) m.prepare(sampleRate, sp);
}

// One sample of small-room: 4 damped parallel combs (prime-spaced, size-scaled) -> allpass.
float CabinetBlock::roomTick(RoomState& rs, float x) noexcept {
    return bankTick(rs, x, roomDense_ ? RoomState::kCombs : RoomState::kClassicCombs,
                    roomDense_ ? 0.2041f : 0.25f,   // 0.25*sqrt(4/6): energy-matched
                    roomFb_, roomDense_);
}

float CabinetBlock::bankTick(RoomState& rs, float x, int nCombs, float norm, float fb, bool twoAp) noexcept {
    float sum = 0.0f;
    for (int k = 0; k < nCombs; ++k) {
        float& cell = rs.comb[k][(size_t)rs.cw[k]];
        const float y = cell;
        rs.damp[k] += 0.35f * (y - rs.damp[k]);          // HF dies faster than lows (walls absorb)
        cell = x + fb * rs.damp[k];
        if (++rs.cw[k] >= rs.clen[k]) rs.cw[k] = 0;
        sum += y;
    }
    sum *= norm;
    float& acell = rs.ap[(size_t)rs.aw];
    const float v   = sum + 0.5f * acell;                 // Schroeder allpass (diffusion)
    const float apy = acell - 0.5f * v;
    acell = v;
    if (++rs.aw >= rs.alen) rs.aw = 0;
    if (!twoAp) return apy;
    float& bcell = rs.ap2[(size_t)rs.aw2];                // Dense: second diffuser
    const float v2   = apy + 0.5f * bcell;
    const float apy2 = bcell - 0.5f * v2;
    bcell = v2;
    if (++rs.aw2 >= rs.alen2) rs.aw2 = 0;
    return apy2;
}

// Space (Phase 4): one receiver of the pair — direct + image-source reflections
// (orders 1/2 through their per-bounce HF loss), then the late field from the
// dense bank fed by the second-order arrivals (a natural predelay).
float CabinetBlock::spaceTick(int c, float x) noexcept {
    auto& s = space_[c];
    s.ring[(size_t)s.w] = x;
    float d0 = 0.0f, o1 = 0.0f, o2 = 0.0f;
    for (int t = 0; t < s.nTaps; ++t) {
        const float v = s.ring[(size_t)((s.w - s.dly[t]) & SpaceState::kMask)] * s.gain[t];
        if      (s.order[t] == 0) d0 += v;
        else if (s.order[t] == 1) o1 += v;
        else                      o2 += v;
    }
    s.w = (s.w + 1) & SpaceState::kMask;
    const float er   = d0 + s.lp1.process(o1) + s.lp2.process(o2);
    const float late = bankTick(room_[c], 0.5f * o2, RoomState::kCombs, 0.2041f, spaceFb_, true);
    return 0.55f * (er + spaceLate_ * late);   // 0.55: level parity with the Classic room at the same Room Mix (measured)
}

// Geometry for the Space room. Size 0 → a 3.9 x 2.9 m booth with the pair 1.2 m
// back; Size 1 → an 8.3 x 6.3 m live room with the pair 3.5 m back. Images to
// order 2 on the mirror lattice (even n: xs + n·L, odd n: −xs + (n+1)·L), wall
// reflection 0.85 per bounce, gains normalised to the direct path (dr/dist) so
// the room signal sits where the Classic room did at the same Room Mix.
void CabinetBlock::rebuildSpace() {
    const double s  = 0.7 + 0.8 * (double)roomAmt_;
    const double Lx = 5.5 * s, Ly = 4.2 * s, Lz = 2.9 * (0.8 + 0.4 * s);
    const double sx = 1.2 * s, sy = 2.1 * s, sz = 0.5;
    const double dr = 1.2 + 2.3 * (double)roomAmt_;
    const double c0 = 343.0, rho = 0.85;
    for (int ch = 0; ch < kMaxCh; ++ch) {
        auto& sp = space_[ch];
        const double rx = sx + dr, ry = sy + (ch == 0 ? -0.3 : 0.3), rz = 1.2;
        sp.nTaps = 0;
        for (int nx = -2; nx <= 2; ++nx)
        for (int ny = -2; ny <= 2; ++ny)
        for (int nz = -2; nz <= 2; ++nz) {
            const int order = std::abs(nx) + std::abs(ny) + std::abs(nz);
            if (order > 2 || sp.nTaps >= SpaceState::kMaxTaps) continue;
            const double xi = (nx % 2 == 0) ? sx + nx * Lx : -sx + (nx + 1) * Lx;
            const double yi = (ny % 2 == 0) ? sy + ny * Ly : -sy + (ny + 1) * Ly;
            const double zi = (nz % 2 == 0) ? sz + nz * Lz : -sz + (nz + 1) * Lz;
            const double dist = std::sqrt((xi - rx) * (xi - rx) + (yi - ry) * (yi - ry) + (zi - rz) * (zi - rz));
            const int d = (int)(dist / c0 * sampleRate + 0.5);
            if (d >= SpaceState::kRing || dist < 0.05) continue;
            sp.dly[sp.nTaps]   = d;
            sp.gain[sp.nTaps]  = (float)(std::pow(rho, order) * dr / dist);
            sp.order[sp.nTaps] = order;
            ++sp.nTaps;
        }
        sp.lp1.setCoeffs(Filters::lowpass1pole(5000.0, sampleRate));
        sp.lp2.setCoeffs(Filters::lowpass1pole(3500.0, sampleRate));
    }
    // late field: Sabine RT60 of the box (mean absorption 0.25) → comb feedback
    const double V = Lx * Ly * Lz, S = 2.0 * (Lx * Ly + Ly * Lz + Lx * Lz);
    const double rt60 = 0.161 * V / (0.25 * S);
    double dMean = 0.0; int n = 0;
    for (int k = 0; k < RoomState::kCombs; ++k) if (!room_[0].comb[k].empty()) { dMean += room_[0].clen[k] / sampleRate; ++n; }
    if (n > 0) dMean /= n; else dMean = 0.035;
    spaceFb_ = (float)std::clamp(std::pow(10.0, -3.0 * dMean / rt60), 0.3, 0.85);
}

// One sample of the pre-convolution speaker-drive chain (item #40). All three
// sub-mechanisms are LEVEL-INVARIANT: each compares a fast/medium envelope
// against its OWN slower reference rather than an absolute threshold, so the
// same spkDriveAmt_ setting behaves consistently whether the cab sits after a
// line-hot fuzz or a quiet clean boost.
float CabinetBlock::spkDriveTick(SpkDriveState& s, float x) noexcept {
    const float depth = spkDriveAmt_;

    // 1) ~1.2:1 program compression (mirrors the Studio-voice bus glue's pattern,
    //    reusing its coefficients, but a much gentler ratio and small max GR).
    {
        const float a = std::fabs(x);
        s.compEnv += (a > s.compEnv ? compAtt_ : compRel_) * (a - s.compEnv);
        s.compRef += compSlow_ * (a - s.compRef);
        if (s.compEnv > s.compRef && s.compRef > 1e-6f) {
            float g = std::pow(s.compRef / s.compEnv, depth * (1.0f - 1.0f / 1.2f));
            if (g < 0.891f) g = 0.891f;   // cap ~1 dB GR at full depth
            x *= g;
        }
    }

    // 2) Level-dependent LF soft-sat below ~120 Hz (Bl droop under excursion):
    //    the LF band saturates harder specifically when the BASS itself is
    //    louder than its own recent average, not on absolute level.
    {
        const float lf = s.lfSplit.process(x);
        const float hf = x - lf;   // complementary band (approximate, fine for character)
        const float a  = std::fabs(lf);
        s.lfFastEnv += (a > s.lfFastEnv ? spkLfAtt_ : spkLfRel_) * (a - s.lfFastEnv);
        s.lfRef     += spkLfRefCoef_ * (a - s.lfRef);
        float lfExcess = 0.0f;
        if (s.lfRef > 1e-6f) lfExcess = std::max(0.0f, s.lfFastEnv / s.lfRef - 1.0f);
        const float drive = 1.0f + 2.5f * depth * std::min(1.0f, lfExcess);
        const float lfSat = std::tanh(lf * drive) / drive;
        x = lfSat + hf;
    }

    // 3) Slow thermal HF tilt: voice-coil heating dulls the top under SUSTAINED
    //    (seconds-scale) drive above the cab's recent "normal" operating level.
    {
        const float a = std::fabs(x);
        s.thermalEnv += (a > s.thermalEnv ? spkThAtt_ : spkThRel_) * (a - s.thermalEnv);
        s.thermalRef += spkThRefCoef_ * (a - s.thermalRef);
        float excess = 0.0f;
        if (s.thermalRef > 1e-6f) excess = std::max(0.0f, s.thermalEnv / s.thermalRef - 1.0f);
        const float tiltAmt = std::min(1.0f, excess) * depth;
        const float shelfOut = s.hfShelf.process(x);   // always run: keeps state warm
        x = x + tiltAmt * (shelfOut - x);
    }

    return x;
}

// Mic 2: type curve → position/distance morphs → time of flight → polarity.
float CabinetBlock::mic2Tick(Mic2State& m, float base) noexcept {
    float w = m.tA.process(base);
    w = m.tB.process(w);
    w = m.tC.process(w);
    w = m.tD.process(w);
    if (mic2PosOn_) w = m.pLP.process(w);   // at the cap the primary has no HF corner either
    w = m.pBite.process(w);
    w = m.pBody.process(w);
    w = m.dProx.process(w);
    w = m.dAir.process(w);
    m.ring[(size_t)m.w] = w;
    w = m.ring[(size_t)((m.w - mic2Delay_) & (EQState::kDlyLen - 1))];
    m.w = (m.w + 1) & (EQState::kDlyLen - 1);
    return mic2Pol_ ? -w : w;
}

void CabinetBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

    // Load front slot once — consistent L/R for the whole block, never blocks.
    const int slot    = frontSlot_.load(std::memory_order_acquire);
    const int chCount = std::min(nCh, kMaxCh);
    if (monoActive_ && chCount > 1) {
        // Returning to true stereo after the mono fast path: ch1's conv/EQ state
        // is stale — clear it (one-time; transitions coincide with user edits).
        monoActive_ = false;
        for (int sl = 0; sl < kNumSlots; ++sl) convolvers_[sl][1].reset();
        spk_[1].reset();
        mic2_[1].reset();
        auto& e = eqState_[1];
        e.lowCut.reset(); e.highCut.reset(); e.micLP.reset(); e.micBite.reset();
        e.micBody.reset(); e.distProx.reset(); e.distAir.reset();
        e.mic2LP.reset(); e.mic2Bite.reset(); e.mic2Body.reset();
        e.stHP.reset(); e.stLP.reset(); e.conA.reset(); e.conB.reset();
        e.compEnv = 0.0f; e.compRef = 0.0f;
        e.ribRing.fill(0.0f); e.distRing.fill(0.0f); e.dlyW = 0;   // item #41 rings
    }

    for (int c = 0; c < chCount; ++c) {
        // Capture dry signal before convolution (handles in-place in==out).
        std::copy(in[c], in[c] + numSamples, dryBuf_.begin());

        // Item #40: speaker-drive compression, PRE-convolution (models the cone/coil's
        // own response to being driven, not the "dry" bypass signal). Off by default.
        // 2026-09-21: the physical speaker model replaces it when engaged.
        if (spkModel_) {
            if (c == 0) spkPrepareIfNeeded();
            auto& m = spk_[c];
            for (int i = 0; i < numSamples; ++i)
                in[c][i] = m.process(in[c][i], spkVolts_);
        } else if (spkDriveOn_) {
            auto& sp = spkState_[c];
            for (int i = 0; i < numSamples; ++i)
                in[c][i] = spkDriveTick(sp, in[c][i]);
        }

        // Overlap-add convolution (writes wet to out[c]).
        convolvers_[slot][c].process(in[c], out[c], numSamples);

        // Post-EQ on wet, then wet/dry mix. Mic placement filters run only when moved
        // off the baseline (micActive_), so the default path stays bit-identical.
        if (studio_) {
            // ── Studio voice: the "recorded" chain (see header). Room forced off. ──
            auto& e = eqState_[c];
            for (int i = 0; i < numSamples; ++i) {
                float base = e.lowCut.process(out[c][i]);
                base = e.highCut.process(base);
                // primary mic = the user's placement path (controls stay live)
                float w1 = base;
                if (micActive_) {
                    w1 = e.micLP.process(w1);
                    w1 = e.micBite.process(w1);
                    w1 = e.micBody.process(w1);
                    w1 = e.distProx.process(w1);
                    w1 = e.distAir.process(w1);
                }
                float w;
                if (mic2Type_ > 0) {
                    // Phase 3: an engaged Mic 2 replaces the fixed ribbon + 35 % blend
                    float w2 = mic2Tick(mic2_[c], base);
                    e.distRing[e.dlyW] = w1;
                    w1 = e.distRing[(e.dlyW - distDelay_) & (EQState::kDlyLen - 1)];
                    e.dlyW = (e.dlyW + 1) & (EQState::kDlyLen - 1);
                    w = (1.0f - mic2Lvl_) * w1 + mic2Lvl_ * w2;
                } else {
                // second virtual mic: fixed darker ribbon-at-edge character
                float w2 = e.mic2LP.process(base);
                w2 = e.mic2Bite.process(w2);
                w2 = e.mic2Body.process(w2);
                // Item #41: real inter-mic time offsets (rings written every
                // sample; distDelay_ 0 reads the current sample = bit-identical)
                e.distRing[e.dlyW] = w1;
                e.ribRing[e.dlyW]  = w2;
                w1 = e.distRing[(e.dlyW - distDelay_)   & (EQState::kDlyLen - 1)];
                w2 = e.ribRing [(e.dlyW - ribbonDelay_) & (EQState::kDlyLen - 1)];
                e.dlyW = (e.dlyW + 1) & (EQState::kDlyLen - 1);
                w = 0.65f * w1 + 0.35f * w2;   // two mics, now honestly displaced in time
                }
                w = e.stHP.process(w);
                w = e.stLP.process(w);
                w = e.conA.process(w);
                w = e.conB.process(w);
                // Gentle bus "glue" — LEVEL-INVARIANT by design: an absolute threshold
                // can't be right across gain stagings (Hex Forge's cab point runs far
                // hotter than a standalone board). Instead compress the fast envelope
                // 2:1 toward the signal's own slow average, capped at 3 dB GR — swells
                // and pick transients are evened out, overall loudness is untouched.
                const float a = std::fabs(w);
                e.compEnv  += (a > e.compEnv ? compAtt_ : compRel_) * (a - e.compEnv);
                e.compRef  += compSlow_ * (a - e.compRef);
                float g = 1.0f;
                if (e.compEnv > e.compRef && e.compRef > 1e-6f) {
                    g = std::sqrt(e.compRef / e.compEnv);   // 2:1 above the sliding reference
                    if (g < 0.708f) g = 0.708f;             // max 3 dB gain reduction
                }
                // Static +2 dB makeup for the chain's average tonal loss (bracket +
                // darker mic-2 blend) so A/B'ing Room<->Studio compares tone, not volume.
                w *= g * 1.26f;
                out[c][i] = dryBuf_[i] * (1.0f - mix_) + w * mix_;
            }
        } else if (mic2Type_ > 0) {
            // Phase 3: Room voice with Mic 2 engaged — primary (with its own
            // placement morphs + time of flight) blended against the second mic.
            auto& e = eqState_[c];
            for (int i = 0; i < numSamples; ++i) {
                float base = e.lowCut.process(out[c][i]);
                base = e.highCut.process(base);
                float w1 = base;
                if (micActive_) {
                    w1 = e.micLP.process(w1);
                    w1 = e.micBite.process(w1);
                    w1 = e.micBody.process(w1);
                    w1 = e.distProx.process(w1);
                    w1 = e.distAir.process(w1);
                }
                e.distRing[e.dlyW] = w1;
                w1 = e.distRing[(e.dlyW - distDelay_) & (EQState::kDlyLen - 1)];
                e.dlyW = (e.dlyW + 1) & (EQState::kDlyLen - 1);
                const float w2 = mic2Tick(mic2_[c], base);
                float w = (1.0f - mic2Lvl_) * w1 + mic2Lvl_ * w2;
                if (roomOn_) w += roomMix_ * roomOut(c, w);
                out[c][i] = dryBuf_[i] * (1.0f - mix_) + w * mix_;
            }
        } else if (micActive_) {
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
                // Item #41: micDist time-of-flight (0 = bit-identical)
                e.distRing[e.dlyW] = w;
                w = e.distRing[(e.dlyW - distDelay_) & (EQState::kDlyLen - 1)];
                e.dlyW = (e.dlyW + 1) & (EQState::kDlyLen - 1);
                if (roomOn_) w += roomMix_ * roomOut(c, w);   // room rides the wet cab signal
                out[c][i] = dryBuf_[i] * (1.0f - mix_) + w * mix_;
            }
        } else if (roomOn_) {
            for (int i = 0; i < numSamples; ++i) {
                float w = out[c][i];
                w = eqState_[c].lowCut.process(w);
                w = eqState_[c].highCut.process(w);
                w += roomMix_ * roomOut(c, w);
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

void CabinetBlock::reset() noexcept {
    for (int s = 0; s < kNumSlots; ++s)
        for (int c = 0; c < kMaxCh; ++c) convolvers_[s][c].reset();
    for (auto& e : eqState_) {
        e.lowCut.reset(); e.highCut.reset();
        e.micLP.reset(); e.micBite.reset(); e.micBody.reset();
        e.distProx.reset(); e.distAir.reset();
        e.mic2LP.reset(); e.mic2Bite.reset(); e.mic2Body.reset();
        e.stHP.reset(); e.stLP.reset(); e.conA.reset(); e.conB.reset();
        e.compEnv = 0.0f; e.compRef = 0.0f;
        e.ribRing.fill(0.0f); e.distRing.fill(0.0f); e.dlyW = 0;   // item #41 rings
    }
    for (auto& rs : room_) {
        for (auto& cb : rs.comb) std::fill(cb.begin(), cb.end(), 0.0f);
        std::fill(rs.ap.begin(), rs.ap.end(), 0.0f);
        std::fill(rs.ap2.begin(), rs.ap2.end(), 0.0f);
        for (auto& d : rs.damp) d = 0.0f;
        for (auto& w : rs.cw) w = 0;
        rs.aw = 0; rs.aw2 = 0;
    }
    for (auto& s : spkState_) {
        s.compEnv = 0.0f; s.compRef = 0.0f;
        s.lfSplit.reset(); s.lfFastEnv = 0.0f; s.lfRef = 0.0f;
        s.hfShelf.reset(); s.thermalEnv = 0.0f; s.thermalRef = 0.0f;
    }
    for (auto& m : spk_) m.reset();
    for (auto& m : mic2_) m.reset();
    for (auto& sp : space_) { std::fill(sp.ring.begin(), sp.ring.end(), 0.0f); sp.w = 0; sp.lp1.reset(); sp.lp2.reset(); }
    monoActive_ = false;
}

// Mono-input fast path: ONE convolution + ONE EQ chain, fanned out to L/R with
// per-channel room (the only decorrelating element). Bit-identical to running two
// identical channels through process() at roughly half the cost.
void CabinetBlock::processMonoToStereo(float* L, float* R, int numSamples) noexcept {
    if (bypassed) { std::copy(L, L + numSamples, R); return; }
    const int slot = frontSlot_.load(std::memory_order_acquire);
    monoActive_ = true;

    std::copy(L, L + numSamples, dryBuf_.begin());
    if (spkModel_) {
        spkPrepareIfNeeded();
        auto& m = spk_[0];
        for (int i = 0; i < numSamples; ++i) L[i] = m.process(L[i], spkVolts_);
    } else if (spkDriveOn_) {
        auto& sp = spkState_[0];
        for (int i = 0; i < numSamples; ++i) L[i] = spkDriveTick(sp, L[i]);
    }
    convolvers_[slot][0].process(L, L, numSamples);
    auto& e = eqState_[0];

    if (studio_) {
        for (int i = 0; i < numSamples; ++i) {
            float base = e.lowCut.process(L[i]);
            base = e.highCut.process(base);
            float w1 = base;
            if (micActive_) {
                w1 = e.micLP.process(w1);
                w1 = e.micBite.process(w1);
                w1 = e.micBody.process(w1);
                w1 = e.distProx.process(w1);
                w1 = e.distAir.process(w1);
            }
            float w;
            if (mic2Type_ > 0) {
                float w2 = mic2Tick(mic2_[0], base);
                e.distRing[e.dlyW] = w1;
                w1 = e.distRing[(e.dlyW - distDelay_) & (EQState::kDlyLen - 1)];
                e.dlyW = (e.dlyW + 1) & (EQState::kDlyLen - 1);
                w = (1.0f - mic2Lvl_) * w1 + mic2Lvl_ * w2;
            } else {
            float w2 = e.mic2LP.process(base);
            w2 = e.mic2Bite.process(w2);
            w2 = e.mic2Body.process(w2);
            // Item #41: real inter-mic time offsets (see stereo path)
            e.distRing[e.dlyW] = w1;
            e.ribRing[e.dlyW]  = w2;
            w1 = e.distRing[(e.dlyW - distDelay_)   & (EQState::kDlyLen - 1)];
            w2 = e.ribRing [(e.dlyW - ribbonDelay_) & (EQState::kDlyLen - 1)];
            e.dlyW = (e.dlyW + 1) & (EQState::kDlyLen - 1);
            w = 0.65f * w1 + 0.35f * w2;
            }
            w = e.stHP.process(w);
            w = e.stLP.process(w);
            w = e.conA.process(w);
            w = e.conB.process(w);
            const float a = std::fabs(w);
            e.compEnv  += (a > e.compEnv ? compAtt_ : compRel_) * (a - e.compEnv);
            e.compRef  += compSlow_ * (a - e.compRef);
            float g = 1.0f;
            if (e.compEnv > e.compRef && e.compRef > 1e-6f) {
                g = std::sqrt(e.compRef / e.compEnv);
                if (g < 0.708f) g = 0.708f;
            }
            w *= g * 1.26f;
            const float y = dryBuf_[i] * (1.0f - mix_) + w * mix_;
            L[i] = y; R[i] = y;                     // studio forces room off: mono out
        }
    } else if (mic2Type_ > 0) {
        for (int i = 0; i < numSamples; ++i) {
            float base = e.lowCut.process(L[i]);
            base = e.highCut.process(base);
            float w1 = base;
            if (micActive_) {
                w1 = e.micLP.process(w1);
                w1 = e.micBite.process(w1);
                w1 = e.micBody.process(w1);
                w1 = e.distProx.process(w1);
                w1 = e.distAir.process(w1);
            }
            e.distRing[e.dlyW] = w1;
            w1 = e.distRing[(e.dlyW - distDelay_) & (EQState::kDlyLen - 1)];
            e.dlyW = (e.dlyW + 1) & (EQState::kDlyLen - 1);
            const float w2 = mic2Tick(mic2_[0], base);
            const float w = (1.0f - mic2Lvl_) * w1 + mic2Lvl_ * w2;
            const float dry = dryBuf_[i] * (1.0f - mix_);
            if (roomOn_) {
                const float rm = roomOut(0, w);
                const float rmR = monoRoom_ ? rm : roomOut(1, w);
                L[i] = dry + (w + roomMix_ * rm)  * mix_;
                R[i] = dry + (w + roomMix_ * rmR) * mix_;
            } else { const float y = dry + w * mix_; L[i] = y; R[i] = y; }
        }
    } else if (micActive_) {
        for (int i = 0; i < numSamples; ++i) {
            float w = e.lowCut.process(L[i]);
            w = e.highCut.process(w);
            w = e.micLP.process(w);
            w = e.micBite.process(w);
            w = e.micBody.process(w);
            w = e.distProx.process(w);
            w = e.distAir.process(w);
            // Item #41: micDist time-of-flight (0 = bit-identical)
            e.distRing[e.dlyW] = w;
            w = e.distRing[(e.dlyW - distDelay_) & (EQState::kDlyLen - 1)];
            e.dlyW = (e.dlyW + 1) & (EQState::kDlyLen - 1);
            const float dry = dryBuf_[i] * (1.0f - mix_);
            if (roomOn_) {
                // monoRoom (2026-08-21): when the host's output is a MONO sum,
                // summing the two DECORRELATED room banks turns their detuned
                // comb sets into a doubled static comb — broadband playing
                // (djent chugs) sweeps through it as a flanger-like "woosh"
                // (user-reported on the Periphery presets through one speaker).
                // One bank for both channels = identical room, no inter-bank
                // comb; bit-identical whenever the host doesn't set monoroom.
                const float rm = roomOut(0, w);
                const float rmR = monoRoom_ ? rm : roomOut(1, w);
                L[i] = dry + (w + roomMix_ * rm)  * mix_;
                R[i] = dry + (w + roomMix_ * rmR) * mix_;
            } else { const float y = dry + w * mix_; L[i] = y; R[i] = y; }
        }
    } else if (roomOn_) {
        for (int i = 0; i < numSamples; ++i) {
            float w = e.lowCut.process(L[i]);
            w = e.highCut.process(w);
            const float dry = dryBuf_[i] * (1.0f - mix_);
            const float rm = roomOut(0, w);              // monoRoom: see mic path above
            const float rmR = monoRoom_ ? rm : roomOut(1, w);
            L[i] = dry + (w + roomMix_ * rm)  * mix_;
            R[i] = dry + (w + roomMix_ * rmR) * mix_;
        }
    } else {
        for (int i = 0; i < numSamples; ++i) {
            float w = e.lowCut.process(L[i]);
            w = e.highCut.process(w);
            const float y = dryBuf_[i] * (1.0f - mix_) + w * mix_;
            L[i] = y; R[i] = y;
        }
    }
}

void CabinetBlock::setParameter(const std::string& id, float v) {
    if      (id == "lowCutHz")  { lowCutHz_  = v; rebuildEQ(); }
    else if (id == "highCutHz") { highCutHz_ = v; rebuildEQ(); }
    else if (id == "mix")       { mix_ = std::clamp(v, 0.0f, 1.0f); }
    else if (id == "micpos")    { micPos_  = std::clamp(v, 0.0f, 1.0f); rebuildEQ(); }
    else if (id == "micdist")   { micDist_ = std::clamp(v, 0.0f, 1.0f); rebuildEQ(); }
    else if (id == "roomon")    { roomOn_  = v > 0.5f; }
    else if (id == "monoroom")  { monoRoom_ = v > 0.5f; }   // host mono-sum hint (2026-08-21)
    else if (id == "roommix")   { roomMix_ = std::clamp(v, 0.0f, 1.0f); }
    else if (id == "roomamt")   { const float a = std::clamp(v, 0.0f, 1.0f);
                                  if (a != roomAmt_) { roomAmt_ = a; rebuildRoom(); } }
    else if (id == "voice")     { studio_ = v > 0.5f; }
    else if (id == "spkdrive")    { spkDriveOn_  = v > 0.5f; }
    else if (id == "spkdriveamt") { spkDriveAmt_ = std::clamp(v, 0.0f, 1.0f); }
    else if (id == "spkmodel")    { spkModel_ = v > 0.5f; }
    else if (id == "mic2type")    { const int t = std::clamp((int)(v + 0.5f), 0, 4);
                                    if (t != mic2Type_) { mic2Type_ = t; rebuildEQ(); } }
    else if (id == "mic2pos")     { const float x = std::clamp(v, 0.0f, 1.0f); if (x != mic2Pos_)  { mic2Pos_  = x; rebuildEQ(); } }
    else if (id == "mic2dist")    { const float x = std::clamp(v, 0.0f, 1.0f); if (x != mic2Dist_) { mic2Dist_ = x; rebuildEQ(); } }
    else if (id == "mic2lvl")     { mic2Lvl_ = std::clamp(v, 0.0f, 1.0f); }
    else if (id == "mic2align")   { const bool b = v > 0.5f; if (b != mic2Align_) { mic2Align_ = b; rebuildEQ(); } }
    else if (id == "mic2pol")     { mic2Pol_ = v > 0.5f; }
    else if (id == "spkvolts")    { spkVolts_ = std::clamp((double)v, 1.0, 2000.0); }
    else if (id == "spkrout")     { const double r = std::clamp((double)v, 0.0, 50.0);
                                    if (r != spkRout_) { spkRout_ = r; spkNeedPrep_ = true; } }
    else if (id == "roomdense") {
        const int  mode = std::clamp((int)(v + 0.5f), 0, 2);   // 0 Classic / 1 Dense / 2 Space
        const bool want = mode >= 1;
        if (want && roomMode_ < 1)   // engaging the extra elements: clear them
            for (auto& rs : room_) {
                for (int k = RoomState::kClassicCombs; k < RoomState::kCombs; ++k) {
                    std::fill(rs.comb[k].begin(), rs.comb[k].end(), 0.0f);
                    rs.cw[k] = 0; rs.damp[k] = 0.0f;
                }
                std::fill(rs.ap2.begin(), rs.ap2.end(), 0.0f);
                rs.aw2 = 0;
            }
        if (mode == 2 && roomMode_ != 2)   // entering Space: clear the reflection rings
            for (auto& sp : space_) { std::fill(sp.ring.begin(), sp.ring.end(), 0.0f); sp.w = 0; sp.lp1.reset(); sp.lp2.reset(); }
        roomMode_  = mode;
        roomDense_ = (mode == 1);
    }
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
    if (id == "voice")     return studio_ ? 1.0f : 0.0f;
    if (id == "spkdrive")    return spkDriveOn_ ? 1.0f : 0.0f;
    if (id == "spkdriveamt") return spkDriveAmt_;
    if (id == "spkmodel")    return spkModel_ ? 1.0f : 0.0f;
    if (id == "mic2type")    return (float)mic2Type_;
    if (id == "mic2pos")     return mic2Pos_;
    if (id == "mic2dist")    return mic2Dist_;
    if (id == "mic2lvl")     return mic2Lvl_;
    if (id == "mic2align")   return mic2Align_ ? 1.0f : 0.0f;
    if (id == "mic2pol")     return mic2Pol_ ? 1.0f : 0.0f;
    if (id == "spkvolts")    return (float)spkVolts_;
    if (id == "spkrout")     return (float)spkRout_;
    if (id == "roomdense") return (float)roomMode_;
    return 0.0f;
}

void CabinetBlock::rebuildRoom() {
    // Amount 0 -> tight booth (~0.12 s), 1 -> live room (~0.45 s): comb lengths scale
    // 0.6..1.4x of the base primes, feedback rises with size.
    const double scale = 0.6 + 0.8 * (double)roomAmt_;
    roomFb_ = 0.42f + 0.33f * roomAmt_;
    static const double kCombMs[RoomState::kCombs] = {25.3, 31.7, 38.1, 44.9, 19.7, 52.3};
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
        if (!rs.ap2.empty()) {
            int len = (int)(0.0089 * chScale * sampleRate);
            rs.alen2 = std::clamp(len, 1, (int)rs.ap2.size() - 1);
        }
    }
    rebuildSpace();
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
    // Item #41 (2026-07-29): real mic time-of-flight on the micDist control
    // (30 cm ~= 0.875 ms; 0 = bit-identical). A fixed ribbon-mic delay was
    // ALSO tried per the roadmap (0.4 ms on the 35% studio blend) and measured
    // via cab_voice_check: it digs ~10 dB comb notches with the first null at
    // ~1.25 kHz, straight through the ear-approved studio voicing (all four
    // spectral-signature checks failed). Any single audible delay at that
    // blend ratio notches somewhere in the passband -- the coherent blend IS
    // the studio voice's deliberate design, so the ribbon stays time-aligned
    // (ribbonDelay_ kept at 0; ring infrastructure retained).
    distDelay_   = std::min(static_cast<int>(d * 0.0009 * sampleRate + 0.5), EQState::kDlyLen - 1);
    ribbonDelay_ = 0;
    // Studio voice fixed chain: ribbon-ish second mic + bracket + console curve.
    const BiquadCoeffs m2l = Filters::lowpass (4800.0, 0.707, sampleRate);
    const BiquadCoeffs m2b = Filters::peaking (4200.0, -3.0, 1.2, sampleRate);
    const BiquadCoeffs m2y = Filters::lowshelf(260.0,   1.2, sampleRate);
    const BiquadCoeffs shp = Filters::highpass(78.0,   0.707, sampleRate);
    const BiquadCoeffs slp = Filters::lowpass (10500.0, 0.707, sampleRate);
    const BiquadCoeffs cnA = Filters::peaking (400.0,  -1.5, 0.8, sampleRate);
    // +2.2 (not the textbook +1.2): the darker mic-2 blend costs ~1 dB here — the
    // console node must WIN so the studio voice nets a slight presence lift vs Room.
    const BiquadCoeffs cnB = Filters::peaking (3200.0,  2.2, 0.9, sampleRate);
    // ── Mic 2 (Phase 3) ─────────────────────────────────────────────────────
    // Type curves are RELATIVE to the mic the IR was made with (a dynamic-57
    // class close mic on the built-ins): type 1 is therefore flat, and the
    // others carry the difference — more low end and less presence peak on a
    // 421, a ribbon's figure-8 proximity + early top rolloff, a condenser's
    // flat extended top.
    {
        const BiquadCoeffs flat = Filters::peaking(1000.0, 0.0, 1.0, sampleRate);
        BiquadCoeffs tA = flat, tB = flat, tC = flat, tD = flat;
        switch (mic2Type_) {
            case 2:   // dynamic, 421-class
                tA = Filters::lowshelf (120.0,   2.0, sampleRate);
                tB = Filters::peaking  (5500.0, -3.0, 1.0, sampleRate);
                tC = Filters::highshelf(10000.0, 1.5, sampleRate);
                break;
            case 3:   // ribbon
                tA = Filters::lowshelf (150.0,   3.0, sampleRate);
                tB = Filters::peaking  (5500.0, -4.5, 1.0, sampleRate);
                tC = Filters::lowpass  (6000.0,  0.707, sampleRate);
                tD = Filters::peaking  (4200.0, -2.0, 1.2, sampleRate);
                break;
            case 4:   // condenser
                tA = Filters::lowshelf (80.0,    1.0, sampleRate);
                tB = Filters::peaking  (5500.0, -2.0, 1.0, sampleRate);
                tC = Filters::peaking  (10000.0, 2.5, 1.0, sampleRate);
                break;
            default: break;   // 0 Off (unused) / 1 dynamic-57 = the IR's own mic
        }
        const double p2 = mic2Pos_, d2 = mic2Dist_;
        const BiquadCoeffs plp2 = Filters::lowpass (12000.0 * std::pow(3300.0 / 12000.0, p2), 0.707, sampleRate);
        const BiquadCoeffs pbt2 = Filters::peaking (4200.0, -4.5 * p2, 1.2, sampleRate);
        const BiquadCoeffs pbd2 = Filters::lowshelf(260.0,   1.5 * p2,      sampleRate);
        const BiquadCoeffs dpx2 = Filters::lowshelf (130.0, -4.5 * d2, sampleRate);
        const BiquadCoeffs dar2 = Filters::highshelf(6500.0, -2.5 * d2, sampleRate);
        const int ownDelay = std::min(static_cast<int>(d2 * 0.0009 * sampleRate + 0.5), EQState::kDlyLen - 1);
        mic2Delay_ = mic2Align_ ? distDelay_ : ownDelay;   // Align = coherent with the primary
        mic2PosOn_ = mic2Pos_ > 0.001f;
        for (auto& m : mic2_) {
            m.tA.setCoeffs(tA); m.tB.setCoeffs(tB); m.tC.setCoeffs(tC); m.tD.setCoeffs(tD);
            m.pLP.setCoeffs(plp2); m.pBite.setCoeffs(pbt2); m.pBody.setCoeffs(pbd2);
            m.dProx.setCoeffs(dpx2); m.dAir.setCoeffs(dar2);
        }
    }
    for (auto& e : eqState_) {
        e.lowCut.setCoeffs(lc);
        e.highCut.setCoeffs(hc);
        e.micLP.setCoeffs(plp);
        e.micBite.setCoeffs(pbt);
        e.micBody.setCoeffs(pbd);
        e.distProx.setCoeffs(dpx);
        e.distAir.setCoeffs(dar);
        e.mic2LP.setCoeffs(m2l); e.mic2Bite.setCoeffs(m2b); e.mic2Body.setCoeffs(m2y);
        e.stHP.setCoeffs(shp);   e.stLP.setCoeffs(slp);
        e.conA.setCoeffs(cnA);   e.conB.setCoeffs(cnB);
    }
}
