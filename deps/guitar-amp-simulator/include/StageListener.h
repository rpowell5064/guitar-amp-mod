#pragma once
// StageListener — hearing the rig as a player standing in front of the cab (2026-10-19).
//
// Everything upstream renders the cab as a close MICROPHONE hears it. A player on stage hears
// something else: the cab sitting on the floor a couple of metres in front of them, its cones beaming
// the treble straight ahead (below the player's ears), the floor bounce, the wall behind the cab
// loading the low end, the side walls and ceiling arriving from their own directions, and the room's
// tail around it all -- through two ears and a head. This renders that for headphones:
//
//   cab (on the floor, against the back wall) --> image sources of a shoebox room up to 2nd order
//     each path: its delay r/c, 1/r spreading, the walls' absorption per bounce, the cone's
//     off-axis roll-off at its angle, then the HEAD for its direction
//   + a late tail (8-line FDN, lengths from the room's mean free path, RT60 from Sabine)
//
// The head is the Brown & Duda structural model (IEEE Trans. Speech Audio Proc. 6(5), 1998): a
// spherical head (interaural delay by Woodworth's formula, a one-pole/one-zero head-shadow filter per
// ear whose zero moves with the angle of incidence) plus their five-echo pinna model for elevation.
// It is computed from geometry, needs no measured HRTF set (so there is nothing to license) and costs
// a few multiplies per path and ear.
//
// The cone: a 12" driver radiates as a piston whose effective radius shrinks as the cone breaks up;
// with a ~7 cm effective radius the off-axis -3 dB point is f = 1.6 c / (2 pi a sin(theta)).
//
// Input: the chain's stereo output (two cabs at +-az if it differs L/R, one cab dead ahead if mono).
// Output: binaural stereo. Off = not called.
#include <algorithm>
#include <cmath>
#include <vector>

class StageListener {
public:
    enum Room { Small = 0, Medium = 1, Large = 2 };

    void prepare(double fs) {
        fs_ = fs;
        const size_t n = size_t(fs * 0.25) + 8;          // 250 ms covers 2nd-order paths in the large room
        for (auto& b : src_) b.assign(n, 0.0f);
        w_ = 0;
        for (int i = 0; i < kFdn; ++i) fdn_[i].assign(size_t(fs * 0.08) + 8, 0.0f);
        configured_ = false;
    }
    void setDistance(float m) { if (m != dist_) { dist_ = std::clamp(m, 0.7f, 6.0f); configured_ = false; } }
    void setRoom(int r)       { if (r != room_) { room_ = std::clamp(r, 0, 2); configured_ = false; } }
    void setStereoSpread(float deg) { if (deg != spread_) { spread_ = std::clamp(deg, 0.0f, 45.0f); configured_ = false; } }
    // introspection for the lab check: per path {delay ms, gain dB, az, el, cone fc Hz, ITD L us, ITD R us, order}
    int pathCount() { if (!configured_) configure(); return int(paths_.size()); }
    void pathInfo(int i, float* o) const {
        const Path& p = paths_[size_t(i)];
        o[0] = float(p.delay / fs_ * 1e3); o[1] = 20.0f * std::log10(std::fabs(p.gain) + 1e-9f); o[2] = p.az; o[3] = p.el; o[4] = p.fc;
        o[5] = float(p.itdL / fs_ * 1e6); o[6] = float(p.itdR / fs_ * 1e6); o[7] = float(p.order);
    }
    float lateDelayMs() const { return float(lateDelay_ / fs_ * 1e3); }

