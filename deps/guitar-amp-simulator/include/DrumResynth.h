#pragma once
// Runtime for the resynthesised drum kit.
//
// Plays back the parametric models produced by build-tools/drum_analyze.cpp:
// tracked partials (frequency and amplitude per frame) plus a noise spectral
// envelope. No sample audio is involved at any point — the kit ships as about
// a megabyte of numbers.
//
// Three things this buys over playing samples, beyond the size:
//   * every hit can differ slightly (partial detune, amplitude jitter, a fresh
//     noise realisation), so there is no machine-gun effect and no need for
//     round-robins;
//   * decay, pitch and brightness stay as continuous controls;
//   * velocity can interpolate rather than step between layers.
//
// CPU notes, because this runs on a pi-Stomp next to an amp model:
//
//   PARTIALS use a unit-magnitude complex rotator per partial, with its
//   coefficients recomputed once per analysis frame (every ~2.7 ms) rather
//   than per sample. That is 4 multiplies per partial per sample and no
//   transcendentals in the inner loop.
//
//   NOISE uses ONE filterbank shared by every voice. Per-voice bandpass banks
//   would dominate the cost (24 biquads each); instead the bank runs once,
//   producing 24 band signals, and each voice just takes a weighted sum of
//   them. Two independent banks are kept and handed out alternately so
//   simultaneous voices do not share an identical noise realisation.
#include "BiquadFilter.h"
#include "DrumPatterns.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace hexdrums {

// ── Decoded model ────────────────────────────────────────────────────────────

inline double dequantAmp(uint8_t q) {
    if (q == 0) return 0.0;
    const double t = double(q - 1) / 254.0;
    return std::pow(10.0, (t * 96.0 - 96.0) / 20.0);
}

// One fixed mode of struck metal: frequency, starting amplitude, decay.
// Cymbals and hats are modelled as hundreds of these rather than as shaped
// noise, which is what they actually are — 24-band noise measured a spectral
// crest of 7.8 dB against a real crash's 17.7, i.e. static.
// -80 dB relative to full scale. Below this a mode cannot be heard under a
// guitar, and the previous cutoff of -140 dB meant hundreds of them were kept
// spinning for the whole tail of every cymbal.
inline constexpr float kModeFloor = 1.0e-4f;

struct ResynthMode {
    float freq{0.0f}, amp{0.0f}, t60{1.0f};
    float lifetime{0.0f};   // seconds until it falls below kModeFloor
};

// Modes must NOT all start in phase. Struck metal excites its modes with
// scattered phase; starting 600 of them at zero makes them sum coherently and
// the peak scales with N instead of sqrt(N) — measured, the hats and ride bell
// came out at 2.6-3.7 full scale. A golden-ratio low-discrepancy sequence
// gives a well-spread, deterministic phase per mode with no table and no
// stored byte, and gives the offline analysis and the runtime the same answer.
inline float modePhase(int i) {
    const float t = float(i) * 0.6180339887f;
    return 6.28318530718f * (t - std::floor(t));
}

inline double dequantT60(uint8_t q) {
    const double lo = 0.02, hi = 25.0;
    return lo * std::pow(hi / lo, double(q) / 255.0);
}

struct ResynthPartial {
    uint16_t start{0};          // first analysis frame
    uint16_t len{0};            // frames
    uint32_t offset{0};         // byte offset of this track's (freq,amp) stream
};

// One analysed hit. Holds its own bytes; the per-frame values are decoded on
// the fly, which keeps a whole kit at its on-disk size in memory.
struct ResynthHit {
    std::vector<uint8_t>        bytes;
    std::vector<ResynthPartial> partials;
    std::vector<ResynthMode>    modes;      // decoded once at load
    uint32_t noiseOffset{0};
    uint16_t noiseFrames{0};
    uint16_t hop{128};
    uint8_t  bands{24};
    double   srcRate{44100.0};

    float partialFreq(const ResynthPartial& p, int frame) const {
        const uint8_t* q = bytes.data() + p.offset + size_t(frame) * 3;
        return float(uint16_t(q[0] | (q[1] << 8))) * 0.25f;
    }
    float partialAmp(const ResynthPartial& p, int frame) const {
        return float(dequantAmp(bytes[p.offset + size_t(frame) * 3 + 2]));
    }
    float noiseBand(int frame, int band) const {
        return float(dequantAmp(bytes[noiseOffset + size_t(frame) * bands + band]));
    }
    // Total length in source samples.
    size_t lengthSamples() const { return size_t(noiseFrames) * hop; }
};

