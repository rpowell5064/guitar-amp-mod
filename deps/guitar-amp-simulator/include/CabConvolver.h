#pragma once
// CabConvolver — the Darkglass Anagram (HEXCHAIN_ANAGRAM) build's cab convolver (2026-11-09).
//
// OlaConvolver's algorithm, bit for bit (the same head FIR, FFT, partition MAC and summation
// order: the steady-state output of every cab is unchanged), plus what a 16-frame host period
// needs:
//
//  * Model/IR changes without a click. A new convolver INHERITS the running one's input history
//    (time-domain window + the frequency-domain delay line: both depend on the input only, not on
//    the IR), so the new IR starts with a full history at the same hop phase -- exactly the state
//    it would have had if it had always been loaded. It goes live at the next hop boundary
//    (<= 64 samples) and the caller crossfades old -> new while both run.
//  * Lazy history copies. The delay-line rows are copied from the source just before they are
//    first used (spread over the sub-blocks of the hop for long IRs) and finished by the next hop
//    boundary -- the source only overwrites a row the copy no longer needs before then. The mono
//    fast path's return to stereo uses the same copy (was ~400 KB in one block for a 1 s IR).
//  * Spread partition MAC (long IRs, >= kSpreadMin tail partitions). At hop P = 64 and a 16-frame
//    period the whole tail MAC landed on every 4th run(). The tail of the NEXT hop is
//    H0 X(m) + sum_{p>=1} Hp X(m-p); every term but the first uses spectra that already exist during
//    the current hop, so they are accumulated over its sub-blocks and only the newest partition,
//    the FFT and the IFFT stay on the boundary. Same latency; the sum is re-associated (one partial
//    + the first term), i.e. float rounding only. Below kSpreadMin (all built-in cabs) the order is
//    the original one, bit-identical.
//  * O(1) reset. The delay line is masked by a count of valid rows instead of being zero-filled
//    (a 1 s IR is 400 KB per channel): kx:Reset and the return from a full bypass cost nothing.
#ifdef HEXCHAIN_ANAGRAM
#include <complex>
#include <vector>

class CabConvolver {
public:
    static constexpr int kSpreadMin = 96;   // tail partitions: IRs longer than ~130 ms (built-ins are 45-100 ms)

    void prepare(int /*maxBlockSize*/) noexcept {}
    // Head taps + tail partition spectra (allocates): only from the thread that owns the slot.
    void setIR(const float* ir, int irLen);
    // Forget all input (O(1): the delay line is masked, not cleared).
    void reset() noexcept;
    // One block, in and out may alias. Returns the first sample written to `out`: 0 when live,
    // later (or numSamples) while an inheriting convolver waits for its first hop boundary.
    int  process(const float* in, float* out, int numSamples) noexcept;
    // Take over `o`'s input history (o holds another IR). Output starts at the next hop
    // boundary. `o` must not change before this convolver has processed up to that boundary,
    // except by its own process() AFTER this one's in the same block.
    void inheritFrom(CabConvolver& o) noexcept;
    // Become `o` (the same IR): the mono fast path's return to stereo. lazy = the delay-line rows
    // are copied by the next hop boundary (valid while `o` crosses at most one boundary first).
    void cloneFrom(CabConvolver& o, bool lazy) noexcept;
    void finishCopy() noexcept;             // complete a pending lazy copy now

    bool hasIR()  const noexcept { return irLen_ > 0; }
    int  irLen()  const noexcept { return irLen_; }
    int  fftSize()const noexcept { return kFft; }
    bool live()   const noexcept { return live_; }

private:
    static constexpr int kHop    = 64;
    static constexpr int kFft    = 2 * kHop;
    static constexpr int kBins   = kHop + 1;
    static constexpr int kStride = 68;      // kBins padded to 4 lanes (padding stays zero)
    using Cx = std::complex<float>;

    // OlaConvolver's members first, in its order and alignment (the FIR's vectorised reduction
    // must compile exactly as there)
    int irLen_ = 0, nTail_ = 0;
    float headRev_[kHop] = {};
    float hist_[kFft]    = {};
    float tail_[kHop]    = {};
    int   hopFill_ = 0;
    std::vector<float> Hre_, Him_, Xre_, Xim_;   // nTail_ * kStride, SoA half spectra
    int fdlPos_ = 0;                             // row of the newest input spectrum (age 0)
    Cx  fftBuf_[kFft];

    bool spread_ = false;
    int valid_  = 0;                             // rows of age < valid_ hold real input

    // spread MAC: sum_{p = 1 .. pDone_-1} Hp X(age p-1) for the coming boundary
    alignas(16) float pR_[kStride] = {};
    alignas(16) float pI_[kStride] = {};
    int pDone_ = 1;

    bool live_ = true;                           // false: inherited, waiting for the next boundary

    // lazy history copy: dst row of age a = (dstPos0_ - a), src row = (srcPos_ - a) of srcRe_/srcIm_
    const float* srcRe_ = nullptr;
    const float* srcIm_ = nullptr;
    int srcN_ = 0, srcPos_ = 0, dstPos0_ = 0, copyTo_ = 0, copied_ = 0;
    bool copyPending_ = false;

    void startCopy(const CabConvolver& o, int dstPos0) noexcept;
    void copyUpTo(int ages) noexcept;
    void hopUpdate() noexcept;
    void spreadStep(int seg) noexcept;
    // acc += H[p] * X[row(fdlPos_ - (p + ageOff))] for p in [p0, p1), the original order
    void mac(float* accR, float* accI, int p0, int p1, int ageOff) const noexcept;
};
#endif
