#pragma once
// StageFeedback — the speaker-to-string acoustic loop of a loud amp in a room (2026-10-19).
//
// A cranked stack drives the guitar's strings with its own sound: the note blooms, sustains and
// tips into harmonic feedback, and how it behaves depends on where the player stands. Direct and
// headphone rigs lose this because nothing couples the speaker back to the strings. This puts the
// loop back:
//
//   speaker out ──► air (distance d: delay d/c, 1/d) ──► string ──► pickup ──► + guitar DI ──► chain
//
// The STRING is a Karplus-Strong resonator (fractional delay = one period, one-pole loss filter)
// tuned to the note the player is fretting, found by YIN on the dry DI. Driving it with the delayed
// speaker signal is what a real string does: only energy near its partials builds up, and which
// partial wins is set by the loop phase (amp + cab + the acoustic delay), so stepping closer or
// further moves the feedback between harmonics as it does on stage. The amp's own saturation sets
// the steady state; the string's swing is additionally capped at the amplitude it was PICKED with
// (a driven string cannot out-swing its own attack), which is level-invariant: nothing here uses an
// absolute threshold, so it behaves the same at any gain staging.
//
// The player's hand is honoured: a fast DI drop (> ~125 dB/s, a palm or fretting-hand mute) damps the
// string at once. A string the air keeps driving does not decay, so it sustains until it is muted
// (judged on the total string motion; it lets go if that falls 45 dB under the attack).
//
// Usage per block: for each sample, x' = process(diSample); after the chain has run the block,
// feedSpeaker(out, n) with the block's (mono) speaker output. The loop latency is therefore the
// acoustic delay plus one block, which is physical as long as the block is shorter than the
// distance (32 frames = 0.67 ms ~ 23 cm).
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

class StageFeedback {
public:
    void prepare(double fs) {
        fs_ = fs;
        spk_.assign(size_t(fs * 0.05) + 4096, 0.0f); spkW_ = 0;            // 50 ms of air (17 m) + a block
        str_.assign(size_t(fs / 40.0) + 8, 0.0f); strW_ = 0;              // one period at 40 Hz
        dec_.assign(kDecLen, 0.0f); decW_ = 0; decPhase_ = 0;
        aa1_.set(fs, 2600.0); aa2_.set(fs, 2600.0);
        body_.setBP(fs, 250.0, 0.8);
        reset();
    }
    void reset() {
        std::fill(spk_.begin(), spk_.end(), 0.0f); std::fill(str_.begin(), str_.end(), 0.0f);
        std::fill(dec_.begin(), dec_.end(), 0.0f);
        aa1_.z1 = aa1_.z2 = aa2_.z1 = aa2_.z2 = body_.z1 = body_.z2 = 0.0; loss_ = 0.0f; lastS_ = 0.0f;
        winPk_ = totPk_ = 0.0f; winCnt_ = 0; floorDb_ = -55.0f; holdLeft_ = 0.0f; decayDbps_ = 8.0f; skipWin_ = 0; pending_ = 0; periodic_ = prevPeriodic_ = false; attackDb_ = -200.0f; aliveTarget_ = 0.0f;
        period_ = periodTarget_ = 0.0; pickAmp_ = 0.0f; envIn_ = envSpk_ = 0.0f; gEst_ = 100.0f;
        muted_ = true; alive_ = 0.0f; hop_ = 0; prevEnvDb_ = -200.0f;
    }

    // amount 0..1 = how loud the stage is / how hard the strings are driven; distance in metres.
    void setAmount(float a)   { amount_ = std::clamp(a, 0.0f, 1.0f); }
    void setDistance(float m) { dist_ = std::clamp(m, 0.3f, 10.0f); }
    void setSustain(float s)  { holdSec_ = 1.0f + 11.0f * std::clamp(s, 0.0f, 1.0f); }   // hold after the DI fades, 1..12 s
    float detectedHz() const  { return period_ > 0.0 ? float(fs_ / period_) : 0.0f; }
    float stringLevel() const { return alive_; }
    bool  muted() const       { return muted_; }
    float lastString() const  { return lastS_; }
    void  debug(float* o) const { o[0] = floorDb_; o[1] = decayDbps_; o[2] = holdLeft_; o[3] = prevEnvDb_; o[4] = attackDb_; }