// Decode one "HXD1" blob. Returns false on a malformed or truncated model
// rather than reading past the end.
inline bool decodeHit(const uint8_t* data, size_t n, ResynthHit& out) {
    if (n < 16) return false;
    const bool v2 = (std::memcmp(data, "HXD2", 4) == 0);
    if (!v2 && std::memcmp(data, "HXD1", 4) != 0) return false;
    auto rd16 = [&](size_t o) { return uint16_t(data[o] | (data[o + 1] << 8)); };
    auto rd32 = [&](size_t o) {
        return uint32_t(data[o]) | (uint32_t(data[o + 1]) << 8) |
               (uint32_t(data[o + 2]) << 16) | (uint32_t(data[o + 3]) << 24);
    };

    out.srcRate     = double(rd32(4));
    const uint16_t nPart = rd16(8);
    out.hop         = rd16(10);
    out.noiseFrames = rd16(12);
    out.bands       = data[14];
    if (out.hop == 0 || out.bands == 0 || out.bands > 64) return false;

    const uint16_t nModes = v2 ? rd16(16) : 0;
    const size_t headerLen = v2 ? 20 : 16;
    if (n < headerLen) return false;

    out.bytes.assign(data, data + n);
    out.partials.clear();
    out.partials.reserve(nPart);
    out.modes.clear();

    size_t o = headerLen;
    for (uint16_t i = 0; i < nPart; ++i) {
        if (o + 4 > n) return false;
        ResynthPartial p;
        p.start  = rd16(o);
        p.len    = rd16(o + 2);
        p.offset = uint32_t(o + 4);
        o += 4 + size_t(p.len) * 3;
        if (o > n) return false;
        out.partials.push_back(p);
    }
    if (o + size_t(out.noiseFrames) * out.bands > n) return false;
    out.noiseOffset = uint32_t(o);
    o += size_t(out.noiseFrames) * out.bands;

    if (nModes) {
        if (o + size_t(nModes) * 4 > n) return false;
        out.modes.reserve(nModes);
        for (uint16_t i = 0; i < nModes; ++i) {
            ResynthMode m;
            m.freq = float(rd16(o)) * 0.25f;
            m.amp  = float(dequantAmp(data[o + 2]));
            m.t60  = float(dequantT60(data[o + 3]));
            o += 4;
            if (m.freq > 10.0f && m.amp > 0.0f) out.modes.push_back(m);
        }
        // Sort by how long each mode stays AUDIBLE, not by its decay time.
        // A long-decaying but very quiet mode dies before a loud short one, so
        // sorting on t60 alone left hundreds of inaudible modes being rotated
        // for the whole tail. Lifetime = t60 * ln(amp/threshold) / ln(1000),
        // which is the time for that mode to reach the threshold from its own
        // starting amplitude. With the list in that order, the renderer can
        // walk a count that only ever shrinks.
        for (ResynthMode& m : out.modes) {
            const float rel = std::max(m.amp / kModeFloor, 1.0f);
            m.lifetime = m.t60 * std::log(rel) / 6.907755f;
        }
        std::sort(out.modes.begin(), out.modes.end(),
                  [](const ResynthMode& a, const ResynthMode& b) { return a.lifetime > b.lifetime; });
    }
    return true;
}

// ── Kit ──────────────────────────────────────────────────────────────────────

struct ResynthLayer {
    uint8_t    loVel{0}, hiVel{127};
    float      gain{1.0f};      // per-instrument normalisation from the kit file
    ResynthHit hit;
};

struct ResynthKit {
    std::vector<ResynthLayer> inst[INST_COUNT];
    std::string name{"Resynth Kit"};

    bool covers(int i) const { return i >= 0 && i < INST_COUNT && !inst[i].empty(); }
    int  voicesCovered() const {
        int n = 0;
        for (int i = 0; i < INST_COUNT; ++i) if (covers(i)) ++n;
        return n;
    }
    size_t bytes() const {
        size_t t = 0;
        for (const auto& v : inst) for (const auto& l : v) t += l.hit.bytes.size();
        return t;
    }
};

