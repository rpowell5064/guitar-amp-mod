#pragma once
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// ssdk::Network — a small nodal (MNA) circuit solved by the DK method, for the
// solid-state twins (2026-11-05, first user: the Peavey Backstage Plus).
//
// The netlist is the drawing's: resistors, capacitors and inductors (trapezoidal
// companions), ideal op-amps (nullors: V+ = V-, the output sources whatever
// current the loop needs), one ideal voltage source (the signal input) and up to
// MP nonlinear two-terminal "ports" (antiparallel diode pairs, back-to-back zener
// pairs, op-amp output stages that clip at their rails). compile() solves the
// linear network once (LU) and reduces it to the state-space form
//     v  = Pz z + Pu u + Pi i(v)          port voltages (damped Newton, MP-dimensional)
//     vr = Mz z + Mu u + Mi i             reactive-element voltages -> state update
//     y  = Oz z + Ou u + Oi i             outputs
// so the per-sample cost is O(states^2) whatever the node count. compile() is not
// real-time cheap (an LU of the full MNA matrix): call it when a pot or switch
// moves, not per sample. Element ORDER must stay the same between compiles that
// keep the state (the state is the reactive elements' history currents).
// ─────────────────────────────────────────────────────────────────────────────
namespace ssdk {

// i(v) = is * (exp((v - voff)/nvt) - exp((-v - voff)/nvt)): an antiparallel pair of
// junctions (voff = 0) or a back-to-back zener pair (voff = the zener voltage).
// opamp = true: an op-amp output stage instead — i = g * vsat * r(gain * v / vsat), the
// current a (1/g)-ohm output resistance pushes into the output node from an internal source
// that follows gain x (V+ - V-) up to the rails (±vsat), with r(x) = 2x / (1 + sqrt(1 + 4x^2))
// (slope 1 at 0; with gain 1e5 the closed-loop knee is a few mV wide). Newton solves such a
// port in its CURRENT (v = (vsat/gain) r / (1 - r^2), r = i / (g vsat)): flat in the linear
// region, a barrier at the rails — well conditioned where the voltage form is not. Built by
// Network::opampSat().
struct PortLaw { double is = 2.52e-9, nvt = 0.0453, voff = 0.0;   // (State::v holds an op-amp port's CURRENT)
                 bool opamp = false; double vsat = 13.5, gain = 1e5, g = 1.0 / 50.0; };

// The 1N4148 (Peavey's default small-signal diode on these sheets): SPICE-class
// Is 2.52 nA, n 1.752 at 25 °C.
inline PortLaw diode1N4148() { return { 2.52e-9, 1.752 * 0.025852, 0.0 }; }

template <int MN, int MS, int MP, int MO>
class Network {
public:
    static constexpr int kMaxR  = 96;
    static constexpr int kMaxOp = 4;
    static constexpr int kMaxU  = MN + 1 + kMaxOp;   // nodes + source current + op-amp currents

    struct State {
        double z[MS] = {};
        double v[MP] = {};
        void reset() noexcept { for (auto& a : z) a = 0.0; for (auto& a : v) a = 0.0; }
    };