    float process(float di) {
        trackDi(di, di + lastS_);
        if (amount_ <= 0.0f) return di;

        // the air: the speaker signal d/c ago, falling off as 1/d (1 m = reference)
        const double delay = dist_ / 343.0 * fs_;
        // the body: a solid guitar takes the air in through its body/bridge resonances (low mids), so
        // the coupling is band-limited around ~250 Hz; it is what makes a low note feed back on its
        // octave or twelfth rather than on a high partial
        const float air = float(body_.p(readFrac(spk_, spkW_, delay))) * (1.0f / dist_);
        // the chain's own gain (speaker / what the chain is fed) comes back out of the loop, so the Feedback
        // amount means the same on a clean amp and a cranked one (2026-10-29: un-normalised, a high-gain
        // preset's +50 dB put the loop over unity at 2 % and every note rang on into the rests)
        envIn_ += kEnvA * (std::fabs(di + lastS_) - envIn_);
        if (envIn_ > 3e-5f) {
            const float r = std::clamp(envSpk_ / envIn_, 0.01f, 3000.0f);
            gEst_ += (r > gEst_ ? 0.01f : 0.0005f) * (r - gEst_);              // rises fast, falls slow: a pick
        }                                                                     // reaches the input before the speaker

        float s = 0.0f;
        if (period_ > 0.0) {
            period_ += (periodTarget_ - period_) * glide_;                    // fret change: ~10 ms glide
            const float back = readFrac(str_, strW_, period_ - 1.0);         // -1: the loss filter's delay
            const float g = muted_ ? kMuteG : loopG();
            loss_ = loss_ + kLossA * (back - loss_);                          // partials above ~3 kHz die faster
            const float drive = coupling() * air / gEst_;
            float v = drive + g * loss_;
            const float cap = std::max(0.8f * pickAmp_, 1e-5f);               // a driven string stays under its own pick
            v = cap * std::tanh(v / cap);
            str_[strW_] = v; if (++strW_ >= str_.size()) strW_ = 0;
            s = v * alive_;
        }
        lastS_ = s;
        return di + s;
    }

    void feedSample(float y) { envSpk_ += kEnvA * (std::fabs(y) - envSpk_); spk_[spkW_] = y; if (++spkW_ >= spk_.size()) spkW_ = 0; }
    // after the chain ran the block: the speaker's (mono) output
    void feedSpeaker(const float* y, int n) { for (int i = 0; i < n; ++i) feedSample(y[i]); }

private:
    static constexpr int   kDecim = 4;            // YIN runs at fs/4
    static constexpr int   kDecLen = 1024;
    static constexpr int   kWin = 384;            // 32 ms at 12 kHz: two periods of a 62 Hz low B
    static constexpr int   kHop = 64;             // re-estimate every 5.3 ms
    static constexpr float kMuteG = 0.80f;        // per-period loss while muted: gone in a few periods
    static constexpr float kLossA = 0.55f;
    static constexpr double kMaxDbps = 40.0;      // the air's drive at amount 1 (1 m, body peak)
    static constexpr float kEnvA = 1.0f / 2400.0f; // 50 ms level followers for the chain-gain estimate

