#include "OlaConvolver.h"
#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── NEON helpers (aarch64 + armv7-with-NEON; scalar fallback everywhere else) ──
// The scalar loops below are kept as the #else paths — they auto-vectorize on
// GCC -O3 for amd64, and are the correctness reference on MSVC.
#if defined(__ARM_NEON)
#include <arm_neon.h>
namespace {
#if defined(__ARM_FEATURE_FMA)   // aarch64 always; armv7 built with -mfpu=neon-vfpv4 (our CI)
inline float32x4_t olaMla(float32x4_t a, float32x4_t b, float32x4_t c) noexcept { return vfmaq_f32(a, b, c); }
inline float32x4_t olaMls(float32x4_t a, float32x4_t b, float32x4_t c) noexcept { return vfmsq_f32(a, b, c); }
#else                            // plain -mfpu=neon (no FMA): multiply-accumulate
inline float32x4_t olaMla(float32x4_t a, float32x4_t b, float32x4_t c) noexcept { return vmlaq_f32(a, b, c); }
inline float32x4_t olaMls(float32x4_t a, float32x4_t b, float32x4_t c) noexcept { return vmlsq_f32(a, b, c); }
#endif
inline float olaHsum(float32x4_t v) noexcept {
#if defined(__aarch64__)
    return vaddvq_f32(v);
#else
    float32x2_t s = vadd_f32(vget_low_f32(v), vget_high_f32(v));
    s = vpadd_f32(s, s);
    return vget_lane_f32(s, 0);
#endif
}
// 64-tap forward dot product (the head FIR): 4 independent accumulators keep
// both NEON FMA pipes fed; one horizontal sum at the end.
inline float olaDot64(const float* __restrict w, const float* __restrict x) noexcept {
    float32x4_t a0 = vdupq_n_f32(0.0f), a1 = a0, a2 = a0, a3 = a0;
    for (int j = 0; j < 64; j += 16) {
        a0 = olaMla(a0, vld1q_f32(w + j),      vld1q_f32(x + j));
        a1 = olaMla(a1, vld1q_f32(w + j + 4),  vld1q_f32(x + j + 4));
        a2 = olaMla(a2, vld1q_f32(w + j + 8),  vld1q_f32(x + j + 8));
        a3 = olaMla(a3, vld1q_f32(w + j + 12), vld1q_f32(x + j + 12));
    }
    return olaHsum(vaddq_f32(vaddq_f32(a0, a1), vaddq_f32(a2, a3)));
}
} // namespace
#endif

// ── 128-point FFT (fixed size: twiddles + bit-reversal precomputed once) ──────

namespace {
constexpr int kN = 128;

struct Tables {
    float twRe[kN / 2];
    float twIm[kN / 2];
    unsigned char rev[kN];
    Tables() {
        for (int k = 0; k < kN / 2; ++k) {
            const double a = -2.0 * M_PI * k / kN;
            twRe[k] = static_cast<float>(std::cos(a));
            twIm[k] = static_cast<float>(std::sin(a));
        }
        for (int i = 0; i < kN; ++i) {
            int j = 0;
            for (int b = 0; b < 7; ++b) j |= ((i >> b) & 1) << (6 - b);
            rev[i] = static_cast<unsigned char>(j);
        }
    }
};
const Tables& tables() { static const Tables t; return t; }
}  // namespace

const float* OlaConvolver::twiddleRe() noexcept { return tables().twRe; }
const float* OlaConvolver::twiddleIm() noexcept { return tables().twIm; }

void OlaConvolver::fft128(Cx* buf, bool inverse) noexcept {
    const Tables& t = tables();
    for (int i = 0; i < kN; ++i) {
        const int j = t.rev[i];
        if (i < j) std::swap(buf[i], buf[j]);
    }
    for (int len = 2; len <= kN; len <<= 1) {
        const int half = len >> 1;
        const int step = kN / len;
        for (int i = 0; i < kN; i += len) {
            for (int j = 0; j < half; ++j) {
                const int   ti = j * step;
                const float wr = t.twRe[ti];
                const float wi = inverse ? -t.twIm[ti] : t.twIm[ti];
                const Cx u = buf[i + j];
                const Cx s = buf[i + j + half];
                const Cx v(s.real() * wr - s.imag() * wi,
                           s.real() * wi + s.imag() * wr);
                buf[i + j]        = u + v;
                buf[i + j + half] = u - v;
            }
        }
    }
    if (inverse) {
        const float sc = 1.0f / kN;
        for (int i = 0; i < kN; ++i) buf[i] *= sc;
    }
}

// ── Public API ───────────────────────────────────────────────────────────────

void OlaConvolver::prepare(int /*maxBlockSize*/) noexcept {}

void OlaConvolver::setIR(const float* ir, int irLen) {
    irLen_ = irLen;
    // Head taps stored REVERSED so the per-sample FIR in process() is a forward
    // contiguous dot. Short IRs leave the LOW (oldest-tap) indices zero.
    std::fill(std::begin(headRev_), std::end(headRev_), 0.0f);
    const int nHead = std::min(irLen, kHop);
    for (int i = 0; i < nHead; ++i) headRev_[kHop - 1 - i] = ir[i];

    nTail_ = (irLen > kHop) ? (irLen - kHop + kHop - 1) / kHop : 0;
    // kStride rows: .assign zero-fills, and bins kBins..kStride-1 are never
    // written afterward — the padding lanes stay exact zeros.
    Hre_.assign(static_cast<size_t>(nTail_) * kStride, 0.0f);
    Him_.assign(static_cast<size_t>(nTail_) * kStride, 0.0f);
    Xre_.assign(static_cast<size_t>(nTail_) * kStride, 0.0f);
    Xim_.assign(static_cast<size_t>(nTail_) * kStride, 0.0f);
    for (int p = 0; p < nTail_; ++p) {
        Cx buf[kFft] = {};
        const int base = kHop + p * kHop;
        const int len  = std::min(kHop, irLen - base);
        for (int i = 0; i < len; ++i) buf[i] = Cx(ir[base + i], 0.0f);
        fft128(buf, false);
        for (int b = 0; b < kBins; ++b) {
            Hre_[static_cast<size_t>(p) * kStride + b] = buf[b].real();
            Him_[static_cast<size_t>(p) * kStride + b] = buf[b].imag();
        }
    }
    reset();
}