// Load a "HXKIT1" container: a table of (instrument, velocity window) entries
// pointing at HXD1 blobs.
inline bool decodeKit(const uint8_t* data, size_t n, ResynthKit& out, std::string* err = nullptr) {
    if (n < 8 || std::memcmp(data, "HXKIT1", 6) != 0) {
        if (err) *err = "not a HXKIT1 file";
        return false;
    }
    const uint8_t version = data[6];
    const uint8_t count   = data[7];
    if (version != 2) { if (err) *err = "unsupported kit version"; return false; }
    auto rd32 = [&](size_t o) {
        return uint32_t(data[o]) | (uint32_t(data[o + 1]) << 8) |
               (uint32_t(data[o + 2]) << 16) | (uint32_t(data[o + 3]) << 24);
    };

    size_t o = 8;
    int loaded = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (o + 16 > n) { if (err) *err = "truncated entry table"; return false; }
        const uint8_t inst = data[o];
        const uint8_t lo   = data[o + 1];
        const uint8_t hi   = data[o + 2];
        const uint32_t off = rd32(o + 4);
        const uint32_t len = rd32(o + 8);
        float gain = 1.0f;
        const uint32_t gbits = rd32(o + 12);
        std::memcpy(&gain, &gbits, sizeof(float));
        o += 16;
        if (inst >= INST_COUNT || size_t(off) + len > n) continue;

        ResynthLayer L;
        L.loVel = lo; L.hiVel = hi; L.gain = gain;
        if (!decodeHit(data + off, len, L.hit)) continue;
        out.inst[inst].push_back(std::move(L));
        ++loaded;
    }
    for (auto& v : out.inst)
        std::sort(v.begin(), v.end(),
                  [](const ResynthLayer& a, const ResynthLayer& b) { return a.loVel < b.loVel; });

    if (loaded == 0) { if (err) *err = "no usable entries"; return false; }
    return true;
}

// ── Shared noise filterbank ──────────────────────────────────────────────────
//
// Runs once per sample for the whole plugin, not once per voice. Band centres
// must match the analyser's exactly or every voice's spectrum tilts.
class NoiseBus {
public:
    static constexpr int kBands = 24;
    static constexpr int kBanks = 2;      // independent realisations

    void prepare(double sampleRate) {
        fs = sampleRate;
        const double lo = 40.0, hi = std::min(fs * 0.45, 16000.0);
        for (int b = 0; b < kBands; ++b) {
            centre[b] = lo * std::pow(hi / lo, double(b) / (kBands - 1));
            for (int k = 0; k < kBanks; ++k)
                filt[k][b].setCoeffs(Filters::bandpass(std::min(centre[b], fs * 0.45), 1.4, fs));
        }
        calibrate();
        for (int k = 0; k < kBanks; ++k) rng[k] = 0x9E3779B9u + uint32_t(k) * 0x7F4A7C15u;
    }

    // Advance one sample; band outputs are then readable via band().
    void tick() {
        for (int k = 0; k < kBanks; ++k) {
            const float x = white(rng[k]);
            for (int b = 0; b < kBands; ++b)
                out[k][b] = filt[k][b].process(x) * corr[b];
        }
    }
    float band(int bank, int b) const { return out[bank][b]; }

private:
    static float white(uint32_t& s) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return float(int32_t(s)) * (1.0f / 2147483648.0f);
    }
    // A model gain of g must produce an output RMS of g, or the analyser's
    // carefully computed band RMS values come out multiplied by whatever units
    // the filter happens to have.
    void calibrate() {
        const int kCal = 8192;
        uint32_t s = 0x2468ACEu;
        for (int b = 0; b < kBands; ++b) {
            BiquadFilter f = filt[0][b];
            double acc = 0.0;
            for (int i = 0; i < kCal; ++i) {
                const float y = f.process(white(s));
                if (i > 512) acc += double(y) * y;
            }
            const double rms = std::sqrt(acc / double(kCal - 512));
            corr[b] = (rms > 1e-12) ? float((1.0 / rms) / std::sqrt(3.0)) : 0.0f;
        }
    }

    double fs{48000.0};
    double centre[kBands]{};
    float  corr[kBands]{};
    float  out[kBanks][kBands]{};
    BiquadFilter filt[kBanks][kBands];
    uint32_t rng[kBanks]{};
};

