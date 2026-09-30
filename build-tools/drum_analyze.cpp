// Drum hit analyser: turn a recorded hit into a compact parametric model.
//
// The goal is to stop shipping audio. A recorded drum hit decomposes into two
// perceptually separate things, and each has a far cheaper representation than
// the samples themselves:
//
//   SINES   the discrete resonances of the shell and heads. Peak-pick the
//           spectrum, then for each peak complex-demodulate the signal down to
//           DC and fit the log-amplitude decay. That yields exact frequency,
//           amplitude, decay rate and phase per mode — the parameters a damped
//           sinusoid rotator needs. Roughly 50 modes at 16 bytes each.
//
//   NOISE   what is left once the sines are resynthesised and subtracted:
//           wire rattle, cymbal wash, stick contact. This is NOT a few
//           sinusoids — a cymbal has thousands of closely-spaced modes — so
//           modelling it as sines is hopeless. Instead we keep only its
//           spectral envelope over time (band energies per frame) and
//           resynthesise by shaping white noise through it. A 3 s crash costs
//           tens of kB instead of half a megabyte.
//
// This is Serra & Smith's spectral modelling synthesis, specialised to
// percussive one-shots.
//
// STATUS / KNOWN LIMITATION (measured against synth renders with known modes):
// each mode is modelled as ONE exponentially decaying sinusoid at a FIXED
// frequency. That assumption holds well for a snare shell (recovers 185 Hz
// against a true 185 Hz, and scores 5.3 dB band-distance) and is irrelevant
// for cymbals and hats, which are pure noise-model material (12-16 dB, and the
// sines stage correctly extracts nothing). It FAILS for:
//
//   * frequency-swept fundamentals -- a kick's head tension drops the pitch
//     from ~112 Hz to ~47 Hz over the first tens of ms. No fixed sinusoid fits
//     that, and subtracting one ADDS energy; the guard in the pursuit loop now
//     detects this and stops rather than corrupting the residual.
//   * non-exponential decays -- a tom rings in two stages, which the pursuit
//     tries to approximate by stacking several exponentials at one frequency.
//
// In both cases the leftover is TONAL, but the second stage models the
// residual as stochastic, so it resynthesises tonal content as noise and
// sounds wrong (40-48 dB).
//
// THE FIX is to give each partial a time-varying amplitude AND frequency
// envelope (sampled every few ms, like the noise bands) instead of a single
// exponential -- i.e. real partial tracking, which is what full SMS does and
// which this deliberately simplified away. That is the next piece of work if
// this route is pursued.
//
// The analyser is offline and does not have to be fast, so it favours the
// obvious method over the clever one throughout.
//
// Build and run (WSL):
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include
//       build-tools/drum_analyze.cpp -o /tmp/drum_analyze
//   /tmp/drum_analyze <hit.wav> [out_dir]
#include "BiquadFilter.h"
#include "WavRead.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Model ────────────────────────────────────────────────────────────────────

struct Mode {
    double freq;        // Hz
    double amp;         // linear, at the onset
    double t60;         // seconds to -60 dB
    double phase;       // radians
};

struct NoiseModel {
    static constexpr int kBands = 24;
    double hopSeconds{0.005};
    std::vector<std::array<float, kBands>> frames;   // per-band linear RMS
    std::array<float, kBands> centre{};              // band centre frequencies
};

struct HitModel {
    std::vector<Mode> modes;
    NoiseModel        noise;
    std::vector<float> transient;    // optional few ms of real audio
    double fs{48000.0};
    std::string name;
};

// ── FFT (radix-2, offline) ───────────────────────────────────────────────────

static void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * M_PI / double(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k]             = u + v;
                a[i + k + len / 2]   = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= double(n);
}

static size_t nextPow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

