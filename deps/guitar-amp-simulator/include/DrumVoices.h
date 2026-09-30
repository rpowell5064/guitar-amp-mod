#pragma once
// Synthesised drum voices for the Practice plugin's drum machine.
//
// Everything here is generated, not sampled: the plugin has to work on a
// headless pi-Stomp with no sample library installed, and a synth kit costs a
// few kB of code instead of tens of MB of WAVs. The trade is realism, so each
// voice models the mechanism rather than EQ-ing noise into shape:
//
//   kick        pitched body sweep + beater click
//   snare/toms  modal shell (damped sinusoid partials) + shaped noise
//   hats/rides  six-oscillator metallic bank (the 808 ratios) + noise wash
//
// Cymbals are the acknowledged weak spot of any synth kit — the bank below is
// convincing for hats and passable for ride/crash, and is the place to swap in
// small samples later if it doesn't hold up in a dense mix.
//
// All voices are monophonic and additive: render() ADDS into the buffer so the
// sequencer can mix the whole kit in one pass with no per-voice scratch space.
#include "BiquadFilter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hexdrums {

// ── Utilities ────────────────────────────────────────────────────────────────

// xorshift32. Deterministic per voice, so a rendered pattern is reproducible
// (the offline harness diffs renders); seeded distinctly per voice so two
// noise-based instruments landing on the same step don't correlate into a spike.
class Rng {
public:
    explicit Rng(uint32_t seed = 0x9E3779B9u) noexcept : s(seed ? seed : 1u) {}
    void reseed(uint32_t seed) noexcept { s = seed ? seed : 1u; }
    uint32_t next() noexcept {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
    }
    // Uniform white noise in [-1, 1).
    float white() noexcept {
        return static_cast<float>(static_cast<int32_t>(next())) * (1.0f / 2147483648.0f);
    }
private:
    uint32_t s;
};

// Percussive AR envelope. The attack is a short linear ramp rather than an
// instantaneous jump: a hard step is a broadband click that survives every
// downstream filter and makes the whole kit sound cheap.
struct ExpEnv {
    float target{0.0f}, level{0.0f}, coef{0.999f}, attackInc{1.0f};
    bool  attacking{false};

    void configure(float attackMs, float decayT60s, double fs) noexcept {
        const float a = std::max(1.0f, attackMs * 1.0e-3f * static_cast<float>(fs));
        attackInc = 1.0f / a;
        // -60 dB over decayT60 seconds: ln(1000) = 6.907755.
        coef = std::exp(-6.907755f / std::max(1.0e-4f, decayT60s * static_cast<float>(fs)));
    }
    void trigger(float amp) noexcept { target = amp; level = 0.0f; attacking = true; }
    void reset()            noexcept { target = level = 0.0f; attacking = false; }
    // Fade what is already sounding instead of cutting it (choke groups).
    void choke(float ms, double fs) noexcept {
        attacking = false;
        coef = std::exp(-6.907755f / std::max(1.0f, ms * 1.0e-3f * static_cast<float>(fs)));
    }
    float tick() noexcept {
        if (attacking) {
            level += attackInc * target;
            if (level >= target) { level = target; attacking = false; }
        } else {
            level *= coef;
        }
        return level;
    }
    bool done() const noexcept { return !attacking && level < 1.0e-5f; }
};

// One mode of a struck shell: a damped sinusoid produced by a complex rotator
// (4 mults/sample, unconditionally stable, no per-sample transcendentals).
// Summing a handful of these IS the modal synthesis — a drum shell rings at a
// set of inharmonic frequencies, each with its own decay rate, and that spread
// of decay rates is what a single filtered noise burst can never reproduce.
struct ModalPartial {
    double re{0.0}, im{0.0}, cr{1.0}, ci{0.0};

    void set(double freq, double t60, double fs) noexcept {
        const double w = 2.0 * M_PI * freq / fs;
        const double r = std::exp(-6.907755 / std::max(1.0e-4, t60 * fs));
        cr = r * std::cos(w);
        ci = r * std::sin(w);
    }
    void trigger(double amp) noexcept { re = amp; im = 0.0; }
    void reset()             noexcept { re = im = 0.0; }
    // Rotating (re, im) by (cr, ci) and taking the imaginary part gives a sine
    // that starts at zero — no DC step at the onset.
    float tick() noexcept {
        const double nr = re * cr - im * ci;
        const double ni = re * ci + im * cr;
        re = nr; im = ni;
        return static_cast<float>(ni);
    }
};

