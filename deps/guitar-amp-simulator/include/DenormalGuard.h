#pragma once
// RAII flush-to-zero / denormals-are-zero guard for the audio thread.
//
// Denormal (subnormal) floats are catastrophically slow on both ARM and x86 when
// they appear in tight feedback/IIR paths (cathode-bypass companion models, coupling
// HP filters, sag envelopes, reverb/delay tails). As a note decays toward silence the
// signal drifts into the denormal range and CPU spikes — which on the pi-Stomp causes
// xruns that make notes "cut out". Setting FZ/DAZ makes the FPU treat denormals as
// zero, keeping cost flat from full signal down to silence.
//
// Construct one at the top of each real-time process()/run() call.
#include <cstdint>
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
  #include <xmmintrin.h>
  #include <pmmintrin.h>
#endif

class DenormalGuard {
public:
#if defined(__aarch64__)
    DenormalGuard() noexcept {
        uint64_t fpcr;
        __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        fpcr |= (1ull << 24);                       // FZ: flush-to-zero
        __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
    }
    ~DenormalGuard() noexcept { __asm__ volatile("msr fpcr, %0" : : "r"(saved_)); }
private:
    uint64_t saved_;
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
    DenormalGuard() noexcept : saved_(_mm_getcsr()) {
        _mm_setcsr(saved_ | 0x8040);                // FTZ (0x8000) | DAZ (0x0040)
    }
    ~DenormalGuard() noexcept { _mm_setcsr(saved_); }
private:
    unsigned saved_;
#else
    DenormalGuard() noexcept {}
    ~DenormalGuard() noexcept {}
#endif
    DenormalGuard(const DenormalGuard&) = delete;
    DenormalGuard& operator=(const DenormalGuard&) = delete;
};
