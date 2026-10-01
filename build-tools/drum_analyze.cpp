// Drum hit analyser: turn a recorded hit into a compact parametric model.
//
// The goal is to ship no audio at all. A recorded hit decomposes into two
// perceptually separate parts, each far cheaper than the samples themselves:
//
//   PARTIALS  the discrete resonances of shell and heads, followed frame by
//             frame as tracks of (frequency, amplitude) over time.
//   NOISE     everything else — wire rattle, cymbal wash, stick contact —
//             stored only as a spectral envelope over time (band energies) and
//             resynthesised by shaping white noise through it.
//
// This is Serra & Smith's spectral modelling synthesis with McAulay-Quatieri
// partial tracking for the sinusoidal stage.
//
// WHY TRACKING, and not the simpler thing:
// The first version of this file modelled each partial as ONE exponentially
// decaying sinusoid at a FIXED frequency, extracted by matching pursuit. That
// works for a snare shell but fails on two cases that matter:
//
//   * A kick's head tension drops its fundamental from ~112 Hz to ~47 Hz over
//     the first few tens of milliseconds. No fixed sinusoid fits a sweep, and
//     subtracting one ADDS energy rather than removing it.
//   * A tom rings in two stages, which a single exponential cannot describe;
//     the pursuit tried to approximate it by stacking several exponentials at
//     the same frequency, which bloated the model and made it sound worse.
//
// Tracking handles both natively: a partial is free to glide and to have any
// decay shape, because its frequency and amplitude are simply sampled.
//
// The residual is also computed differently, and this matters. Subtracting
// resynthesised sinusoids in the TIME domain requires their phase to match the
// original almost exactly — get it wrong and subtraction doubles the energy
// instead of cancelling it, which is precisely how the earlier version ended up
// with more energy than it started with. Instead we suppress the tracked peaks
// in each frame's SPECTRUM and read the noise model straight off what remains.
// No phase accuracy required, and over-subtraction becomes impossible.
//
// The analyser is offline, so it favours the obvious method over the clever one.
//
// Build and run (WSL):
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include
//       build-tools/drum_analyze.cpp -o /tmp/drum_analyze
//   /tmp/drum_analyze <hit.wav> [out_dir] [--verbose]
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

// ── Analysis geometry ────────────────────────────────────────────────────────
// A 1024-sample window (21 ms at 48 kHz) is the compromise: long enough to
// resolve a 47 Hz kick fundamental, short enough to follow its sweep. Zero
// padding to 4x costs nothing offline and sharpens the parabolic peak
// interpolation well past the raw bin spacing.
// Runtime-settable, because one window size does not fit the whole kit. A
// 1024-point window is 23 ms — barely ONE CYCLE of a 47 Hz kick fundamental,
// with a mainlobe ~170 Hz wide, so the kick's lowest and most important
// partials are smeared into each other and largely missed. Measured, the
// sinusoidal part of the kick was 12 dB down at 20-40 Hz and the missing
// energy fell into the residual, where it was resynthesised as NOISE: a kick
// rendered as rumble rather than as a note. Low drums need a longer window;
// cymbals and hats do not care, since their partials are high and dense.
static size_t kWin = 1024;
static size_t kPad = 4096;
static size_t kHop = 128;          // 2.7 ms
static constexpr double kHannMeanSq = 0.375;  // mean(w^2) for Hann

// ── Model ────────────────────────────────────────────────────────────────────

struct Partial {
    int                start{0};   // first frame index
    std::vector<float> freq;       // Hz, per frame
    std::vector<float> amp;        // linear amplitude, per frame
    float              jitter{0.0f};  // residual frequency wobble after smoothing
    float peakAmp() const {
        float p = 0.0f;
        for (float a : amp) p = std::max(p, a);
        return p;
    }
};

struct NoiseModel {
    static constexpr int kBands = 24;
    double hopSeconds{double(kHop) / 48000.0};
    std::vector<std::array<float, kBands>> frames;   // per-band RMS
    std::array<float, kBands> centre{};
};

// ── FFT ──────────────────────────────────────────────────────────────────────

static void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / double(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

static std::vector<double> hann(size_t n) {
    std::vector<double> w(n);
    for (size_t i = 0; i < n; ++i) w[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / double(n - 1));
    return w;
}

static size_t findOnset(const std::vector<float>& x) {
    float pk = 0.0f;
    for (float s : x) pk = std::max(pk, std::fabs(s));
    if (pk <= 0.0f) return 0;
    const float thr = pk * 0.02f;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::fabs(x[i]) >= thr) return (i > 64) ? i - 64 : 0;
    return 0;
}

// ── STFT ─────────────────────────────────────────────────────────────────────

struct Frame { std::vector<double> mag; };   // magnitude only, kPad/2 bins

static std::vector<Frame> stft(const std::vector<float>& x) {
    const auto w = hann(kWin);
    std::vector<Frame> out;
    for (size_t s = 0; s + kWin <= x.size(); s += kHop) {
        std::vector<std::complex<double>> buf(kPad, {0.0, 0.0});
        for (size_t i = 0; i < kWin; ++i) buf[i] = x[s + i] * w[i];
        fft(buf);
        Frame f;
        f.mag.resize(kPad / 2);
        for (size_t k = 0; k < kPad / 2; ++k) f.mag[k] = std::abs(buf[k]);
        out.push_back(std::move(f));
    }
    return out;
}

struct FramePeak { double freq, amp; };

// Peaks in one frame, parabolically interpolated. Amplitude is converted out of
// FFT units: a sinusoid of amplitude A through a Hann window of length W peaks
// at A*W/4, and zero padding does not change that because only W samples are
// non-zero.
static std::vector<FramePeak> framePeaks(const Frame& f, double fs, double floorRel) {
    std::vector<FramePeak> peaks;
    double fmax = 0.0;
    for (double m : f.mag) fmax = std::max(fmax, m);
    if (fmax <= 0.0) return peaks;
    const double thr = fmax * floorRel;

    const double binHz = fs / double(kPad);
    for (size_t k = 2; k + 2 < f.mag.size(); ++k) {
        if (f.mag[k] < thr) continue;
        if (f.mag[k] <= f.mag[k - 1] || f.mag[k] < f.mag[k + 1]) continue;
        const double a = std::log(f.mag[k - 1] + 1e-20);
        const double b = std::log(f.mag[k]     + 1e-20);
        const double c = std::log(f.mag[k + 1] + 1e-20);
        const double d = std::clamp(0.5 * (a - c) / (a - 2.0 * b + c + 1e-30), -0.5, 0.5);
        const double mag = std::exp(b - 0.25 * (a - c) * d);
        peaks.push_back({ (double(k) + d) * binHz, mag * 4.0 / double(kWin) });
    }
    return peaks;
}

