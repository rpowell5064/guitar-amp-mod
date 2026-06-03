#include "OlaConvolver.h"
#include <cmath>
#include <algorithm>
#include <cassert>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Helpers ──────────────────────────────────────────────────────────────────

int OlaConvolver::nextPow2(int n) noexcept {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

void OlaConvolver::bitReversal(Cx* buf, int N) noexcept {
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(buf[i], buf[j]);
    }
}

// ── FFT ──────────────────────────────────────────────────────────────────────
// Iterative Cooley-Tukey DIT with pre-computed twiddle table.
// twiddles_[k] = exp(-2πi*k/N), k = 0..N/2-1.
// For inverse: conjugate twiddles are used (exp(+2πi*k/N)), output scaled 1/N.

void OlaConvolver::fft(Cx* buf, bool inverse) const noexcept {
    const int N = fftSize_;
    bitReversal(buf, N);

    for (int len = 2; len <= N; len <<= 1) {
        const int halfLen = len >> 1;
        const int step    = N / len;   // stride into twiddle table for this stage
        for (int i = 0; i < N; i += len) {
            for (int j = 0; j < halfLen; ++j) {
                const Cx& tw = twiddles_[static_cast<size_t>(j) * step];
                // Forward: use twiddle as-is.  Inverse: conjugate (flip Im sign).
                const Cx w = inverse ? Cx(tw.real(), -tw.imag()) : tw;
                const Cx u = buf[i + j];
                const Cx v = buf[i + j + halfLen] * w;
                buf[i + j]           = u + v;
                buf[i + j + halfLen] = u - v;
            }
        }
    }

    if (inverse) {
        const float scale = 1.0f / static_cast<float>(N);
        for (int i = 0; i < N; ++i) buf[i] *= scale;
    }
}

// ── Public API ───────────────────────────────────────────────────────────────

void OlaConvolver::prepare(int maxBlockSize) noexcept {
    blockSize_ = maxBlockSize;
}

void OlaConvolver::setIR(const float* ir, int irLen) {
    irLen_   = irLen;
    fftSize_ = nextPow2(blockSize_ + irLen - 1);

    // Pre-compute twiddle factors: exp(-2πi*k/N) for k = 0..N/2-1.
    twiddles_.resize(static_cast<size_t>(fftSize_) / 2);
    const float ang = -2.0f * static_cast<float>(M_PI) / static_cast<float>(fftSize_);
    for (int k = 0; k < fftSize_ / 2; ++k) {
        const float a = ang * static_cast<float>(k);
        twiddles_[k]  = Cx(std::cos(a), std::sin(a));
    }

    // Zero-pad IR to fftSize_ and transform into H_.
    H_.assign(static_cast<size_t>(fftSize_), Cx{0.0f, 0.0f});
    for (int i = 0; i < irLen; ++i) H_[i] = Cx{ir[i], 0.0f};
    fft(H_.data(), false);

    // Resize working buffers; clear overlap (discontinuity on IR change is
    // acceptable and far better than the previous try_lock dry-signal glitch).
    workBuf_.resize(static_cast<size_t>(fftSize_));
    overlap_.assign(static_cast<size_t>(fftSize_), 0.0f);
}

void OlaConvolver::reset() noexcept {
    std::fill(overlap_.begin(), overlap_.end(), 0.0f);
}

void OlaConvolver::process(const float* in, float* out, int numSamples) noexcept {
    if (H_.empty()) {
        // No IR — pass through
        if (in != out) std::copy(in, in + numSamples, out);
        return;
    }

    const int n = std::min(numSamples, blockSize_);

    // Zero-pad input block to fftSize_ in workBuf_.
    std::fill(workBuf_.begin(), workBuf_.end(), Cx{0.0f, 0.0f});
    for (int i = 0; i < n; ++i) workBuf_[i] = Cx{in[i], 0.0f};

    // Forward FFT, complex multiply with pre-computed IR spectrum, inverse FFT.
    fft(workBuf_.data(), false);
    for (int k = 0; k < fftSize_; ++k) workBuf_[k] *= H_[k];
    fft(workBuf_.data(), true);

    // Overlap-add: accumulate linear convolution result into overlap_ buffer,
    // then output the first n samples and shift the buffer left by n.
    for (int i = 0; i < fftSize_; ++i) overlap_[i] += workBuf_[i].real();
    std::copy(overlap_.begin(), overlap_.begin() + n, out);
    std::copy(overlap_.begin() + n, overlap_.end(), overlap_.begin());
    std::fill(overlap_.end() - n, overlap_.end(), 0.0f);
}
