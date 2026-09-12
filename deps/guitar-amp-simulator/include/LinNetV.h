#pragma once
#include <array>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// LinNetV — small linear R/C/L network, solved as drawn (component-amp
// programme, 2026-09-12, for the Ampeg SVT's twin-T "Ultra Lo" network, its
// bridged-pot tone stack and the toroidal-inductor mid-range).
//
// Nodes: 0 = ground, 1 = the driven input (an ideal voltage source), 2.. =
// the unknown node voltages. Inductors add one branch-current unknown each.
// The network is assembled as C·dx/dt + G·x = 0 (modified nodal analysis) and
// integrated with the trapezoidal rule, which is exact for linear time-
// invariant networks up to the usual bilinear frequency warp:
//   (2C/T + G)·x[n] = (2C/T − G)·x[n−1]   (columns of the input node move to the
//   right-hand side). Everything is precomputed at prepare(); per sample it is
//   one K×K matrix-vector product, K = unknowns.
// Rebuild on any element change (a pot moved, a switch thrown).
// ─────────────────────────────────────────────────────────────────────────────
namespace evhcomp {

class LinNetV {
public:
    static constexpr int kMaxNodes = 12;   // node indices 0..11
    static constexpr int kMaxL     = 3;
    static constexpr int kMaxK     = kMaxNodes + kMaxL;

    void clear() noexcept { nR_ = nC_ = nL_ = 0; maxNode_ = 1; }
    void addR(int a, int b, double R) noexcept { if (nR_ < kMaxE) { r_[nR_++] = { a, b, R }; note(a, b); } }
    void addC(int a, int b, double C) noexcept { if (nC_ < kMaxE) { c_[nC_++] = { a, b, C }; note(a, b); } }
    void addL(int a, int b, double L) noexcept { if (nL_ < kMaxL) { l_[nL_++] = { a, b, L }; note(a, b); } }
    void setOutput(int node) noexcept { outNode_ = node; }