// ── Partial tracking ─────────────────────────────────────────────────────────
//
// Link frame peaks into tracks by frequency proximity. This is also the
// sines/noise discriminator, and a better one than the decay-shape tests the
// previous version used: a real partial produces a peak at a coherent
// frequency in frame after frame, whereas noise throws up peaks that scatter
// and never link into a long track.
static std::vector<Partial> trackPartials(const std::vector<Frame>& frames, double fs,
                                          int maxPartials, int minLen, float maxJitter,
                                          float maxWanderSemis, float ampFloor) {
    struct Live { Partial p; double lastFreq; int missing; bool open; };
    std::vector<Live> live;
    std::vector<Partial> done;

    for (size_t fi = 0; fi < frames.size(); ++fi) {
        auto peaks = framePeaks(frames[fi], fs, 0.002);
        std::sort(peaks.begin(), peaks.end(),
                  [](const FramePeak& a, const FramePeak& b) { return a.amp > b.amp; });
        if (peaks.size() > 40) peaks.resize(40);

        std::vector<bool> used(peaks.size(), false);

        // Extend existing tracks first, strongest track first.
        for (auto& L : live) {
            if (!L.open) continue;
            int    bestIdx = -1;
            double bestDist = 1e30;
            const double tol = std::max(25.0, L.lastFreq * 0.06);
            for (size_t pi = 0; pi < peaks.size(); ++pi) {
                if (used[pi]) continue;
                const double d = std::fabs(peaks[pi].freq - L.lastFreq);
                if (d < tol && d < bestDist) { bestDist = d; bestIdx = int(pi); }
            }
            if (bestIdx >= 0) {
                used[bestIdx] = true;
                L.p.freq.push_back(float(peaks[bestIdx].freq));
                L.p.amp.push_back(float(peaks[bestIdx].amp));
                L.lastFreq = peaks[bestIdx].freq;
                L.missing  = 0;
            } else {
                // Tolerate a couple of dropped frames: a partial can dip below
                // the peak threshold briefly without having ended. Bridge the
                // gap by HOLDING the amplitude (decayed slightly), never by
                // inserting a zero — a track that drops to silence and comes
                // back flickers, and a flickering partial is heard as tremolo.
                if (++L.missing > 2) {
                    L.open = false;
                } else {
                    L.p.freq.push_back(L.p.freq.back());
                    L.p.amp.push_back(L.p.amp.back() * 0.7f);
                }
            }
        }

        // Unmatched peaks begin new tracks.
        for (size_t pi = 0; pi < peaks.size(); ++pi) {
            if (used[pi]) continue;
            Live L;
            L.p.start = int(fi);
            L.p.freq.push_back(float(peaks[pi].freq));
            L.p.amp.push_back(float(peaks[pi].amp));
            L.lastFreq = peaks[pi].freq;
            L.missing  = 0;
            L.open     = true;
            live.push_back(std::move(L));
        }

        // Retire closed tracks.
        for (auto it = live.begin(); it != live.end();) {
            if (!it->open) { done.push_back(std::move(it->p)); it = live.erase(it); }
            else ++it;
        }
    }
    for (auto& L : live) done.push_back(std::move(L.p));

    // Keep the tracks that are long enough to be real and loud enough to matter.
    done.erase(std::remove_if(done.begin(), done.end(),
                              [minLen](const Partial& p) {
                                  return int(p.amp.size()) < minLen || p.peakAmp() < 1e-4f;
                              }),
               done.end());

    // ── Smooth the frequency contours, and drop the ones that never settle ──
    //
    // A peak's frequency is re-estimated every frame from a noisy spectrum, so
    // successive estimates wobble by a fraction of a bin even for a perfectly
    // steady partial. Replaying that wobble as real frequency modulation is
    // audible as warble — the characteristic artefact of naive sinusoidal
    // resynthesis. A real drum partial holds one frequency or glides smoothly,
    // so median-filtering the contour removes the estimation noise while
    // preserving a genuine glide such as a kick's pitch drop.
    //
    // The jitter REMAINING after smoothing is then a good tonality test: a real
    // partial sits close to its own smoothed contour, whereas a track threaded
    // through noise peaks keeps jumping away from it. Those are dropped rather
    // than resynthesised, because the noise model already represents that
    // energy properly — keeping them both warbles AND double-counts.
    for (Partial& p : done) {
        const size_t n = p.freq.size();
        if (n < 3) { p.jitter = 0.0f; continue; }

        std::vector<float> med(n);
        for (size_t i = 0; i < n; ++i) {
            const size_t a = (i >= 2) ? i - 2 : 0;
            const size_t b = std::min(n - 1, i + 2);
            std::vector<float> w(p.freq.begin() + a, p.freq.begin() + b + 1);
            std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
            med[i] = w[w.size() / 2];
        }
        // A light moving average after the median removes the staircase the
        // median itself leaves behind.
        std::vector<float> sm(n);
        for (size_t i = 0; i < n; ++i) {
            const size_t a = (i >= 1) ? i - 1 : 0;
            const size_t b = std::min(n - 1, i + 1);
            float acc = 0.0f; int c = 0;
            for (size_t k = a; k <= b; ++k) { acc += med[k]; ++c; }
            sm[i] = acc / float(c);
        }

        double acc = 0.0, wsum = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double w = p.amp[i];                 // weight by audibility
            acc  += w * std::fabs(double(p.freq[i]) - sm[i]) / std::max(20.0f, sm[i]);
            wsum += w;
        }
        p.jitter = (wsum > 1e-12) ? float(acc / wsum) : 1.0f;
        p.freq = sm;

        // Amplitude gets a gentle 3-point average too: frame-to-frame
        // amplitude noise reads as roughness on a sustained partial.
        std::vector<float> sa(n);
        for (size_t i = 0; i < n; ++i) {
            const size_t a = (i >= 1) ? i - 1 : 0;
            const size_t b = std::min(n - 1, i + 1);
            float s = 0.0f; int c = 0;
            for (size_t k = a; k <= b; ++k) { s += p.amp[k]; ++c; }
            sa[i] = s / float(c);
        }
        p.amp = sa;
    }

    // ── Reject tracks that are not really partials ──────────────────────────
    //
    // The decisive test is not high-frequency wobble but slow WANDER. A track
    // threaded through unrelated noise peaks drifts a long way over its life —
    // measured on a real snare, tracks ran 1374 -> 1067 Hz and even 2494 ->
    // 3405 Hz. A struck drum's partial does not rise in pitch as it decays, so
    // those are noise, and resynthesising them as gliding tones is exactly the
    // warble this is here to remove.
    //
    // Rejecting them is also self-correcting: the noise model is read off the
    // spectrum with the KEPT partials notched out, so whatever is dropped here
    // is automatically represented as noise instead — which is what it is.
    {
        float loudest = 0.0f;
        for (const Partial& p : done) loudest = std::max(loudest, p.peakAmp());

        std::vector<const Partial*> byAmp;
        for (const Partial& p : done) byAmp.push_back(&p);
        std::sort(byAmp.begin(), byAmp.end(),
                  [](const Partial* a, const Partial* b) { return a->peakAmp() > b->peakAmp(); });

        // The few loudest partials are structural and exempt from the wander
        // test: a kick's fundamental legitimately sweeps 112 -> 47 Hz, which is
        // a bigger excursion than any noise artefact.
        std::vector<const Partial*> exempt(byAmp.begin(),
                                           byAmp.begin() + std::min<size_t>(3, byAmp.size()));

        done.erase(std::remove_if(done.begin(), done.end(),
            [&](const Partial& p) {
                if (p.peakAmp() < ampFloor * loudest) return true;
                for (const Partial* e : exempt) if (e == &p) return false;
                if (p.freq.size() < 3) return true;

                // Reject on how far a track departs from a SMOOTH TREND, not on
                // how far it travels. Total excursion was the wrong test: a
                // kick's fundamental sweeps 112 -> 47 Hz, so every harmonic
                // sweeps with it, and a 15% excursion limit threw away every
                // partial except the two or three loudest ones that were
                // exempt. The kick's tonal content then fell into the residual
                // and got resynthesised as NOISE — measured, the sinusoidal
                // part was 4-18 dB down across the whole spectrum and the kick
                // was mostly hiss.
                //
                // A real glide is smooth and monotonic in log-frequency; a
                // track threaded through noise peaks wanders erratically. So
                // fit a line to log2(f) over time and measure the residual
                // around it, in semitones.
                double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
                for (size_t i = 0; i < p.freq.size(); ++i) {
                    if (p.freq[i] < 1.0f) return true;
                    const double x = double(i);
                    const double y = std::log2(double(p.freq[i]));
                    const double w = p.amp[i];
                    sw += w; sx += w * x; sy += w * y; sxx += w * x * x; sxy += w * x * y;
                }
                if (sw < 1e-12) return true;
                const double den = sw * sxx - sx * sx;
                if (std::fabs(den) < 1e-20) return true;
                const double sl = (sw * sxy - sx * sy) / den;
                const double ic = (sy - sl * sx) / sw;

                double acc = 0.0;
                for (size_t i = 0; i < p.freq.size(); ++i) {
                    const double r = std::log2(double(p.freq[i])) - (ic + sl * double(i));
                    acc += p.amp[i] * r * r;
                }
                const double semitones = 12.0 * std::sqrt(acc / sw);
                if (semitones > maxWanderSemis) return true;

                return maxJitter > 0.0f && p.jitter > maxJitter;
            }), done.end());
    }
    std::sort(done.begin(), done.end(), [](const Partial& a, const Partial& b) {
        return a.peakAmp() * a.amp.size() > b.peakAmp() * b.amp.size();
    });
    if (int(done.size()) > maxPartials) done.resize(maxPartials);
    return done;
}