// Soft saturation. Used to glue a voice's transient to its body the way a close
// mic and preamp do, rather than to add obvious distortion.
inline float softClip(float x) noexcept { return std::tanh(x); }

// ── Kick ─────────────────────────────────────────────────────────────────────
// A real kick is a pitched membrane whose tension makes the fundamental fall
// steeply over the first few tens of milliseconds, plus the beater's contact
// click. Keeping the sweep and the click on SEPARATE envelopes is what makes it
// cut through a dense mix: the click carries the attack past the guitars, the
// body carries the weight under them.
class KickVoice {
public:
    void prepare(double fs_) noexcept {
        fs = fs_;
        clickHp.setCoeffs(Filters::highpass(1400.0, 0.7, fs));
        rng.reseed(0x01234567u);
        reset();
    }
    void reset() noexcept {
        amp.reset(); clickEnv.reset(); phase = 0.0; pitchEnv = 0.0f; clickHp.reset();
    }
    void setTuning(float semitones) noexcept { tune = std::pow(2.0f, semitones / 12.0f); }
    void setDecay(float seconds)    noexcept { decaySec = std::max(0.05f, seconds); }
    void setClick(float amount)     noexcept { clickAmt = std::clamp(amount, 0.0f, 1.0f); }
    void setDrive(float amount)     noexcept { drive = 1.0f + 3.0f * std::clamp(amount, 0.0f, 1.0f); }

    void trigger(float vel) noexcept {
        amp.configure(1.5f, decaySec, fs);
        amp.trigger(vel);
        // Harder hits click harder AND sweep from a higher pitch, because a
        // heavier strike stretches the head further before it settles.
        clickEnv.configure(0.15f, 0.005f + 0.003f * vel, fs);
        clickEnv.trigger(vel * vel);
        pitchEnv  = 0.7f + 0.3f * vel;
        pitchCoef = std::exp(-1.0f / (0.028f * static_cast<float>(fs)));
        phase     = 0.0;
    }

    bool active() const noexcept { return !amp.done(); }

    void render(float* out, int n, float gain) noexcept {
        if (!active()) return;
        const double twoPiOverFs = 2.0 * M_PI / fs;
        for (int i = 0; i < n; ++i) {
            pitchEnv *= pitchCoef;
            const double f = (kFreqEnd + (kFreqStart - kFreqEnd) * pitchEnv) * tune;
            phase += twoPiOverFs * f;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;

            const float body  = static_cast<float>(std::sin(phase)) * amp.tick();
            const float click = clickHp.process(rng.white()) * clickEnv.tick() * clickAmt * 0.6f;
            // The body is driven, the click is not: saturating the click would
            // dull the very transient it exists to provide.
            out[i] += (softClip(body * drive) / drive * 1.4f + click) * gain;
        }
    }

private:
    static constexpr double kFreqStart = 112.0;   // head tension at impact
    static constexpr double kFreqEnd   = 47.0;    // settled fundamental

    double fs{48000.0}, phase{0.0};
    float  tune{1.0f}, decaySec{0.42f}, clickAmt{0.5f}, drive{2.0f};
    float  pitchEnv{0.0f}, pitchCoef{0.999f};
    ExpEnv amp, clickEnv;
    BiquadFilter clickHp;
    Rng rng{0x01234567u};
};

// ── Snare ────────────────────────────────────────────────────────────────────
// Two mechanisms in one instrument, and they must stay separable: the shell
// (tuned modes, pitched, decays smoothly) and the wire snares underneath
// (broadband noise, rattles longer, and is most of what you hear in a mix).
// The Snappy control crossfades them, which is the physical control a drummer
// actually has (snare tension) rather than an EQ tilt.
class SnareVoice {
public:
    void prepare(double fs_) noexcept {
        fs = fs_;
        noiseBp.setCoeffs(Filters::bandpass(2200.0, 0.55, fs));
        noiseHp.setCoeffs(Filters::highpass(420.0, 0.7, fs));
        rng.reseed(0x2BADF00Du);
        retune();
        reset();
    }
    void reset() noexcept {
        shellEnv.reset(); wireEnv.reset(); spikeEnv.reset();
        for (auto& m : modes) m.reset();
        noiseBp.reset(); noiseHp.reset();
    }
    void setTuning(float semitones) noexcept { tune = std::pow(2.0f, semitones / 12.0f); retune(); }
    void setDecay(float seconds)    noexcept { decaySec = std::max(0.04f, seconds); retune(); }
    void setSnappy(float amount)    noexcept { snappy = std::clamp(amount, 0.0f, 1.0f); }