// ── Voices ───────────────────────────────────────────────────────────────────

class ResynthVoices {
public:
    static constexpr int kMaxVoices   = 12;
    static constexpr int kMaxPartials = 48;
    // Cymbals need hundreds to low thousands. Measured against the real
    // recordings' spectral centroid, more modes keep buying brightness well
    // past 600: a china reaches 3564 Hz at 1000 modes against the real 3637,
    // where 600 modes stalls at 3269. Only the BANKS hold these now, one per
    // instrument, so the cost is bounded by the kit rather than by polyphony.
    static constexpr int kMaxModes    = 1536;

    void prepare(double sampleRate) {
        fs = sampleRate;
        bus.prepare(sampleRate);
        reset();
    }
    void reset() {
        for (auto& v : voices) v.active = false;
        for (auto& b : banks) { b = ModeBank{}; }
        nextBank = 0;
        age = 0;
    }

    // Swapping kits must silence everything: a sounding voice points into the
    // old kit's bytes.
    void setKit(const ResynthKit* k) { reset(); kit = k; }
    const ResynthKit* currentKit() const { return kit; }
    bool covers(int inst) const { return kit && kit->covers(inst); }

    // How many partials each voice may render. Lower trades realism for CPU.
    void setPartialLimit(int n) { partialLimit = std::clamp(n, 4, kMaxPartials); }
    // 0 = every hit identical; 1 = full per-hit variation.
    void setVariation(float v) { variation = std::clamp(v, 0.0f, 1.0f); }

    void trigger(int inst, float velocity01) {
        if (!kit || inst < 0 || inst >= INST_COUNT) return;
        const auto& layers = kit->inst[inst];
        if (layers.empty()) return;

        const int vel = std::clamp(int(velocity01 * 127.0f + 0.5f), 1, 127);

        const ResynthLayer* best = nullptr;
        for (const auto& L : layers)
            if (vel >= L.loVel && vel <= L.hiVel) { best = &L; break; }
        if (!best) {   // outside every window: nearest by centre
            for (const auto& L : layers)
                if (!best || std::abs((L.loVel + L.hiVel) / 2 - vel) <
                             std::abs((best->loVel + best->hiVel) / 2 - vel)) best = &L;
        }
        if (!best) return;

        // Hi-hat choking is handled inside the shared bank (closing the hat
        // damps the ring); here it only needs to stop the previous hat's
        // noise burst.
        if (inst == INST_HAT_CLOSED || inst == INST_HAT_PEDAL)
            for (auto& v : voices)
                if (v.active && (v.inst == INST_HAT_CLOSED || v.inst == INST_HAT_PEDAL ||
                                 v.inst == INST_HAT_OPEN))
                    v.choking = true;

        Voice* v = allocate();
        if (!v) return;

        v->hit      = &best->hit;
        v->inst     = inst;
        v->active   = true;
        v->choking  = false;
        v->chokeEnv = 1.0f;
        v->pos      = 0.0;
        v->frame    = -1;
        v->bank     = nextBank; nextBank = (nextBank + 1) % NoiseBus::kBanks;
        v->order    = ++age;

        // Analysis frames advance in ENGINE samples: the model's frame k sits
        // at k*hop/srcRate seconds, so a 44.1 kHz kit plays at the right speed
        // and pitch in a 48 kHz engine without resampling anything.
        v->framePeriod = double(best->hit.hop) * fs / best->hit.srcRate;

        // A layer already carries most of the dynamic difference, so velocity
        // only trims within it — a full linear scale would double-count it.
        const float span = float(std::max(1, int(best->hiVel) - int(best->loVel)));
        const float t    = std::clamp((vel - best->loVel) / span, 0.0f, 1.0f);
        v->gain = (0.80f + 0.20f * t) * best->gain;

        // Per-hit variation. A real drummer never strikes twice identically;
        // this is what removes the machine-gun artefact WITHOUT round-robins.
        v->detune = 1.0f + variation * 0.003f * urand(v->order);
        v->trim   = std::pow(10.0f, variation * 0.5f * urand(v->order * 7919u) / 20.0f);

        // Modal instruments excite their shared persistent bank instead of
        // carrying their own oscillators; the voice then handles only the
        // noise (the stick attack).
        if (!best->hit.modes.empty()) exciteBank(inst, vel, v->gain * v->trim);

        const int np = std::min<int>(int(best->hit.partials.size()), partialLimit);
        v->numOsc = np;
        for (int i = 0; i < np; ++i) {
            v->osc[i].re = 1.0f; v->osc[i].im = 0.0f;
            v->osc[i].cr = 1.0f; v->osc[i].ci = 0.0f;
            v->osc[i].amp = 0.0f; v->osc[i].ampStep = 0.0f;
            v->osc[i].live = false;
        }
        for (int b = 0; b < NoiseBus::kBands; ++b) { v->nb[b] = 0.0f; v->nbStep[b] = 0.0f; }
    }