    // ── netlist (node 0 = ground; nodes 1..nNodes) ──
    void clear(int nNodes) noexcept { nn_ = nNodes; nR_ = nX_ = nOp_ = nP_ = nO_ = 0; src_ = 0; }
    void R(int a, int b, double ohm) noexcept { if (nR_ < kMaxR) r_[nR_++] = { a, b, 1.0 / std::max(ohm, 1e-6) }; }
    void C(int a, int b, double f)   noexcept { if (nX_ < MS) x_[nX_++] = { a, b, f, false }; }
    void L(int a, int b, double h)   noexcept { if (nX_ < MS) x_[nX_++] = { a, b, h, true }; }
    void opamp(int p, int n, int o)  noexcept { if (nOp_ < kMaxOp) op_[nOp_++] = { p, n, o }; }
    void source(int node)            noexcept { src_ = node; }
    void port(int a, int b, const PortLaw& law) noexcept { if (nP_ < MP) { pt_[nP_] = { a, b }; dr_[nP_] = { a, b }; law_[nP_] = law; ++nP_; } }
    // An op-amp that clips at its rails: open-loop gain `gain`, output resistance rout, swing ±vsat.
    // Sensed across (p, n), drives node o (its rout to ground is the linear part of the stage).
    void opampSat(int p, int n, int o, double vsat, double gain = 1e5, double rout = 50.0) noexcept {
        if (nP_ >= MP) return;
        R(o, 0, rout);
        PortLaw L; L.opamp = true; L.vsat = vsat; L.gain = gain; L.g = 1.0 / rout;
        pt_[nP_] = { p, n }; dr_[nP_] = { 0, o }; law_[nP_] = L; ++nP_;
    }
    void output(int a, int b)        noexcept { if (nO_ < MO) out_[nO_++] = { a, b }; }

    int numStates() const noexcept { return nX_; }
    int numPorts()  const noexcept { return nP_; }
    long failures() const noexcept { return fails_; }   // samples whose Newton hit the iteration cap

    // Build the reduced matrices for sample rate fs. Returns false on a singular network.
    bool compile(double fs) noexcept {
        const double T = 1.0 / fs;
        const int nu = nn_ + 1 + nOp_;
        const int iSrc = nn_;              // row/col of the source current
        auto& A = A_;
        for (int i = 0; i < nu; ++i) for (int j = 0; j < nu; ++j) A[i][j] = 0.0;
        auto stampG = [&](int a, int b, double g) {
            if (a > 0) A[a - 1][a - 1] += g;
            if (b > 0) A[b - 1][b - 1] += g;
            if (a > 0 && b > 0) { A[a - 1][b - 1] -= g; A[b - 1][a - 1] -= g; }
        };
        for (int k = 0; k < nR_; ++k) stampG(r_[k].a, r_[k].b, r_[k].g);
        for (int k = 0; k < nX_; ++k) {
            const auto& e = x_[k];
            g_[k] = e.ind ? T / (2.0 * e.val) : 2.0 * e.val / T;
            stampG(e.a, e.b, g_[k]);
        }
        // ideal source: V(src) = u, its current is unknown iSrc
        if (src_ > 0) { A[src_ - 1][iSrc] += 1.0; A[iSrc][src_ - 1] += 1.0; }
        else A[iSrc][iSrc] = 1.0;
        // nullors: output current unknown injected at o; constraint row V(p) - V(n) = 0
        for (int k = 0; k < nOp_; ++k) {
            const int row = nn_ + 1 + k;
            if (op_[k].o > 0) A[op_[k].o - 1][row] += 1.0;
            if (op_[k].p > 0) A[row][op_[k].p - 1] += 1.0;
            if (op_[k].n > 0) A[row][op_[k].n - 1] -= 1.0;
        }
        // LU with partial pivoting
        auto& piv = piv_;
        for (int c = 0; c < nu; ++c) {
            int best = c; double bv = std::fabs(A[c][c]);
            for (int r = c + 1; r < nu; ++r) if (std::fabs(A[r][c]) > bv) { bv = std::fabs(A[r][c]); best = r; }
            if (bv < 1e-300) return false;
            piv[c] = best;
            if (best != c) for (int j = 0; j < nu; ++j) std::swap(A[c][j], A[best][j]);
            const double inv = 1.0 / A[c][c];
            for (int r = c + 1; r < nu; ++r) {
                const double f = A[r][c] * inv;
                if (f == 0.0) continue;
                A[r][c] = f;
                for (int j = c + 1; j < nu; ++j) A[r][j] -= f * A[c][j];
            }
        }
        auto solve = [&](double* b) {
            for (int c = 0; c < nu; ++c) { if (piv[c] != c) std::swap(b[c], b[piv[c]]); }
            for (int r = 0; r < nu; ++r) { double s = b[r]; for (int j = 0; j < r; ++j) s -= A[r][j] * b[j]; b[r] = s; }
            for (int r = nu - 1; r >= 0; --r) { double s = b[r]; for (int j = r + 1; j < nu; ++j) s -= A[r][j] * b[j]; b[r] = s / A[r][r]; }
        };
        auto diff = [&](const double* x, int a, int b) { return (a > 0 ? x[a - 1] : 0.0) - (b > 0 ? x[b - 1] : 0.0); };
        auto& col = col_;
        // Column by column: each excitation (state j, the source, port k) -> node voltages -> reduced rows.
        auto reduce = [&](double* pCol, int pStride, double* mCol, int mStride, double* oCol, int oStride) {
            for (int k = 0; k < nP_; ++k) pCol[k * pStride] = diff(col, pt_[k].a, pt_[k].b);
            for (int k = 0; k < nX_; ++k) mCol[k * mStride] = diff(col, x_[k].a, x_[k].b);
            for (int k = 0; k < nO_; ++k) oCol[k * oStride] = diff(col, out_[k].a, out_[k].b);
        };
        for (int j = 0; j < nX_; ++j) {
            for (int i = 0; i < nu; ++i) col[i] = 0.0;
            // capacitor history: i = G v - h  -> source h into a; inductor: i = G v + h -> source h out of a
            const double s = x_[j].ind ? -1.0 : 1.0;
            if (x_[j].a > 0) col[x_[j].a - 1] += s;
            if (x_[j].b > 0) col[x_[j].b - 1] -= s;
            solve(col);
            reduce(&Pz_[0][j], MS, &Mz_[0][j], MS, &Oz_[0][j], MS);
        }
        {
            for (int i = 0; i < nu; ++i) col[i] = 0.0;
            col[iSrc] = 1.0;
            solve(col);
            reduce(&Pu_[0], 1, &Mu_[0], 1, &Ou_[0], 1);
        }
        for (int j = 0; j < nP_; ++j) {
            for (int i = 0; i < nu; ++i) col[i] = 0.0;
            // port current i flows a -> b through the device: it leaves node a, enters node b
            if (dr_[j].a > 0) col[dr_[j].a - 1] -= 1.0;
            if (dr_[j].b > 0) col[dr_[j].b - 1] += 1.0;
            solve(col);
            reduce(&Pi_[0][j], MP, &Mi_[0][j], MP, &Oi_[0][j], MP);
        }
        for (int k = 0; k < nX_; ++k) sgn_[k] = x_[k].ind ? 1.0 : -1.0;
        for (int k = 0; k < nP_; ++k) {
            const PortLaw& L = law_[k];
            vcrit_[k] = L.opamp ? 0.0 : L.nvt * std::log(L.nvt / (std::sqrt(2.0) * L.is));
        }
        return true;
    }

