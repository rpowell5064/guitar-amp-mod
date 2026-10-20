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
//   + a late tail: the suite's Hall FDN (input diffusion, modulated lines; ringing index 10.5 against
//     8.2 for ideal noise) set to the room's Sabine RT60 -- a bare short-line FDN rang like a tiled room
//     (index 19-22 dB: the user heard it as "metallic, like a toilet")
//   Walls SCATTER: a share of every bounce's energy leaves the specular path for the diffuse tail.
//
// VOICED NEAR, not textbook (2026-10-20, the user: "distanced and overly reverberated" at every setting).
// A bare diffuse-field render puts the room 5-12 dB ABOVE the cab at 1.5-3 m (the critical distance of a
// rehearsal room is ~1 m) and the floor-level cone beams its treble under the player's ears (direct path
// -12 dB at 4 kHz) -- correct for a microphone at head height, wrong for what a player hears: they tilt
// the cab back at their head, the room is full of gear and people, and the ear's precedence effect
// keeps the cab in front while the room only widens it. So: the cab is AIMED at the ears, the walls
// absorb like a furnished room, the 2nd-order images are halved and the tail is trimmed kTailTrim below
// the diffuse-field level -- the cab leads by ~8-12 dB at 1 m and the room grows with Stand At.
//
// TONE-NEUTRAL (2026-10-20, the user: "listen makes the mids go away"). Mirror-perfect first reflections
// (floor, ceiling, back and side walls) are coherent copies of the cab; together they dug 5-10 dB out of
// 160-315 Hz (the guitar's body) and the bare head model took 5 dB off the top (a headphone is tuned so a
// source dead ahead reads flat). So each reflection passes a short Schroeder all-pass (a furnished wall
// scatters, it does not mirror) and an 8-band octave EQ, solved at configure from the render's own
// response, holds the octave balance of what was played: Listen moves the cab into a room, it does not
// re-EQ the rig. The fine comb inside each octave (the room's signature) is kept.
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
#include <complex>
#include <cmath>
#include <vector>
#include "StudioReverbs.h"
#include "BiquadFilter.h"

class StageListener {
public:
    enum Room { Small = 0, Medium = 1, Large = 2 };

    void prepare(double fs) {
        fs_ = fs;
        const size_t n = size_t(fs * 0.25) + 8;          // 250 ms covers 2nd-order paths in the large room
        for (auto& b : src_) b.assign(n, 0.0f);
        w_ = 0;
        hall_.prepare(fs);
        configured_ = false;
    }
    void reset() {
        for (auto& b : src_) std::fill(b.begin(), b.end(), 0.0f);
        hall_.clear();
        configured_ = false;   // rebuilds the paths (and their ear filters) on the next block
    }
    // compare the CLAMPED value: an out-of-range input must not read as a change on every block (each
    // reconfigure rebuilds the paths and clears the tail)
    void setDistance(float m) { m = std::clamp(m, 0.5f, 4.0f); if (m != dist_) { dist_ = m; configured_ = false; } }
    void setRoom(int r)       { r = std::clamp(r, 0, 2); if (r != room_) { room_ = r; configured_ = false; } }
    void setStereoSpread(float deg) { deg = std::clamp(deg, 0.0f, 45.0f); if (deg != spread_) { spread_ = deg; configured_ = false; } }
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
                float s = p.lp * p.gain;
                if (p.order > 0) {   // no body in the reflections: the coherent low-mid comb is what hollowed the tone
                    p.hp1 += hpA_ * (s - p.hp1); s -= p.hp1;
                    p.hp2 += hpA_ * (s - p.hp2); s -= p.hp2;
                }
                if (p.apD > 0) {   // the wall's scatter: (-g + z^-D) / (1 - g z^-D)
                    const float dl = p.ap[(p.apW - p.apD) & 255], y = -p.apG * s + dl;
                    p.ap[p.apW] = s + p.apG * y; p.apW = (p.apW + 1) & 255; s = y;
                }
                // the head: each ear has its own delay (ITD) and shadow filter
                p.earL.push(s); p.earR.push(s);
                el += p.earL.out(p.itdL); er += p.earR.out(p.itdR);
            }
            // late field: both cabs, from just after the direct sound, into the true-stereo Hall FDN
            float tl, tr;
            hall_.process(read(src_[0], lateDelay_), read(src_[1], lateDelay_), tl, tr);
            tl *= lateGain_; tr *= lateGain_;
            float ol = el + tl, orr = er + tr;
            for (int b = 0; b < kBands; ++b) { ol = eqL_[b].process(ol); orr = eqR_[b].process(orr); }
            L[i] = ol; R[i] = orr;
            if (++w_ >= src_[0].size()) w_ = 0;
        }
    }

