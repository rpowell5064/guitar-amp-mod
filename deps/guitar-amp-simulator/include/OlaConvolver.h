#pragma once
#include <vector>
#include <complex>

// Zero-latency PARTITIONED FIR convolver (rewritten 2026-07-23).
//
// The previous implementation was single-partition overlap-add: one FFT of
// nextPow2(blockSize + irLen) per 64-sample block. Fine for tiny IRs, but a
// user 2-second .wav meant a 131072-point FFT (plus a 131k-float buffer shift)
// every 1.3 ms — the reported CPU explosion with IR files.
//
// New algorithm: uniform partitioned convolution at hop P = 64 with a direct
// time-domain HEAD, which keeps it ZERO-LATENCY for any host block size:
//   * head: the first P taps run as a plain FIR on the input history — this
//     covers the part of the convolution that depends on the current samples.
//   * tail: taps [P, irLen) are split into P-sized partitions, each stored as
//     a half-spectrum (2P-point FFT). A frequency-domain delay line keeps the
//     spectra of past input hops; at every hop boundary one FFT + one
//     multiply-accumulate pass + one IFFT produce the tail's contribution to
//     the NEXT P output samples — which by construction depends only on input
//     BEFORE that hop, so it is always ready in time (no added latency, and
//     the work is smooth, not bursty).
//   Cost per sample: P head MACs + ~(irLen/P)·(P+1)/P complex MACs + O(log P),
//   vs the old O(irLen·log(irLen)/blockSize). Factory cabs: ~20x cheaper;
//   long IRs: no longer catastrophic.
//
// Each instance is single-threaded. CabinetBlock keeps two instances per
// channel in a double-buffer so the message thread can update one while the
// audio thread reads the other — no mutex ever touches the audio thread.
// No external dependencies — self-contained for Pi portability.
class OlaConvolver {
public:
    // Kept for API compatibility (partition size is fixed internally).
    void prepare(int maxBlockSize) noexcept;

    // Pre-computes the head taps + tail partition spectra. Safe to call only
    // from the thread that owns this instance (message thread for the back slot).
    void setIR(const float* ir, int irLen);

    // Clears all running state (call after a discontinuity).
    void reset() noexcept;

    // Process one block. in and out may alias. Any numSamples > 0.
    void process(const float* in, float* out, int numSamples) noexcept;

    bool hasIR()  const noexcept { return irLen_ > 0; }
    int  irLen()  const noexcept { return irLen_; }
    int  fftSize()const noexcept { return kFft; }

private:
    static constexpr int kHop  = 64;         // partition/hop size (= device JACK period)
    static constexpr int kFft  = 2 * kHop;   // 128-point FFTs
    static constexpr int kBins = kHop + 1;   // half-spectrum bins 0..64

    using Cx = std::complex<float>;

    int irLen_  = 0;
    int nTail_  = 0;                          // number of tail partitions

    float head_[kHop]   = {};                 // taps 0..P-1 (direct FIR)
    float hist_[kFft]   = {};                 // last 2P input samples (linear, shifted per call)
    float tail_[kHop]   = {};                 // tail contribution for the CURRENT hop
    int   hopFill_ = 0;                       // samples consumed of the current hop

    // Tail spectra + FDL, SoA half-spectrum layout [part * kBins + bin].
    std::vector<float> Hre_, Him_;            // nTail_ * kBins
    std::vector<float> Xre_, Xim_;            // FDL ring: nTail_ * kBins
    int fdlPos_ = 0;                          // slot holding the most recent hop's spectrum

    Cx  fftBuf_[kFft];                        // FFT workspace
    static const float* twiddleRe() noexcept; // 128-point twiddle tables (shared, static)
    static const float* twiddleIm() noexcept;

    void hopUpdate() noexcept;                // at each hop boundary: FFT + MAC + IFFT -> tail_
    static void fft128(Cx* buf, bool inverse) noexcept;
};
