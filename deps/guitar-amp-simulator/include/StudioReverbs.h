#pragma once
// Two modern reverb tanks for PlateReverbBlock (2026-10-18), header-only:
//
//   DattorroPlate (type 3, "Studio Plate") — the plate-class figure-of-eight
//     tank published by J. Dattorro, "Effect Design Part 1: Reverberator and
//     Other Filters", JAES 45(9), 1997: four input diffusers, two cross-fed
//     halves (modulated allpass -> delay -> absorber -> allpass -> delay), and
//     the paper's 7+7 output taps for a decorrelated stereo field from a mono
//     send (a mechanical plate is mono in, two pickups out).
//
//   FdnHall (type 4, "Hall") — an 8-line feedback delay network with an
//     orthonormal Hadamard mixing matrix (Jot & Chaigne 1991): TRUE stereo in
//     (L feeds the even lines, R the odd), per-line absorbers so every line
//     decays at the same RT60, two output rows of the matrix for L/R.
//
// Both use frequency-dependent absorbers instead of a fixed loop LP: each loop
// segment of length Ls gets the DC gain 10^(-3 Ls / T60) (the Decay knob is a
// true RT60 at low frequencies) and a one-pole whose loss at 4 kHz is matched
// to what the classic comb tank's damping LP does per second at the same
// Damping setting — so the Damping knob means the same thing on every type.
//
// Everything is deterministic (no RNG): dual-mono determinism and kx:Reset
// hold, and clear() returns the exact post-prepare state.
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace studiorev {

constexpr double kTwoPi = 6.283185307179586;

// Power-of-two ring. Convention: read before write; read(d) = x[n-d], d >= 1.
struct Ring {
    std::vector<float> b;
    uint32_t mask = 0, w = 0;
    void alloc(int minLen) {
        uint32_t n = 16;
        while (n < static_cast<uint32_t>(minLen) + 8u) n <<= 1;
        b.assign(n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill(b.begin(), b.end(), 0.0f); w = 0; }
    inline void push(float v) noexcept { b[w] = v; w = (w + 1) & mask; }
    inline float read(int d) const noexcept { return b[(w - static_cast<uint32_t>(d)) & mask]; }
    // 4-point Hermite at a fractional delay d (>= 2): the moving taps stay free
    // of the fraction-dependent HF loss a linear read adds.
    inline float readHermite(float d) const noexcept {
        const int   d0 = static_cast<int>(d);
        const float fr = d - static_cast<float>(d0);
        const float xm1 = read(d0 - 1), x0 = read(d0), x1 = read(d0 + 1), x2 = read(d0 + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * fr + c2) * fr + c1) * fr + x0;
    }
};

// Schroeder allpass H = (z^-L - g) / (1 - g z^-L); the internal line is
// exposed for output taps.
struct Allpass {
    Ring line; int len = 1; float g = 0.5f;
    void alloc(int L, int extra = 0) { len = std::max(2, L); line.alloc(len + extra + 4); }
    inline float process(float x) noexcept {
        const float d = line.read(len);
        const float v = x + g * d;
        line.push(v);
        return d - g * v;
    }
    inline float processMod(float x, float delay) noexcept {
        const float d = line.readHermite(delay);
        const float v = x + g * d;
        line.push(v);
        return d - g * v;
    }
};

// Loop absorber: y = k * one-pole LP(x), unity-DC LP with pole b.
struct Absorber {
    float k = 0.5f, b = 0.0f, z = 0.0f;
    inline float process(float x) noexcept { z = (1.0f - b) * x + b * z; return k * z; }
};

// The classic tank's damping LP is y = (1-a) x + a y once per comb pass of
// mean length Tc seconds. Its loss per second at w becomes, for a segment of
// Ls seconds, the relative magnitude r = |H_a(w)|^(Ls/Tc). Return the pole b
// of a unity-DC one-pole with exactly that magnitude at w.
inline double matchDampingPole(double a, double segSec, double classicTc, double w) {
    if (a <= 1e-6) return 0.0;
    const double c   = std::cos(w);
    const double ha2 = (1.0 - a) * (1.0 - a) / (1.0 - 2.0 * a * c + a * a);
    const double r2  = std::pow(ha2, segSec / classicTc);          // target |H|^2
    if (r2 >= 1.0 - 1e-12) return 0.0;
    // (1-b)^2 = r2 (1 - 2bc + b^2)  ->  (1-r2) b^2 - 2(1 - r2 c) b + (1-r2) = 0
    const double A = 1.0 - r2, B = 1.0 - r2 * c;
    const double disc = std::max(0.0, B * B - A * A);
    const double b = (B - std::sqrt(disc)) / A;                     // root inside the unit circle
    return std::clamp(b, 0.0, 0.9995);
}

// Mean classic comb pass (PlateReverbBlock classic set {1687,1601,2053,2251} @25 kHz).
constexpr double kClassicTc = (1687.0 + 1601.0 + 2053.0 + 2251.0) / 4.0 / 25000.0;
constexpr double kDampRefHz = 4000.0;

inline void setAbsorber(Absorber& ab, double segSamples, double fs, double t60, double damping) {
    const double seg = segSamples / fs;
    ab.k = static_cast<float>(std::pow(10.0, -3.0 * seg / std::max(0.05, t60)));
    ab.b = static_cast<float>(matchDampingPole(damping, seg, kClassicTc, kTwoPi * kDampRefHz / fs));
}

// ───────────────────────────────────────────────────────────────────────────
class DattorroPlate {
public:
    void prepare(double fs) {
        fs_ = fs;
        const double sc = fs / 29761.0;                       // the paper's reference rate
        auto S = [sc](int n) { return std::max(2, static_cast<int>(std::lround(n * sc))); };
        pre_.alloc(static_cast<int>(fs * 0.1) + 4);
        const int inL[4] = { 142, 107, 379, 277 };
        const float inG[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
        for (int i = 0; i < 4; ++i) { inAp_[i].alloc(S(inL[i])); inAp_[i].g = inG[i]; }
        excMax_ = static_cast<float>(16.0 * sc);
        apA1_.alloc(S(672), static_cast<int>(excMax_) + 4);  apA1_.g = -0.70f;
        apB1_.alloc(S(908), static_cast<int>(excMax_) + 4);  apB1_.g = -0.70f;
        apA2_.alloc(S(1800)); apA2_.g = 0.50f;
        apB2_.alloc(S(2656)); apB2_.g = 0.50f;
        LA1_ = S(4453); LA2_ = S(3720); LB1_ = S(4217); LB2_ = S(3163);
        dA1_.alloc(LA1_); dA2_.alloc(LA2_); dB1_.alloc(LB1_); dB2_.alloc(LB2_);
        // Output taps (paper Table 2), scaled
        const int tl[7] = { 266, 2974, 1913, 1996, 1990, 187, 1066 };
        const int tr[7] = { 353, 3627, 1228, 2673, 2111, 335, 121 };
        for (int i = 0; i < 7; ++i) { tapL_[i] = S(tl[i]); tapR_[i] = S(tr[i]); }
        segA_ = apA1_.len + LA1_ + apA2_.len + LA2_;
        segB_ = apB1_.len + LB1_ + apB2_.len + LB2_;
        clear();
        update();
    }
    void clear() {
        pre_.clear();
        for (auto& a : inAp_) a.line.clear();
        apA1_.line.clear(); apA2_.line.clear(); apB1_.line.clear(); apB2_.line.clear();
        dA1_.clear(); dA2_.clear(); dB1_.clear(); dB2_.clear();
        absA_.z = absB_.z = 0.0f;
        phase_ = 0.0;
    }
    void set(float t60, float damping, float modDepth, float modRate, float preMs) {
        if (t60 != t60_ || damping != damp_) { t60_ = t60; damp_ = damping; update(); }
        exc_ = std::clamp(modDepth, 0.0f, 1.0f) * excMax_;
        inc_ = kTwoPi * std::max(0.0f, modRate) / fs_;
        preLen_ = std::max(1, static_cast<int>(preMs * 0.001f * static_cast<float>(fs_)));
    }
    // mono in -> decorrelated stereo wet (no dry)
    inline void process(float x, float& wl, float& wr) noexcept {
        pre_.push(x);
        float s = pre_.read(preLen_);
        for (auto& a : inAp_) s = a.process(s);
        phase_ += inc_; if (phase_ >= kTwoPi) phase_ -= kTwoPi;
        const float mA = exc_ * static_cast<float>(std::sin(phase_));
        const float mB = exc_ * static_cast<float>(std::cos(phase_));
        const float outA = dA2_.read(LA2_), outB = dB2_.read(LB2_);   // last half outputs
        // half A (fed by half B)
        float u = apA1_.processMod(s + outB, static_cast<float>(apA1_.len) + mA);
        float t = dA1_.read(LA1_); dA1_.push(u);
        t = apA2_.process(absA_.process(t));
        dA2_.push(t);
        // half B (fed by half A)
        u = apB1_.processMod(s + outA, static_cast<float>(apB1_.len) + mB);
        t = dB1_.read(LB1_); dB1_.push(u);
        t = apB2_.process(absB_.process(t));
        dB2_.push(t);
        // paper Table 2 (node24_30=dA1, 31_33=apA2, 33_39=dA2, 48_54=dB1, 55_59=apB2, 59_63=dB2)
        wl = 0.6f * ( dB1_.read(tapL_[0]) + dB1_.read(tapL_[1]) - apB2_.line.read(tapL_[2])
                    + dB2_.read(tapL_[3]) - dA1_.read(tapL_[4]) - apA2_.line.read(tapL_[5])
                    - dA2_.read(tapL_[6]) );
        wr = 0.6f * ( dA1_.read(tapR_[0]) + dA1_.read(tapR_[1]) - apA2_.line.read(tapR_[2])
                    + dA2_.read(tapR_[3]) - dB1_.read(tapR_[4]) - apB2_.line.read(tapR_[5])
                    - dB2_.read(tapR_[6]) );
    }
private:
    // The tank allpasses' group delay peaks well above their length near their
    // poles, so modes there circulate slower than the segment length implies and
    // the EDC reads ~6-7 % long; scale the loss target so T30 lands on the knob.
    static constexpr double kRtCal = 0.94;
    void update() {
        setAbsorber(absA_, segA_, fs_, t60_ * kRtCal, damp_);
        setAbsorber(absB_, segB_, fs_, t60_ * kRtCal, damp_);
    }
    double fs_ = 48000.0, phase_ = 0.0, inc_ = 0.0;
    float t60_ = 1.5f, damp_ = 0.3f, exc_ = 0.0f, excMax_ = 16.0f;
    int preLen_ = 1;
    Ring pre_;
    Allpass inAp_[4], apA1_, apA2_, apB1_, apB2_;
    Ring dA1_, dA2_, dB1_, dB2_;
    int LA1_ = 0, LA2_ = 0, LB1_ = 0, LB2_ = 0, segA_ = 0, segB_ = 0;
    int tapL_[7] = {}, tapR_[7] = {};
    Absorber absA_, absB_;
};

// ───────────────────────────────────────────────────────────────────────────
class FdnHall {
public:
    static constexpr int N = 8;
    void prepare(double fs) {
        fs_ = fs;
        // Line lengths: mutually prime, spread over ~1:2.4 so no two lines share
        // low-order modes; mean ~58 ms = a mid-size hall's mean free path.
        const double ms[N] = { 37.3, 41.9, 47.1, 53.3, 59.9, 67.7, 76.1, 83.9 };
        for (int i = 0; i < N; ++i) {
            int n = static_cast<int>(std::lround(ms[i] * 0.001 * fs));
            while (!isPrime(n) || used(n, i)) ++n;
            len_[i] = n;
            line_[i].alloc(n + kModMax + 4);
        }
        // Input diffusion, separate per side (true stereo in)
        const double dms[2][4] = { { 4.71, 3.63, 12.7, 19.9 }, { 5.27, 3.11, 11.3, 17.3 } };
        const float  dg[4]     = { 0.70f, 0.70f, 0.60f, 0.55f };
        for (int s = 0; s < 2; ++s) {
            pre_[s].alloc(static_cast<int>(fs * 0.1) + 4);
            for (int i = 0; i < 4; ++i) {
                diff_[s][i].alloc(static_cast<int>(std::lround(dms[s][i] * 0.001 * fs)));
                diff_[s][i].g = dg[i];
            }
        }
        clear();
        update();
    }
    void clear() {
        for (auto& l : line_) l.clear();
        for (auto& a : abs_) a.z = 0.0f;
        for (int s = 0; s < 2; ++s) { pre_[s].clear(); for (auto& a : diff_[s]) a.line.clear(); }
        for (int i = 0; i < 4; ++i) phase_[i] = kPhase0[i];
    }
    void set(float t60, float damping, float modDepth, float modRate, float preMs) {
        if (t60 != t60_ || damping != damp_) { t60_ = t60; damp_ = damping; update(); }
        exc_ = std::clamp(modDepth, 0.0f, 1.0f) * static_cast<float>(kModMax - 2) * 0.5f;
        for (int i = 0; i < 4; ++i) inc_[i] = kTwoPi * std::max(0.0f, modRate) * kRateMul[i] / fs_;
        preLen_ = std::max(1, static_cast<int>(preMs * 0.001f * static_cast<float>(fs_)));
    }
    inline void process(float xl, float xr, float& wl, float& wr) noexcept {
        pre_[0].push(xl); pre_[1].push(xr);
        float sl = pre_[0].read(preLen_), sr = pre_[1].read(preLen_);
        for (auto& a : diff_[0]) sl = a.process(sl);
        for (auto& a : diff_[1]) sr = a.process(sr);
        float y[N], o[N];
        for (int i = 0; i < N; ++i) {
            float d;
            if (i & 1) d = line_[i].read(len_[i]);
            else {   // even lines carry slow incommensurate modulation (mode smoothing)
                const int m = i >> 1;
                phase_[m] += inc_[m]; if (phase_[m] >= kTwoPi) phase_[m] -= kTwoPi;
                d = line_[i].readHermite(static_cast<float>(len_[i]) + exc_ * (1.0f + static_cast<float>(std::sin(phase_[m]))));
            }
            o[i] = d;                       // taps read the line, before this pass's loss
            y[i] = abs_[i].process(d);
        }
        // outputs: two orthogonal Hadamard rows (decorrelated L/R)
        wl = 0.35355f * ( o[0] + o[1] + o[2] + o[3] + o[4] + o[5] + o[6] + o[7]) * kOutG;
        wr = 0.35355f * ( o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7]) * kOutG;
        // 8-point fast Walsh-Hadamard, normalised (orthonormal => lossless mixing)
        hadamard8(y);
        for (int i = 0; i < N; ++i)
            line_[i].push(y[i] + ((i & 1) ? sr : sl) * kInG[i]);
    }
private:
    static constexpr int kModMax = 26;     // samples of line headroom for modulation
    static constexpr float kOutG = 1.0f;
    static constexpr float kInG[N] = { 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f };
    static constexpr float kRateMul[4] = { 1.0f, 1.137f, 0.871f, 1.313f };
    static constexpr double kPhase0[4] = { 0.0, 1.571, 3.142, 4.712 };
    static bool isPrime(int n) {
        if (n < 2) return false;
        for (int d = 2; d * d <= n; ++d) if (n % d == 0) return false;
        return true;
    }
    bool used(int n, int upto) const { for (int j = 0; j < upto; ++j) if (len_[j] == n) return true; return false; }
    static inline void hadamard8(float* v) noexcept {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j) { const float a = v[j], b = v[j + h]; v[j] = a + b; v[j + h] = a - b; }
        for (int i = 0; i < N; ++i) v[i] *= 0.35355339f;
    }
    void update() { for (int i = 0; i < N; ++i) setAbsorber(abs_[i], len_[i], fs_, t60_, damp_); }
    double fs_ = 48000.0;
    double phase_[4] = {}, inc_[4] = {};
    float t60_ = 1.5f, damp_ = 0.3f, exc_ = 0.0f;
    int preLen_ = 1;
    int len_[N] = {};
    Ring line_[N], pre_[2];
    Allpass diff_[2][4];
    Absorber abs_[N];
};

} // namespace studiorev