// Resynthesise tracks with a phase-accumulating oscillator, interpolating
// frequency and amplitude across each hop. Absolute phase is NOT matched to the
// original — it does not need to be, because the residual is computed
// spectrally rather than by subtraction.
static std::vector<float> synthPartials(const std::vector<Partial>& ps, double fs, size_t n) {
    std::vector<float> y(n, 0.0f);
    for (const Partial& p : ps) {
        double phase = 0.0;
        for (size_t fi = 0; fi + 1 < p.amp.size(); ++fi) {
            const size_t s0 = (size_t(p.start) + fi) * kHop + kWin / 2;
            if (s0 >= n) break;
            const size_t s1 = std::min(n, s0 + kHop);
            const double f0 = p.freq[fi],  f1 = p.freq[fi + 1];
            const double a0 = p.amp[fi],   a1 = p.amp[fi + 1];
            for (size_t i = s0; i < s1; ++i) {
                const double t = double(i - s0) / double(kHop);
                const double f = f0 + (f1 - f0) * t;
                const double a = a0 + (a1 - a0) * t;
                phase += 2.0 * M_PI * f / fs;
                if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
                y[i] += float(a * std::sin(phase));
            }
        }
    }
    return y;
}

// ── Dense static modes (cymbals, hats) ───────────────────────────────────────
//
// A cymbal is not a few gliding partials plus noise; it is HUNDREDS of fixed
// inharmonic modes beating against each other. Tracking buys nothing here —
// nothing sweeps — and it cost the warble that forced the partial budget down
// to 4-8, which left cymbals as 24-band shaped noise. Measured, that reads as
// static: spectral crest 7.8 dB against the real crash's 17.7.
//
// A dense bank of FIXED damped modes measures 17.3 dB on the same test, costs
// 4 bytes per mode instead of 24 bytes per frame, and cannot warble because no
// frequency ever moves.
//
// Mode count matters and has an optimum: too few is audibly sparse (100 modes
// measured 25.1 dB, far peakier than the real thing), too many goes flat again
// (600 gave 15.5). Around 400 matches a real crash.
struct DenseMode { double freq, amp, t60; };