void OlaConvolver::reset() noexcept {
    std::fill(std::begin(hist_), std::end(hist_), 0.0f);
    std::fill(std::begin(tail_), std::end(tail_), 0.0f);
    std::fill(Xre_.begin(), Xre_.end(), 0.0f);
    std::fill(Xim_.begin(), Xim_.end(), 0.0f);
    fdlPos_  = 0;
    hopFill_ = 0;
}

// At a hop boundary: hist_ holds the last 2P inputs, the newest P of which are
// the hop just completed. FFT that window, push its spectrum into the FDL, then
// accumulate H_k * X_(m-k) over the tail partitions and IFFT — the LAST P
// samples of the result are the tail contribution for the UPCOMING hop.
void OlaConvolver::hopUpdate() noexcept {
    if (nTail_ == 0) { std::fill(std::begin(tail_), std::end(tail_), 0.0f); return; }

    for (int i = 0; i < kFft; ++i) fftBuf_[i] = Cx(hist_[i], 0.0f);
    fft128(fftBuf_, false);

    fdlPos_ = (fdlPos_ + 1) % nTail_;
    float* xr = &Xre_[static_cast<size_t>(fdlPos_) * kStride];
    float* xi = &Xim_[static_cast<size_t>(fdlPos_) * kStride];
    for (int b = 0; b < kBins; ++b) { xr[b] = fftBuf_[b].real(); xi[b] = fftBuf_[b].imag(); }

    // Accumulate the tail sum in half-spectrum SoA. Rows are kStride (padded)
    // wide so the loop is an exact number of 4-lane iterations — the padding
    // lanes are 0*0 and acc[kBins..] is never read by the Hermitian rebuild.
    alignas(16) float accR[kStride];
    alignas(16) float accI[kStride];
    std::fill(accR, accR + kStride, 0.0f);
    std::fill(accI, accI + kStride, 0.0f);
    int slot = fdlPos_;
    for (int p = 0; p < nTail_; ++p) {
        const float* __restrict hr = &Hre_[static_cast<size_t>(p) * kStride];
        const float* __restrict hi = &Him_[static_cast<size_t>(p) * kStride];
        const float* __restrict fr = &Xre_[static_cast<size_t>(slot) * kStride];
        const float* __restrict fi = &Xim_[static_cast<size_t>(slot) * kStride];
#if defined(__ARM_NEON)
        for (int b = 0; b < kStride; b += 4) {
            const float32x4_t vhr = vld1q_f32(hr + b), vhi = vld1q_f32(hi + b);
            const float32x4_t vfr = vld1q_f32(fr + b), vfi = vld1q_f32(fi + b);
            float32x4_t ar = vld1q_f32(accR + b), ai = vld1q_f32(accI + b);
            ar = olaMla(ar, vhr, vfr);   // accR += hr*fr
            ar = olaMls(ar, vhi, vfi);   //       - hi*fi
            ai = olaMla(ai, vhr, vfi);   // accI += hr*fi
            ai = olaMla(ai, vhi, vfr);   //       + hi*fr
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

    // Rebuild the full Hermitian spectrum and invert; the last P samples are the
    // (overlap-save) tail block for the next hop.
    fftBuf_[0] = Cx(accR[0], accI[0]);
    for (int b = 1; b < kBins; ++b) {
        fftBuf_[b]        = Cx(accR[b], accI[b]);
        fftBuf_[kFft - b] = Cx(accR[b], -accI[b]);
    }
    fft128(fftBuf_, true);
    for (int i = 0; i < kHop; ++i) tail_[i] = fftBuf_[kHop + i].real();
}

void OlaConvolver::process(const float* in, float* out, int numSamples) noexcept {
    if (irLen_ <= 0) {
        if (in != out) std::copy(in, in + numSamples, out);
        return;
    }
    int done = 0;
    while (done < numSamples) {
        const int seg = std::min(numSamples - done, kHop - hopFill_);
        // Shift the history window left and append this segment (hist_ stays a
        // LINEAR buffer so the head FIR below is a contiguous, vectorizable dot).
        std::memmove(hist_, hist_ + seg, sizeof(float) * (kFft - seg));
        std::memcpy(hist_ + kFft - seg, in + done, sizeof(float) * seg);
        for (int i = 0; i < seg; ++i) {
            // headRev_ is the head reversed, so the dot runs FORWARD over the
            // 64-sample window ending at the newest sample. Min index of w is
            // kFft - seg - (kHop-1) >= 128 - 64 - 63 = 1: always in-bounds.
            const float* __restrict w = hist_ + (kFft - seg + i) - (kHop - 1);
#if defined(__ARM_NEON)
            const float acc = olaDot64(headRev_, w);
#else
            float acc = 0.0f;
            for (int j = 0; j < kHop; ++j) acc += headRev_[j] * w[j];
#endif
            out[done + i] = acc + tail_[hopFill_ + i];
        }
        hopFill_ += seg;
        done     += seg;
        if (hopFill_ == kHop) { hopUpdate(); hopFill_ = 0; }
    }
}
