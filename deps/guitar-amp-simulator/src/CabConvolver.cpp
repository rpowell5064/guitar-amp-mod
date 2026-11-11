// CabConvolver: see include/CabConvolver.h. Anagram build only. The FIR head, the MAC loop and
// the hop arithmetic are OlaConvolver.cpp's, verbatim, so the cabs' steady state is unchanged.
#ifdef HEXCHAIN_ANAGRAM
#include "CabConvolver.h"
#include "OlaConvolver.h"
#include <algorithm>
#include <cstring>

#if defined(__ARM_NEON)
#include <arm_neon.h>
namespace {
#if defined(__ARM_FEATURE_FMA)
inline float32x4_t ccMla(float32x4_t a, float32x4_t b, float32x4_t c) noexcept { return vfmaq_f32(a, b, c); }
inline float32x4_t ccMls(float32x4_t a, float32x4_t b, float32x4_t c) noexcept { return vfmsq_f32(a, b, c); }
#else
inline float32x4_t ccMla(float32x4_t a, float32x4_t b, float32x4_t c) noexcept { return vmlaq_f32(a, b, c); }
inline float32x4_t ccMls(float32x4_t a, float32x4_t b, float32x4_t c) noexcept { return vmlsq_f32(a, b, c); }
#endif
inline float ccHsum(float32x4_t v) noexcept {
#if defined(__aarch64__)
    return vaddvq_f32(v);
#else
    float32x2_t s = vadd_f32(vget_low_f32(v), vget_high_f32(v));
    s = vpadd_f32(s, s);
    return vget_lane_f32(s, 0);
#endif
}
inline float ccDot64(const float* __restrict w, const float* __restrict x) noexcept {
    float32x4_t a0 = vdupq_n_f32(0.0f), a1 = a0, a2 = a0, a3 = a0;
    for (int j = 0; j < 64; j += 16) {
        a0 = ccMla(a0, vld1q_f32(w + j),      vld1q_f32(x + j));
        a1 = ccMla(a1, vld1q_f32(w + j + 4),  vld1q_f32(x + j + 4));
        a2 = ccMla(a2, vld1q_f32(w + j + 8),  vld1q_f32(x + j + 8));
        a3 = ccMla(a3, vld1q_f32(w + j + 12), vld1q_f32(x + j + 12));
    }
    return ccHsum(vaddq_f32(vaddq_f32(a0, a1), vaddq_f32(a2, a3)));
}
} // namespace
#endif

namespace { inline int wrap(int i, int n) noexcept { i %= n; return i < 0 ? i + n : i; } }

void CabConvolver::setIR(const float* ir, int irLen) {
    irLen_ = irLen;
    std::fill(std::begin(headRev_), std::end(headRev_), 0.0f);
    const int nHead = std::min(irLen, kHop);
    for (int i = 0; i < nHead; ++i) headRev_[kHop - 1 - i] = ir[i];

    nTail_  = (irLen > kHop) ? (irLen - kHop + kHop - 1) / kHop : 0;
    spread_ = nTail_ >= kSpreadMin;
    Hre_.assign(static_cast<size_t>(nTail_) * kStride, 0.0f);
    Him_.assign(static_cast<size_t>(nTail_) * kStride, 0.0f);
    Xre_.assign(static_cast<size_t>(nTail_) * kStride, 0.0f);
    Xim_.assign(static_cast<size_t>(nTail_) * kStride, 0.0f);
    for (int p = 0; p < nTail_; ++p) {
        Cx buf[kFft] = {};
        const int base = kHop + p * kHop;
        const int len  = std::min(kHop, irLen - base);
        for (int i = 0; i < len; ++i) buf[i] = Cx(ir[base + i], 0.0f);
        OlaConvolver::fft128(buf, false);
        for (int b = 0; b < kBins; ++b) {
            Hre_[static_cast<size_t>(p) * kStride + b] = buf[b].real();
            Him_[static_cast<size_t>(p) * kStride + b] = buf[b].imag();
        }
    }
    reset();
}

void CabConvolver::reset() noexcept {
    std::fill(std::begin(hist_), std::end(hist_), 0.0f);
    std::fill(std::begin(tail_), std::end(tail_), 0.0f);
    std::fill(std::begin(pR_), std::end(pR_), 0.0f);
    std::fill(std::begin(pI_), std::end(pI_), 0.0f);
    pDone_ = 1;
    fdlPos_ = 0; valid_ = 0; hopFill_ = 0;
    live_ = true; copyPending_ = false;
}

// ── History copies ───────────────────────────────────────────────────────────
void CabConvolver::startCopy(const CabConvolver& o, int dstPos0) noexcept {
    srcRe_ = o.Xre_.data(); srcIm_ = o.Xim_.data();
    srcN_ = o.nTail_; srcPos_ = o.fdlPos_; dstPos0_ = dstPos0;
    // rows the next boundary still needs: ages 0 .. n-2 (the push overwrites age n-1)
    copyTo_ = std::min(valid_, nTail_ - 1);
    copied_ = 0;
    copyPending_ = copyTo_ > 0 && srcN_ > 0;
}