static bool fitStaticMode(const std::vector<float>& x, double fs, double f, DenseMode& out) {
    const size_t n = x.size();
    const double w  = 2.0 * M_PI * f / fs;

    // The amplitude and the decay want DIFFERENT filters, so the mode is
    // tracked twice.
    //
    // AMPLITUDE needs a wide enough band to collect the mode's real energy. A
    // cymbal partial is not a pure tone; narrowing the filter to isolate it
    // throws away the skirts that make it audible, and the model comes out
    // dark (measured: the china's centroid fell from 3059 to 2198 Hz).
    //
    // DECAY needs the opposite. Cymbal modes are packed within a few Hz of
    // each other up high, so a wide filter sums several of them, and the sum
    // decays at the rate of the SLOWEST one it caught. That is why the china's
    // top octave came out ringing 3x too long while its low body died too
    // early: every mode's decay was being dragged toward the same average.
    auto track = [&](double bw, std::vector<double>& env) {
        const double a = std::exp(-2.0 * M_PI * bw / fs);
        const std::complex<double> step(std::cos(-w), std::sin(-w));
        std::complex<double> rot(1.0, 0.0), lp(0.0, 0.0);
        env.clear();
        env.reserve(n / 64 + 1);
        for (size_t i = 0; i < n; ++i) {
            lp = (1.0 - a) * (double(x[i]) * rot) + a * lp;
            rot *= step;
            if ((i & 63) == 0) env.push_back(std::abs(lp));
        }
    };

    std::vector<double> envA, envD;
    track(std::max(6.0, f * 0.004),  envA);    // amplitude
    track(std::max(4.0, f * 0.0015), envD);    // decay

    double pk = 0.0;
    for (double v : envA) pk = std::max(pk, v);
    if (pk < 1.0e-7) return false;

    double pkD = 0.0; size_t at = 0;
    for (size_t i = 0; i < envD.size(); ++i) if (envD[i] > pkD) { pkD = envD[i]; at = i; }
    if (pkD < 1.0e-9) return false;

    // Stop the fit before the RECORDING'S NOISE FLOOR, not merely 60 dB below
    // the mode's peak. A quiet high mode reaches the floor long before -60 dB,
    // and once there its envelope stops falling; a log-linear fit through that
    // plateau reads a far slower slope than the mode really has. The floor is
    // taken from the END of the capture, where the instrument has stopped.
    double noiseFloor = 0.0;
    {
        const size_t tailFrom = envD.size() - std::max<size_t>(4, envD.size() / 10);
        std::vector<double> tail(envD.begin() + tailFrom, envD.end());
        std::sort(tail.begin(), tail.end());
        noiseFloor = tail[tail.size() / 2];
    }
    // Only trust the tail as a noise floor if it really is one. A long capture
    // of a cymbal that is STILL RINGING at the end gives a high "floor", which
    // would truncate every fit early and make the model darken faster than the
    // instrument -- the crash is 8 seconds long and does exactly that.
    const double floorL = (noiseFloor < pkD * 0.03)            // 30 dB down
                        ? std::max(pkD * 1.0e-3, noiseFloor * 2.0)
                        : pkD * 1.0e-3;

    double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    size_t used = 0;
    for (size_t i = at; i < envD.size(); ++i) {
        if (envD[i] < floorL) break;
        const double t  = double((i - at) * 64) / fs;
        const double y  = std::log(envD[i]);
        const double wt = envD[i] / pkD;
        sw += wt; sx += wt * t; sy += wt * y; sxx += wt * t * t; sxy += wt * t * y;
        ++used;
    }
    if (used < 8) return false;
    const double den = sw * sxx - sx * sx;
    if (std::fabs(den) < 1.0e-20) return false;
    const double slope = (sw * sxy - sx * sy) / den;
    if (slope >= 0.0) return false;
    const double t60 = -std::log(1000.0) / slope;
    if (t60 < 0.02 || t60 > 25.0) return false;

    out.freq = f; out.amp = pk * 2.0; out.t60 = t60;
    return true;
}

// Modes must NOT all start in phase. Struck metal excites its modes with
// scattered phase; starting 600 of them at zero makes them sum coherently and
// the peak scales with N instead of sqrt(N) — measured, the hats and ride bell
// came out at 2.6-3.7 full scale. A golden-ratio low-discrepancy sequence
// gives a well-spread, deterministic phase per mode with no table and no
// stored byte, and gives the offline analysis and the runtime the same answer.
inline float modePhase(int i) {
    const float t = float(i) * 0.6180339887f;
    return 6.28318530718f * (t - std::floor(t));
}

static std::vector<float> synthDenseModes(const std::vector<DenseMode>& modes, double fs, size_t n) {
    std::vector<float> y(n, 0.0f);
    int idx = 0;
    for (const DenseMode& m : modes) {
        const double w  = 2.0 * M_PI * m.freq / fs;
        const double r  = std::exp(-std::log(1000.0) / (m.t60 * fs));
        const double cr = r * std::cos(w), ci = r * std::sin(w);
        const double ph = modePhase(idx++);
        double re = m.amp * std::cos(ph), im = m.amp * std::sin(ph);
        for (size_t i = 0; i < n; ++i) {
            const double nr = re * cr - im * ci;
            const double ni = re * ci + im * cr;
            re = nr; im = ni;
            y[i] += float(ni);
        }
    }
    return y;
}

// How far below the strongest third-octave band the mode search is allowed
// to reach. Too small and the model is left hollow where the instrument
// does have content; too large and it fits noise below the instrument's
// natural cliff, which is what made the cymbals 2 kHz too dark.
static double gWeakCullDb = 45.0;
static double gLowCutDb = 25.0;