    void trigger(float vel) noexcept {
        shellEnv.configure(0.5f, decaySec * 0.75f, fs);
        shellEnv.trigger(vel);
        wireEnv.configure(0.3f, decaySec, fs);
        wireEnv.trigger(vel);
        // The wires' initial burst is far louder and far shorter than their
        // rattle; one envelope cannot do both, so the spike rides on top.
        spikeEnv.configure(0.1f, 0.012f, fs);
        spikeEnv.trigger(vel * vel);
        // Partials are struck with alternating sign so they don't all add
        // constructively at t=0 into an artificial peak.
        modes[0].trigger(vel *  0.85);
        modes[1].trigger(vel * -0.45);
        modes[2].trigger(vel *  0.28);
    }

    bool active() const noexcept { return !(shellEnv.done() && wireEnv.done()); }

    void render(float* out, int n, float gain) noexcept {
        if (!active()) return;
        const float shellMix = 1.0f - 0.65f * snappy;
        const float wireMix  = 0.35f + 0.85f * snappy;
        for (int i = 0; i < n; ++i) {
            const float shell = (modes[0].tick() + modes[1].tick() + modes[2].tick())
                              * shellEnv.tick();
            const float wires = noiseHp.process(noiseBp.process(rng.white()))
                              * (wireEnv.tick() + spikeEnv.tick() * 1.8f);
            out[i] += (shell * shellMix * 0.8f + wires * wireMix) * gain;
        }
    }

private:
    void retune() noexcept {
        // Batter-head modes of a 14-inch wood snare. Inharmonic on purpose: the
        // integer-ratio version reads as a tom, not a snare.
        modes[0].set(185.0 * tune, decaySec * 0.70, fs);
        modes[1].set(331.0 * tune, decaySec * 0.45, fs);
        modes[2].set(492.0 * tune, decaySec * 0.28, fs);
    }

    double fs{48000.0};
    float  tune{1.0f}, decaySec{0.20f}, snappy{0.6f};
    ModalPartial modes[3];
    ExpEnv shellEnv, wireEnv, spikeEnv;
    BiquadFilter noiseBp, noiseHp;
    Rng rng{0x2BADF00Du};
};

// Sidestick / rim click: almost all shell and stick, no wires, very short.
// Cheap to model as its own voice and it keeps the snare's Snappy control from
// having to cover a completely different articulation.
class RimVoice {
public:
    void prepare(double fs_) noexcept {
        fs = fs_;
        rng.reseed(0x5EED0011u);
        bp.setCoeffs(Filters::bandpass(1700.0, 1.6, fs));
        mode.set(810.0, 0.055, fs);
        reset();
    }
    void reset() noexcept { env.reset(); clickEnv.reset(); mode.reset(); bp.reset(); }

    void trigger(float vel) noexcept {
        env.configure(0.2f, 0.07f, fs);
        env.trigger(vel);
        clickEnv.configure(0.1f, 0.004f, fs);
        clickEnv.trigger(vel);
        mode.trigger(vel * 0.6);
    }
    bool active() const noexcept { return !env.done(); }

    void render(float* out, int n, float gain) noexcept {
        if (!active()) return;
        for (int i = 0; i < n; ++i) {
            const float woody = mode.tick() * env.tick();
            const float stick = bp.process(rng.white()) * clickEnv.tick();
            out[i] += (woody * 0.8f + stick * 0.9f) * gain;
        }
    }

private:
    double fs{48000.0};
    ModalPartial mode;
    ExpEnv env, clickEnv;
    BiquadFilter bp;
    Rng rng{0x5EED0011u};
};

// ── Tom ──────────────────────────────────────────────────────────────────────
// The snare's shell model with the wires removed and a slight pitch drop on the
// fundamental, which is what gives toms their downward "boiing" under a hard hit.
class TomVoice {
public:
    void prepare(double fs_, double baseFreq, uint32_t seed) noexcept {
        fs = fs_; base = baseFreq;
        rng.reseed(seed);
        headHp.setCoeffs(Filters::highpass(200.0, 0.7, fs));
        retune();
        reset();
    }
    void reset() noexcept {
        env.reset(); attackEnv.reset();
        for (auto& m : modes) m.reset();
        headHp.reset(); phase = 0.0; pitchEnv = 0.0f;
    }
    void setTuning(float semitones) noexcept { tune = std::pow(2.0f, semitones / 12.0f); retune(); }
    void setDecay(float seconds)    noexcept { decaySec = std::max(0.05f, seconds); retune(); }