private:
    static constexpr double kC = 343.0;
    static constexpr double kHeadR = 0.0875;           // head radius (m)
    static constexpr double kScatter = 0.35;           // scattered share of each bounce's reflected energy
    static constexpr double kTailTrim = 0.3;           // tail re the diffuse-field level (-10.5 dB, see the header)
    static constexpr double kOrder2 = 0.5;             // extra gain on the 2nd-order images
    static constexpr double kRear = 0.5;               // extra gain on paths leaving the back of the cab
    static constexpr double kFloor = 0.55;             // extra gain on paths that bounce off the floor

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
    struct Path { int src = 0; float delay = 0, gain = 0, lpA = 1, lp = 0, itdL = 0, itdR = 0, az = 0, el = 0, fc = 0; int order = 0; Ear earL, earR;
                  float ap[256] = {}; int apD = 0, apW = 0; float apG = 0.0f; float hp1 = 0.0f, hp2 = 0.0f; };
    static constexpr int kBands = 8;                   // octave (a half-octave EQ chased the fine comb and did no better)
    static constexpr double kBandHz[kBands] = { 63, 125, 250, 500, 1000, 2000, 4000, 8000 };
    static constexpr double kBandQ = 1.41;             // octave bandwidth
    static constexpr double kReflHpHz = 300.0;         // reflections carry no body: 2 x one-pole high-pass

    // the complex response of one path at w (rad/sample) into the LEFT ear: wall/cone low-pass, scatter
    // all-pass, travel delay, interaural delay, pinna echoes, head shadow -- the same terms process() runs
    std::complex<double> pathResp(const Path& p, double w) const {
        using C = std::complex<double>;
        const C z1 = std::polar(1.0, -w);
        const double a = 1.0 - p.lpA;
        C h = double(p.lpA) / (1.0 - a * z1) * double(p.gain);
        if (p.order > 0) { const double ah = 1.0 - double(hpA_); const C hp = ah * (1.0 - z1) / (1.0 - ah * z1); h *= hp * hp; }
        auto lin = [&](double d) { const double f = d - std::floor(d); return (1.0 - f) + f * z1; };   // the reads' linear interpolation
        h *= lin(p.delay) * lin(p.itdL);
        if (p.apD > 0) { const C zd = std::polar(1.0, -w * p.apD); h *= (-double(p.apG) + zd) / (1.0 - double(p.apG) * zd); }
        h *= std::polar(1.0, -w * (double(p.delay) + double(p.itdL)));
        C pin = 1.0; for (int k = 0; k < 5; ++k) if (p.earL.pr[k] != 0.0f) pin += double(p.earL.pr[k]) * std::polar(1.0, -w * p.earL.pd[k]);
        return h * pin * (double(p.earL.a0) + double(p.earL.a1) * z1) / (1.0 + double(p.earL.b1) * z1);
    }
    static double peakPow(const BiquadCoeffs& c, double w) {
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
        return std::norm((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
    }

    float read(const std::vector<float>& b, float d) const {
        const size_t n = b.size();
        const double p = double(w_) - double(d) + double(n);
        const size_t i = size_t(p) % n; const float f = float(p - std::floor(p));
        return b[i] * (1.0f - f) + b[(i + 1) % n] * f;
    }

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
            e.pd[k] = float(std::max(1.0, t * fs_ / 44100.0)); e.pr[k] = float(rho[k] * 0.15);   // 0.15: at 0.35 the echoes notched the top like a comb
        }
    }

    void configure() {
        // room (m): width x depth x height; absorption per surface (rehearsal-room materials)
        static const double dims[3][3] = { { 4.0, 5.0, 2.7 }, { 6.0, 8.0, 3.0 }, { 10.0, 14.0, 4.5 } };
        static const double alphaWall[3] = { 0.40, 0.35, 0.30 };   // furnished: gear, carpet, the band
        const double W = dims[room_][0], Dp = dims[room_][1], H = dims[room_][2], al = alphaWall[room_];
        // pressure reflection of each surface, SPECULAR share only: kScatter of the reflected energy is scattered
        // (walls, gear, people) and lives in the diffuse tail -- mirror-perfect walls comb the tone
        const double R = std::sqrt((1.0 - al) * (1.0 - kScatter));
        // cab: centred in width, its back 0.3 m off the back wall (y = 0), cone centre 0.5 m up;
        // listener: dist_ in front, ears 1.6 m up
        const double cy = 0.35, cz = 0.5, ly = cy + dist_, lz = 1.6;
        paths_.clear();
        hpA_ = float(1.0 - std::exp(-2 * M_PI * kReflHpHz / fs_));
        const int nsrc = spread_ > 0.5f ? 2 : 1;
        for (int s = 0; s < nsrc; ++s) {
            // two cabs side by side 1.2 m apart when the rig is stereo (the spread sets their separation)
            const double cx = W / 2 + (nsrc == 2 ? (s == 0 ? -1.0 : 1.0) * std::tan(spread_ * M_PI / 180.0) * dist_ : 0.0);
            // the cone's axis: tilted back and turned at the player's head (a tilt-back stand, as players set it)
            double ax[3] = { W / 2 - cx, ly - cy, lz - cz };
            { const double m = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); for (double& v : ax) v /= m; }
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
                // a floor bounce (nz -1, or +-2 = floor and ceiling) meets carpet and a 2-cone baffle spread over
                // 0.3 m, so it does not cancel the direct sound cleanly (the bare image notched 250 Hz by 8-12 dB)
                const bool floorHit = nz == -1 || std::abs(nz) == 2;
                p.gain = float(std::pow(R, order) / std::max(r, 0.3) * (order == 2 ? kOrder2 : 1.0) * (floorHit ? kFloor : 1.0));
                // listener faces the cab (towards -y); azimuth positive to the right, elevation positive up
                const double az = std::atan2(dx, -dy) * 180.0 / M_PI, el = std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 180.0 / M_PI;
                // the angle off the cone's axis for this path, seen from the image (each mirror flips its axis component)
                const double c = (-dx * ax[0] * (nx % 2 == 0 ? 1 : -1) - dy * ax[1] * (ny % 2 == 0 ? 1 : -1) - dz * ax[2] * (nz % 2 == 0 ? 1 : -1)) / r;
                const double offAxis = std::acos(std::clamp(c, -1.0, 1.0));
                const double sOff = std::max(std::sin(std::min(offAxis, M_PI / 2)), 0.05);
                double fc = 1.6 * kC / (2 * M_PI * 0.07 * sOff);                       // piston, a_eff 7 cm
                fc = std::min(fc, 16000.0) * std::pow(0.82, order);                    // walls: duller per bounce
                // behind the cab: very dull, and quieter -- a closed 2x12 baffle already beams forward at 250 Hz, so
                // the back-wall image must not cancel the front at the wall's quarter-wave (it dug 10 dB at 250 Hz)
                if (std::cos(offAxis) < 0) { fc *= 0.35; p.gain *= float(kRear); }
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
                if (order >= 1) {   // incommensurate 1.4-4.9 ms scatter per reflection
                    static const int kApD[8] = { 67, 89, 113, 139, 163, 191, 211, 233 };
                    p.apD = std::min(250, int(kApD[paths_.size() % 8] * fs_ / 48000.0)); p.apG = 0.6f;
                }
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
        // the Hall FDN at the room's decay; its impulse energy is ~0.205 + 0.098 RT60 (measured, damping 0.3),
        // so this puts the tail at rRatio x the direct sound in pressure, as the diffuse-field theory has it
        // x1.18 on the decay and x0.5 on the level: what the impulse check measured against Sabine and the
        // diffuse-field ratio once the tank sits in this render (lab tools/stage_listener_check)
        hall_.set(float(rt60 * 1.18), 0.3f, 0.25f, 0.5f, 1.0f);
        lateGain_ = float(0.5 * kTailTrim * rRatio / std::sqrt(0.205 + 0.098 * rt60 * 1.18));
        (void)mfp;
        // TONE-NEUTRAL + unity loudness. P[b] = the power of the summed early response (every path of every
        // cab fed the same signal: the chain's two sides are near-identical, and coherent paths add in pressure)
        // averaged over octave b. The EQ divides each octave back to the 250 Hz-2 kHz reference (iterated: the
        // octave peaks overlap), then one gain puts reference + tail at the input's level.
        constexpr int kPts = 5;
        double P[kBands], wpt[kBands][kPts];
        for (int b = 0; b < kBands; ++b) {
            P[b] = 0.0;
            for (int k = 0; k < kPts; ++k) {
                const double f = std::min(kBandHz[b] * std::pow(2.0, (k - (kPts - 1) * 0.5) / (kPts - 1)), fs_ * 0.45);
                wpt[b][k] = 2 * M_PI * f / fs_;
                std::complex<double> h = 0.0; for (const Path& p : paths_) h += pathResp(p, wpt[b][k]);
                P[b] += std::norm(h) / kPts;
            }
        }
        const double ref = std::pow(P[2] * P[3] * P[4] * P[5], 0.25);   // 250 Hz-2 kHz
        double gdB[kBands]; for (int b = 0; b < kBands; ++b) gdB[b] = std::clamp(-10.0 * std::log10(P[b] / ref), -9.0, 9.0);
        BiquadCoeffs co[kBands];
        for (int it = 0; it < 4; ++it) {
            for (int b = 0; b < kBands; ++b) co[b] = Filters::peaking(kBandHz[b], gdB[b], kBandQ, fs_);
            for (int b = 0; b < kBands; ++b) {
                double q = 0.0;
                for (int k = 0; k < kPts; ++k) { double g = 1.0; for (int c = 0; c < kBands; ++c) g *= peakPow(co[c], wpt[b][k]); q += g / kPts; }
                gdB[b] = std::clamp(gdB[b] - 0.8 * 10.0 * std::log10(q * P[b] / ref), -9.0, 9.0);
            }
        }
        for (int b = 0; b < kBands; ++b) {
            co[b] = Filters::peaking(kBandHz[b], gdB[b], kBandQ, fs_);
            eqL_[b].setCoeffs(co[b]); eqR_[b].setCoeffs(co[b]); eqL_[b].reset(); eqR_[b].reset();
            eqDb_[b] = float(gdB[b]);
        }
        const double e = ref + 0.25 * kTailTrim * kTailTrim * rRatio * rRatio;   // the tail's level (see the Hall level above)
        const float og = float(1.0 / std::sqrt(std::max(e, 1e-6)));
        for (Path& p : paths_) p.gain *= og;
        lateGain_ *= og;
        hall_.clear();
        configured_ = true;
    }

    double fs_ = 48000.0;
    float dist_ = 1.0f, spread_ = 0.0f; int room_ = Medium;
    bool configured_ = false;
    std::vector<float> src_[2]; size_t w_ = 0;
    std::vector<Path> paths_;
    studiorev::FdnHall hall_;
    float lateGain_ = 0.0f, lateDelay_ = 1.0f;
    BiquadFilter eqL_[kBands], eqR_[kBands]; float eqDb_[kBands] = {};
    float hpA_ = 0.04f;
public:
    float eqDb(int b) const { return eqDb_[b]; }   // the lab check prints the neutralising EQ
};
