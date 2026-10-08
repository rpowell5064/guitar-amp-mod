#include "ClawNoise.h"
#include <algorithm>

namespace {
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kCeiling = 0.8913f;              // -1.0 dBFS

inline float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float onePoleCoef(float hz, float fs) noexcept { return 1.0f - std::exp(-kTwoPi * hz / fs); }
inline float timeCoef(float seconds, float fs) noexcept { return 1.0f - std::exp(-1.0f / (seconds * fs)); }
}

void ClawNoise::resetChannel(Ch& c, int idx) noexcept {
    // Everything but the loop buffer's allocation is re-initialised here; the seed is
    // per channel so a stereo host never gets correlated noise on the two sides.
    c.rng.reseed(0xC1A70000u + 0x9E3779B9u * static_cast<uint32_t>(idx + 1));
    c.env = c.envSlow = 0.0f; c.refr = 0;
    c.burst = c.burstTarget = 0.0f; c.burstRising = false;
    c.pb0 = c.pb1 = c.pb2 = 0.0f;
    c.hissBp.clear(); c.hissClip.reset();
    c.loop.clear(); c.howlBp.clear(); c.loopClip.reset(); c.loopOut = 0.0f;
    c.lx = c.ly = c.lz = 1.0;
    c.ringClip.reset();
    for (auto& a : c.apI) a = AP2{};
    for (auto& a : c.apQ) a = AP2{};
    c.hilbZ1 = c.carPh = c.walk = c.noiseLp = c.noiseHeld = 0.0f;
    c.swClip.reset();
    c.blendCur = blend_; c.gainCur = 2.0f * level_ * level_;
    c.dc.reset(); c.ceil.reset();
}

void ClawNoise::prepare(double sr, int maxBlock, int nch) {
    sampleRate = sr; maxBlockSize = maxBlock; numChannels = nch;
    fs_ = static_cast<float>(sr);
    envA_  = timeCoef(0.002f, fs_);                 // peak follower attack 2 ms
    envR_  = timeCoef(0.080f, fs_);                 // release 80 ms
    envS_  = timeCoef(0.150f, fs_);                 // slow follower 150 ms
    refrN_ = static_cast<int>(0.040f * fs_);        // 40 ms attack refractory
    lpNoiseA_ = onePoleCoef(30.0f, fs_);            // Shortwave AM noise band
    walkA_    = onePoleCoef(0.3f, fs_);             // Shortwave carrier wander
    const int loopMax = static_cast<int>(fs_ / 120.0f) + 8;   // Howl: 120 Hz lowest loop pitch
    for (int i = 0; i < 2; ++i) {
        ch_[i].loop.resize(loopMax);
        ch_[i].dc.setCoeffs(Filters::highpass1pole(8.0f, fs_));
        ch_[i].hissClip.set(0.60f, 0.95f);
        ch_[i].loopClip.set(0.70f, 0.95f);
        ch_[i].ringClip.set(0.60f, 0.95f);
        ch_[i].swClip.set(0.65f, 0.95f);
        ch_[i].ceil.set(0.70f, kCeiling);
        resetChannel(ch_[i], i);
    }
    modeApplied_ = mode_;
}

void ClawNoise::clearTails() noexcept {
    for (int i = 0; i < 2; ++i) resetChannel(ch_[i], i);
}

void ClawNoise::setParameter(const std::string& id, float value) {
    const float v = clamp01(value);
    if      (id == "mode")  mode_  = std::max(0, std::min(3, static_cast<int>(std::lround(value))));
    else if (id == "feed")  feed_  = v;
    else if (id == "pitch") pitch_ = v;
    else if (id == "grit")  grit_  = v;
    else if (id == "blend") blend_ = v;
    else if (id == "level") level_ = v;
    else if (id == "grab")  grab_  = value > 0.5f;
}

float ClawNoise::getParameter(const std::string& id) const {
    if (id == "mode")  return static_cast<float>(mode_);
    if (id == "feed")  return feed_;
    if (id == "pitch") return pitch_;
    if (id == "grit")  return grit_;
    if (id == "blend") return blend_;
    if (id == "level") return level_;
    if (id == "grab")  return grab_ ? 1.0f : 0.0f;
    return 0.0f;
}