    // in-place on a block: L/R in = the chain's output, L/R out = what the player's two ears hear
    void process(float* L, float* R, int n) {
        if (!configured_) configure();
        for (int i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            src_[0][w_] = xl; src_[1][w_] = xr;
            float el = 0.0f, er = 0.0f;
            for (Path& p : paths_) {
                const float x = read(src_[p.src], p.delay);
                // walls (per-bounce absorption LP) and the cone's off-axis roll-off, folded into one pole
                p.lp += p.lpA * (x - p.lp);
                const float s = p.lp * p.gain;
                // the head: each ear has its own delay (ITD) and shadow filter
                p.earL.push(s); p.earR.push(s);
                el += p.earL.out(p.itdL); er += p.earR.out(p.itdR);
            }
            // late field: feed the 8-line FDN with the direct-path signal of both cabs
            const float mono = 0.5f * (read(src_[0], lateDelay_) + read(src_[1], lateDelay_));   // the tail starts after the direct sound
            float o[kFdn], acc = 0.0f;
            for (int k = 0; k < kFdn; ++k) { o[k] = fdnRead(k); acc += o[k]; }
            acc *= 2.0f / kFdn;                                                      // Householder mixing
            for (int k = 0; k < kFdn; ++k) {
                float v = o[k] - acc;
                fdnLp_[k] += fdnLpA_ * (v - fdnLp_[k]);
                fdnWrite(k, fdnLp_[k] * fdnG_[k] + mono * kFdnIn[k]);
            }
            const float tl = (o[0] - o[2] + o[4] - o[6]) * lateGain_, tr = (o[1] - o[3] + o[5] - o[7]) * lateGain_;
            L[i] = el + tl; R[i] = er + tr;
            if (++w_ >= src_[0].size()) w_ = 0;
            ++fdnW_;
        }
    }

private:
    static constexpr int kFdn = 8;
    static constexpr double kC = 343.0;
    static constexpr double kHeadR = 0.0875;           // head radius (m)
    static constexpr float kFdnIn[kFdn] = { 0.35f, -0.35f, 0.35f, 0.35f, -0.35f, 0.35f, -0.35f, -0.35f };

    // one ear of one path: a short fractional delay line (ITD) into a head-shadow one-pole/one-zero and
    // the pinna's five echoes (Brown & Duda) -- all at the path's own angles
    struct Ear {
        float buf[64] = {}; int w = 0;
        float a0 = 1, a1 = 0, b1 = 0, z = 0, xz = 0;   // shadow: y = a0 x + a1 x[-1] - b1 y[-1]
        float pr[5] = {}; float pd[5] = {};            // pinna echo gains and delays (samples)
        void push(float x) { buf[w] = x; w = (w + 1) & 63; }
        float tap(float d) const {                     // linear fractional read, d < 60
            const float p = float(w) - 1.0f - d + 128.0f; const int i = int(p); const float f = p - float(i);
            return buf[i & 63] * (1.0f - f) + buf[(i + 1) & 63] * f;
        }
        float out(float itd) {
            float x = tap(itd);
            for (int k = 0; k < 5; ++k) if (pr[k] != 0.0f) x += pr[k] * tap(itd + pd[k]);
            const float y = a0 * x + a1 * xz - b1 * z; xz = x; z = y; return y;
        }
    };
    struct Path { int src = 0; float delay = 0, gain = 0, lpA = 1, lp = 0, itdL = 0, itdR = 0, az = 0, el = 0, fc = 0; int order = 0; Ear earL, earR; };

    float read(const std::vector<float>& b, float d) const {
        const size_t n = b.size();
        const double p = double(w_) - double(d) + double(n);
        const size_t i = size_t(p) % n; const float f = float(p - std::floor(p));
        return b[i] * (1.0f - f) + b[(i + 1) % n] * f;
    }
    float fdnRead(int k) const { const auto& b = fdn_[k]; return b[(fdnW_ + b.size() - fdnLen_[k]) % b.size()]; }
    void  fdnWrite(int k, float v) { auto& b = fdn_[k]; b[fdnW_ % b.size()] = v; }

    // Brown & Duda head shadow for the angle between the source and the ear axis (theta, degrees):
    //   H(w) = (1 + j alpha w / 2w0) / (1 + j w / 2w0),  w0 = c/a,  alpha = 1.05 + 0.95 cos(theta * 180/150)
    void setShadow(Ear& e, double thetaDeg) const {
        const double alpha = 1.05 + 0.95 * std::cos(thetaDeg / 150.0 * M_PI);
        const double w0 = kC / kHeadR, T = 1.0 / fs_;
        // bilinear transform of (alpha s + 2 w0) / (s + 2 w0)
        const double k = 2.0 / T, b0 = alpha * k + 2 * w0, b1 = -alpha * k + 2 * w0, a0 = k + 2 * w0, a1 = -k + 2 * w0;
        e.a0 = float(b0 / a0); e.a1 = float(b1 / a0); e.b1 = float(a1 / a0);
    }
    // Brown & Duda pinna echoes: tau_n = A_n cos(az/2) sin(D_n (90 - el)) + B_n  (samples at 44.1 kHz)
    void setPinna(Ear& e, double azDeg, double elDeg) const {
        static const double rho[5] = { 0.5, -1.0, 0.5, -0.25, 0.25 }, A[5] = { 1, 5, 5, 5, 5 },
                            B[5] = { 2, 4, 7, 11, 13 }, D[5] = { 1, 0.5, 0.5, 0.5, 0.5 };
        for (int k = 0; k < 5; ++k) {
            const double t = A[k] * std::cos(azDeg * M_PI / 360.0) * std::sin(D[k] * (90.0 - elDeg) * M_PI / 180.0) + B[k];
            e.pd[k] = float(std::max(1.0, t * fs_ / 44100.0)); e.pr[k] = float(rho[k] * 0.35);   // 0.35: a gentle pinna
        }
    }