    void chokeInstrument(int inst) {
        for (auto& v : voices) if (v.active && v.inst == inst) v.choking = true;
    }

    // Stop the whole kit, gracefully. Both halves of it: the one-shot voices
    // get the existing choke envelope, and the cymbal banks -- which are
    // persistent and would otherwise ring straight through -- get a hand laid
    // on them. Used by the count-in, where the only thing that should be
    // audible is the click.
    void chokeAll(float ms = 25.0f) {
        for (auto& v : voices) if (v.active) v.choking = true;
        const float n = std::max(1.0f, ms * 1.0e-3f * static_cast<float>(fs));
        const float c = std::exp(-9.21f / n);        // ~-80 dB over `ms`
        for (auto& B : banks)
            if (B.active) { B.dampCoef = c; }
    }

    bool anyActive() const {
        for (const auto& v : voices) if (v.active) return true;
        return false;
    }

    // ADDS the kit into out[0..n). instGain is indexed by instrument.
    void render(float* out, int n, const float* instGain) {
        if (!kit) return;
        for (int i = 0; i < n; ++i) {
            bus.tick();
            float acc = renderBanks(instGain);
            for (auto& v : voices) {
                if (!v.active) continue;
                acc += renderSample(v) * (instGain ? instGain[v.inst] : 1.0f);
            }
            out[i] += acc;
        }
    }

private:
    struct Osc {
        float re, im;       // unit-magnitude rotator
        float cr, ci;       // rotation per sample
        float amp, ampStep; // linear ramp across the frame
        bool  live;
    };

    // A damped rotator. The decay is built into the rotation coefficient, so
    // there is nothing to update per frame and no magnitude to renormalise —
    // unlike the unit rotators the tracked partials use.
    struct ModeOsc { float re, im, cr, ci; };

    // ONE persistent bank per cymbal, not one voice per strike.
    //
    // A real cymbal struck repeatedly is not many cymbals: it is one piece of
    // metal whose modes are re-excited, so a new strike ADDS energy to what is
    // already ringing. Allocating a fresh voice per hit modelled it as a stack
    // of independent copies, and a ride on eighth notes needs about 43 of them
    // (5.3 hits/s over an 8 s decay) against a 12-voice pool — so the pool
    // stole voices every couple of seconds and cut a ringing cymbal dead. That
    // is the "repeat glitch".
    //
    // Exciting a persistent bank is both the physically correct model and far
    // cheaper: one bank per cymbal regardless of how fast it is played, and the
    // ring carries over the next strike the way a real one does.
    struct ModeBank {
        ModeOsc osc[kMaxModes];
        int     count{0};        // modes in this bank
        int     live{0};         // still above the audibility floor
        const ResynthHit* src{nullptr};
        bool    active{false};
        // Choke envelope. A cymbal bank rings for seconds and is never
        // retriggered to silence, so stopping the kit needs an explicit damp
        // -- the hand on the cymbal. 1.0 with coef 1.0 is "not choking".
        float   damp{1.0f}, dampCoef{1.0f};
    };

    struct Voice {
        const ResynthHit* hit{nullptr};
        Osc      osc[kMaxPartials];
        float    nb[NoiseBus::kBands]{}, nbStep[NoiseBus::kBands]{};
        double   pos{0.0}, framePeriod{128.0};
        int      frame{-1}, numOsc{0}, inst{-1}, bank{0};
        uint32_t order{0};
        float    gain{1.0f}, detune{1.0f}, trim{1.0f}, chokeEnv{1.0f};
        bool     active{false}, choking{false};
    };

    static float urand(uint32_t s) {          // deterministic per voice, in [-1,1]
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return float(int32_t(s)) * (1.0f / 2147483648.0f);
    }