    void trigger(float vel) noexcept {
        env.configure(0.8f, decaySec, fs);
        env.trigger(vel);
        attackEnv.configure(0.1f, 0.004f, fs);
        attackEnv.trigger(vel * vel);
        modes[0].trigger(vel *  0.9);
        modes[1].trigger(vel * -0.35);
        modes[2].trigger(vel *  0.18);
        pitchEnv  = 0.10f * vel;
        pitchCoef = std::exp(-1.0f / (0.045f * static_cast<float>(fs)));
        phase     = 0.0;
    }

    bool active() const noexcept { return !env.done(); }

    void render(float* out, int n, float gain) noexcept {
        if (!active()) return;
        const double twoPiOverFs = 2.0 * M_PI / fs;
        for (int i = 0; i < n; ++i) {
            // The swept fundamental sits alongside the fixed modes: the modes
            // supply the shell's character, the sweep supplies the gesture.
            pitchEnv *= pitchCoef;
            phase += twoPiOverFs * base * tune * (1.0 + pitchEnv);
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;

            const float e     = env.tick();
            const float body  = static_cast<float>(std::sin(phase)) * e * 0.7f;
            const float ring  = (modes[0].tick() + modes[1].tick() + modes[2].tick()) * e * 0.5f;
            const float stick = headHp.process(rng.white()) * attackEnv.tick() * 0.35f;
            out[i] += (body + ring + stick) * gain;
        }
    }

private:
    void retune() noexcept {
        // Circular-membrane mode ratios (Bessel zeros), damped by the shell.
        modes[0].set(base * tune * 1.00, decaySec * 0.80, fs);
        modes[1].set(base * tune * 1.59, decaySec * 0.40, fs);
        modes[2].set(base * tune * 2.14, decaySec * 0.22, fs);
    }

    double fs{48000.0}, base{160.0}, phase{0.0};
    float  tune{1.0f}, decaySec{0.45f}, pitchEnv{0.0f}, pitchCoef{0.999f};
    ModalPartial modes[3];
    ExpEnv env, attackEnv;
    BiquadFilter headHp;
    Rng rng{0x51EED001u};
};

// ── Metallic bank (hats, ride, crash) ────────────────────────────────────────
// Six detuned square oscillators at the classic 808 ratios. Squares rather than
// sines because their odd harmonics multiply into the dense, clangorous
// spectrum a struck cymbal has; six is the point where the result stops sounding
// like discrete pitches and starts sounding like metal.
class MetallicBank {
public:
    void prepare(double fs_, double baseFreq, uint32_t seed) noexcept {
        fs = fs_; base = baseFreq;
        rng.reseed(seed);
        reset();
    }
    void reset() noexcept { for (auto& p : phase) p = 0.0; }
    void setBase(double f) noexcept { base = f; }

    // Advance all six oscillators one sample and return their sum in [-1, 1].
    float tick() noexcept {
        static constexpr double kRatios[6] = { 1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637 };
        float sum = 0.0f;
        for (int k = 0; k < 6; ++k) {
            phase[k] += base * kRatios[k] / fs;
            if (phase[k] >= 1.0) phase[k] -= 1.0;
            sum += (phase[k] < 0.5) ? 1.0f : -1.0f;
        }
        return sum * (1.0f / 6.0f);
    }
    float noise() noexcept { return rng.white(); }

private:
    double fs{48000.0}, base{205.3}, phase[6]{};
    Rng rng{0x00C0FFEEu};
};

// Hi-hat: the metallic bank through a steep high-pass, with closed/pedal/open
// sharing ONE voice so an open hat is genuinely cut off by the next closed hat
// (a choke, not a crossfade — two hi-hats cannot ring at once on one stand).
class HiHatVoice {
public:
    void prepare(double fs_) noexcept {
        fs = fs_;
        bank.prepare(fs, 205.3, 0x0A75EED1u);
        hp.setCoeffs(Filters::highpass(7200.0, 0.8, fs));
        bp.setCoeffs(Filters::bandpass(9500.0, 0.5, fs));
        reset();
    }
    void reset() noexcept { env.reset(); bank.reset(); hp.reset(); bp.reset(); isOpen = false; }
    void setDecay(float openSeconds) noexcept { openSec = std::max(0.05f, openSeconds); }
    void setTone(float amount)       noexcept { tone = std::clamp(amount, 0.0f, 1.0f); }