    // One sample: u = the source voltage; y[0..MO) receives the outputs.
    void process(State& st, double u, double* y) const noexcept {
        double v0[MP], i[MP];
        for (int k = 0; k < nP_; ++k) {
            double s = Pu_[k] * u;
            for (int j = 0; j < nX_; ++j) s += Pz_[k][j] * st.z[j];
            v0[k] = s;
        }
        solvePorts(st.v, v0, i);
        double vr[MS];
        for (int k = 0; k < nX_; ++k) {
            double s = Mu_[k] * u;
            for (int j = 0; j < nX_; ++j) s += Mz_[k][j] * st.z[j];
            for (int j = 0; j < nP_; ++j) s += Mi_[k][j] * i[j];
            vr[k] = s;
        }
        for (int k = 0; k < nO_; ++k) {
            double s = Ou_[k] * u;
            for (int j = 0; j < nX_; ++j) s += Oz_[k][j] * st.z[j];
            for (int j = 0; j < nP_; ++j) s += Oi_[k][j] * i[j];
            y[k] = s;
        }
        // trapezoidal history: cap h' = 2 G v - h, inductor h' = 2 G v + h
        for (int k = 0; k < nX_; ++k) st.z[k] = 2.0 * g_[k] * vr[k] + sgn_[k] * st.z[k];
    }

private:
    struct Rr { int a, b; double g; };
    struct Xx { int a, b; double val; bool ind; };
    struct Op { int p, n, o; };
    struct Pp { int a, b; };
    int nn_ = 0, nR_ = 0, nX_ = 0, nOp_ = 0, nP_ = 0, nO_ = 0, src_ = 0;
    Rr r_[kMaxR] {};
    Xx x_[MS] {};
    Op op_[kMaxOp] {};
    Pp pt_[MP] {};    // sensed pair (the port voltage)
    Pp dr_[MP] {};    // driven pair (where the port current flows: a -> b)
    Pp out_[MO] {};
    PortLaw law_[MP] {};
    double g_[MS] {}, sgn_[MS] {}, vcrit_[MP] {};
    double Pz_[MP][MS] {}, Pu_[MP] {}, Pi_[MP][MP] {};
    double Mz_[MS][MS] {}, Mu_[MS] {}, Mi_[MS][MP] {};
    double Oz_[MO][MS] {}, Ou_[MO] {}, Oi_[MO][MP] {};
    mutable long fails_ = 0;
    double A_[kMaxU][kMaxU] {};   // compile() scratch
    int    piv_[kMaxU] {};
    double col_[kMaxU] {};

