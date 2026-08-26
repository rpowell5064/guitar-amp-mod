// ─────────────────────────────────────────────────────────────────────────────
// Hex Forge desktop — streaming sample-rate converter (M3).
//
// The engine always runs at 48 kHz (every factory preset's out_level was
// measured there and NAM captures don't resample). Hosts at other rates get
// wrapped by a pair of these: host→48k in front of the engine, 48k→host
// behind it. Rational-ratio polyphase windowed-sinc:
//   44100 ⇄ 48000 : L/M = 160/147      88200 → 48000 : 80/147
//   96000 → 48000 : 1/2                192000 → 48000 : 1/4
// Prototype: Kaiser-windowed sinc, kTaps per phase, cutoff 0.45·min(src,dst)
// (≈ −90 dB stopband). Group delay is (kTaps−1)/2 samples at the INPUT rate
// of each stage — reported to the host via setLatencySamples.
// Self-contained; desktop-only (the LV2/pi build is 48k-native).
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <cstring>
#include <vector>
#include <numeric>

class HfPolyResampler {
public:
    static constexpr int kTaps = 32;   // per output phase

    void prepare(double srcRate, double dstRate, int maxInBlock) {
        const long s = (long) (srcRate + 0.5), d = (long) (dstRate + 0.5);
        const long g = std::gcd(s, d);
        L = (int) (d / g);
        M = (int) (s / g);
        buildTable(srcRate, dstRate);
        hist.assign((size_t) (kTaps - 1) * 2, 0.0f);   // interleaved-by-channel history
        phase = 0;
        outCapHint = (int) std::ceil((double) maxInBlock * L / M) + 8;
        (void) outCapHint;
    }

    // Stereo, non-interleaved. Consumes nIn frames, writes produced frames to
    // outL/outR, returns the count (≤ ceil(nIn·L/M)+1).
    int process(const float* inL, const float* inR, int nIn, float* outL, float* outR) {
        // Work buffer = history ++ new input (per channel).
        const int h = kTaps - 1;
        workL.resize((size_t) (h + nIn));
        workR.resize((size_t) (h + nIn));
        std::memcpy(workL.data(), hist.data(), (size_t) h * sizeof(float));
        std::memcpy(workR.data(), hist.data() + h, (size_t) h * sizeof(float));
        std::memcpy(workL.data() + h, inL, (size_t) nIn * sizeof(float));
        std::memcpy(workR.data() + h, inR, (size_t) nIn * sizeof(float));

        int produced = 0;
        // Output y[j] uses input window starting at inIdx with phase p:
        //   y = Σ_t table[p][t] · x[inIdx + t]
        // then the (p, inIdx) pair advances by M steps of the L-upsampled clock.
        while (inIdx + kTaps <= h + nIn) {
            const float* tp = table.data() + (size_t) phase * kTaps;
            const float* xl = workL.data() + inIdx;
            const float* xr = workR.data() + inIdx;
            float al = 0.0f, ar = 0.0f;
            for (int t = 0; t < kTaps; ++t) { al += tp[t] * xl[t]; ar += tp[t] * xr[t]; }
            outL[produced] = al;
            outR[produced] = ar;
            ++produced;
            phase += M;
            inIdx += phase / L;
            phase %= L;
        }
        // Keep the last kTaps−1 frames as history; rebase inIdx onto it.
        const int total = h + nIn;
        std::memcpy(hist.data(), workL.data() + (total - h), (size_t) h * sizeof(float));
        std::memcpy(hist.data() + h, workR.data() + (total - h), (size_t) h * sizeof(float));
        inIdx -= nIn;   // history keeps absolute alignment: consumed nIn input frames
        return produced;
    }

    void reset() {
        std::fill(hist.begin(), hist.end(), 0.0f);
        phase = 0;
        inIdx = 0;
    }

    // Filter group delay in samples AT THE INPUT RATE of this stage. The
    // prototype length is L·kTaps (center (L·kTaps−1)/2 upsampled ticks), and
    // the first output window ends on the first real input sample — working
    // through the indexing gives kTaps/2 − 1/(2L), not (kTaps−1)/2 (the 997 Hz
    // round-trip test measures the half-sample difference loudly).
    double groupDelayInputSamples() const { return kTaps / 2.0 - 1.0 / (2.0 * L); }

private:
    void buildTable(double srcRate, double dstRate) {
        // Prototype length L·kTaps at the L-upsampled rate; cutoff at
        // 0.45·min(src,dst) of the ORIGINAL band, gain L (upsampler makeup).
        const int N = L * kTaps;
        const double fs = srcRate * L;
        const double fc = 0.44 * (srcRate < dstRate ? srcRate : dstRate);
        const double beta = 9.0;   // Kaiser: ~90 dB stopband
        const double i0b = besselI0(beta);
        std::vector<double> proto((size_t) N);
        const double center = (N - 1) / 2.0;
        for (int i = 0; i < N; ++i) {
            const double t = (i - center) / fs;
            const double x = 2.0 * fc * t;
            const double sinc = (std::abs(x) < 1e-12) ? 1.0 : std::sin(3.14159265358979323846 * x)
                                                                / (3.14159265358979323846 * x);
            const double r = (i - center) / center;
            const double win = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            proto[(size_t) i] = 2.0 * fc / fs * sinc * win * L;
        }
        // Polyphase decomposition: phase p, tap t → proto[t·L + p], and the
        // taps are applied to CONSECUTIVE input samples (time-reversed FIR).
        table.assign((size_t) L * kTaps, 0.0f);
        for (int p = 0; p < L; ++p)
            for (int t = 0; t < kTaps; ++t) {
                const int idx = t * L + p;
                if (idx < N)
                    table[(size_t) p * kTaps + (size_t) (kTaps - 1 - t)] = (float) proto[(size_t) idx];
            }
    }
    static double besselI0(double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 32; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum) break;
        }
        return sum;
    }

    int L = 1, M = 1;
    int phase = 0;    // position on the L-upsampled clock, 0..L-1
    int inIdx = 0;    // window start in the work buffer (history-based)
    int outCapHint = 0;
    std::vector<float> table;    // L phases × kTaps, time-reversed
    std::vector<float> hist;     // kTaps−1 frames, L then R
    std::vector<float> workL, workR;
};
