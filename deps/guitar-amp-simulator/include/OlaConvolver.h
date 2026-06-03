#pragma once
#include <vector>
#include <complex>

// Zero-latency overlap-add FIR convolver.
//
// Each instance is single-threaded. CabinetBlock keeps two instances per
// channel in a double-buffer so the message thread can update one while the
// audio thread reads the other — no mutex ever touches the audio thread.
//
// Algorithm: uniform overlap-add with a single FFT partition.
//   fftSize = nextPow2(blockSize + irLen - 1)
//   Cost: O((blockSize + irLen) * log(blockSize + irLen)) per block
//   rather than the direct O(blockSize * irLen).
//
// No external dependencies — self-contained for Pi portability.
class OlaConvolver {
public:
    // Sets the block size used for FFT sizing.  Must be called before setIR().
    void prepare(int maxBlockSize) noexcept;

    // Pre-computes frequency-domain IR.  Safe to call only from the thread
    // that owns this instance (message thread for the back slot).
    void setIR(const float* ir, int irLen);

    // Clears the OLA tail accumulator (call after a discontinuity).
    void reset() noexcept;

    // Process one block.  in and out may alias.  numSamples <= maxBlockSize.
    void process(const float* in, float* out, int numSamples) noexcept;

    bool hasIR()  const noexcept { return !H_.empty(); }
    int  irLen()  const noexcept { return irLen_; }
    int  fftSize()const noexcept { return fftSize_; }

private:
    int blockSize_ = 0;
    int fftSize_   = 0;
    int irLen_     = 0;

    using Cx = std::complex<float>;

    std::vector<Cx>    H_;        // frequency-domain IR  [fftSize_]
    std::vector<Cx>    twiddles_; // exp(-2πi*k/N) table [fftSize_/2]
    std::vector<Cx>    workBuf_;  // per-block FFT workspace [fftSize_]
    std::vector<float> overlap_;  // OLA tail accumulator    [fftSize_]

    // In-place iterative Cooley-Tukey FFT using the pre-computed twiddle table.
    // For inverse=true the conjugate twiddles are used and output is scaled by 1/N.
    void fft(Cx* buf, bool inverse) const noexcept;

    static void bitReversal(Cx* buf, int N) noexcept;
    static int  nextPow2(int n) noexcept;
};