void CabConvolver::copyUpTo(int ages) noexcept {
    const int lim = std::min(ages, copyTo_);
    for (int a = copied_; a < lim; ++a) {
        const size_t d = static_cast<size_t>(wrap(dstPos0_ - a, nTail_)) * kStride;
        const size_t s = static_cast<size_t>(wrap(srcPos_ - a, srcN_)) * kStride;
        std::memcpy(&Xre_[d], srcRe_ + s, sizeof(float) * kStride);
        std::memcpy(&Xim_[d], srcIm_ + s, sizeof(float) * kStride);
    }
    if (lim > copied_) copied_ = lim;
}

void CabConvolver::finishCopy() noexcept {
    if (!copyPending_) return;
    copyUpTo(copyTo_);
    copyPending_ = false;
}

void CabConvolver::inheritFrom(CabConvolver& o) noexcept {
    o.finishCopy();
    std::copy(o.hist_, o.hist_ + kFft, hist_);
    std::fill(std::begin(tail_), std::end(tail_), 0.0f);
    std::fill(std::begin(pR_), std::end(pR_), 0.0f);
    std::fill(std::begin(pI_), std::end(pI_), 0.0f);
    pDone_   = 1;
    hopFill_ = o.hopFill_;
    live_    = false;
    fdlPos_  = 0;
    valid_   = (o.nTail_ > 0) ? std::min(o.valid_, nTail_) : 0;
    copyPending_ = false;
    if (nTail_ > 0 && valid_ > 0) {
        startCopy(o, 0);
        if (!spread_) finishCopy();   // short IR: a few KB, now
    }
}

void CabConvolver::cloneFrom(CabConvolver& o, bool lazy) noexcept {
    if (&o == this) return;
    if (o.irLen_ != irLen_ || o.nTail_ != nTail_) { reset(); return; }
    o.finishCopy();
    std::copy(o.hist_, o.hist_ + kFft, hist_);
    std::copy(o.tail_, o.tail_ + kHop, tail_);
    std::copy(o.pR_, o.pR_ + kStride, pR_);
    std::copy(o.pI_, o.pI_ + kStride, pI_);
    pDone_ = o.pDone_; hopFill_ = o.hopFill_; live_ = o.live_;
    fdlPos_ = o.fdlPos_; valid_ = o.valid_;
    copyPending_ = false;
    if (nTail_ > 0 && valid_ > 0) {
        startCopy(o, fdlPos_);
        if (!lazy) finishCopy();
    }
}

// ── Processing ───────────────────────────────────────────────────────────────
void CabConvolver::mac(float* accR, float* accI, int p0, int p1, int ageOff) const noexcept {
    if (p1 <= p0) return;
    int slot = wrap(fdlPos_ - (p0 + ageOff), nTail_);
    for (int p = p0; p < p1; ++p) {
        const float* __restrict hr = &Hre_[static_cast<size_t>(p) * kStride];
        const float* __restrict hi = &Him_[static_cast<size_t>(p) * kStride];
        const float* __restrict fr = &Xre_[static_cast<size_t>(slot) * kStride];
        const float* __restrict fi = &Xim_[static_cast<size_t>(slot) * kStride];
#if defined(__ARM_NEON)
        for (int b = 0; b < kStride; b += 4) {
            const float32x4_t vhr = vld1q_f32(hr + b), vhi = vld1q_f32(hi + b);
            const float32x4_t vfr = vld1q_f32(fr + b), vfi = vld1q_f32(fi + b);
            float32x4_t ar = vld1q_f32(accR + b), ai = vld1q_f32(accI + b);
            ar = ccMla(ar, vhr, vfr);
            ar = ccMls(ar, vhi, vfi);
            ai = ccMla(ai, vhr, vfi);
            ai = ccMla(ai, vhi, vfr);
            vst1q_f32(accR + b, ar);
            vst1q_f32(accI + b, ai);
        }
#else
        for (int b = 0; b < kStride; ++b) {
            accR[b] += hr[b] * fr[b] - hi[b] * fi[b];
            accI[b] += hr[b] * fi[b] + hi[b] * fr[b];
        }
#endif
        slot = (slot == 0) ? nTail_ - 1 : slot - 1;
    }
}

// Between boundaries (long IRs): accumulate the partitions of the coming boundary that only need
// spectra which already exist, in step with the hop (a quarter per 16-frame block); the boundary
// itself keeps the newest partition, the FFT and the IFFT.
void CabConvolver::spreadStep(int /*seg*/) noexcept {
    const int nEff = std::min(nTail_, valid_ + 1);
    if (pDone_ >= nEff) return;
    int tgt = 1 + ((nEff - 1) * hopFill_ + kHop - 1) / kHop;
    if (tgt > nEff) tgt = nEff;
    if (tgt <= pDone_) return;
    if (copyPending_) copyUpTo(tgt - 1);
    mac(pR_, pI_, pDone_, tgt, -1);
    pDone_ = tgt;
}