    // open == false → closed/pedal (short); open == true → open (long, chokeable)
    void trigger(float vel, bool open) noexcept {
        env.configure(0.2f, open ? openSec : 0.045f, fs);
        env.trigger(vel);
        isOpen = open;
    }
    void choke() noexcept { if (isOpen) { env.choke(12.0f, fs); isOpen = false; } }

    bool active() const noexcept { return !env.done(); }

    void render(float* out, int n, float gain) noexcept {
        if (!active()) return;
        for (int i = 0; i < n; ++i) {
            const float metal = bank.tick();
            const float wash  = bank.noise() * 0.25f;
            const float sig   = hp.process(metal + wash) * (1.0f - 0.4f * tone)
                              + bp.process(metal) * (0.6f * tone);
            out[i] += sig * env.tick() * gain * 0.7f;
        }
    }

private:
    double fs{48000.0};
    float  openSec{0.45f}, tone{0.5f};
    bool   isOpen{false};
    MetallicBank bank;
    ExpEnv env;
    BiquadFilter hp, bp;
};

// Ride / crash. Same bank, but the decay is long enough that the spectrum has to
// EVOLVE or it reads as a synth tone held under a fader: the high partials of a
// real cymbal die well before the low ones, so a slowly closing low-pass tracks
// the envelope. The ride additionally gets a discrete "ping" partial, which is
// the stick contact the bank alone cannot produce.
class CymbalVoice {
public:
    void prepare(double fs_, double baseFreq, double pingFreq, uint32_t seed) noexcept {
        fs = fs_; pingHz = pingFreq;
        bank.prepare(fs, baseFreq, seed);
        hp.setCoeffs(Filters::highpass(2600.0, 0.6, fs));
        reset();
    }
    void reset() noexcept {
        env.reset(); pingEnv.reset(); ping.reset(); bank.reset(); hp.reset(); lp.reset();
        lpHz = kLpOpen; tickCount = 0;
        lp.setCoeffs(Filters::lowpass(lpHz, 0.7, fs));
    }
    void setDecay(float seconds) noexcept { decaySec = std::max(0.2f, seconds); }
    void setPing(float amount)   noexcept { pingAmt = std::clamp(amount, 0.0f, 1.0f); }

    void trigger(float vel) noexcept {
        env.configure(0.6f, decaySec, fs);
        env.trigger(vel);
        pingEnv.configure(0.1f, 0.09f, fs);
        pingEnv.trigger(vel * vel);
        ping.set(pingHz, decaySec * 0.25, fs);
        ping.trigger(vel * 0.5);
        lpHz      = kLpOpen;
        tickCount = 0;
        // Sizzle fades over roughly the first third of the hit. Pre-raised to
        // the 32-sample control period so the render loop stays branch-cheap.
        const float perSample = std::exp(-1.0f / std::max(1.0f, 0.33f * decaySec * static_cast<float>(fs)));
        lpCoef32 = std::pow(perSample, 32.0f);
    }
    void choke() noexcept { env.choke(25.0f, fs); }

    bool active() const noexcept { return !env.done(); }

    void render(float* out, int n, float gain) noexcept {
        if (!active()) return;
        for (int i = 0; i < n; ++i) {
            // Re-cook the low-pass every 32 samples: audibly continuous, and it
            // keeps a long crash from costing a biquad design per sample.
            if ((tickCount++ & 31u) == 0u) {
                lpHz = kLpFloor + (lpHz - kLpFloor) * lpCoef32;
                lp.setCoeffs(Filters::lowpass(lpHz, 0.7, fs));
            }
            const float metal = bank.tick();
            const float wash  = bank.noise() * 0.5f;
            float sig = lp.process(hp.process(metal * 0.8f + wash));
            sig += ping.tick() * pingEnv.tick() * pingAmt;
            out[i] += sig * env.tick() * gain * 0.6f;
        }
    }

private:
    static constexpr double kLpOpen  = 16000.0;
    static constexpr double kLpFloor = 3200.0;

    double   fs{48000.0}, pingHz{520.0}, lpHz{kLpOpen};
    float    decaySec{2.0f}, pingAmt{0.0f}, lpCoef32{0.99f};
    uint32_t tickCount{0};
    MetallicBank bank;
    ModalPartial ping;
    ExpEnv env, pingEnv;
    BiquadFilter hp, lp;
};

} // namespace hexdrums