    // Hi-hats are ONE pair of cymbals on one stand, so closed, pedal and open
    // share a bank — which is also what makes closing the hat damp an open
    // ring, rather than leaving two hats sounding at once.
    static int bankIndexFor(int inst) {
        if (inst == INST_HAT_PEDAL || inst == INST_HAT_OPEN) return INST_HAT_CLOSED;
        return inst;
    }

    // Harder strikes are BRIGHTER, not merely louder: the high modes come up
    // faster than the low ones. Raising a level below 1 to a larger exponent
    // does exactly that, and it is why one mode set can serve every velocity.
    static float velExcite(float vel01, float freq) {
        const float base = std::clamp(0.18f + 0.82f * vel01, 0.02f, 1.0f);
        const float expo = 1.0f + std::min(freq, 14000.0f) / 9000.0f;
        return std::pow(base, expo);
    }

    void exciteBank(int inst, float vel01, float gain) {
        const int bi = bankIndexFor(inst);
        ModeBank& B = banks[bi];

        // All velocities share ONE mode set — the hardest layer's. A cymbal's
        // modes do not move with how hard it is hit, only their excitation
        // does, and a persistent bank cannot accumulate strikes whose mode
        // frequencies differ.
        const auto& layers = kit->inst[bi];
        if (layers.empty()) return;
        const ResynthHit& src = layers.back().hit;
        if (src.modes.empty()) return;

        if (B.src != &src) {
            B.src = &src;
            B.count = std::min<int>(int(src.modes.size()), kMaxModes);
            for (int i = 0; i < B.count; ++i) {
                const ResynthMode& m = src.modes[i];
                const float w = 2.0f * float(M_PI) * m.freq / float(fs);
                const float r = std::exp(-6.907755f / std::max(1e-4f, m.t60 * float(fs)));
                B.osc[i].cr = r * std::cos(w);
                B.osc[i].ci = r * std::sin(w);
                B.osc[i].re = B.osc[i].im = 0.0f;
            }
        }

        // Closing the hat clamps the cymbals, so it damps whatever is ringing
        // before adding the new strike. Open hits just add.
        if (inst == INST_HAT_CLOSED || inst == INST_HAT_PEDAL)
            for (int i = 0; i < B.count; ++i) { B.osc[i].re *= 0.10f; B.osc[i].im *= 0.10f; }

        for (int i = 0; i < B.count; ++i) {
            const ResynthMode& m = src.modes[i];
            const float a  = m.amp * gain * velExcite(vel01, m.freq);
            const float ph = modePhase(i);
            // ADD, do not replace: this is the superposition of a new strike
            // onto a cymbal that is still ringing.
            B.osc[i].re += a * std::cos(ph);
            B.osc[i].im += a * std::sin(ph);
        }
        B.live   = B.count;      // previously culled modes are alive again
        B.active = true;
        B.damp = 1.0f; B.dampCoef = 1.0f;   // a new strike lifts any hand on it
    }

    // Render every ringing bank. Culling works the same way as before: modes
    // are ordered by how long they stay audible, so the count only shrinks.
    float renderBanks(const float* instGain) {
        float s = 0.0f;
        for (int bi = 0; bi < INST_COUNT; ++bi) {
            ModeBank& B = banks[bi];
            if (!B.active) continue;
            while (B.live > 0) {
                const ModeOsc& last = B.osc[B.live - 1];
                if (last.re * last.re + last.im * last.im > kModeFloor * kModeFloor) break;
                --B.live;
            }
            if (B.live == 0) { B.active = false; continue; }
            float acc = 0.0f;
            for (int i = 0; i < B.live; ++i) {
                ModeOsc& m = B.osc[i];
                const float nr = m.re * m.cr - m.im * m.ci;
                const float ni = m.re * m.ci + m.im * m.cr;
                m.re = nr; m.im = ni;
                acc += ni;
            }
            if (B.dampCoef < 1.0f) {
                B.damp *= B.dampCoef;
                if (B.damp < 1.0e-4f) { B.active = false; B.live = 0; continue; }
                acc *= B.damp;
            }
            s += acc * (instGain ? instGain[bi] : 1.0f);
        }
        return s;
    }

