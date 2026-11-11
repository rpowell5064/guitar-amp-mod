#pragma once
// Darkglass KosmOS (HEXCHAIN_ANAGRAM) real-time helpers shared by the plugins (2026-11-09).
//
// The device runs a 16-frame period (0.33 ms): anything done per run() is done 3000 times a
// second, and anything that cuts the signal from one sample to the next is a click.
//
//  ANAGRAM_PUSH  a setParameter() that is skipped while the value is unchanged. Most setters
//                build a std::string and walk a chain of compares, and many recompute filter
//                coefficients without checking for a change: pushing every block was pure waste.
//  XFade         a short smoothstep ramp, 0..1, for bypass (dry <-> wet) and for model switches
//                (old -> new). The ramp length is fixed in samples, so it is the same at any
//                period.
#ifdef HEXCHAIN_ANAGRAM
#include <cstdint>
#include <cstring>
#include <algorithm>

// ── Deduplicated parameter pushes ────────────────────────────────────────────
// Each ANAGRAM_PUSH call site owns a slot (numbered once, on first use) in a PushCache. The
// cache's generation is bumped whenever the target object is new or reset (invalidate()), and
// every slot pushes again once.
template <int N>
struct PushCache {
    float    last[N] = {};
    uint32_t gen[N]  = {};
    uint32_t cur     = 1;
    void invalidate() noexcept { ++cur; }
    // true = push (and remember v)
    bool want(int slot, float v) noexcept {
        if (slot >= N) return true;
        if (gen[slot] == cur && last[slot] == v) return false;
        last[slot] = v; gen[slot] = cur; return true;
    }
};
// Slots are compile-time constants (__COUNTER__, per translation unit = per plugin): a function-
// local static numbered at first use would run a thread-safe init guard, which may lock, on the
// audio thread.
enum : int { kAnagramPushBase = __COUNTER__ };
#define ANAGRAM_PUSH(cache, obj, id, val) do {                         \
        constexpr int s_ = __COUNTER__ - kAnagramPushBase - 1;         \
        const float v_ = (val);                                        \
        if ((cache).want(s_, v_)) (obj).setParameter(id, v_);          \
    } while (0)

// ── Ramps ───────────────────────────────────────────────────────────────────
struct XFade {
    static constexpr int kLen = 384;   // 8 ms at 48 kHz
    float pos = 1.0f;                  // 0 = fully A (dry / old), 1 = fully B (wet / new)
    static float shape(float x) noexcept { return x * x * (3.0f - 2.0f * x); }   // smoothstep
    bool settled(bool toB) const noexcept { return toB ? pos >= 1.0f : pos <= 0.0f; }
    // out = A + g (B - A) per sample, moving pos toward the target. A and B may alias out.
    void mix(const float* a, const float* b, float* out, uint32_t n, bool toB) noexcept {
        const float step = (toB ? 1.0f : -1.0f) / kLen;
        float p = pos;
        for (uint32_t i = 0; i < n; ++i) {
            p = std::min(1.0f, std::max(0.0f, p + step));
            const float g = shape(p);
            out[i] = a[i] + g * (b[i] - a[i]);
        }
        pos = p;
    }
    // the same ramp for two channels (stereo plugins), one step per frame
    void mix2(const float* a0, const float* b0, float* o0, const float* a1, const float* b1, float* o1,
              uint32_t n, bool toB) noexcept {
        const float step = (toB ? 1.0f : -1.0f) / kLen;
        float p = pos;
        for (uint32_t i = 0; i < n; ++i) {
            p = std::min(1.0f, std::max(0.0f, p + step));
            const float g = shape(p);
            o0[i] = a0[i] + g * (b0[i] - a0[i]);
            o1[i] = a1[i] + g * (b1[i] - a1[i]);
        }
        pos = p;
    }
    // gain-only ramp (fade a signal out to 0 or in from 0)
    void gain(float* x, uint32_t n, bool up) noexcept {
        const float step = (up ? 1.0f : -1.0f) / kLen;
        float p = pos;
        for (uint32_t i = 0; i < n; ++i) { p = std::min(1.0f, std::max(0.0f, p + step)); x[i] *= shape(p); }
        pos = p;
    }
};
#endif