    void configure() {
        // room (m): width x depth x height; absorption per surface (rehearsal-room materials)
        static const double dims[3][3] = { { 4.0, 5.0, 2.7 }, { 6.0, 8.0, 3.0 }, { 10.0, 14.0, 4.5 } };
        static const double alphaWall[3] = { 0.30, 0.25, 0.22 };
        const double W = dims[room_][0], Dp = dims[room_][1], H = dims[room_][2], al = alphaWall[room_];
        const double R = std::sqrt(1.0 - al);           // pressure reflection coefficient of each surface
        // cab: centred in width, its back 0.3 m off the back wall (y = 0), cone centre 0.5 m up;
        // listener: dist_ in front, ears 1.6 m up
        const double cy = 0.35, cz = 0.5, ly = cy + dist_, lz = 1.6;
        paths_.clear();
        const int nsrc = spread_ > 0.5f ? 2 : 1;
        for (int s = 0; s < nsrc; ++s) {
            // two cabs side by side 1.2 m apart when the rig is stereo (the spread sets their separation)
            const double cx = W / 2 + (nsrc == 2 ? (s == 0 ? -1.0 : 1.0) * std::tan(spread_ * M_PI / 180.0) * dist_ : 0.0);
            for (int nx = -2; nx <= 2; ++nx) for (int ny = -2; ny <= 2; ++ny) for (int nz = -2; nz <= 2; ++nz) {
                const int order = std::abs(nx) + std::abs(ny) + std::abs(nz);
                if (order > 2) continue;
                // image positions in a shoebox [0,W]x[0,Dp]x[0,H]
                const double ix = (nx % 2 == 0) ? nx * W + cx : nx * W + (W - cx);
                const double iy = (ny % 2 == 0) ? ny * Dp + cy : ny * Dp + (Dp - cy);
                const double iz = (nz % 2 == 0) ? nz * H + cz : nz * H + (H - cz);
                const double dx = ix - W / 2, dy = iy - ly, dz = iz - lz;       // listener at (W/2, ly, lz)
                const double r = std::sqrt(dx * dx + dy * dy + dz * dz);
                Path p; p.src = s;
                p.delay = float(r / kC * fs_);
                if (p.delay > float(src_[0].size() - 4)) continue;
                p.gain = float(std::pow(R, order) / std::max(r, 0.3));
                // listener faces the cab (towards -y); azimuth positive to the right, elevation positive up
                const double az = std::atan2(dx, -dy) * 180.0 / M_PI, el = std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 180.0 / M_PI;
                // the cone's axis points at +y (into the room): the angle off that axis for this path, seen from the image
                const double offAxis = std::acos(std::clamp((ly - iy) * (ny % 2 == 0 ? 1.0 : -1.0) / r, -1.0, 1.0));
                const double sOff = std::max(std::sin(std::min(offAxis, M_PI / 2)), 0.05);
                double fc = 1.6 * kC / (2 * M_PI * 0.07 * sOff);                       // piston, a_eff 7 cm
                fc = std::min(fc, 16000.0) * std::pow(0.82, order);                    // walls: duller per bounce
                if (std::cos(offAxis) < 0) fc *= 0.35;                                 // behind the cab: very dull
                p.lpA = float(1.0 - std::exp(-2 * M_PI * fc / fs_));
                p.az = float(az); p.el = float(el); p.fc = float(fc); p.order = order;
                // ears at +-90 deg: the angle between the source and each ear axis
                const double thL = std::acos(std::clamp(std::cos(el * M_PI / 180) * std::sin(-az * M_PI / 180), -1.0, 1.0)) * 180 / M_PI;
                const double thR = std::acos(std::clamp(std::cos(el * M_PI / 180) * std::sin(az * M_PI / 180), -1.0, 1.0)) * 180 / M_PI;
                setShadow(p.earL, thL); setShadow(p.earR, thR);
                if (order <= 1) { setPinna(p.earL, az, el); setPinna(p.earR, az, el); }
                // Woodworth ITD, the far ear late: tau = a/c (theta + sin theta), theta = lateral angle
                const double lat = std::asin(std::clamp(std::cos(el * M_PI / 180) * std::sin(az * M_PI / 180), -1.0, 1.0));
                const double itd = kHeadR / kC * (std::fabs(lat) + std::sin(std::fabs(lat))) * fs_;
                p.itdL = float(lat > 0 ? itd : 0.0); p.itdR = float(lat < 0 ? itd : 0.0);
                paths_.push_back(p);
            }
        }
        // normalise to the direct path of one cab at 1 m (so Distance changes the balance, not the level)
        float gd = 0.0f; for (const Path& p : paths_) gd = std::max(gd, p.gain);
        const float norm = gd > 0 ? 1.0f / gd : 1.0f;
        for (Path& p : paths_) p.gain *= norm;
        float dmin = 1e9f; for (const Path& p : paths_) dmin = std::min(dmin, p.delay);
        lateDelay_ = std::min(dmin + float(0.004 * fs_), float(src_[0].size() - 4));
        // late field: Sabine RT60 and the reverberant-to-direct ratio at the listening distance
        const double V = W * Dp * H, S = 2 * (W * Dp + W * H + Dp * H), A = S * al;
        const double rt60 = std::clamp(0.161 * V / A, 0.2, 2.5);
        const double mfp = 4.0 * V / S;                                                // mean free path (m)
        const double rRatio = std::sqrt(16.0 * M_PI * dist_ * dist_ / A);              // reverberant / direct (pressure)
        static const double spreadL[kFdn] = { 0.71, 0.83, 0.94, 1.07, 1.19, 1.31, 1.46, 1.62 };
        for (int k = 0; k < kFdn; ++k) {
            int len = int(mfp / kC * fs_ * spreadL[k]) + 37 * k;
            len = std::clamp(len, 64, int(fdn_[k].size()) - 2);
            fdnLen_[k] = size_t(len);
            fdnG_[k] = float(std::pow(10.0, -3.0 * len / (fs_ * rt60 * 1.45)));   // x1.45: the in-loop 5 kHz LP's share of the decay
        }
        fdnLpA_ = float(1.0 - std::exp(-2 * M_PI * 5000.0 / fs_));
        // the FDN's own output at unit input sits ~kFdnIn * sqrt(N)/sqrt(1-g^2); scale so its RMS is rRatio x direct
        const double g0 = fdnG_[3]; const double fdnRms = 0.35 * std::sqrt(1.0 / std::max(1e-3, 1.0 - g0 * g0));
        lateGain_ = float(rRatio / std::max(fdnRms, 1e-3) * norm / std::max(dist_, 0.3f) * 0.5);
        // unity loudness: the render carries the whole room's energy (25 paths + the tail), so scale it back to
        // the energy of the signal it was given -- Distance and Room change the balance, never the level
        double e = 0.0; for (const Path& p : paths_) e += double(p.gain) * p.gain;
        e /= double(nsrc); e += std::pow(rRatio, 2.0);
        const float og = float(1.0 / std::sqrt(std::max(e, 1e-6)));
        for (Path& p : paths_) p.gain *= og;
        lateGain_ *= og;
        for (auto& b : fdn_) std::fill(b.begin(), b.end(), 0.0f);
        for (auto& z : fdnLp_) z = 0.0f;
        configured_ = true;
    }

    double fs_ = 48000.0;
    float dist_ = 2.0f, spread_ = 0.0f; int room_ = Medium;
    bool configured_ = false;
    std::vector<float> src_[2]; size_t w_ = 0;
    std::vector<Path> paths_;
    std::vector<float> fdn_[kFdn]; size_t fdnLen_[kFdn] = {}; size_t fdnW_ = 0;
    float fdnG_[kFdn] = {}, fdnLp_[kFdn] = {}, fdnLpA_ = 0.5f, lateGain_ = 0.0f, lateDelay_ = 1.0f;
};