    Voice* allocate() {
        Voice* oldest = nullptr;
        for (auto& v : voices) {
            if (!v.active) return &v;
            if (!oldest || v.order < oldest->order) oldest = &v;
        }
        return oldest;      // drums decay fast, so the oldest is the quietest
    }

    // Re-arm every oscillator and noise-band ramp for the frame we just entered.
    void startFrame(Voice& v, int f) {
        const ResynthHit& h = *v.hit;
        const float inv = 1.0f / float(v.framePeriod);

        for (int i = 0; i < v.numOsc; ++i) {
            const ResynthPartial& p = h.partials[i];
            const int rel = f - int(p.start);
            Osc& o = v.osc[i];
            if (rel < 0 || rel >= int(p.len)) { o.live = false; o.amp = 0.0f; o.ampStep = 0.0f; continue; }

            const float freq = h.partialFreq(p, rel) * v.detune;
            const float a    = h.partialAmp(p, rel) * v.gain * v.trim;
            const float aNext = (rel + 1 < int(p.len))
                              ? h.partialAmp(p, rel + 1) * v.gain * v.trim : 0.0f;

            if (!o.live) {                   // partial starts here
                o.re = 1.0f; o.im = 0.0f;
                o.live = true;
            }
            // One cos/sin per partial per FRAME (~2.7 ms), not per sample.
            const float w = 2.0f * float(M_PI) * freq / float(fs);
            o.cr = std::cos(w);
            o.ci = std::sin(w);
            o.amp = a;
            o.ampStep = (aNext - a) * inv;
        }

        for (int b = 0; b < NoiseBus::kBands; ++b) {
            const float g  = (f < int(h.noiseFrames)) ? h.noiseBand(f, b) * v.gain * v.trim : 0.0f;
            const float g1 = (f + 1 < int(h.noiseFrames)) ? h.noiseBand(f + 1, b) * v.gain * v.trim : 0.0f;
            v.nb[b] = g;
            v.nbStep[b] = (g1 - g) * inv;
        }
    }

    float renderSample(Voice& v) {
        const ResynthHit& h = *v.hit;

        const int f = int(v.pos / v.framePeriod);
        if (f != v.frame) {
            // A modal voice carries only the attack noise; its ring lives in
            // the shared bank, which outlives it.
            if (f >= int(h.noiseFrames)) { v.active = false; return 0.0f; }
            v.frame = f;
            startFrame(v, f);
        }

        float s = 0.0f;
        for (int i = 0; i < v.numOsc; ++i) {
            Osc& o = v.osc[i];
            if (!o.live) continue;
            // Rotate. Im(z) is the sine; the tiny magnitude drift a float
            // rotator accumulates is corrected once per frame below.
            const float nr = o.re * o.cr - o.im * o.ci;
            const float ni = o.re * o.ci + o.im * o.cr;
            o.re = nr; o.im = ni;
            s += ni * o.amp;
            o.amp += o.ampStep;
        }
        if (f < int(h.noiseFrames)) {
            for (int b = 0; b < NoiseBus::kBands; ++b) {
                s += bus.band(v.bank, b) * v.nb[b];
                v.nb[b] += v.nbStep[b];
            }
        }

        if (v.choking) {
            v.chokeEnv *= 0.9993f;               // ~12 ms at 48 kHz
            if (v.chokeEnv < 1e-4f) { v.active = false; return 0.0f; }
            s *= v.chokeEnv;
        }

        v.pos += 1.0;

        // Renormalise the rotators once per frame. Without this a float
        // rotator's magnitude drifts, and over a long cymbal decay that is an
        // audible level error rather than a rounding detail.
        if (int(v.pos / v.framePeriod) != v.frame) {
            for (int i = 0; i < v.numOsc; ++i) {
                Osc& o = v.osc[i];
                if (!o.live) continue;
                const float m = std::sqrt(o.re * o.re + o.im * o.im);
                if (m > 1e-6f) { o.re /= m; o.im /= m; }
            }
        }
        return s;
    }

    Voice    voices[kMaxVoices];
    ModeBank banks[INST_COUNT];
    NoiseBus bus;
    const ResynthKit* kit{nullptr};
    double fs{48000.0};
    int    partialLimit{kMaxPartials};
    int    nextBank{0};
    uint32_t age{0};
    float  variation{1.0f};
};

} // namespace hexdrums