static std::vector<DenseMode> extractDenseModes(const std::vector<float>& x, double fs, int want) {
    const size_t NF = 1u << 16;                       // fine enough to separate dense modes
    std::vector<std::complex<double>> spec(NF, { 0.0, 0.0 });
    const size_t take = std::min(x.size(), NF);
    const auto w = hann(take);
    for (size_t i = 0; i < take; ++i) spec[i] = x[i] * w[i];
    fft(spec);

    // Where does this instrument's spectrum actually start?
    //
    // A crash has a cliff below ~300 Hz: the real one measures 43 dB at
    // 300-500 Hz and only 6 dB at 130-200. Fitting modes down there fits
    // NOISE, and fifty spurious low modes summed to ~20 dB of excess, which
    // dragged the model's spectral centroid ~2 kHz below the real cymbal's and
    // is most of why it read as dark rather than metallic.
    //
    // A fixed floor would be a guess that suits the crash and not the ride
    // bell, which genuinely has low content. So the cut is derived from the
    // recording: the lowest third-octave band whose energy comes within 30 dB
    // of the strongest band.
    const double binHz = fs / double(NF);
    double loCut = 250.0, hiCut = 16000.0;
    {
        constexpr int kB = 30;               // third-octaves from 60 Hz up
        std::vector<double> e(kB, 0.0);
        for (size_t k = 1; k < NF / 2; ++k) {
            const double f = double(k) * binHz;
            if (f < 60.0 || f > 16000.0) continue;
            int b = int(std::log(f / 60.0) / std::log(16000.0 / 60.0) * (kB - 1) + 0.5);
            b = std::clamp(b, 0, kB - 1);
            e[b] += std::norm(spec[k]);
        }
        double peak = 0.0;
        for (double v : e) peak = std::max(peak, v);
        const double thr = peak * std::pow(10.0, -gLowCutDb / 10.0);
        for (int b = 0; b < kB; ++b) {
            if (e[b] >= thr) {
                loCut = 60.0 * std::pow(16000.0 / 60.0, double(b) / (kB - 1));
                break;
            }
        }
        loCut = std::clamp(loCut, 80.0, 900.0);

        // NO high cut. One was tried, derived the same way as the low cut, to
        // remove a measured 19 dB excess above 12 kHz on the china. It made
        // every cymbal WORSE (china 16.3 -> 22.3 dB) and darker, because the
        // top octave is where a cymbal's air lives even when its energy there
        // is small: cutting it removed more than the excess it was aimed at.
        // The asymmetry is real — a spectrum's low end has a cliff below which
        // there is nothing, but its high end just thins out.
    }

    struct Cand { double f, m; };
    std::vector<Cand> cands;
    for (size_t k = 2; k + 2 < NF / 2; ++k) {
        const double m = std::abs(spec[k]);
        if (m <= std::abs(spec[k - 1]) || m < std::abs(spec[k + 1])) continue;
        const double f = double(k) * binHz;
        if (f < loCut || f > hiCut) continue;
        cands.push_back({ f, m });
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.m > b.m; });

    // Spend the mode budget PER OCTAVE BAND, not globally by amplitude.
    //
    // A cymbal's loudest modes are all low, so taking the strongest N peaks
    // overall starves the top end. Measured as spectral centroid 10 ms into a
    // hit: a real crash sits at 6373 Hz and a real ride at 7109, while the
    // globally-selected model came out at 4293 and 4193 — about 2 kHz too
    // dark, which is most of why it did not sound like metal. The high modes
    // are quiet individually but there are a great many of them, and together
    // they are the air and shimmer.
    // A UNIFORM quota per octave band, not one proportional to each band's
    // energy. Proportional allocation was tried and measured WORSE on every
    // cymbal (china 16.3 -> 19.7 dB, the ride's centroid 6137 -> 4513 Hz):
    // weighting by loudness hands the budget to the low bands, but the high
    // bands need many modes precisely BECAUSE they are dense and individually
    // quiet. Spending where the energy is is the wrong rule for a cymbal.
    //
    // The real problem proportional allocation was aimed at — modes fitted
    // into bands the instrument does not occupy — is handled at both ends by
    // the derived cuts instead.
    constexpr int kBands = 12;
    const double lo = loCut, hi = hiCut;
    std::vector<int> used(kBands, 0);
    const int quota = std::max(1, want / kBands);

    std::vector<DenseMode> modes;
    // Two passes: fill each band's quota first, then spend anything left over
    // on the strongest remaining peaks wherever they are.
    for (int pass = 0; pass < 2; ++pass) {
        for (const Cand& c : cands) {
            if (int(modes.size()) >= want) break;
            int b = int(std::log(c.f / lo) / std::log(hi / lo) * (kBands - 1) + 0.5);
            b = std::clamp(b, 0, kBands - 1);
            if (pass == 0 && used[b] >= quota) continue;

            // 1.2 Hz, not 3. Cymbal modes are mostly SPLIT DOUBLETS a few Hz
            // apart, and the slow beating between each pair is the shimmer —
            // it is the difference between metal and a bell. A 3 Hz dedup
            // threshold merged exactly those pairs and threw the shimmer away.
            // The 65536-point analysis resolves 0.67 Hz, so they are there.
            bool dup = false;
            for (const DenseMode& m : modes) if (std::fabs(m.freq - c.f) < 1.2) { dup = true; break; }
            if (dup) continue;

            DenseMode m;
            if (fitStaticMode(x, fs, c.f, m)) { modes.push_back(m); ++used[b]; }
            // (weak modes are culled below, once the strongest is known)
        }
    }

    // Drop modes more than 45 dB below the strongest. Below that they are
    // inaudible individually, and a fit that weak is as likely to have latched
    // onto the noise floor as onto a real mode — which is exactly how spurious
    // energy accumulates in bands where the instrument has none.
    if (!modes.empty()) {
        double loudest = 0.0;
        for (const DenseMode& m : modes) loudest = std::max(loudest, m.amp);
        const double floorAmp = loudest * std::pow(10.0, -gWeakCullDb / 20.0);
        modes.erase(std::remove_if(modes.begin(), modes.end(),
                                   [floorAmp](const DenseMode& m) { return m.amp < floorAmp; }),
                    modes.end());
    }
    return modes;
}

// ── Noise model ──────────────────────────────────────────────────────────────
//
// Read straight off the spectrum with the tracked partials notched out. No
// time-domain subtraction, so no phase-cancellation requirement and no way to
// end up with more energy than we started with.
// Noise for a DENSE MODAL instrument, by energy difference rather than by
// notching.
//
// Notching cannot work here. A mode's Hann mainlobe is ~16 bins wide at 4x
// zero padding, so 600 notched modes erase 10,000 bins from a 2048-bin
// spectrum — the whole thing. The residual came out empty, which is why the
// noise length made no measurable difference to any cymbal, and it matters
// most for a china, which is the trashiest and most genuinely noisy of them.
//
// Instead: synthesise the modes, take their spectrum, and keep whatever energy
// the original has that they do not account for. Energy cannot go negative, so
// this can never invent noise the instrument does not have.
static NoiseModel analyseNoiseDiff(const std::vector<Frame>& frames,
                                   const std::vector<Frame>& modalFrames, double fs) {
    NoiseModel nm;
    nm.hopSeconds = double(kHop) / fs;
    const double lo = 40.0, hi = std::min(fs * 0.45, 16000.0);
    for (int b = 0; b < NoiseModel::kBands; ++b)
        nm.centre[b] = float(lo * std::pow(hi / lo, double(b) / (NoiseModel::kBands - 1)));

    const double binHz = fs / double(kPad);
    constexpr double kHannMS = 0.375;

    for (size_t fi = 0; fi < frames.size(); ++fi) {
        std::array<double, NoiseModel::kBands> accA{}, accB{};
        for (size_t k = 1; k < frames[fi].mag.size(); ++k) {
            const double f = double(k) * binHz;
            if (f < lo || f > hi) continue;
            int b = int(std::log(f / lo) / std::log(hi / lo) * (NoiseModel::kBands - 1) + 0.5);
            b = std::clamp(b, 0, NoiseModel::kBands - 1);
            const double a = frames[fi].mag[k];
            accA[b] += a * a;
            if (fi < modalFrames.size() && k < modalFrames[fi].mag.size()) {
                const double m = modalFrames[fi].mag[k];
                accB[b] += m * m;
            }
        }
        std::array<float, NoiseModel::kBands> e{};
        for (int b = 0; b < NoiseModel::kBands; ++b) {
            const double resid = std::max(0.0, accA[b] - accB[b]);
            e[b] = float(std::sqrt(2.0 * resid / (double(kPad) * double(kWin) * kHannMS)));
        }
        nm.frames.push_back(e);
    }
    return nm;
}

