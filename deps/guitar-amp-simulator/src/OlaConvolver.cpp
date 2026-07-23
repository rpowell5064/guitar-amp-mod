#include "OlaConvolver.h"
#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
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
    std::fill(std::begin(head_), std::end(head_), 0.0f);
    const int nHead = std::min(irLen, kHop);
    std::copy(ir, ir + nHead, head_);

    nTail_ = (irLen > kHop) ? (irLen - kHop + kHop - 1) / kHop : 0;
    Hre_.assign(static_cast<size_t>(nTail_) * kBins, 0.0f);
    Him_.assign(static_cast<size_t>(nTail_) * kBins, 0.0f);
    Xre_.assign(static_cast<size_t>(nTail_) * kBins, 0.0f);
    Xim_.assign(static_cast<size_t>(nTail_) * kBins, 0.0f);
    for (int p = 0; p < nTail_; ++p) {
        Cx buf[kFft] = {};
        const int base = kHop + p * kHop;
        const int len  = std::min(kHop, irLen - base);
        for (int i = 0; i < len; ++i) buf[i] = Cx(ir[base + i], 0.0f);
        fft128(buf, false);
        for (int b = 0; b < kBins; ++b) {
            Hre_[static_cast<size_t>(p) * kBins + b] = buf[b].real();
            Him_[static_cast<size_t>(p) * kBins + b] = buf[b].imag();
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
    float* xr = &Xre_[static_cast<size_t>(fdlPos_) * kBins];
    float* xi = &Xim_[static_cast<size_t>(fdlPos_) * kBins];
    for (int b = 0; b < kBins; ++b) { xr[b] = fftBuf_[b].real(); xi[b] = fftBuf_[b].imag(); }

    // Accumulate the tail sum in half-spectrum SoA (auto-vectorizes under -ffast-math).
    float accR[kBins];
    float accI[kBins];
    std::fill(accR, accR + kBins, 0.0f);
    std::fill(accI, accI + kBins, 0.0f);
    int slot = fdlPos_;
    for (int p = 0; p < nTail_; ++p) {
        const float* hr = &Hre_[static_cast<size_t>(p) * kBins];
        const float* hi = &Him_[static_cast<size_t>(p) * kBins];
        const float* fr = &Xre_[static_cast<size_t>(slot) * kBins];
        const float* fi = &Xim_[static_cast<size_t>(slot) * kBins];
        for (int b = 0; b < kBins; ++b) {
            accR[b] += hr[b] * fr[b] - hi[b] * fi[b];
            accI[b] += hr[b] * fi[b] + hi[b] * fr[b];
        }
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
            const float* x = hist_ + (kFft - seg + i);   // newest sample at *x
            float acc = 0.0f;
            for (int j = 0; j < kHop; ++j) acc += head_[j] * x[-j];
            out[done + i] = acc + tail_[hopFill_ + i];
        }
        hopFill_ += seg;
        done     += seg;
        if (hopFill_ == kHop) { hopUpdate(); hopFill_ = 0; }
    }
}
