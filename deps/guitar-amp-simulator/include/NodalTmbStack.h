#pragma once
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// NodalTmbStack — the passive TMB (treble / bass / mid) tone stack solved as a
// nodal network WITH its real source and load (2026-11-05, MT15 twin).
//
// YehSmithToneStack is exact for an ideal voltage source and an open output;
// the twins fold a driving impedance into the slope resistor, which is close
// when that impedance is a cathode follower's ~1 k. Some amps feed the stack
// through a large series resistor (the MT15 lead channel: a 12AX7 plate behind
// a 100k feed resistor, ~170 k in all) and load its wiper with a few hundred
// k: folded into the slope, that misplaces the treble cap's share of the source
// drop. This class keeps both in the network:
//
//   vin ── Rs ──A──┬── C1 ──T── (1-t)R1 ──W── t R1 ──B── l R2 ──M── Rmid ── gnd
//                  │                      │          │          │
//                Rsh (opt)             RL (opt)      C2         C3
//                  │                      │          │          │
//                 gnd                    gnd         └─S────────┘
//                  A ── R4 (slope) ───────────────── S
//
// Rmid is the whole node-M-to-ground resistance (a mid pot wired as a rheostat,
// plus any fixed resistor under it). Three capacitors, so H(s) is a ratio of
// cubics; the coefficients are recovered exactly from four evaluations of the
// two nodal determinants (a cubic is fixed by four points) and discretised by
// the bilinear transform, the same discretisation YehSmithToneStack uses.
// Recomputed only when a control moves; the per-sample cost is one 3rd-order
// direct-form-II-transposed section.
// ─────────────────────────────────────────────────────────────────────────────
class NodalTmbStack {
public:
    struct Circuit {
        double C1, C2, C3;      // treble cap, bass cap, mid cap (F)
        double R1, R2, R4;      // treble pot, bass pot (full track), slope resistor (Ω)
        double Rs;              // source resistance in series with the input (Ω)
        double Rsh = 0.0;       // shunt from the input node to ground (Ω); 0 = none
        double RL  = 0.0;       // load from the wiper to ground (Ω); 0 = open
    };

    void prepare(double fs, const Circuit& c) noexcept { fs_ = fs; c_ = c; recalc(); reset(); }
    // Pot positions as resistance fractions (the caller applies the tapers): t = treble wiper
    // (1 = top), l = bass in-circuit fraction, rMid = the mid leg's resistance in ohms.
    void set(double t, double l, double rMid) noexcept { t_ = t; l_ = l; rMid_ = rMid; recalc(); }
    // Change the load without touching the pots (e.g. a relay that shorts a series resistor).
    void setLoad(double RL) noexcept { c_.RL = RL; recalc(); }
    void reset() noexcept { s1_ = s2_ = s3_ = 0.0; }

    float process(float x) noexcept {
        const double xd = x;
        const double y  = b0_ * xd + s1_;
        s1_ = b1_ * xd - a1_ * y + s2_;
        s2_ = b2_ * xd - a2_ * y + s3_;
        s3_ = b3_ * xd - a3_ * y;
        return static_cast<float>(y);
    }

    // |H(j 2 pi f)| of the continuous network, solved directly (verification only).
    double magAt(double f) const noexcept {
        const double w = 2.0 * M_PI * f;
        double nr, ni, dr, di;
        solveComplex(w, nr, ni, dr, di);
        return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
    }

private:
    static constexpr int N = 6;   // A T W B S M
    enum { A = 0, T = 1, W = 2, B = 3, S = 4, M = 5 };