static NoiseModel analyseNoise(const std::vector<Frame>& frames,
                               const std::vector<Partial>& ps,
                               const std::vector<DenseMode>& modes, double fs) {
    NoiseModel nm;
    nm.hopSeconds = double(kHop) / fs;
    const double lo = 40.0, hi = std::min(fs * 0.45, 16000.0);
    for (int b = 0; b < NoiseModel::kBands; ++b)
        nm.centre[b] = float(lo * std::pow(hi / lo, double(b) / (NoiseModel::kBands - 1)));

    const double binHz = fs / double(kPad);

    for (size_t fi = 0; fi < frames.size(); ++fi) {
        std::vector<double> mag = frames[fi].mag;

        // Notch out every dense mode. These are static, so the same bins are
        // removed in every frame. Without this the "noise" model still
        // contains the entire modal spectrum, and adding it back on top of the
        // modes double-counts everything — measured, that turned a 17.2 dB
        // model into a 35.3 dB one.
        for (const DenseMode& m : modes) {
            const int centre = int(m.freq / binHz + 0.5);
            const int halfW  = int(2 * (kPad / kWin));
            for (int k = centre - halfW; k <= centre + halfW; ++k)
                if (k >= 0 && k < int(mag.size())) mag[k] = 0.0;
        }

        // Notch out every partial present in this frame. The window's mainlobe
        // is 4 bins wide for Hann, times the 4x zero padding.
        for (const Partial& p : ps) {
            const int rel = int(fi) - p.start;
            if (rel < 0 || rel >= int(p.freq.size())) continue;
            if (p.amp[rel] <= 0.0f) continue;
            const int centre = int(p.freq[rel] / binHz + 0.5);
            const int halfW  = int(4 * (kPad / kWin));
            for (int k = centre - halfW; k <= centre + halfW; ++k)
                if (k >= 0 && k < int(mag.size())) mag[k] = 0.0;
        }

        std::array<float, NoiseModel::kBands> e{};
        std::array<double, NoiseModel::kBands> acc{};
        for (size_t k = 1; k < mag.size(); ++k) {
            const double f = double(k) * binHz;
            if (f < lo || f > hi) continue;
            int band = int(std::log(f / lo) / std::log(hi / lo) * (NoiseModel::kBands - 1) + 0.5);
            band = std::clamp(band, 0, NoiseModel::kBands - 1);
            acc[band] += mag[k] * mag[k];
        }
        // Parseval with zero padding: sum over all N bins of |X|^2 equals
        // N * sum of the windowed samples squared, and the window costs
        // mean(w^2) of the signal's power over its W samples.
        for (int b = 0; b < NoiseModel::kBands; ++b)
            e[b] = float(std::sqrt(2.0 * acc[b] / (double(kPad) * double(kWin) * kHannMeanSq)));
        nm.frames.push_back(e);
    }
    return nm;
}

static std::vector<float> synthNoise(const NoiseModel& nm, double fs, size_t n) {
    std::vector<float> y(n, 0.0f);
    if (nm.frames.empty()) return y;

    uint32_t rng = 0x13579BDFu;
    auto white = [&]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return float(int32_t(rng)) * (1.0f / 2147483648.0f);
    };

    // Cookbook biquads: the Chamberlin state-variable form used originally is
    // only conditionally stable and produced NaN on the top bands.
    std::array<BiquadFilter, NoiseModel::kBands> filt{};
    for (int b = 0; b < NoiseModel::kBands; ++b)
        filt[b].setCoeffs(Filters::bandpass(std::min(double(nm.centre[b]), fs * 0.45), 1.4, fs));

    // Calibrate so a model gain of g produces an output RMS of g.
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
                const float o = s.process(float(cwhite()));
                if (i > 512) acc += double(o) * o;
            }
            const double rms = std::sqrt(acc / double(kCal - 512));
            corr[b] = (rms > 1e-12) ? (1.0 / rms) * (1.0 / std::sqrt(3.0)) : 0.0;
        }
    }

    const double hop = double(kHop);
    for (size_t i = 0; i < n; ++i) {
        const double fpos = double(i) / hop;
        // The noise model ENDS when its frames end. Clamping to the last frame
        // held that frame's band gains for the rest of the hit, so a model
        // trimmed to 80 ms of attack emitted a constant noise floor for the
        // remaining six seconds. The runtime already stopped correctly, so
        // every score reported for a trimmed-noise instrument was measuring
        // something the plugin never produced — and it made SHORTER noise
        // models look worse, which is backwards.
        if (size_t(fpos) >= nm.frames.size()) break;
        const size_t f0 = std::min(nm.frames.size() - 1, size_t(fpos));
        const size_t f1 = std::min(nm.frames.size() - 1, f0 + 1);
        const float  fr = float(fpos - double(f0));

        const float x = white();
        float acc = 0.0f;
        for (int b = 0; b < NoiseModel::kBands; ++b) {
            const float bp = filt[b].process(x);
            const float g  = nm.frames[f0][b] * (1.0f - fr) + nm.frames[f1][b] * fr;
            acc += bp * float(corr[b]) * g;
        }
        y[i] = acc;
    }
    return y;
}

// ── Scoring ──────────────────────────────────────────────────────────────────
// 1/3-octave band energies over time. Per-bin comparison is the wrong measure:
// the noise half is stochastic by construction and will never match bin for bin
// yet can be perceptually identical.
static double spectralDistanceDb(const std::vector<float>& a, const std::vector<float>& b, double fs) {
    const size_t win = 1024, hop = 256;
    const auto w = hann(win);
    double total = 0.0; int frames = 0;
    const size_t n = std::min(a.size(), b.size());

    for (size_t s = 0; s + win < n; s += hop) {
        std::vector<std::complex<double>> A(win), B(win);
        for (size_t i = 0; i < win; ++i) { A[i] = a[s + i] * w[i]; B[i] = b[s + i] * w[i]; }
        fft(A); fft(B);

        constexpr int kB = 24;
        double ea[kB] = {}, eb[kB] = {}; int cn[kB] = {};
        const double lo = 50.0, hi = 12000.0;
        for (size_t k = 1; k < win / 2; ++k) {
            const double f = double(k) * fs / double(win);
            if (f < lo || f > hi) continue;
            int bd = int(std::log(f / lo) / std::log(hi / lo) * (kB - 1) + 0.5);
            bd = std::clamp(bd, 0, kB - 1);
            ea[bd] += std::norm(A[k]); eb[bd] += std::norm(B[k]); cn[bd]++;
        }
        double acc = 0.0; int cnt = 0;
        for (int bd = 0; bd < kB; ++bd) {
            if (!cn[bd]) continue;
            const double pa = 10.0 * std::log10(ea[bd] / cn[bd] + 1e-12);
            const double pb = 10.0 * std::log10(eb[bd] / cn[bd] + 1e-12);
            acc += (pa - pb) * (pa - pb); ++cnt;
        }
        if (cnt) { total += std::sqrt(acc / cnt); ++frames; }
    }
    return frames ? total / frames : 0.0;
}

