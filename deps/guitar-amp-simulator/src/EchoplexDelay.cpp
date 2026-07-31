#include "EchoplexDelay.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void EchoplexDelay::prepare(double sr, int maxBlock, int numCh) {
    sampleRate_   = sr;
    maxBlockSize_ = maxBlock;
    numChannels_  = numCh;

    const int bufLen = static_cast<int>(sr * kMaxDelayMs / 1000.0) + 8;
    for (int c = 0; c < kMaxCh; ++c) {
        ch_[c].buf.assign(static_cast<size_t>(bufLen), 0.0f);
        ch_[c].writeIdx  = 0;
        ch_[c].ageLPz    = 0.0f;
        ch_[c].wowFreqHz = 0.15f + 0.05f * static_cast<float>(c);   // decorrelated start
        ch_[c].wowPhase  = static_cast<float>(c) * 1.5707963f;      // π/2 stagger
        ch_[c].flutWalk.prepare(12.0f, static_cast<float>(sr), 0x0EC30u + 0x3000u * c);
        ch_[c].flutHPz   = 0.0f;
        ch_[c].seed      = 0x9E3779B9u + 0x9000u * c;               // fixed: deterministic
    }
    flutHPa_ = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * 6.0f
                               / static_cast<float>(sr));

    timeSmoother_.prepare(static_cast<float>(sr), 250.0f);   // transport inertia
    feedbackSmoother_.prepare(static_cast<float>(sr), 5.0f);
    mixSmoother_.prepare(static_cast<float>(sr), 5.0f);
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(feedback_);
    mixSmoother_.setImmediate(mix_);

    // Record-path JFET at base rate: 3rd-order poly at 48 k aliases below the
    // repro LP; the drive slot's oversampled instance is the hi-fi one.
    preamp_.prepare(sr, maxBlock);
    preamp_.setParameter("tone", 0.5f);      // stock 4.2 kHz cable
    applyPregain();                          // drive + level-compensated trim

    rebuildAge();
    reset();
}

void EchoplexDelay::reset() noexcept {
    for (auto& c : ch_) {
        std::fill(c.buf.begin(), c.buf.end(), 0.0f);
        c.writeIdx = 0;
        c.ageLPz   = 0.0f;
        c.wowPhase = 0.0f;
        c.flutWalk.setImmediate(0.0f);
        c.flutHPz  = 0.0f;
    }
    preamp_.reset();
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(feedback_);
    mixSmoother_.setImmediate(mix_);
}

void EchoplexDelay::advanceSmoothing() noexcept {
    timeSmoother_.tick(timeMs_);
    feedbackSmoother_.tick(feedback_);
    mixSmoother_.tick(mix_);
    preamp_.advanceSmoothing();
}

float EchoplexDelay::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];
    const int   bufLen = static_cast<int>(s.buf.size());
    const float fs     = static_cast<float>(sampleRate_);
    const float twoPi  = 2.0f * static_cast<float>(M_PI);

    // ── Record head hears the JFET; the dry path never does ────────────────
    const float pre = preamp_.processSample(x, ch);

    // ── Transport: glided time + worn-transport wobble ─────────────────────
    float delaySamples = timeSmoother_.current() * fs / 1000.0f;

    // Wow: sine whose RATE random-walks in [0.1, 0.3] Hz — bounded ±1 by
    // construction, ±2e-5 Hz/sample walk step.
    s.wowFreqHz = std::clamp(s.wowFreqHz + s.lcg() * 2.0e-5f, 0.1f, 0.3f);
    s.wowPhase += twoPi * s.wowFreqHz / fs;
    if (s.wowPhase > twoPi) s.wowPhase -= twoPi;
    const float wow = std::sin(s.wowPhase);

    // Flutter: 12 Hz random-walk noise minus a 6 Hz 1-pole = 6–12 Hz band
    // (σ ≈ 1 from RandomWalk's variance normalizer), hard-bounded ±1.
    const float fl = s.flutWalk.next();
    s.flutHPz += flutHPa_ * (fl - s.flutHPz);
    const float flutter = std::clamp(fl - s.flutHPz, -1.0f, 1.0f);

    // Worn transport wobbles more: wear = 0.35 (serviced) … 1.0 (thrashed).
    const float wear = 0.35f + 0.65f * age_;
    delaySamples += delaySamples * wear * (wow * 0.0035f + flutter * 0.0008f);
    delaySamples  = std::clamp(delaySamples, 1.0f, static_cast<float>(bufLen - 3));

    // ── Playback head (Hermite) through the aging repro chain ──────────────
    // The LP sits BEFORE both output and feedback: first repeat is one pass
    // dark, repeat N is N passes — cumulative like real oxide.
    const float raw = readFracHermite(s.buf, delaySamples, s.writeIdx);
    s.ageLPz = ageLPCoeff_ * s.ageLPz + (1.0f - ageLPCoeff_) * raw;
    const float wet = s.ageLPz;

    // ── Feedback: oxide saturation, unity-normalized ───────────────────────
    // tanh(2.5·u)·0.4 → small-signal loop gain exactly fb (< 1, stable);
    // fb 0.95 self-oscillates without runaway.
    const float fbSig = std::tanh(2.5f * (wet * feedbackSmoother_.current())) * 0.4f;

    // ── Record head: JFET'd input + loop + circulating hiss ────────────────
    s.buf[s.writeIdx] = pre + fbSig + noiseLin_ * s.lcg();
    s.writeIdx = (s.writeIdx + 1) % bufLen;

    return x + mixSmoother_.current() * wet;   // dry unity + wet on top (house rule)
}

void EchoplexDelay::setParameter(const std::string& id, float v) noexcept {
    if      (id == "timeMs")   timeMs_   = std::clamp(v, 65.0f, kMaxDelayMs);
    else if (id == "feedback") feedback_ = std::clamp(v, 0.0f, 0.95f);
    else if (id == "mix")      mix_      = std::clamp(v, 0.0f, 1.0f);
    else if (id == "age")    { age_      = std::clamp(v, 0.0f, 1.0f); rebuildAge(); }
    else if (id == "pregain"){ pregain_  = std::clamp(v, 0.0f, 1.0f); applyPregain(); }
}

float EchoplexDelay::getParameter(const std::string& id) const noexcept {
    if (id == "timeMs")   return timeMs_;
    if (id == "feedback") return feedback_;
    if (id == "mix")      return mix_;
    if (id == "age")      return age_;
    if (id == "pregain")  return pregain_;
    return 0.0f;
}

// Pregain adds COLOR, not level: the trim inverts the JFET's linear gain so
// more pregain means a harder knee into the tape at the SAME small-signal
// level (level-invariant pattern -- repeats never jump vs the dry note).
void EchoplexDelay::applyPregain() noexcept {
    preamp_.setParameter("drive", pregain_);
    const float gLin = std::pow(10.0f, pregain_ * 11.0f * (1.0f / 20.0f));
    preamp_.setParameter("level", 0.5f / gLin);   // level 0.5 = x1 -> net unity
}

void EchoplexDelay::rebuildAge() noexcept {
    if (sampleRate_ <= 0.0) return;
    const float fs = static_cast<float>(sampleRate_);
    // Repro LP: 6 kHz (serviced) → 2.5 kHz (thrashed).
    const float fc = 6000.0f - 3500.0f * age_;
    ageLPCoeff_ = std::exp(-2.0f * static_cast<float>(M_PI) * fc / fs);
    // Hiss: −90 dB → −60 dB.
    noiseLin_ = std::pow(10.0f, (-90.0f + 30.0f * age_) * (1.0f / 20.0f));
}