    // Stamp G (conductances) and C (capacitances) matrices.
    void stamp(double G[N][N], double C[N][N], double& gs) const noexcept {
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) { G[i][j] = 0.0; C[i][j] = 0.0; }
        auto g2 = [&G](int a, int b, double r) { const double g = 1.0 / std::max(r, 1.0);
            G[a][a] += g; G[b][b] += g; G[a][b] -= g; G[b][a] -= g; };
        auto g1 = [&G](int a, double r) { G[a][a] += 1.0 / std::max(r, 1.0); };
        auto c2 = [&C](int a, int b, double c) { C[a][a] += c; C[b][b] += c; C[a][b] -= c; C[b][a] -= c; };
        gs = 1.0 / std::max(c_.Rs, 1.0);
        G[A][A] += gs;
        if (c_.Rsh > 0.0) g1(A, c_.Rsh);
        c2(A, T, c_.C1);
        g2(A, S, c_.R4);
        g2(T, W, (1.0 - t_) * c_.R1);
        g2(W, B, t_ * c_.R1);
        c2(S, B, c_.C2);
        c2(S, M, c_.C3);
        g2(B, M, l_ * c_.R2);
        g1(M, rMid_);
        if (c_.RL > 0.0) g1(W, c_.RL);
    }

    static double det(double m[N][N]) noexcept {
        double d = 1.0;
        for (int k = 0; k < N; ++k) {
            int p = k;
            for (int i = k + 1; i < N; ++i) if (std::fabs(m[i][k]) > std::fabs(m[p][k])) p = i;
            if (m[p][k] == 0.0) return 0.0;
            if (p != k) { for (int j = 0; j < N; ++j) std::swap(m[k][j], m[p][j]); d = -d; }
            d *= m[k][k];
            for (int i = k + 1; i < N; ++i) {
                const double f = m[i][k] / m[k][k];
                for (int j = k; j < N; ++j) m[i][j] -= f * m[k][j];
            }
        }
        return d;
    }

    // Numerator (Cramer: the W column replaced by the source vector) and denominator at real s.
    void evalAt(double s, double& num, double& den) const noexcept {
        double G[N][N], C[N][N], gs;
        stamp(G, C, gs);
        double Y[N][N];
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) Y[i][j] = G[i][j] + s * C[i][j];
        double Yn[N][N];
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) Yn[i][j] = (j == W) ? (i == A ? gs : 0.0) : Y[i][j];
        den = det(Y);
        num = det(Yn);
    }

    void solveComplex(double w, double& nr, double& ni, double& dr, double& di) const noexcept {
        // Complex determinant by elimination on (re, im) pairs.
        double G[N][N], C[N][N], gs;
        stamp(G, C, gs);
        auto cdet = [](double re[N][N], double im[N][N], double& r, double& i) {
            r = 1.0; i = 0.0;
            for (int k = 0; k < N; ++k) {
                int p = k; double best = -1.0;
                for (int q = k; q < N; ++q) { const double m = re[q][k] * re[q][k] + im[q][k] * im[q][k]; if (m > best) { best = m; p = q; } }
                if (best <= 0.0) { r = i = 0.0; return; }
                if (p != k) { for (int j = 0; j < N; ++j) { std::swap(re[k][j], re[p][j]); std::swap(im[k][j], im[p][j]); } r = -r; i = -i; }
                const double pr = re[k][k], pi = im[k][k];
                const double nr2 = r * pr - i * pi, ni2 = r * pi + i * pr; r = nr2; i = ni2;
                const double inv = 1.0 / (pr * pr + pi * pi);
                for (int q = k + 1; q < N; ++q) {
                    const double fr = (re[q][k] * pr + im[q][k] * pi) * inv;
                    const double fi = (im[q][k] * pr - re[q][k] * pi) * inv;
                    for (int j = k; j < N; ++j) {
                        const double ar = re[k][j], ai = im[k][j];
                        re[q][j] -= fr * ar - fi * ai;
                        im[q][j] -= fr * ai + fi * ar;
                    }
                }
            }
        };
        double re[N][N], im[N][N], ren[N][N], imn[N][N];
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) {
            re[i][j] = G[i][j]; im[i][j] = w * C[i][j];
            ren[i][j] = (j == W) ? (i == A ? gs : 0.0) : re[i][j];
            imn[i][j] = (j == W) ? 0.0 : im[i][j];
        }
        cdet(re, im, dr, di);
        cdet(ren, imn, nr, ni);
    }

    void recalc() noexcept {
        if (fs_ <= 0.0) return;
        // Four real points pin each cubic. Scale s to the network's audio band so the
        // four samples are equally conditioned (the coefficients are rescaled back).
        constexpr double sc = 2.0 * M_PI * 1000.0;
        double nv[4], dv[4];
        for (int k = 0; k < 4; ++k) evalAt(sc * k, nv[k], dv[k]);
        // Newton forward differences on x = 0,1,2,3 -> monomial coefficients in x = s/sc.
        auto fit = [](const double v[4], double p[4]) {
            const double d1 = v[1] - v[0], d2 = v[2] - 2.0 * v[1] + v[0], d3 = v[3] - 3.0 * v[2] + 3.0 * v[1] - v[0];
            // v(x) = v0 + d1 x + d2 x(x-1)/2 + d3 x(x-1)(x-2)/6
            p[0] = v[0];
            p[1] = d1 - d2 / 2.0 + d3 / 3.0;
            p[2] = d2 / 2.0 - d3 / 2.0;
            p[3] = d3 / 6.0;
        };
        double np[4], dp[4];
        fit(nv, np); fit(dv, dp);
        // Bilinear: s = c (1 - z^-1)/(1 + z^-1), with s = sc x  ->  x = (c/sc)(1 - z^-1)/(1 + z^-1).
        const double k = 2.0 * fs_ / sc;
        static constexpr double kE[4][4] = {
            { 1.0,  3.0,  3.0,  1.0 },    // (1+z)^3
            { 1.0,  1.0, -1.0, -1.0 },    // (1-z)(1+z)^2
            { 1.0, -1.0, -1.0,  1.0 },    // (1-z)^2(1+z)
            { 1.0, -3.0,  3.0, -1.0 } };  // (1-z)^3
        double B[4] = {}, Aq[4] = {};
        double kp = 1.0;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) { B[j] += np[i] * kp * kE[i][j]; Aq[j] += dp[i] * kp * kE[i][j]; }
            kp *= k;
        }
        if (Aq[0] == 0.0) return;
        const double inv = 1.0 / Aq[0];
        b0_ = B[0] * inv; b1_ = B[1] * inv; b2_ = B[2] * inv; b3_ = B[3] * inv;
        a1_ = Aq[1] * inv; a2_ = Aq[2] * inv; a3_ = Aq[3] * inv;
    }

    double fs_ = 0.0;
    Circuit c_{ 270e-12, 10e-9, 47e-9, 250e3, 1e6, 47e3, 1e3, 0.0, 0.0 };
    double t_ = 0.5, l_ = 0.5, rMid_ = 12.5e3;
    double b0_{1}, b1_{0}, b2_{0}, b3_{0}, a1_{0}, a2_{0}, a3_{0};
    double s1_{}, s2_{}, s3_{};
};
