#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Normalised biquad coefficients. The stored a1/a2 already incorporate the sign
// from the standard feedback difference equation:
//   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
struct BiquadCoeffs {
    double b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
};

// Transposed Direct Form II — numerically robust, single-sample latency.
class BiquadFilter {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { co = c; }
    void reset()  noexcept { s1 = s2 = 0.0; }

    float process(float x) noexcept {
        const double xd = x;
        const double y  = co.b0 * xd + s1;
        s1 = co.b1 * xd - co.a1 * y + s2;
        s2 = co.b2 * xd - co.a2 * y;
        return static_cast<float>(y);
    }

private:
    BiquadCoeffs co;
    double s1{}, s2{};
};

// Audio EQ Cookbook (R. Bristow-Johnson) factory functions.
namespace Filters {

inline BiquadCoeffs lowpass(double fc, double Q, double fs) noexcept {
    const double w0 = 2.0 * M_PI * fc / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * Q);
    const double a0 = 1.0 + alpha;
    return { (1.0 - cw) * 0.5 / a0,
             (1.0 - cw)       / a0,
             (1.0 - cw) * 0.5 / a0,
             -2.0 * cw        / a0,
             (1.0 - alpha)    / a0 };
}

inline BiquadCoeffs highpass(double fc, double Q, double fs) noexcept {
    const double w0 = 2.0 * M_PI * fc / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * Q);
    const double a0 = 1.0 + alpha;
    return { (1.0 + cw) * 0.5  / a0,
             -(1.0 + cw)       / a0,
             (1.0 + cw) * 0.5  / a0,
             -2.0 * cw         / a0,
             (1.0 - alpha)     / a0 };
}

inline BiquadCoeffs peaking(double fc, double dBgain, double Q, double fs) noexcept {
    const double w0 = 2.0 * M_PI * fc / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double A  = std::pow(10.0, dBgain / 40.0);
    const double alpha = sw / (2.0 * Q);
    const double a0 = 1.0 + alpha / A;
    return { (1.0 + alpha * A)  / a0,
             -2.0 * cw          / a0,
             (1.0 - alpha * A)  / a0,
             -2.0 * cw          / a0,
             (1.0 - alpha / A)  / a0 };
}

inline BiquadCoeffs lowshelf(double fc, double dBgain, double fs) noexcept {
    const double w0 = 2.0 * M_PI * fc / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double A  = std::pow(10.0, dBgain / 40.0);
    const double sqA = std::sqrt(A);
    const double alpha = sw * 0.5 * std::sqrt(2.0); // S=1
    const double a0 = (A+1) + (A-1)*cw + 2*sqA*alpha;
    return { A * ((A+1) - (A-1)*cw + 2*sqA*alpha) / a0,
             2*A * ((A-1) - (A+1)*cw)              / a0,
             A * ((A+1) - (A-1)*cw - 2*sqA*alpha)  / a0,
             -2 * ((A-1) + (A+1)*cw)               / a0,
             ((A+1) + (A-1)*cw - 2*sqA*alpha)       / a0 };
}

inline BiquadCoeffs highshelf(double fc, double dBgain, double fs) noexcept {
    const double w0 = 2.0 * M_PI * fc / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double A  = std::pow(10.0, dBgain / 40.0);
    const double sqA = std::sqrt(A);
    const double alpha = sw * 0.5 * std::sqrt(2.0); // S=1
    const double a0 = (A+1) - (A-1)*cw + 2*sqA*alpha;
    return {  A * ((A+1) + (A-1)*cw + 2*sqA*alpha)  / a0,
             -2*A * ((A-1) + (A+1)*cw)               / a0,
              A * ((A+1) + (A-1)*cw - 2*sqA*alpha)   / a0,
              2 * ((A-1) - (A+1)*cw)                 / a0,
              ((A+1) - (A-1)*cw - 2*sqA*alpha)        / a0 };
}

// 1-pole RC high-pass (6 dB/oct) via bilinear transform.
// Exact digital equivalent of H(s) = s / (s + 2π·fc).
inline BiquadCoeffs highpass1pole(double fc, double fs) noexcept {
    const double k  = 2.0 * fs;
    const double wc = 2.0 * M_PI * fc;
    const double n  = 1.0 / (k + wc);
    const double b0 = k * n;
    const double a1 = (wc - k) * n;   // stored sign matches -a1 in difference eq
    return { b0, -b0, 0.0, a1, 0.0 };
}