    struct LP2 { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
        void set(double fs, double fc) { const double w = 2 * M_PI * fc / fs, c = std::cos(w), al = std::sin(w) / (2 * 0.7071), a0 = 1 + al;
            b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
        void setBP(double fs, double fc, double q) { const double w = 2 * M_PI * fc / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
            b0 = al / a0; b1 = 0.0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
        double p(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };

    float coupling() const {
        // With the chain gain divided out, the coupling is the per-period gain the air adds to the string at
        // the body resonance. It is set as a RATE (dB/s, 0..kMaxDbps along amount^2) so it means the same on
        // a low E and a high e: the string's own decay is 5-30 dB/s, so low settings only lengthen a note and
        // a held note tips into feedback once the air out-drives its damping (~amount .5 for a ringing note).
        if (period_ <= 0.0) return 0.0f;
        const double dbps = kMaxDbps * double(amount_) * amount_;
        return float(1.0 - std::pow(10.0, -dbps * period_ / (20.0 * fs_)));
    }
    float loopG() const {   // per-period gain at the string's measured decay (>= 3 dB/s: a T60 of 20 s at most)
        return period_ > 0.0 ? float(std::pow(10.0, -std::max(decayDbps_, 3.0f) * period_ / (20.0 * fs_))) : 0.0f;
    }
    static float readFrac(const std::vector<float>& b, size_t w, double back) {
        const size_t n = b.size();
        if (back < 1.0) back = 1.0;
        if (back > double(n - 4)) back = double(n - 4);
        const double p = double(w) - back + double(n);
        const size_t i = size_t(p) % n; const float f = float(p - std::floor(p));
        // 4-point Lagrange: a linear read would low-pass the string loop every period
        const float xm1 = b[(i + n - 1) % n], x0 = b[i], x1 = b[(i + 1) % n], x2 = b[(i + 2) % n];
        const float c0 = x0, c1 = x1 - xm1 / 3.0f - x0 / 2.0f - x2 / 6.0f,
                    c2 = (xm1 + x1) / 2.0f - x0, c3 = (x2 - xm1) / 6.0f + (x0 - x1) / 2.0f;
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    void trackDi(float di, float total) {
        // The hand: peak per 20 ms window of the DRY DI (a window longer than the lowest period, or a
        // low E's own ripple reads as a mute). A drop of > 2.5 dB between windows (> 125 dB/s, a string's
        // natural decay is 5-30 dB/s) once past the pick's own transient = a palm / fretting-hand mute.
        winPk_ = std::max(winPk_, std::fabs(di));
        totPk_ = std::max(totPk_, std::fabs(total));
        if (++winCnt_ >= int(fs_ * 0.020)) {
            winCnt_ = 0;
            const float db = 20.0f * std::log10(winPk_ + 1e-9f);
            const float tot = 20.0f * std::log10(totPk_ + 1e-9f);
            if (db - prevEnvDb_ > 6.0f && db > floorDb_ + 6.0f) {
                // a pick OR a hand touching the strings: either way the contact damps what the string was doing.
                // It is a note if a pitch follows within 80 ms, otherwise a touch (= a mute: once the dry string
                // has sunk into the pickup's hiss, the touch is the only trace a mute leaves).
                for (float& v : str_) v *= 0.1f;
                pending_ = 4; pendPk_ = winPk_; pendDb_ = db;
            }
            if (pending_ > 0) {
                if (periodic_) {
                    muted_ = false; pickAmp_ = std::max(pendPk_, 1e-5f) * 1.2f; attackDb_ = pendDb_; holdLeft_ = holdSec_;
                    decayDbps_ = 8.0f; skipWin_ = 1; pending_ = 0;
                } else if (--pending_ == 0) {
                    muted_ = true;
                }
            } else if (skipWin_ > 0) {
                --skipWin_;
            } else if (periodic_ && prevPeriodic_ && db > floorDb_ + 10.0f) {  // a pitched string clearly over the hiss
                // the real string's damping, read off the DI's decay: an open note 5-30 dB/s, a palm on the strings
                // hundreds. The virtual string gets the SAME damping, so a muted or palm-muted string cannot feed
                // back however loud the stage is -- exactly the physics, no switch.
                const float inst = std::clamp((prevEnvDb_ - db) * 50.0f, 0.0f, 600.0f);
                decayDbps_ += 0.5f * (inst - decayDbps_);
                if (inst > 125.0f && db < attackDb_ - 6.0f) muted_ = true;
            } else if (prevPeriodic_ && !periodic_ && prevEnvDb_ > floorDb_ + 12.0f && prevEnvDb_ - db > 6.0f) {
                muted_ = true;                                                // pitch gone on a fast drop from well above the hiss: a hand
            }
            // A string the air keeps driving does not decay, but once the DRY DI has sunk into the pickup's floor a
            // mute can no longer be seen, so the loop holds for the Sustain time and then lets go.
            if (!muted_ && !periodic_) holdLeft_ -= 0.020f;                    // the dry string has sunk into the hiss
            prevPeriodic_ = periodic_;
            const bool sounding = !muted_ && holdLeft_ > 0.0f && tot > attackDb_ - 45.0f;
            aliveTarget_ = sounding ? 1.0f : 0.0f;
            // pickup floor: learnt from the windows with no pitch in them (hiss by definition), never from a note
            if (!periodic_ && pending_ == 0) floorDb_ = std::max(-100.0f, floorDb_ + 0.05f * (db - floorDb_));
            prevEnvDb_ = db; winPk_ = 0.0f; totPk_ = 0.0f;
        }
        alive_ += (aliveTarget_ - alive_) * ((muted_ || aliveTarget_ > alive_) ? 0.0005f : 0.00002f);   // on / a mute: ~40 ms; a hold running out: ~1 s
        // YIN pitch on the decimated DI
        const double v = aa2_.p(aa1_.p(di));
        if (++decPhase_ >= kDecim) {
            decPhase_ = 0; dec_[decW_] = float(v); decW_ = (decW_ + 1) % kDecLen;
            if (++hop_ >= kHop) { hop_ = 0; yin(); }
        }
    }

    void yin() {
        const double fsd = fs_ / kDecim;
        const int tauMin = int(fsd / 1200.0), tauMax = std::min(int(fsd / 55.0), kDecLen - kWin - 2);
        float x[kWin + 256];
        const int need = kWin + tauMax;
        for (int i = 0; i < need; ++i) x[i] = dec_[(decW_ + kDecLen - need + i) % kDecLen];
        float d[256]; d[0] = 1.0f; float run = 0.0f; int best = -1;
        for (int tau = 1; tau <= tauMax; ++tau) {
            float acc = 0.0f;
            for (int j = 0; j < kWin; ++j) { const float e = x[j] - x[j + tau]; acc += e * e; }
            run += acc; d[tau] = run > 0.0f ? acc * float(tau) / run : 1.0f;
        }
        for (int tau = std::max(2, tauMin); tau < tauMax; ++tau)
            if (d[tau] < 0.15f && d[tau] <= d[tau + 1]) { best = tau; break; }
        periodic_ = best >= 0 && d[best] < 0.10f;                              // a clearly pitched DI (noise reads ~1)
        if (!periodic_) return;                                                // no confident pitch: hold the tuning
        const float y0 = d[best - 1], y1 = d[best], y2 = d[best + 1];
        const float den = y0 - 2.0f * y1 + y2;
        const double t = best + (std::fabs(den) > 1e-9f ? 0.5f * (y0 - y2) / den : 0.0f);
        const double p = t * kDecim;                                           // period at fs
        if (period_ <= 0.0) period_ = p;
        periodTarget_ = p;
    }

    double fs_ = 48000.0;
    float amount_ = 0.0f, dist_ = 1.0f, holdSec_ = 6.5f, holdLeft_ = 0.0f, decayDbps_ = 8.0f; int skipWin_ = 0, pending_ = 0; float pendPk_ = 0.0f, pendDb_ = -200.0f;
    std::vector<float> spk_, str_, dec_;
    size_t spkW_ = 0, strW_ = 0; int decW_ = 0, decPhase_ = 0, hop_ = 0;
    LP2 aa1_, aa2_, body_;
    float lastS_ = 0.0f, winPk_ = 0.0f, totPk_ = 0.0f, floorDb_ = -55.0f; int winCnt_ = 0; bool periodic_ = false, prevPeriodic_ = false;
    float loss_ = 0.0f, envIn_ = 0.0f, envSpk_ = 0.0f, gEst_ = 100.0f;
    double period_ = 0.0, periodTarget_ = 0.0, glide_ = 0.002;
    float pickAmp_ = 0;
    bool  muted_ = true; float alive_ = 0.0f, aliveTarget_ = 0.0f;
    float prevEnvDb_ = -200.0f, attackDb_ = -200.0f;
};