    static double law(const PortLaw& L, double v, double& di) noexcept {
        auto ex = [](double a, double& d) {   // exp with a linear continuation past 60 (Newton safety)
            if (a < 60.0) { const double e = std::exp(a); d = e; return e; }
            const double e = std::exp(60.0); d = e; return e * (1.0 + (a - 60.0));
        };
        double d1, d2, e1, e2;
        const double a = v / L.nvt;
        if (L.voff == 0.0 && std::fabs(a) < 60.0) {   // a diode pair: one exp serves both junctions
            e1 = std::exp(a); e2 = 1.0 / e1; d1 = e1; d2 = e2;
        } else {
            e1 = ex((v - L.voff) / L.nvt, d1);
            e2 = ex((-v - L.voff) / L.nvt, d2);
        }
        di = L.is * (d1 + d2) / L.nvt;
        return L.is * (e1 - e2);
    }
    // Port k at unknown w: a diode port's unknown is its voltage, an op-amp port's is its output
    // current (in the current the stiff high-gain stage is well conditioned: v = h(i) is nearly
    // flat in the linear region and a barrier at the rails). Returns v, i and their slopes in w.
    void evalPort(int k, double w, double& v, double& i, double& dv, double& di) const noexcept {
        const PortLaw& L = law_[k];
        if (!L.opamp) { v = w; dv = 1.0; i = law(L, w, di); return; }
        const double imax = L.g * L.vsat, c = L.vsat / L.gain;
        const double r = w / imax, q = 1.0 - r * r;
        v = c * r / q; dv = (c / imax) * (1.0 + r * r) / (q * q);
        i = w; di = 1.0;
    }
    // SPICE pnjlim on the conducting side of a symmetric diode port; an op-amp port's current
    // stays strictly inside its rails (fraction-to-boundary).
    double limit(int k, double wNew, double wOld) const noexcept {
        const PortLaw& L = law_[k];
        if (L.opamp) {
            const double imax = L.g * L.vsat * (1.0 - 1e-12);
            if (std::fabs(wNew) < imax) return wNew;
            return wOld + 0.9 * ((wNew > 0.0 ? imax : -imax) - wOld);
        }
        const double s = wNew >= 0.0 ? 1.0 : -1.0;
        double an = std::fabs(wNew) - L.voff;
        const double ao = (wNew * wOld > 0.0 ? std::fabs(wOld) : 0.0) - L.voff;
        if (an > vcrit_[k] && std::fabs(an - ao) > 2.0 * L.nvt) {
            if (ao > 0.0) { const double arg = 1.0 + (an - ao) / L.nvt; an = arg > 0.0 ? ao + L.nvt * std::log(arg) : vcrit_[k]; }
            else          { an = L.nvt * std::log(an / L.nvt); }
        }
        return s * (an + L.voff);
    }
    // Residual F(w) = v(w) - v0 - Pi i(w); returns |F|^2 (dv, di: the slopes for the Jacobian).
    double residual(const double* w, const double* v0, double* i, double* dv, double* di, double* F) const noexcept {
        const int P = nP_;
        double v[MP];
        for (int k = 0; k < P; ++k) evalPort(k, w[k], v[k], i[k], dv[k], di[k]);
        double n2 = 0.0;
        for (int k = 0; k < P; ++k) {
            double s = v[k] - v0[k];
            for (int j = 0; j < P; ++j) s -= Pi_[k][j] * i[j];
            F[k] = s; n2 += s * s;
        }
        return n2;
    }
    // Damped Newton: the full step (diode ports pnj-limited), halved until |F| falls (Armijo).
    // The op-amp ports make the system stiff (open-loop gain 1e5): undamped, a stage leaving
    // its rail and the diodes it drives can trade places forever.
    void solvePorts(double* v, const double* v0, double* i) const noexcept {
        const int P = nP_;
        if (P == 0) return;
        double dv[MP], g[MP], F[MP];
        double n2 = residual(v, v0, i, dv, g, F);
        for (int it = 0; it < 100; ++it) {
            if (n2 < 1e-24) break;
            double J[MP][MP], d[MP], R[MP];
            for (int k = 0; k < P; ++k) {
                R[k] = F[k];
                for (int j = 0; j < P; ++j) J[k][j] = (k == j ? dv[k] : 0.0) - Pi_[k][j] * g[j];
            }
            for (int c = 0; c < P; ++c) {
                int b = c; for (int r = c + 1; r < P; ++r) if (std::fabs(J[r][c]) > std::fabs(J[b][c])) b = r;
                if (b != c) { for (int j = 0; j < P; ++j) std::swap(J[c][j], J[b][j]); std::swap(R[c], R[b]); }
                const double inv = 1.0 / J[c][c];
                for (int r = c + 1; r < P; ++r) {
                    const double f = J[r][c] * inv;
                    for (int j = c; j < P; ++j) J[r][j] -= f * J[c][j];
                    R[r] -= f * R[c];
                }
            }
            for (int r = P - 1; r >= 0; --r) { double s = R[r]; for (int j = r + 1; j < P; ++j) s -= J[r][j] * d[j]; d[r] = s / J[r][r]; }
            double full[MP], vt[MP], it2[MP], dvt[MP], gt[MP], Ft[MP];
            double maxd = 0.0;
            // The limiters (pnjlim, fraction-to-boundary) shorten the step as ONE scale on the whole
            // Newton direction: limiting each port on its own bends the direction off descent.
            double sc = 1.0;
            for (int k = 0; k < P; ++k) {
                if (d[k] == 0.0) continue;
                const double lim = limit(k, v[k] - d[k], v[k]) - v[k];
                sc = std::min(sc, std::max(0.0, lim / -d[k]));
            }
            for (int k = 0; k < P; ++k) { full[k] = -sc * d[k]; maxd = std::max(maxd, std::fabs(full[k])); }
            double a = 1.0, nt = 0.0;
            for (int h = 0; h < 16; ++h, a *= 0.5) {
                for (int k = 0; k < P; ++k) vt[k] = v[k] + a * full[k];
                nt = residual(vt, v0, it2, dvt, gt, Ft);
                if (nt < (1.0 - 1e-4 * a) * n2) break;
            }
            for (int k = 0; k < P; ++k) { v[k] = vt[k]; i[k] = it2[k]; dv[k] = dvt[k]; g[k] = gt[k]; F[k] = Ft[k]; }
            n2 = nt;
            if (a * maxd < 1e-9) break;
            if (it == 99) ++fails_;
        }
    }
};

} // namespace ssdk