// ── Model file ───────────────────────────────────────────────────────────────
//
// One analysed hit, quantised. This is what ships — there is no audio in it.
//
//   header   "HXD1", rate, counts
//   partials per track: start frame, length, then (freq, amp) per frame
//   noise    band RMS per frame
//
// Frequencies go in quarter-Hz steps (0.25 Hz resolution to 16 kHz fits a
// uint16 exactly). Amplitudes go in a 96 dB logarithmic byte, because drum
// decays are exponential: a linear byte would quantise the tail into steps
// while wasting most of its range on the first few milliseconds.
static uint8_t quantAmp(double a) {
    if (a <= 1e-5) return 0;                       // 0 is reserved for silence
    const double db = 20.0 * std::log10(a);
    if (db <= -96.0) return 0;
    const double t = (db + 96.0) / 96.0;           // 0..1 over -96..0 dB
    return uint8_t(std::clamp(int(t * 254.0 + 0.5) + 1, 1, 255));
}

static void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); }

// t60 spans 0.02 s (a stick tick) to 25 s (a ride tail), so it is stored
// logarithmically in a byte — a linear byte would quantise the short decays
// into steps while wasting most of its range on tails nobody hears.
static uint8_t quantT60(double t) {
    const double lo = 0.02, hi = 25.0;
    const double u = std::log(std::clamp(t, lo, hi) / lo) / std::log(hi / lo);
    return uint8_t(std::clamp(int(u * 255.0 + 0.5), 0, 255));
}

static std::vector<uint8_t> encodeHit(const std::vector<Partial>& ps, const NoiseModel& nm,
                                      const std::vector<DenseMode>& modes, double fs) {
    std::vector<uint8_t> v;
    v.push_back('H'); v.push_back('X'); v.push_back('D'); v.push_back('2');
    put32(v, uint32_t(fs));
    put16(v, uint16_t(ps.size()));
    put16(v, uint16_t(kHop));
    put16(v, uint16_t(nm.frames.size()));
    v.push_back(uint8_t(NoiseModel::kBands));
    v.push_back(0);
    put16(v, uint16_t(modes.size()));
    put16(v, 0);                                      // reserved

    for (const Partial& p : ps) {
        put16(v, uint16_t(p.start));
        put16(v, uint16_t(p.amp.size()));
        for (size_t i = 0; i < p.amp.size(); ++i) {
            put16(v, uint16_t(std::clamp(int(p.freq[i] * 4.0f + 0.5f), 0, 65535)));
            v.push_back(quantAmp(p.amp[i]));
        }
    }
    for (const auto& fr : nm.frames)
        for (int b = 0; b < NoiseModel::kBands; ++b) v.push_back(quantAmp(fr[b]));

    // Dense static modes: frequency, amplitude, decay. Four bytes each.
    for (const DenseMode& m : modes) {
        put16(v, uint16_t(std::clamp(int(m.freq * 4.0 + 0.5), 0, 65535)));
        v.push_back(quantAmp(m.amp));
        v.push_back(quantT60(m.t60));
    }
    return v;
}

// ── WAV out ──────────────────────────────────────────────────────────────────

