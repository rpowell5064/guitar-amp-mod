#pragma once
// ── Automatic 50/60 Hz mains detection (Anagram build only, 2026-11-10) ─────────────────────
// The device has no Mains control: the amp listens for the hum on its own raw input and tunes
// the input hum comb + the gate's detector comb to the mains it finds. Default 60 Hz until sure.
//
// Measurement (audio thread, allocation/lock/syscall free, ~10 flops per input sample):
//   mono x = 0.5*(L+R) of the RAW input -> two one-pole low-passes (500 Hz) -> boxcar-average
//   and decimate by D = round(fs/2000) (~2 kHz) -> 0.5 s rectangular windows (~1000 samples).
//   Per window, Goertzel power at the 50 Hz family {50,100,150,200,250} and the 60 Hz family
//   {60,120,180,240,360} (300 Hz = 6x50 = 5x60 is shared, so it is left out), plus the window's
//   AC energy. 0.5 s = 25 / 30 cycles of the two fundamentals: every family line sits on an
//   exact bin, the rectangular window's zeros fall on the other family's lines (2 Hz bins).
// A window VOTES for a family only when it looks like hum and nothing else:
//   quiet      LF-band RMS between -100 and -35 dBFS (between notes; digital silence never votes)
//   tonal      that family holds >= 60 % of the window's AC energy (a chord / a note's harmonic
//              series lands mostly off the lines -- a guitar note leaks only through sidelobes)
//   decisive   that family >= 10x (10 dB) the other one
//   stationary that family's power within 1.5 dB of the last window that passed the three tests
//              above for it (however long ago: hum is steady across the gaps between phrases; a
//              decaying note tail at a line frequency rarely lands on the same level twice)
// Decision with hysteresis: the first change away from the 60 Hz default takes 4 votes, any later
//   change 12 votes, for the other family, with no vote for the current family in between (a vote
//   for the current one clears the count) and no gap of more than 20 windows (10 s) between votes.
//   Windows that do not vote (playing, silence, hiss) leave the count alone. So: ~2.5 s of quiet
//   hum to leave the default, ~6.5 s to change again -- it cannot flap.
// Determinism (tests/dualmono): plain arithmetic on the input only, fixed evaluation order; a
// kx:Reset re-constructs it (= a fresh instance).
#include <cmath>
#include <cstdint>

struct MainsDetect {
    static constexpr int    kBins      = 5;
    static constexpr double kFsDec     = 2000.0;   // decimated rate target
    static constexpr double kWinSec    = 0.5;
    static constexpr double kQuietMs   = 3.1622776601683795e-4;   // -35 dBFS (mean square)
    static constexpr double kFloorMs   = 1e-10;                    // -100 dBFS
    static constexpr double kMinFrac   = 0.6;
    static constexpr double kMinRatio  = 10.0;
    static constexpr double kStatDb    = 1.5;
    static constexpr int    kVotesFirst = 4, kVotesLater = 12, kMaxGapWin = 20;

    // config (prepare)
    int    dec = 24, winN = 1000;
    float  lpA = 0.0f;
    double coef[2][kBins] = {};      // Goertzel 2cos(w): [0] = 50 Hz family, [1] = 60 Hz family
    // decimator
    float  lp1 = 0.0f, lp2 = 0.0f, acc = 0.0f;
    int    decCnt = 0;
    // window
    double g1[2][kBins] = {}, g2[2][kBins] = {};
    double sx = 0.0, sxx = 0.0;
    int    winCnt = 0;
    // decision
    double refF[2] = { 0.0, 0.0 };   // each family's power in its last candidate window
    int    winIdx = 0, lastVoteWin = 0;
    int    votes = 0, switches = 0;
    float  hz = 60.0f;               // the decided mains

    void prepare(double fs) noexcept {
        dec  = static_cast<int>(std::lround(fs / kFsDec)); if (dec < 1) dec = 1;
        const double fsd = fs / dec;
        winN = static_cast<int>(std::lround(kWinSec * fsd));
        lpA  = static_cast<float>(1.0 - std::exp(-2.0 * 3.14159265358979323846 * 500.0 / fs));
        static const double f[2][kBins] = { { 50, 100, 150, 200, 250 }, { 60, 120, 180, 240, 360 } };
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kBins; ++k)
                coef[c][k] = 2.0 * std::cos(2.0 * 3.14159265358979323846 * f[c][k] / fsd);
    }

    void feed(const float* l, const float* r, uint32_t n) noexcept {
        for (uint32_t i = 0; i < n; ++i) {
            const float x = 0.5f * (l[i] + r[i]);
            lp1 += lpA * (x - lp1);
            lp2 += lpA * (lp1 - lp2);
            acc += lp2;
            if (++decCnt >= dec) { push(static_cast<double>(acc) / dec); acc = 0.0f; decCnt = 0; }
        }
    }

private:
    void push(double y) noexcept {
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kBins; ++k) {
                const double s0 = y + coef[c][k] * g1[c][k] - g2[c][k];
                g2[c][k] = g1[c][k]; g1[c][k] = s0;
            }
        sx += y; sxx += y * y;
        if (++winCnt >= winN) endWindow();
    }

    void endWindow() noexcept {
        const double N  = static_cast<double>(winN);
        const double ms = (sxx - sx * sx / N) / N;   // AC mean square of the window
        double F[2];
        for (int c = 0; c < 2; ++c) {
            double s = 0.0;
            for (int k = 0; k < kBins; ++k) {
                const double a = g1[c][k], b = g2[c][k];
                s += a * a + b * b - coef[c][k] * a * b;   // |X|^2
                g1[c][k] = g2[c][k] = 0.0;
            }
            F[c] = 2.0 * s / (N * N);                       // the family's mean-square power
        }
        sx = sxx = 0.0; winCnt = 0;
        ++winIdx;

        int vote = -1;   // 0 = 50 Hz, 1 = 60 Hz
        if (ms > kFloorMs && ms < kQuietMs) {
            const int w = (F[0] > F[1]) ? 0 : 1;
            const double fw = F[w], fl = F[1 - w], ref = refF[w];
            if (fw >= kMinFrac * ms && fw >= kMinRatio * fl) {   // hum-like: a candidate
                const double lo = std::pow(10.0, -kStatDb / 10.0), hi = std::pow(10.0, kStatDb / 10.0);
                if (ref > 0.0 && fw >= lo * ref && fw <= hi * ref) vote = w;   // ... as loud as the last one
                refF[w] = fw;
            }
        }
        if (vote < 0) return;

        const int cur = (hz < 55.0f) ? 0 : 1;
        if (vote == cur) { votes = 0; return; }
        if (votes > 0 && winIdx - lastVoteWin > kMaxGapWin) votes = 0;
        lastVoteWin = winIdx;
        if (++votes >= (switches == 0 ? kVotesFirst : kVotesLater)) {
            hz = (vote == 0) ? 50.0f : 60.0f;
            votes = 0; ++switches;
        }
    }
};