// First sample crossing a fraction of the peak — a drum hit's onset is
// unambiguous, so nothing cleverer than a threshold is warranted.
static size_t findOnset(const std::vector<float>& x) {
    float pk = 0.0f;
    for (float s : x) pk = std::max(pk, std::fabs(s));
    if (pk <= 0.0f) return 0;
    const float thr = pk * 0.02f;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::fabs(x[i]) >= thr) return (i > 64) ? i - 64 : 0;   // small pre-roll
    return 0;
}

static std::vector<double> hann(size_t n) {
    std::vector<double> w(n);
    for (size_t i = 0; i < n; ++i) w[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / double(n - 1));
    return w;
}

// ── Mode extraction ──────────────────────────────────────────────────────────

// Spectral peaks, parabolically interpolated for sub-bin frequency accuracy.
// A bin-accurate frequency would be audibly wrong: at 48 kHz with a 32k FFT the
// bin spacing is 1.5 Hz, which is enough to beat against the original.
static std::vector<double> findPeaks(const std::vector<float>& x, double fs,
                                     int maxPeaks, double minFreq, double maxFreq,
                                     double minSepHz) {
    const size_t n = nextPow2(std::min<size_t>(x.size(), 1u << 16));
    std::vector<std::complex<double>> spec(n, {0.0, 0.0});
    const auto w = hann(std::min(n, x.size()));
    for (size_t i = 0; i < std::min(n, x.size()); ++i) spec[i] = x[i] * w[i];
    fft(spec, false);

    const size_t half = n / 2;
    std::vector<double> mag(half);
    for (size_t i = 0; i < half; ++i) mag[i] = std::abs(spec[i]);

    struct Cand { double freq, mag; };
    std::vector<Cand> cands;
    const double binHz = fs / double(n);
    for (size_t i = 1; i + 1 < half; ++i) {
        if (mag[i] <= mag[i - 1] || mag[i] < mag[i + 1]) continue;
        const double f0 = i * binHz;
        if (f0 < minFreq || f0 > maxFreq) continue;
        // Parabolic interpolation on the log magnitude.
        const double a = std::log(mag[i - 1] + 1e-20);
        const double b = std::log(mag[i]     + 1e-20);
        const double c = std::log(mag[i + 1] + 1e-20);
        const double d = 0.5 * (a - c) / (a - 2.0 * b + c + 1e-30);
        cands.push_back({ (i + std::clamp(d, -0.5, 0.5)) * binHz, mag[i] });
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& p, const Cand& q) { return p.mag > q.mag; });

    std::vector<double> out;
    for (const Cand& c : cands) {
        if (static_cast<int>(out.size()) >= maxPeaks) break;
        bool tooClose = false;
        for (double f : out) if (std::fabs(f - c.freq) < minSepHz) { tooClose = true; break; }
        if (!tooClose) out.push_back(c.freq);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Complex-demodulate around `freq` and fit the amplitude decay. Returns false
// if the mode is too weak or does not actually decay like a damped sinusoid,
// which is how we avoid fitting modes to noise-floor bumps.
// `qualityOut` receives the R-squared of the exponential-decay fit. This is the
// sines/noise discriminator: a real mode's demodulated envelope IS a decaying
// exponential and fits with R^2 near 1, whereas a noise-band peak has a
// fluctuating envelope that fits one badly. Without this test the analyser
// happily fits dozens of 'modes' to a snare's wire noise.
static bool fitMode(const std::vector<float>& x, double fs, double freq, Mode& out,
                    double* qualityOut, double* r2Out = nullptr, double* cohOut = nullptr) {
    const size_t n = x.size();
    // Shift to DC and low-pass with a one-pole per quadrature. The bandwidth
    // has to admit the mode's own decay (a fast-decaying mode is spectrally
    // wide) without admitting its neighbours.
    const double bw   = std::max(12.0, freq * 0.03);
    const double a    = std::exp(-2.0 * M_PI * bw / fs);
    std::complex<double> lp(0.0, 0.0);
    std::vector<double> env(n);
    std::vector<double> ph(n);

    const double wRad = 2.0 * M_PI * freq / fs;
    const std::complex<double> step(std::cos(-wRad), std::sin(-wRad));
    std::complex<double> rot(1.0, 0.0);
    for (size_t i = 0; i < n; ++i) {
        lp = (1.0 - a) * (double(x[i]) * rot) + a * lp;
        env[i] = std::abs(lp);
        ph[i]  = std::arg(lp);
        rot *= step;
    }

    double peak = 0.0; size_t peakAt = 0;
    for (size_t i = 0; i < n; ++i) if (env[i] > peak) { peak = env[i]; peakAt = i; }
    if (peak < 1e-6) return false;

    // Weighted least squares on ln(env) from the peak down to -40 dB. Weighting
    // by amplitude keeps the noise floor at the tail from dragging the slope.
    const double floorLevel = peak * std::pow(10.0, -40.0 / 20.0);
    double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    size_t used = 0;
    for (size_t i = peakAt; i < n; ++i) {
        if (env[i] < floorLevel) break;
        const double t  = double(i - peakAt) / fs;
        const double y  = std::log(env[i]);
        const double wt = env[i] / peak;
        sw += wt; sx += wt * t; sy += wt * y; sxx += wt * t * t; sxy += wt * t * y;
        ++used;
    }
    if (used < 64) return false;

    const double denom = sw * sxx - sx * sx;
    if (std::fabs(denom) < 1e-20) return false;
    const double slope     = (sw * sxy - sx * sy) / denom;
    const double intercept = (sy - slope * sx) / sw;
    if (slope >= 0.0) return false;                       // not decaying: not a mode

    const double t60 = -std::log(1000.0) / slope;
    if (t60 < 0.004 || t60 > 30.0) return false;

    // R^2 of the same weighted fit — how exponential this envelope really is.
    const double ybar = sy / sw;
    double ssRes = 0.0, ssTot = 0.0;
    for (size_t i = peakAt; i < n; ++i) {
        if (env[i] < floorLevel) break;
        const double t  = double(i - peakAt) / fs;
        const double y  = std::log(env[i]);
        const double wt = env[i] / peak;
        const double yh = intercept + slope * t;
        ssRes += wt * (y - yh) * (y - yh);
        ssTot += wt * (y - ybar) * (y - ybar);
    }
    const double r2 = (ssTot > 1e-20) ? (1.0 - ssRes / ssTot) : 0.0;

    // R^2 alone is NOT enough to tell a mode from noise. Narrowband noise whose
    // source decays exponentially — a snare's wires, a cymbal's wash — has an
    // exponentially decaying envelope too, and sails through the R^2 test. The
    // property that genuinely separates them is PHASE COHERENCE: a real mode
    // has one constant frequency, so its demodulated phase is flat, whereas
    // filtered noise has a phase that random-walks across the filter's
    // bandwidth. So we measure how much the instantaneous frequency wanders.
    // Unwrap the demodulated phase and fit a straight line to it. A true mode's
    // phase is linear in time (a constant offset if our frequency estimate is
    // exact, a constant slope if it is slightly off — both fine). Noise wanders
    // away from any line. So the RMS residual about that line, in radians, is
    // the measure: near 0 for a tone, order 1 radian for noise.
    //
    // Differencing the phase sample by sample was the wrong estimator — at a
    // 12 Hz demodulation bandwidth the per-sample increment is far smaller than
    // its own noise, and every mode failed. Fitting over the whole decay
    // averages that noise away.
    double swc = 0.0, sxc = 0.0, syc = 0.0, sxxc = 0.0, sxyc = 0.0;
    std::vector<double> tt, uu, ww;
    {
        double unwrapped = ph[peakAt], prev = ph[peakAt];
        for (size_t i = peakAt; i < n; i += 16) {         // decimated: phase is smooth
            if (env[i] < floorLevel) break;
            double d = ph[i] - prev;
            prev = ph[i];
            while (d >  M_PI) d -= 2.0 * M_PI;
            while (d < -M_PI) d += 2.0 * M_PI;
            unwrapped += d;

            const double t  = double(i - peakAt) / fs;
            const double wt = env[i] / peak;
            tt.push_back(t); uu.push_back(unwrapped); ww.push_back(wt);
            swc += wt; sxc += wt * t; syc += wt * unwrapped;
            sxxc += wt * t * t; sxyc += wt * t * unwrapped;
        }
    }
    double coherence = 0.0;
    if (tt.size() >= 8 && swc > 1e-12) {
        const double den = swc * sxxc - sxc * sxc;
        if (std::fabs(den) > 1e-20) {
            const double sl = (swc * sxyc - sxc * syc) / den;
            const double ic = (syc - sl * sxc) / swc;
            double acc = 0.0;
            for (size_t j = 0; j < tt.size(); ++j) {
                const double r = uu[j] - (ic + sl * tt[j]);
                acc += ww[j] * r * r;
            }
            const double rmsRad = std::sqrt(acc / swc);
            coherence = std::clamp(1.0 - rmsRad / 1.2, 0.0, 1.0);
        }
    }

    if (r2Out)      *r2Out      = r2;
    if (cohOut)     *cohOut     = coherence;
    if (qualityOut) *qualityOut = r2 * coherence;

    // Amplitude at sample 0, extrapolated back along the fitted decay, but
    // clamped: extrapolating through the attack can overshoot wildly, and an
    // over-large mode would subtract more than is there.
    // `peak` is max|demodulated| = A/2, so the honest amplitude is 2*peak. The
    // back-extrapolation may only nudge that: allowing 4x (as it first did) let
    // a mis-fitted slope invent amplitude that was never in the signal, and
    // matching pursuit then subtracted more than was there.
    const double ampAtPeak = std::exp(intercept) * 2.0;
    const double amp0 = std::min(ampAtPeak * std::exp(-slope * double(peakAt) / fs),
                                 peak * 2.2);

    out.freq  = freq;
    out.t60   = t60;
    out.amp   = amp0;
    // Demodulating by exp(-jwt) leaves (A/2)exp(j(phi - pi/2)), and that phase
    // is time-invariant for a damped sinusoid, so it IS the mode's phase at
    // t = 0 once the quarter turn is added back.
    out.phase = ph[peakAt] + M_PI * 0.5;
    return true;
}

// ── Resynthesis ──────────────────────────────────────────────────────────────

static std::vector<float> synthModes(const std::vector<Mode>& modes, double fs, size_t n) {
    std::vector<float> y(n, 0.0f);
    for (const Mode& m : modes) {
        const double w = 2.0 * M_PI * m.freq / fs;
        const double r = std::exp(-std::log(1000.0) / (m.t60 * fs));
        const double cr = r * std::cos(w), ci = r * std::sin(w);
        double re = m.amp * std::cos(m.phase), im = m.amp * std::sin(m.phase);
        for (size_t i = 0; i < n; ++i) {
            y[i] += static_cast<float>(im);
            const double nr = re * cr - im * ci;
            const double ni = re * ci + im * cr;
            re = nr; im = ni;
        }
    }
    return y;
}

// Log-spaced band energies of the residual, frame by frame. This IS the noise
// model: no phase, no waveform, just how much energy sits in each band at each
// moment. Resynthesis re-imposes it on white noise, which is indistinguishable
// for stochastic content and costs a few kB.
static NoiseModel analyseNoise(const std::vector<float>& res, double fs) {
    NoiseModel nm;
    const size_t win = 512;
    const size_t hop = static_cast<size_t>(nm.hopSeconds * fs);
    const auto w = hann(win);

    for (int b = 0; b < NoiseModel::kBands; ++b) {
        const double lo = 40.0, hi = std::min(fs * 0.45, 16000.0);
        nm.centre[b] = static_cast<float>(lo * std::pow(hi / lo, double(b) / (NoiseModel::kBands - 1)));
    }

    for (size_t start = 0; start + win < res.size(); start += hop) {
        std::vector<std::complex<double>> spec(win, {0.0, 0.0});
        for (size_t i = 0; i < win; ++i) spec[i] = res[start + i] * w[i];
        fft(spec, false);

        std::array<float, NoiseModel::kBands> e{};
        std::array<int, NoiseModel::kBands>   cnt{};
        for (size_t k = 1; k < win / 2; ++k) {
            const double f = double(k) * fs / double(win);
            int band = 0;
            double best = 1e30;
            for (int b = 0; b < NoiseModel::kBands; ++b) {
                const double d = std::fabs(std::log(f + 1e-9) - std::log(double(nm.centre[b])));
                if (d < best) { best = d; band = b; }
            }
            e[band] += static_cast<float>(std::norm(spec[k]));
            cnt[band]++;
        }
        // Parseval for a windowed frame: the per-band RMS of the underlying
        // signal is sqrt(2 * sum|X|^2 / (W^2 * mean(w^2))), and Hann's
        // mean(w^2) is 3/8. Getting this right matters because the synthesis
        // side is calibrated to reproduce exactly this quantity, so an
        // arbitrary scale factor here would come back as a level error.
        constexpr double kHannMeanSq = 0.375;
        for (int b = 0; b < NoiseModel::kBands; ++b)
            e[b] = (cnt[b] > 0)
                 ? static_cast<float>(std::sqrt(2.0 * double(e[b]) /
                       (double(win) * double(win) * kHannMeanSq)))
                 : 0.0f;
        nm.frames.push_back(e);
    }
    return nm;
}

// One-pole state-variable bandpass per band, fed white noise, gains
// interpolated between analysis frames.
static std::vector<float> synthNoise(const NoiseModel& nm, double fs, size_t n) {
    std::vector<float> y(n, 0.0f);
    if (nm.frames.empty()) return y;

    const size_t hop = static_cast<size_t>(nm.hopSeconds * fs);
    uint32_t rng = 0x13579BDFu;
    auto white = [&]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return float(int32_t(rng)) * (1.0f / 2147483648.0f);
    };

    // Biquad bandpasses, not the Chamberlin state-variable filter I reached for
    // first: that form is only conditionally stable and blew up (to NaN) on the
    // top bands, where its tuning coefficient approaches its stability limit.
    // A cookbook biquad is stable at any centre frequency.
    std::array<BiquadFilter, NoiseModel::kBands> filt{};
    for (int b = 0; b < NoiseModel::kBands; ++b)
        filt[b].setCoeffs(Filters::bandpass(
            std::min(double(nm.centre[b]), fs * 0.45), 1.4, fs));

    // Calibrate each band: measure what unit white noise actually comes out at,
    // then scale so a model gain of g yields an output RMS of g. Without this
    // the band gains are in whatever units the filter happens to have, and the
    // analysis side's careful RMS figures would be multiplied by an arbitrary
    // constant that varies with centre frequency.
    std::array<double, NoiseModel::kBands> corr{};
    {
        const int kCal = 8192;
        uint32_t cr = 0x2468ACEu;
        auto cwhite = [&]() {
            cr ^= cr << 13; cr ^= cr >> 17; cr ^= cr << 5;
            return double(int32_t(cr)) * (1.0 / 2147483648.0);
        };
        for (int b = 0; b < NoiseModel::kBands; ++b) {
            BiquadFilter s = filt[b];
            double acc = 0.0;
            for (int i = 0; i < kCal; ++i) {
                const float y = s.process(static_cast<float>(cwhite()));
                if (i > 512) acc += double(y) * y;    // skip the filter's start-up
            }
            const double rms = std::sqrt(acc / double(kCal - 512));
            // The calibration noise has RMS 1/sqrt(3); divide that out so the
            // correction describes the filter alone.
            corr[b] = (rms > 1e-12) ? (1.0 / rms) * (1.0 / std::sqrt(3.0)) : 0.0;
        }
    }

    for (size_t i = 0; i < n; ++i) {
        const double fpos = double(i) / double(hop);
        const size_t f0 = std::min(nm.frames.size() - 1, size_t(fpos));
        const size_t f1 = std::min(nm.frames.size() - 1, f0 + 1);
        const float  fr = static_cast<float>(fpos - double(f0));

        const float x = white();
        float acc = 0.0f;
        for (int b = 0; b < NoiseModel::kBands; ++b) {
            const float bp = filt[b].process(x);
            const float g  = nm.frames[f0][b] * (1.0f - fr) + nm.frames[f1][b] * fr;
            acc += bp * static_cast<float>(corr[b]) * g;
        }
        y[i] = acc;
    }
    return y;
}