static float peakOfVec(const std::vector<float>& v) {
    float p = 0.0f;
    for (float s : v) p = std::max(p, std::fabs(s));
    return p;
}

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
        const int16_t v = int16_t(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: drum_analyze <hit.wav> [out_dir] [--verbose] [--partials N]\n");
        return 2;
    }
    const std::string inPath = argv[1];
    const std::string outDir = (argc > 2 && argv[2][0] != '-') ? argv[2] : ".";
    bool verbose = false, quiet = false;
    int  maxPartials = 24;   // fewer, but genuinely tonal; the rest becomes noise
    double maxSeconds = 0.0;      // 0 = keep the whole hit
    double maxJitter  = 0.0;      // 0 = no high-frequency-wobble test
    double maxWanderSemis = 0.60; // reject a track departing >0.6 semitone from a smooth glide
    int    winSize = 0;           // 0 = default 1024; larger resolves low fundamentals
    int    denseModes   = 0;      // >0 = model as fixed modes (cymbals, hats)
    double noiseSeconds = 0.0;    // 0 = model noise for the whole hit
    double ampFloor     = 0.02;   // and any quieter than 2% of the loudest
    std::string emitPath;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        if (!std::strcmp(argv[i], "--quiet"))   quiet = true;
        if (!std::strcmp(argv[i], "--partials") && i + 1 < argc) maxPartials = std::atoi(argv[++i]);
        if (!std::strcmp(argv[i], "--emit")     && i + 1 < argc) emitPath = argv[++i];
        if (!std::strcmp(argv[i], "--max-seconds") && i + 1 < argc) maxSeconds = std::atof(argv[++i]);
        if (!std::strcmp(argv[i], "--max-jitter")  && i + 1 < argc) maxJitter  = std::atof(argv[++i]);
        if (!std::strcmp(argv[i], "--max-wander") && i + 1 < argc) maxWanderSemis = std::atof(argv[++i]);
        if (!std::strcmp(argv[i], "--win") && i + 1 < argc) winSize = std::atoi(argv[++i]);
        if (!std::strcmp(argv[i], "--amp-floor") && i + 1 < argc) ampFloor = std::atof(argv[++i]);
        if (!std::strcmp(argv[i], "--dense-modes") && i + 1 < argc) denseModes = std::atoi(argv[++i]);
        if (!std::strcmp(argv[i], "--lowcut-db") && i + 1 < argc) gLowCutDb = std::atof(argv[++i]);
        if (!std::strcmp(argv[i], "--noise-seconds") && i + 1 < argc) noiseSeconds = std::atof(argv[++i]);
    }
    const bool writeWavs = emitPath.empty();

    if (winSize >= 256 && winSize <= 8192) {
        kWin = size_t(winSize);
        kPad = kWin * 4;
        kHop = kWin / 8;          // keep the 8x overlap
    }

    std::vector<float> L, R;
    uint32_t rate = 0;
    if (!wavread::readWav(inPath.c_str(), L, R, rate) || L.empty()) {
        std::printf("cannot read %s\n", inPath.c_str());
        return 1;
    }
    if (!R.empty()) for (size_t i = 0; i < L.size(); ++i) L[i] = 0.5f * (L[i] + R[i]);
    const double fs = rate;

    const size_t onset = findOnset(L);
    std::vector<float> x(L.begin() + onset, L.end());

    // Cap the modelled length. An 18 s ride is mostly inaudible tail, and its
    // noise frames dominate the model size (152 kB of 190 kB); trimming is a
    // far better trade than shipping a decay nobody hears under a guitar.
    if (maxSeconds > 0.0) {
        const size_t cap = size_t(maxSeconds * fs);
        if (x.size() > cap) {
            x.resize(cap);
            // Fade the new end so the truncation is not a step.
            // Linear, and long (up to a second). A short or squared fade is a
            // fast amplitude move the partial tracker cannot follow, and it
            // measurably worsened the fit rather than just shortening it.
            const size_t fade = std::min<size_t>(cap / 4, size_t(1.0 * fs));
            for (size_t i = 0; i < fade; ++i)
                x[cap - fade + i] *= float(fade - i) / float(fade);
        }
    }
    float pk = 0.0f;
    for (float s : x) pk = std::max(pk, std::fabs(s));
    std::printf("%s\n  %zu frames @ %.0f Hz, peak %.3f\n", inPath.c_str(), x.size(), fs, pk);

    // ── Analyse ──────────────────────────────────────────────────────────────
    const auto frames = stft(x);

    // Two models, chosen per instrument. Drums get tracked partials, because
    // their fundamentals glide and their decays are not single exponentials.
    // Cymbals and hats get a dense bank of FIXED modes, because nothing about
    // them sweeps and tracking only introduced warble.
    std::vector<DenseMode> modes;
    std::vector<Partial>   partials;
    if (denseModes > 0) {
        modes = extractDenseModes(x, fs, denseModes);
    } else {
        partials = trackPartials(frames, fs, maxPartials, 6, float(maxJitter),
                                 float(maxWanderSemis), float(ampFloor));
    }

    std::printf("  %zu STFT frames -> %zu tracked partials, %zu dense modes\n",
                frames.size(), partials.size(), modes.size());
    if (verbose) {
        for (size_t i = 0; i < std::min<size_t>(10, partials.size()); ++i) {
            const Partial& p = partials[i];
            std::printf("    %2zu  %7.1f -> %7.1f Hz  peak %.4f  jitter %.4f  %zu frames\n",
                        i, p.freq.front(), p.freq.back(), p.peakAmp(), p.jitter, p.amp.size());
        }
    }

    auto noise = modes.empty()
               ? analyseNoise(frames, partials, modes, fs)
               : analyseNoiseDiff(frames, stft(synthDenseModes(modes, fs, x.size())), fs);

    // With a modal model, the noise part only has to carry the ATTACK — the
    // stick contact — because the modes carry the whole ringing body. Keeping
    // seconds of it would restore exactly the broadband hiss this replaces,
    // and the noise frames are what dominate the model size.
    if (noiseSeconds > 0.0) {
        const size_t keep = size_t(noiseSeconds * fs / double(kHop));
        if (noise.frames.size() > keep) noise.frames.resize(keep);
    }

    // ── Resynthesise ─────────────────────────────────────────────────────────
    std::vector<float> sines = synthPartials(partials, fs, x.size());
    if (!modes.empty()) {
        const std::vector<float> mv = synthDenseModes(modes, fs, x.size());
        for (size_t i = 0; i < sines.size(); ++i) sines[i] += mv[i];
    }
    const std::vector<float> noiseSg = synthNoise(noise, fs, x.size());
    std::vector<float> resynth(x.size());
    for (size_t i = 0; i < x.size(); ++i) resynth[i] = sines[i] + noiseSg[i];

    const double dSines = spectralDistanceDb(x, sines, fs);
    const double dFull  = spectralDistanceDb(x, resynth, fs);
    std::printf("  log-spectral distance: partials only %.2f dB, partials+noise %.2f dB\n",
                dSines, dFull);

    // ── Size ─────────────────────────────────────────────────────────────────
    size_t partialFrames = 0;
    for (const Partial& p : partials) partialFrames += p.amp.size();
    const size_t bytesP = partialFrames * 2;                       // amp + freq, 1 B each
    const size_t bytesM = modes.size() * 4;                        // freq, amp, decay
    const size_t bytesN = noise.frames.size() * NoiseModel::kBands;
    const size_t bytesW = x.size() * sizeof(int16_t);
    std::printf("  size: model %zu B (partials %zu + modes %zu + noise %zu) vs WAV %zu B = %.1fx smaller\n",
                bytesP + bytesM + bytesN, bytesP, bytesM, bytesN, bytesW,
                double(bytesW) / double(bytesP + bytesM + bytesN + 1));

    if (!emitPath.empty()) {
        const std::vector<uint8_t> blob = encodeHit(partials, noise, modes, fs);
        FILE* f = std::fopen(emitPath.c_str(), "wb");
        if (!f) { std::printf("  !! cannot write %s\n", emitPath.c_str()); return 1; }
        std::fwrite(blob.data(), 1, blob.size(), f);
        std::fclose(f);
        if (!quiet) std::printf("  emitted %s (%zu B)\n", emitPath.c_str(), blob.size());
        // Machine-readable line for the batch builder's summary table.
        // Machine-readable summary for the kit builder. The source peak is
        // what lets it normalise each instrument: the close mics in a sampled
        // kit are recorded at very different gains.
        std::printf("STAT %s %.2f %zu %zu %zu %.6f\n", emitPath.c_str(), dFull,
                    partials.size(), noise.frames.size(), blob.size(),
                    double(peakOfVec(resynth)));
    }

    if (writeWavs) {
        std::string base = inPath;
        const size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos) base = base.substr(slash + 1);
        const size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);

        writeWav(outDir + "/" + base + "_orig.wav",    x,       fs);
        writeWav(outDir + "/" + base + "_resynth.wav", resynth, fs);
        writeWav(outDir + "/" + base + "_sines.wav",   sines,   fs);
        writeWav(outDir + "/" + base + "_noise.wav",   noiseSg, fs);
        std::printf("  wrote %s_{orig,resynth,sines,noise}.wav\n\n", base.c_str());
    }
    return 0;
}