    // Assemble + precompute. Returns false if the matrix is singular.
    bool prepare(double fs) noexcept {
        const int nNodes = maxNode_ + 1;          // 0..maxNode_
        const int nUnk   = (nNodes - 2) + nL_;    // unknown nodes (2..) + inductor currents
        K_ = nUnk;
        if (K_ <= 0 || K_ > kMaxK) return false;
        const double twoOverT = 2.0 * fs;
        // full-size G and C over indices: node i -> i, inductor k -> nNodes + k
        const int N = nNodes + nL_;
        std::array<double, kMaxK * 2 * kMaxK * 2> Gf{}, Cf{};
        auto G = [&](int i, int j) -> double& { return Gf[size_t(i) * size_t(N) + size_t(j)]; };
        auto C = [&](int i, int j) -> double& { return Cf[size_t(i) * size_t(N) + size_t(j)]; };
        for (int e = 0; e < nR_; ++e) {
            const double g = 1.0 / std::max(1e-3, r_[e].v);
            const int a = r_[e].a, b = r_[e].b;
            G(a, a) += g; G(b, b) += g; G(a, b) -= g; G(b, a) -= g;
        }
        for (int e = 0; e < nC_; ++e) {
            const double c = c_[e].v;
            const int a = c_[e].a, b = c_[e].b;
            C(a, a) += c; C(b, b) += c; C(a, b) -= c; C(b, a) -= c;
        }
        for (int k = 0; k < nL_; ++k) {
            const int j = nNodes + k, a = l_[k].a, b = l_[k].b;
            G(a, j) += 1.0; G(b, j) -= 1.0;          // KCL: current leaves a, enters b
            G(j, a) += 1.0; G(j, b) -= 1.0;          // v_a − v_b − L di/dt = 0
            C(j, j) -= l_[k].v;
        }
        // Unknown index map: node i (i >= 2) -> i − 2; inductor k -> (nNodes − 2) + k.
        auto uidx = [&](int i) -> int { return i >= nNodes ? (nNodes - 2) + (i - nNodes) : i - 2; };
        // P = 2C/T + G (uu), Q = 2C/T − G (uu); Pi, Qi = the input-node columns
        std::array<double, kMaxK * kMaxK> P{}, Q{};
        std::array<double, kMaxK> Pi{}, Qi{};
        for (int i = 0; i < N; ++i) {
            if (i == 0 || i == 1) continue;
            const int ui = uidx(i);
            for (int j = 0; j < N; ++j) {
                const double p = twoOverT * C(i, j) + G(i, j);
                const double q = twoOverT * C(i, j) - G(i, j);
                if (j == 0) continue;
                if (j == 1) { Pi[size_t(ui)] = p; Qi[size_t(ui)] = q; continue; }
                P[size_t(ui) * kMaxK + size_t(uidx(j))] = p;
                Q[size_t(ui) * kMaxK + size_t(uidx(j))] = q;
            }
        }
        // M = inv(P) by Gauss-Jordan with partial pivoting
        std::array<double, kMaxK * kMaxK> M{};
        for (int i = 0; i < K_; ++i) M[size_t(i) * kMaxK + size_t(i)] = 1.0;
        for (int col = 0; col < K_; ++col) {
            int piv = col; double best = std::abs(P[size_t(col) * kMaxK + size_t(col)]);
            for (int r = col + 1; r < K_; ++r) {
                const double v = std::abs(P[size_t(r) * kMaxK + size_t(col)]);
                if (v > best) { best = v; piv = r; }
            }
            if (best < 1e-300) return false;
            if (piv != col)
                for (int j = 0; j < K_; ++j) {
                    std::swap(P[size_t(col) * kMaxK + size_t(j)], P[size_t(piv) * kMaxK + size_t(j)]);
                    std::swap(M[size_t(col) * kMaxK + size_t(j)], M[size_t(piv) * kMaxK + size_t(j)]);
                }
            const double inv = 1.0 / P[size_t(col) * kMaxK + size_t(col)];
            for (int j = 0; j < K_; ++j) { P[size_t(col) * kMaxK + size_t(j)] *= inv; M[size_t(col) * kMaxK + size_t(j)] *= inv; }
            for (int r = 0; r < K_; ++r) {
                if (r == col) continue;
                const double f = P[size_t(r) * kMaxK + size_t(col)];
                if (f == 0.0) continue;
                for (int j = 0; j < K_; ++j) {
                    P[size_t(r) * kMaxK + size_t(j)] -= f * P[size_t(col) * kMaxK + size_t(j)];
                    M[size_t(r) * kMaxK + size_t(j)] -= f * M[size_t(col) * kMaxK + size_t(j)];
                }
            }
        }
        // A = M·Q ; B1 = −M·Pi ; B0 = M·Qi
        for (int i = 0; i < K_; ++i) {
            double b1 = 0.0, b0 = 0.0;
            for (int j = 0; j < K_; ++j) {
                double acc = 0.0;
                for (int k = 0; k < K_; ++k) acc += M[size_t(i) * kMaxK + size_t(k)] * Q[size_t(k) * kMaxK + size_t(j)];
                A_[size_t(i) * kMaxK + size_t(j)] = acc;
                b1 -= M[size_t(i) * kMaxK + size_t(j)] * Pi[size_t(j)];
                b0 += M[size_t(i) * kMaxK + size_t(j)] * Qi[size_t(j)];
            }
            B1_[size_t(i)] = b1; B0_[size_t(i)] = b0;
        }
        outIdx_ = uidx(outNode_);
        if (outIdx_ < 0 || outIdx_ >= K_) return false;
        reset();
        return true;
    }

    void reset() noexcept { x_.fill(0.0); uPrev_ = 0.0; }

    double process(double u) noexcept {
        std::array<double, kMaxK> xn{};
        for (int i = 0; i < K_; ++i) {
            double acc = B1_[size_t(i)] * u + B0_[size_t(i)] * uPrev_;
            const double* row = &A_[size_t(i) * kMaxK];
            for (int j = 0; j < K_; ++j) acc += row[j] * x_[size_t(j)];
            xn[size_t(i)] = acc;
        }
        x_ = xn; uPrev_ = u;
        return x_[size_t(outIdx_)];
    }

    // Steady-state complex gain at f (Hz) from the continuous-time network —
    // for the lab tools (nodal solve, no bilinear warp).
    // (Not needed at runtime; omitted to keep the header small.)

private:
    static constexpr int kMaxE = 40;
    struct El { int a, b; double v; };
    void note(int a, int b) noexcept { maxNode_ = std::max(maxNode_, std::max(a, b)); }
    El r_[kMaxE] = {}, c_[kMaxE] = {}, l_[kMaxL] = {};
    int nR_ = 0, nC_ = 0, nL_ = 0, maxNode_ = 1, outNode_ = 2, outIdx_ = 0, K_ = 0;
    std::array<double, kMaxK * kMaxK> A_{};
    std::array<double, kMaxK> B1_{}, B0_{}, x_{};
    double uPrev_ = 0.0;
};

} // namespace evhcomp