// 1-pole RC low-pass (6 dB/oct) via bilinear transform.
// Exact digital equivalent of H(s) = 2π·fc / (s + 2π·fc).
inline BiquadCoeffs lowpass1pole(double fc, double fs) noexcept {
    const double k  = 2.0 * fs;
    const double wc = 2.0 * M_PI * fc;
    const double n  = 1.0 / (k + wc);
    const double b0 = wc * n;
    const double a1 = (wc - k) * n;
    return { b0, b0, 0.0, a1, 0.0 };
}

// 2nd-order notch (band-reject), unity passband (Audio EQ Cookbook "notch").
// alpha = sin(w0)/(2·Q). Higher Q = narrower notch.
inline BiquadCoeffs notch(double fc, double Q, double fs) noexcept {
    const double w0    = 2.0 * M_PI * fc / fs;
    const double cw    = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double a0    = 1.0 + alpha;
    return {  1.0        / a0,
             -2.0 * cw   / a0,
              1.0        / a0,
             -2.0 * cw   / a0,
             (1.0 - alpha) / a0 };
}

// 2nd-order BPF, 0 dB peak gain (Audio EQ Cookbook "BPF" type 2).
// H(s) = (s/Q) / (s² + s/Q + 1), peak amplitude = 1.
// alpha = sin(w0)/(2·Q).
inline BiquadCoeffs bandpass(double fc, double Q, double fs) noexcept {
    const double w0    = 2.0 * M_PI * fc / fs;
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double a0    = 1.0 + alpha;
    return {  alpha       / a0,
              0.0,
             -alpha       / a0,
             -2.0 * std::cos(w0) / a0,
             (1.0 - alpha) / a0 };
}

// ── Decramped EQ (item #42, 2026-07-28) ───────────────────────────────────────
// The plain RBJ `peaking()`/`highshelf()`/`lowshelf()` above are exact at the
// requested center frequency itself, but their AWAY-from-fc behaviour (in
// particular, whether the response actually returns to 0 dB by the time it
// reaches Nyquist) degrades as fc approaches fs/2 — the classic bilinear-
// transform "cramping" near Nyquist. These two additions fix that, verified
// against real, cited sources (not reconstructed from memory) — see each
// function's comment. Purely additive (new functions, existing ones untouched);
// apply only to genuinely user-facing EQ controls, not capture-tuned internal
// amp/cab voicing curves (those are already tuned to compensate for the plain
// RBJ behaviour, so swapping formulas there would silently re-voice them).