void CabConvolver::hopUpdate() noexcept {
    if (nTail_ == 0) { std::fill(std::begin(tail_), std::end(tail_), 0.0f); return; }
    finishCopy();   // every row the sums below and the next hops read, before the push
    if (spread_) {
        mac(pR_, pI_, pDone_, std::min(nTail_, valid_ + 1), -1);
        pDone_ = 1;
    }

    for (int i = 0; i < kFft; ++i) fftBuf_[i] = Cx(hist_[i], 0.0f);
    OlaConvolver::fft128(fftBuf_, false);

    fdlPos_ = (fdlPos_ + 1) % nTail_;
    if (valid_ < nTail_) ++valid_;
    float* xr = &Xre_[static_cast<size_t>(fdlPos_) * kStride];
    float* xi = &Xim_[static_cast<size_t>(fdlPos_) * kStride];
    for (int b = 0; b < kBins; ++b) { xr[b] = fftBuf_[b].real(); xi[b] = fftBuf_[b].imag(); }

    alignas(16) float accR[kStride];
    alignas(16) float accI[kStride];
    std::fill(accR, accR + kStride, 0.0f);
    std::fill(accI, accI + kStride, 0.0f);
    if (spread_) {
        mac(accR, accI, 0, 1, 0);
        for (int b = 0; b < kStride; ++b) { accR[b] += pR_[b]; accI[b] += pI_[b]; }
        std::fill(std::begin(pR_), std::end(pR_), 0.0f);
        std::fill(std::begin(pI_), std::end(pI_), 0.0f);
    } else {   // OlaConvolver::hopUpdate's loop verbatim (rows past valid_ are masked: were zeros)
        const int nAct = std::min(nTail_, valid_);
        int slot = fdlPos_;
        for (int p = 0; p < nAct; ++p) {
            const float* __restrict hr = &Hre_[static_cast<size_t>(p) * kStride];
            const float* __restrict hi = &Him_[static_cast<size_t>(p) * kStride];
            const float* __restrict fr = &Xre_[static_cast<size_t>(slot) * kStride];
            const float* __restrict fi = &Xim_[static_cast<size_t>(slot) * kStride];
#if defined(__ARM_NEON)
            for (int b = 0; b < kStride; b += 4) {
                const float32x4_t vhr = vld1q_f32(hr + b), vhi = vld1q_f32(hi + b);
                const float32x4_t vfr = vld1q_f32(fr + b), vfi = vld1q_f32(fi + b);
                float32x4_t ar = vld1q_f32(accR + b), ai = vld1q_f32(accI + b);
                ar = ccMla(ar, vhr, vfr);
                ar = ccMls(ar, vhi, vfi);
                ai = ccMla(ai, vhr, vfi);
                ai = ccMla(ai, vhi, vfr);
                vst1q_f32(accR + b, ar);
                vst1q_f32(accI + b, ai);
            }
#else
            for (int b = 0; b < kStride; ++b) {
                accR[b] += hr[b] * fr[b] - hi[b] * fi[b];
                accI[b] += hr[b] * fi[b] + hi[b] * fr[b];
            }
#endif
            slot = (slot == 0) ? nTail_ - 1 : slot - 1;
        }
    }

    fftBuf_[0] = Cx(accR[0], accI[0]);
    for (int b = 1; b < kBins; ++b) {
        fftBuf_[b]        = Cx(accR[b], accI[b]);
        fftBuf_[kFft - b] = Cx(accR[b], -accI[b]);
    }
    OlaConvolver::fft128(fftBuf_, true);
    for (int i = 0; i < kHop; ++i) tail_[i] = fftBuf_[kHop + i].real();
}

int CabConvolver::process(const float* in, float* out, int numSamples) noexcept {
    if (irLen_ <= 0) {
        if (in != out) std::copy(in, in + numSamples, out);
        return 0;
    }
    int first = live_ ? 0 : numSamples;
    int done = 0;
    while (done < numSamples) {
        const int seg = std::min(numSamples - done, kHop - hopFill_);
        std::memmove(hist_, hist_ + seg, sizeof(float) * (kFft - seg));
        std::memcpy(hist_ + kFft - seg, in + done, sizeof(float) * seg);
        if (live_) {
            for (int i = 0; i < seg; ++i) {
                const float* __restrict w = hist_ + (kFft - seg + i) - (kHop - 1);
#if defined(__ARM_NEON)
                const float acc = ccDot64(headRev_, w);
#else
                float acc = 0.0f;
                for (int j = 0; j < kHop; ++j) acc += headRev_[j] * w[j];
#endif
                out[done + i] = acc + tail_[hopFill_ + i];
            }
        }
        hopFill_ += seg;
        done     += seg;
        if (hopFill_ == kHop) {
            hopUpdate(); hopFill_ = 0;
            if (!live_) { live_ = true; first = done; }
        } else if (spread_) {
            spreadStep(seg);
        }
    }
    return first;
}
#endif