void ClawNoise::process(float** in, float** out, int n, int nch) {
    if (bypassed) { copyBlock(in, out, n, nch); return; }
    const int chs = std::min(nch, 2);
    for (int c = chs; c < nch; ++c)
        if (in[c] != out[c]) for (int i = 0; i < n; ++i) out[c][i] = in[c][i];

    // A mode change drops the old mode's memory so a loop or an attractor never
    // leaks into the new one (also what makes mode flips reproducible).
    if (mode_ != modeApplied_) { modeApplied_ = mode_; for (int i = 0; i < 2; ++i) resetChannel(ch_[i], i); }

    // ── block-rate coefficients ──
    const float gainTarget = 2.0f * level_ * level_;          // unity at 0.707
    // Hiss
    const float hissFc   = std::min(200.0f * std::pow(2.0f, 5.3f * pitch_), fs_ * 0.45f);
    const float hissG    = std::tan(kPi * hissFc / fs_);
    const float hissK    = 0.5f - 0.35f * grit_;
    const float hissDrive = 1.0f + 6.0f * grit_;
    const float burstDec = std::exp(-1.0f / ((0.06f + 0.54f * feed_) * fs_));
    const float burstAtkStep = 1.0f / (0.001f * fs_);
    const float hissFloor = feed_ > 0.7f ? (feed_ - 0.7f) / 0.3f * 0.25f : 0.0f;
    // Howl
    const float howlFc   = std::min(120.0f * std::pow(2.0f, 5.0f * pitch_), fs_ * 0.45f);
    const float howlG    = std::tan(kPi * howlFc / fs_);
    const float howlK    = 0.6f - 0.4f * grit_;
    const float loopGain = std::min(1.45f, feed_ / 0.70f);      // crosses 1.0 at Feed 0.70
    const float loopDrive = 1.0f + 4.0f * grit_;
    const float loopDriveInv = 1.0f / loopDrive;
    const float loopLenBase = fs_ / howlFc;
    const float loopLenMax  = static_cast<float>(ch_[0].loop.buf.size()) - 3.0f;
    // Butterfly
    const double rho = 18.0 + 22.0 * pitch_;
    const double dt  = 0.003 + 0.017 * pitch_;
    const double inj = 0.8 * feed_;
    const float ringDrive = 1.0f + 5.0f * grit_;
    const float rawMix = 0.15f * grit_;
    // Shortwave
    const float carHz   = 40.0f * std::pow(2.0f, 7.0f * pitch_);
    const float carInc  = kTwoPi * carHz / fs_;
    const float leak    = 0.25f * feed_ * feed_;
    const float amDepth = 0.6f * grit_;
    const uint32_t crackleThresh = static_cast<uint32_t>(grit_ * grit_ * 1.0e-4f * 4294967296.0f);
    const float swDrive = 1.0f + 3.0f * grit_;

    for (int c = 0; c < chs; ++c) {
        Ch& s = ch_[c];
        for (int i = 0; i < n; ++i) {
            const float x = in[c][i];

            // ── shared front end: fast + slow envelopes, pick-attack detector ──
            const float r = std::fabs(x);
            s.env     += (r > s.env ? envA_ : envR_) * (r - s.env);
            s.envSlow += envS_ * (r - s.envSlow);
            bool attack = false;
            if (s.refr > 0) --s.refr;
            else if (s.env > 1.8f * s.envSlow + 0.01f) { attack = true; s.refr = refrN_; }

            float wet = 0.0f;
            switch (mode_) {
            case 0: {   // ── Hiss ──
                if (attack && !grab_) { s.burstTarget = std::min(1.0f, 0.5f + 2.0f * s.env); s.burstRising = true; }
                if (s.burstRising) {
                    s.burst += burstAtkStep;
                    if (s.burst >= s.burstTarget) { s.burst = s.burstTarget; s.burstRising = false; }
                } else if (!grab_) s.burst *= burstDec;
                const float w = s.rng.white();
                s.pb0 = 0.99765f * s.pb0 + w * 0.0990460f;        // Kellett economy pink
                s.pb1 = 0.96300f * s.pb1 + w * 0.2965164f;
                s.pb2 = 0.57000f * s.pb2 + w * 1.0526913f;
                const float pink = (s.pb0 + s.pb1 + s.pb2 + w * 0.1848f) * 0.11f;
                const float noise = pink * (1.0f - grit_) + w * grit_;
                const float v = noise * (s.burst + hissFloor);
                const float bp = s.hissBp.bp(v, hissG, hissK) * 2.0f;
                wet = s.hissClip.process(bp * hissDrive);
            } break;
            case 1: {   // ── Howl ──
                float lenT = loopLenBase * (1.0f + 0.06f * (s.env - s.envSlow));
                lenT = std::max(2.0f, std::min(loopLenMax, lenT));
                s.lenCur += 0.002f * (lenT - s.lenCur);
                const float lo = s.loop.readFrac(s.lenCur);
                if (grab_) {
                    s.loop.advance();                           // frozen content keeps cycling
                    wet = lo * 2.0f;
                } else {
                    const float u  = 0.6f * x + loopGain * lo + 1.0e-18f * s.rng.white();
                    const float bp = s.howlBp.bp(u, howlG, howlK);               // unity peak: the loop gain IS loopGain
                    const float w  = s.loopClip.process(bp * loopDrive) * loopDriveInv;   // the clipper shapes, it does not add gain
                    s.loop.write(w);
                    wet = w * 2.0f;                             // post-loop makeup (outside the loop: no gain change inside it)
                }
            } break;
            case 2: {   // ── Butterfly ── Lorenz, midpoint integration, guitar injected into x
                if (!grab_) {
                    const double k1x = 10.0 * (s.ly - s.lx);
                    const double k1y = s.lx * (rho - s.lz) - s.ly;
                    const double k1z = s.lx * s.ly - (8.0 / 3.0) * s.lz;
                    const double mx = s.lx + 0.5 * dt * k1x, my = s.ly + 0.5 * dt * k1y, mz = s.lz + 0.5 * dt * k1z;
                    s.lx += dt * (10.0 * (my - mx)) + inj * x;
                    s.ly += dt * (mx * (rho - mz) - my);
                    s.lz += dt * (mx * my - (8.0 / 3.0) * mz);
                } else {
                    const double k1x = 10.0 * (s.ly - s.lx);
                    const double k1y = s.lx * (rho - s.lz) - s.ly;
                    const double k1z = s.lx * s.ly - (8.0 / 3.0) * s.lz;
                    const double mx = s.lx + 0.5 * dt * k1x, my = s.ly + 0.5 * dt * k1y, mz = s.lz + 0.5 * dt * k1z;
                    s.lx += dt * (10.0 * (my - mx));
                    s.ly += dt * (mx * (rho - mz) - my);
                    s.lz += dt * (mx * my - (8.0 / 3.0) * mz);
                }
                if (!(std::isfinite(s.lx) && std::isfinite(s.ly) && std::isfinite(s.lz))) { s.lx = s.ly = s.lz = 1.0; }
                s.lx = std::max(-60.0, std::min(60.0, s.lx));
                s.ly = std::max(-60.0, std::min(60.0, s.ly));
                s.lz = std::max(0.0,   std::min(100.0, s.lz));
                const float osc = static_cast<float>(std::max(-1.0, std::min(1.0, s.lx / 20.0)));
                wet = s.ringClip.process((x * osc + rawMix * osc) * ringDrive);
            } break;
            default: {  // ── Shortwave ── SSB heterodyne + carrier leak + AM static + crackle
                const float I = hilbert(s.apI, kApI, s.hilbZ1);
                const float Q = hilbert(s.apQ, kApQ, x);
                s.hilbZ1 = x;
                const float w = s.rng.white();
                if (!grab_) {
                    s.walk    += walkA_ * (w * 60.0f - s.walk);          // sigma ~0.15 of the carrier
                    s.noiseLp += lpNoiseA_ * (w * 38.0f - s.noiseLp);    // ~unit-variance 30 Hz noise
                    s.noiseHeld = s.noiseLp;
                }
                const float fMul = 1.0f + feed_ * std::max(-0.6f, std::min(0.6f, s.walk));
                s.carPh += carInc * fMul;
                if (s.carPh > kPi) s.carPh -= kTwoPi;
                const float cw = std::cos(s.carPh), sw = std::sin(s.carPh);
                const float shifted = I * cw + Q * sw;
                const float nl = std::max(-1.0f, std::min(1.0f, s.noiseHeld));
                const float am = 1.0f - amDepth * (0.5f + 0.5f * nl);
                float v = (shifted + leak * sw) * am;
                if (s.rng.next() < crackleThresh) v += 0.5f;
                wet = s.swClip.process(v * swDrive);
            } break;
            }

            // ── output stage: blend, level, DC block, -1 dBFS ADAA ceiling, finite guard ──
            s.blendCur += 0.002f * (blend_ - s.blendCur);
            s.gainCur  += 0.002f * (gainTarget - s.gainCur);
            float y = ((1.0f - s.blendCur) * x + s.blendCur * wet) * s.gainCur;
            y = s.dc.process(y);
            y = s.ceil.process(y);
            if (!std::isfinite(y)) { resetChannel(s, c); y = 0.0f; }
            out[c][i] = y;
        }
        // once per block: a blown state anywhere resets the channel (DenormalGuard is not
        // available on every host, so the block polices its own memory)
        if (!(std::isfinite(s.env) && std::isfinite(s.loopOut) && std::isfinite(s.hissBp.ic1) &&
              std::isfinite(s.howlBp.ic1) && std::isfinite(s.walk) && std::isfinite(s.noiseLp)))
            resetChannel(s, c);
    }
}