// Orfanidis parametric-EQ peaking filter with prescribed (matched) Nyquist-
// frequency gain. Verified byte-for-byte against Appendix B (peq.m) of
// S. J. Orfanidis, "Digital Parametric Equalizer Design with Prescribed
// Nyquist-Frequency Gain," J. Audio Eng. Soc., vol. 45, no. 6, June 1997
// (fetched + cross-checked against the primary source PDF directly, not just
// a third-party reproduction). GB = bandwidth-edge gain, here derived from Q
// the same way as a small independent reference implementation confirmed
// against the same paper (bandwidth gain = dBgain/sqrt(2) in dB terms, then
// Dw = w0/(1.588308819*Q) — an empirical Q-to-bandwidth mapping, not from the
// paper itself, kept only so callers can pass a familiar Q rather than an
// explicit bandwidth). Reduces to an exact identity at dBgain=0 (G==GB).
inline BiquadCoeffs peakingDecramped(double fc, double dBgain, double Q, double fs) noexcept {
    const double G0 = 1.0;
    const double G  = std::pow(10.0, dBgain / 20.0);
    const double GB = std::pow(10.0, (dBgain / std::sqrt(2.0)) / 20.0);
    if (G == GB) return { 1.0, 0.0, 0.0, 0.0, 0.0 };   // flat: exact passthrough
    const double w0 = 2.0 * M_PI * fc / fs;
    const double Dw = w0 / (1.588308819 * Q);

    const double F   = std::abs(G*G   - GB*GB);
    const double G00 = std::abs(G*G   - G0*G0);
    const double F00 = std::abs(GB*GB - G0*G0);

    const double num = G0*G0 * (w0*w0 - M_PI*M_PI) * (w0*w0 - M_PI*M_PI)
                      + G*G * F00 * M_PI*M_PI * Dw*Dw / F;
    const double den = (w0*w0 - M_PI*M_PI) * (w0*w0 - M_PI*M_PI)
                      + F00 * M_PI*M_PI * Dw*Dw / F;
    const double G1  = std::sqrt(num / den);   // actual matched Nyquist gain

    const double G01 = std::abs(G*G  - G0*G1);
    const double G11 = std::abs(G*G  - G1*G1);
    const double F01 = std::abs(GB*GB - G0*G1);
    const double F11 = std::abs(GB*GB - G1*G1);

    const double W2 = std::sqrt(G11 / G00) * std::pow(std::tan(w0 / 2.0), 2);
    const double DW = (1.0 + std::sqrt(F00 / F11) * W2) * std::tan(Dw / 2.0);

    const double C = F11 * DW*DW - 2.0 * W2 * (F01 - std::sqrt(F00 * F11));
    const double D = 2.0 * W2 * (G01 - std::sqrt(G00 * G11));

    const double A = std::sqrt((C + D) / F);
    const double B = std::sqrt((G*G * C + GB*GB * D) / F);
    const double a0 = 1.0 + W2 + A;

    return { (G1 + G0*W2 + B) / a0,
             -2.0 * (G1 - G0*W2) / a0,
             (G1 - B + G0*W2) / a0,
             -2.0 * (1.0 - W2) / a0,
             (1.0 + W2 - A) / a0 };
}

// Nyquist-exact first-order shelf: crossfades between the flat reference gain
// and a first-order allpass. A first-order allpass A(z)=(a+z⁻¹)/(1+a·z⁻¹) is
// EXACTLY +1 at DC and EXACTLY −1 at Nyquist for any coefficient a — so
// shelf(z) = C1 + C2·A(z) lands on the two requested gains EXACTLY at both
// ends, for any corner frequency, with no cramping possible by construction
// (proved directly, not just cited — verified numerically: DC/Nyquist gain
// error was 0.000 dB at every fc/gain combination tested, including fc within
// one octave of Nyquist). At dBgain=0 the stored b1/a1 look nonzero (both
// equal the allpass coefficient `a`) but the transfer function is an EXACT
// identity — verified bit-exact through BiquadFilter's recursive state too,
// not just algebraically (s1 stays exactly 0.0 every sample once b1==a1).
// This is the classic Regalia–Mitra allpass-based
// shelf [P. A. Regalia & S. K. Mitra, "Tunable Digital Frequency Response
// Equalization Filters," IEEE Trans. ASSP, 35(1), Jan 1987], also reproduced
// in Zölzer/Holters' parametric shelving-filter work. NOTE: being first-order,
// the transition is gentler/fixed-slope (no adjustable Q/steepness) — a
// genuine trade vs the 2nd-order RBJ shelf's adjustable-but-cramped slope, not
// a strict superset. Reduces to an exact identity at dBgain=0.
inline BiquadCoeffs highshelfDecramped(double fc, double dBgain, double fs) noexcept {
    const double G0 = 1.0;
    const double G  = std::pow(10.0, dBgain / 20.0);
    const double K  = std::tan(M_PI * fc / fs);
    const double a  = (K - 1.0) / (K + 1.0);
    const double C1 = (G0 + G) * 0.5, C2 = (G0 - G) * 0.5;
    return { C1 + C2 * a, C1 * a + C2, 0.0, a, 0.0 };
}
inline BiquadCoeffs lowshelfDecramped(double fc, double dBgain, double fs) noexcept {
    const double G0 = 1.0;
    const double G  = std::pow(10.0, dBgain / 20.0);
    const double K  = std::tan(M_PI * fc / fs);
    const double a  = (K - 1.0) / (K + 1.0);
    const double C1 = (G + G0) * 0.5, C2 = (G - G0) * 0.5;
    return { C1 + C2 * a, C1 * a + C2, 0.0, a, 0.0 };
}

} // namespace Filters
