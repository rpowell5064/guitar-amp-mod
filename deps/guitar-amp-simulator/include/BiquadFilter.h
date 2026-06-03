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

} // namespace Filters