// ── WAV out ──────────────────────────────────────────────────────────────────

static void writeWav(const std::string& path, const std::vector<float>& x, double fs) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("  !! cannot write %s\n", path.c_str()); return; }
    const uint32_t n = uint32_t(x.size()), dataSize = n * 2u, rate = uint32_t(fs);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36u + dataSize); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16u); u16(1); u16(1);
    u32(rate); u32(rate * 2u); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataSize);
    for (float s : x) {
        const float c = std::clamp(s, -1.0f, 1.0f);
        const int16_t v = int16_t(std::lround(c * 32767.0f));
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

// ── Scoring ──────────────────────────────────────────────────────────────────

// Log-spectral distance over time. Waveform difference is the wrong measure for
// resynthesis: the noise part is stochastic and will never match sample for
// sample, yet can be perceptually identical. Comparing band energies over time
// asks the question that matters — does it have the same spectrum evolving the
// same way?
static double spectralDistanceDb(const std::vector<float>& a, const std::vector<float>& b, double fs) {
    const size_t win = 1024, hop = 256;
    const auto w = hann(win);
    double acc = 0.0; int frames = 0;
    const size_t n = std::min(a.size(), b.size());

    for (size_t s = 0; s + win < n; s += hop) {
        std::vector<std::complex<double>> A(win), B(win);
        for (size_t i = 0; i < win; ++i) { A[i] = a[s + i] * w[i]; B[i] = b[s + i] * w[i]; }
        fft(A, false); fft(B, false);

        // Compare 1/3-octave BAND energies, not individual bins. The noise half
        // of the model is stochastic by construction and will never match the
        // original bin for bin, yet can be perceptually identical; a per-bin
        // comparison would score a perfect noise model as badly wrong. Bands
        // ask the question that matters: is the same energy in the same place
        // at the same time?
        constexpr int kCmpBands = 24;
        double ea[kCmpBands] = {}, eb[kCmpBands] = {};
        int    cn[kCmpBands] = {};
        const double lo = 50.0, hi = 12000.0;
        for (size_t k = 1; k < win / 2; ++k) {
            const double f = double(k) * fs / double(win);
            if (f < lo || f > hi) continue;
            int bnd = int(std::log(f / lo) / std::log(hi / lo) * (kCmpBands - 1) + 0.5);
            bnd = std::clamp(bnd, 0, kCmpBands - 1);
            ea[bnd] += std::norm(A[k]);
            eb[bnd] += std::norm(B[k]);
            cn[bnd]++;
        }
        double frameAcc = 0.0; int cnt = 0;
        for (int b = 0; b < kCmpBands; ++b) {
            if (!cn[b]) continue;
            const double pa = 10.0 * std::log10(ea[b] / cn[b] + 1e-12);
            const double pb = 10.0 * std::log10(eb[b] / cn[b] + 1e-12);
            frameAcc += (pa - pb) * (pa - pb);
            ++cnt;
        }
        if (cnt) { acc += std::sqrt(frameAcc / cnt); ++frames; }
    }
    return frames ? acc / frames : 0.0;
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: drum_analyze <hit.wav> [out_dir] [--transient-ms N] [--modes N]\n");
        return 2;
    }
    const std::string inPath = argv[1];
    const std::string outDir = (argc > 2 && argv[2][0] != '-') ? argv[2] : ".";

    int maxModes = 48;
    double transientMs = 0.0;
    bool verbose = false;
    double minQuality = -1.0;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--modes") && i + 1 < argc)         maxModes = std::atoi(argv[++i]);
        if (!std::strcmp(argv[i], "--verbose"))                       verbose = true;
        if (!std::strcmp(argv[i], "--min-quality") && i + 1 < argc)   minQuality = std::atof(argv[++i]);
        if (!std::strcmp(argv[i], "--transient-ms") && i + 1 < argc)  transientMs = std::atof(argv[++i]);
    }

    std::vector<float> L, R;
    uint32_t rate = 0;
    if (!wavread::readWav(inPath.c_str(), L, R, rate) || L.empty()) {
        std::printf("cannot read %s\n", inPath.c_str());
        return 1;
    }
    if (!R.empty()) for (size_t i = 0; i < L.size(); ++i) L[i] = 0.5f * (L[i] + R[i]);
    const double fs = rate;

    // Trim to the hit.
    const size_t onset = findOnset(L);
    std::vector<float> x(L.begin() + onset, L.end());
    float pk = 0.0f;
    for (float s : x) pk = std::max(pk, std::fabs(s));
    std::printf("%s\n  %zu frames @ %.0f Hz, peak %.3f\n", inPath.c_str(), x.size(), fs, pk);

    // ── Sines, by matching pursuit ───────────────────────────────────────────
    // Each accepted mode is SUBTRACTED before the next is looked for. Fitting
    // every spectral peak against the original independently is what made the
    // first version stack forty "modes" onto one noise band and end up with
    // five times the energy it started with — each fit claimed the same
    // spectrum. Subtracting as we go keeps the energy budget honest and lets
    // the search stop naturally once only noise is left.
    double kMinQuality = (minQuality >= 0.0) ? minQuality : 0.45;   // R^2 * phase coherence

    HitModel model;
    model.fs = fs;
    std::vector<float> work = x;

    for (int k = 0; k < maxModes; ++k) {
        const auto peaks = findPeaks(work, fs, 48, 30.0, std::min(fs * 0.45, 16000.0), 15.0);
        if (peaks.empty()) break;

        Mode   best{};
        double bestScore = 0.0;
        bool   found = false;
        for (double f : peaks) {
            // Don't re-fit a frequency we already took. Without this the
            // pursuit approximates a NON-exponential decay (a tom's two-stage
            // ring) by stacking several exponentials at the same frequency —
            // it lowers the residual but bloats the model and, because each
            // one slightly overshoots, makes the resynthesis worse.
            bool dup = false;
            for (const Mode& got : model.modes)
                if (std::fabs(got.freq - f) < std::max(8.0, f * 0.005)) { dup = true; break; }
            if (dup) continue;

            Mode m; double q = 0.0, r2d = 0.0, cohd = 0.0;
            if (!fitMode(work, fs, f, m, &q, &r2d, &cohd)) continue;
            if (verbose && k == 0 && m.amp > 0.005)
                std::printf("      cand %8.1f Hz  amp %.4f  r2 %.3f  coh %.3f  q %.3f\n",
                            f, m.amp, r2d, cohd, q);
            if (q < kMinQuality) continue;
            const double score = m.amp * q;      // loud AND genuinely tonal
            if (score > bestScore) { bestScore = score; best = m; found = true; }
        }
        if (!found) break;                       // nothing tonal remains

        // Only keep the mode if removing it actually removes energy. A
        // frequency-MODULATED component — a kick's fundamental sweeping from
        // 112 Hz down to 47 — is not a stationary damped sinusoid, so
        // subtracting a fixed-frequency fit for it ADDS energy rather than
        // removing it. Measuring instead of assuming turns that from a silent
        // corruption into a clean stopping condition.
        const std::vector<float> one = synthModes({ best }, fs, work.size());
        double before = 0.0, after = 0.0;
        for (size_t i = 0; i < work.size(); ++i) {
            before += double(work[i]) * work[i];
            const double r = double(work[i]) - one[i];
            after += r * r;
        }
        if (after >= before) break;              // this fit does not help: stop

        model.modes.push_back(best);
        for (size_t i = 0; i < work.size(); ++i) work[i] -= one[i];
    }

    std::printf("  %zu modes extracted (stopped when no peak fitted an exponential)\n",
                model.modes.size());
    {
        std::vector<Mode> byAmp = model.modes;
        std::sort(byAmp.begin(), byAmp.end(),
                  [](const Mode& a, const Mode& b) { return a.amp > b.amp; });
        for (size_t i = 0; i < std::min<size_t>(8, byAmp.size()); ++i)
            std::printf("    %2zu  %8.1f Hz  amp %.4f  t60 %6.3f s\n",
                        i, byAmp[i].freq, byAmp[i].amp, byAmp[i].t60);
    }

    const std::vector<float> sines = synthModes(model.modes, fs, x.size());

    // ── Noise residual ───────────────────────────────────────────────────────
    const std::vector<float> residual = work;   // exactly what the sines left

    double eSig = 0.0, eRes = 0.0;
    for (size_t i = 0; i < x.size(); ++i) { eSig += double(x[i]) * x[i]; eRes += double(residual[i]) * residual[i]; }
    std::printf("  sines capture %.1f%% of the energy; residual is the noise part\n",
                100.0 * (1.0 - eRes / (eSig + 1e-20)));

    model.noise = analyseNoise(residual, fs);
    const std::vector<float> noise = synthNoise(model.noise, fs, x.size());

    // ── Recombine and score ──────────────────────────────────────────────────
    std::vector<float> resynth(x.size());
    for (size_t i = 0; i < x.size(); ++i) resynth[i] = sines[i] + noise[i];

    // Optional real-audio transient spliced over the front.
    const size_t tLen = static_cast<size_t>(transientMs * 1e-3 * fs);
    if (tLen > 0 && tLen < x.size()) {
        model.transient.assign(x.begin(), x.begin() + tLen);
        for (size_t i = 0; i < tLen; ++i) {
            const float w = float(i) / float(tLen);       // crossfade into the model
            resynth[i] = x[i] * (1.0f - w) + resynth[i] * w;
        }
    }

    const double dSines   = spectralDistanceDb(x, sines, fs);
    const double dFull    = spectralDistanceDb(x, resynth, fs);
    std::printf("  log-spectral distance: sines only %.2f dB, sines+noise %.2f dB\n", dSines, dFull);

    // ── Size accounting: the whole point of the exercise ─────────────────────
    const size_t bytesModes = model.modes.size() * 4 * sizeof(float);
    const size_t bytesNoise = model.noise.frames.size() * NoiseModel::kBands * sizeof(uint8_t);
    const size_t bytesTrans = model.transient.size() * sizeof(int16_t);
    const size_t bytesWav   = x.size() * sizeof(int16_t);
    std::printf("  size: model %zu B (modes %zu + noise %zu + transient %zu) vs WAV %zu B  = %.1fx smaller\n",
                bytesModes + bytesNoise + bytesTrans, bytesModes, bytesNoise, bytesTrans,
                bytesWav, double(bytesWav) / double(bytesModes + bytesNoise + bytesTrans + 1));

    // ── Artefacts for listening ──────────────────────────────────────────────
    std::string base = inPath;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);

    writeWav(outDir + "/" + base + "_orig.wav",     x,        fs);
    writeWav(outDir + "/" + base + "_resynth.wav",  resynth,  fs);
    writeWav(outDir + "/" + base + "_sines.wav",    sines,    fs);
    writeWav(outDir + "/" + base + "_residual.wav", residual, fs);
    std::printf("  wrote %s_{orig,resynth,sines,residual}.wav\n\n", base.c_str());
    return 0;
}
