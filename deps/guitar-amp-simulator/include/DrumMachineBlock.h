#pragma once
// Sequencer + kit for the Practice plugin's drum machine.
//
// Renders ADDITIVELY into the output buffer so the plugin can mix drums with
// the guitar and the looper in a single pass.
//
// Timing is scheduled, not polled. Each block we look ahead for step
// boundaries, apply swing and humanisation to work out when each hit ACTUALLY
// lands, and push it onto a small queue of pending triggers with an absolute
// sample time. Events then fire from the queue at their sample offset within
// whichever block contains them. That indirection is what makes negative
// humanisation possible (a hit landing slightly EARLY has to be known about
// before its nominal step arrives) and it keeps swing correct at the 32-frame
// buffers the pi-Stomp runs, where a swung sixteenth is often several blocks
// away from its own downbeat.
#include "CompressorBlock.h"
#include "DrumPatterns.h"
#include "DrumResynth.h"
#include "PlateReverbBlock.h"
#include "DrumVoices.h"
#include "TransportClock.h"
#include <algorithm>
#include <atomic>
#include <vector>
#include <cmath>
#include <cstdint>

namespace hexdrums {

// A groove loaded from a MIDI file, as opposed to one from the factory table.
// Fixed capacity and no heap use: the loader fills an INACTIVE slot on the
// worker thread and then publishes it with one atomic store, so the audio
// thread never allocates, locks, or reads a half-written pattern.
struct UserPattern {
    static constexpr int kMaxEvents = 1024;

    struct Event { uint16_t step; uint8_t inst; uint8_t vel; };

    Event    events[kMaxEvents]{};
    int      numEvents{0};
    uint8_t  stepsPerBar{16};
    uint8_t  bars{1};
    char     name[64]{"User"};
};

class DrumMachineBlock {
public:
    // ── Lifecycle ────────────────────────────────────────────────────────────
    void prepare(double sampleRate, int maxBlock = 8192) noexcept {
        fs = (sampleRate > 0.0) ? sampleRate : 48000.0;
        // The kit is rendered into this at unity and the bus processes it in
        // place, so the compressor always sees the same level no matter where
        // the user has the Drums fader.
        scratch.assign(size_t(std::max(maxBlock, 1024)), 0.0f);
        parallel.assign(scratch.size(), 0.0f);
        comp.prepare(fs, int(scratch.size()), 1);
        room.prepare(fs, int(scratch.size()), 1);
        applyBus();
        resynth.prepare(fs);
        kick.prepare(fs);
        snare.prepare(fs);
        rim.prepare(fs);
        tomHi.prepare(fs, 220.0, 0x7031A001u);
        tomMid.prepare(fs, 160.0, 0x7031A002u);
        tomFloor.prepare(fs, 104.0, 0x7031A003u);
        hat.prepare(fs);
        crash.prepare(fs, 318.0, 0.0, 0x00C3A511u);
        ride.prepare(fs, 268.0, 522.0, 0x0021DE01u);
        crash.setDecay(2.6f);
        ride.setDecay(3.4f);
        ride.setPing(0.85f);
        reset();
    }

    // Silence every voice and drop anything queued. Used on transport stop-to-
    // start and on host reset; NOT on a plain stop, where tails should ring on.
    void reset() noexcept {
        kick.reset(); snare.reset(); rim.reset();
        tomHi.reset(); tomMid.reset(); tomFloor.reset();
        hat.reset(); crash.reset(); ride.reset();
        resynth.reset();
        room.reset();
        queueCount   = 0;
        nextScanStep = kNoStep;
    }

    // Re-arm the step scanner at the current musical position. Call when the
    // transport starts or relocates, so the sequencer doesn't try to catch up
    // on every step between the old position and the new one.
    void rearm(const TransportClock& clk) noexcept {
        queueCount   = 0;
        nextScanStep = static_cast<int64_t>(std::floor(stepPositionAt(clk, 0.0)));
    }

    // ── Parameters ───────────────────────────────────────────────────────────
    void setPattern(int index) noexcept {
        int n = 0; patternTable(n);
        patternIdx = std::clamp(index, 0, n - 1);
    }
    void setUseUserPattern(bool b) noexcept { useUser.store(b, std::memory_order_relaxed); }
    bool usingUserPattern() const  noexcept {
        return useUser.load(std::memory_order_relaxed) && userSlotValid();
    }

    // 0 = straight, 1 = full triplet swing on the odd steps of the grid.
    void setSwing(float amount)    noexcept { swing = std::clamp(amount, 0.0f, 1.0f); }
    void setHumanize(float amount) noexcept { humanize = std::clamp(amount, 0.0f, 1.0f); }
    void setMasterGain(float lin)  noexcept { master = std::max(0.0f, lin); }

    // Trim relative to the kit's voiced balance; 1.0 leaves it as designed.
    void setInstrumentTrim(int inst, float lin) noexcept {
        if (inst >= 0 && inst < INST_COUNT) trims[inst] = std::max(0.0f, lin);
    }
    void setInstrumentMuted(int inst, bool m) noexcept {
        if (inst >= 0 && inst < INST_COUNT) muted[inst] = m;
    }

    // ── Drum bus: compression and room ───────────────────────────────────────
    //
    // Both sit BEFORE the Drums Level control, on purpose. A fixed compressor
    // threshold placed after a user fader is the mistake that cost a previous
    // session: the chain ran hotter than the threshold assumed and it slammed
    // 8 dB of gain reduction. Compressing at a known, fixed operating point and
    // scaling afterwards makes the behaviour independent of gain staging.
    //
    // The room matters more than it sounds: the kit is modelled from CLOSE mics
    // only, deliberately, so that overhead bleed could not contaminate each
    // instrument's analysis. That leaves it accurate and bone dry. The
    // ambience a real kit gets from the room it was played in has to be put
    // back here, over the whole kit at once — which is also how a real room
    // works, and why baking it per voice would have been wrong.
    // PARALLEL, not series. Measured on the real kit: a series bus compressor
    // is useless here. The drum bus has a 22 dB crest factor (RMS -27 dBFS,
    // peaks -5.5), so a threshold high enough to catch peaks never engages on
    // the body, and one low enough to catch the body ducks it while a
    // transient-safe attack misses the peaks entirely — crest went UP, which
    // is the opposite of glue. Blending a hard-crushed COPY under the
    // untouched dry raises the body by several dB with the transients still
    // intact, which is what makes a kit sound dense rather than squashed.
    void setCompAmount(float a) noexcept { compAmt = std::clamp(a, 0.0f, 1.0f); applyBus(); }
    void setRoomAmount(float a) noexcept { roomAmt = std::clamp(a, 0.0f, 1.0f); applyBus(); }
    void setRoomSize(float a)   noexcept { roomSize = std::clamp(a, 0.0f, 1.0f); applyBus(); }

    // ── Resynthesised kit ────────────────────────────────────────────────────
    // When a kit is loaded, its instruments play from the analysed models and
    // the synthesised voices are used only for whatever it does not cover.
    void setResynthKit(const ResynthKit* k) noexcept { resynth.setKit(k); }
    const ResynthKit* resynthKit() const noexcept    { return resynth.currentKit(); }
    void setResynthPartialLimit(int n) noexcept      { resynth.setPartialLimit(n); }
    void setResynthVariation(float v) noexcept       { resynth.setVariation(v); }

    // Kit voicing, exposed so a metal kit and a jazz kit are the same code.
    void setKickTuning(float semis) noexcept { kick.setTuning(semis); }
    void setKickDecay(float sec)    noexcept { kick.setDecay(sec); }
    void setKickClick(float amt)    noexcept { kick.setClick(amt); }
    void setKickDrive(float amt)    noexcept { kick.setDrive(amt); }
    void setSnareTuning(float semis)noexcept { snare.setTuning(semis); }
    void setSnareDecay(float sec)   noexcept { snare.setDecay(sec); }
    void setSnareSnappy(float amt)  noexcept { snare.setSnappy(amt); }
    void setHatDecay(float sec)     noexcept { hat.setDecay(sec); }
    void setHatTone(float amt)      noexcept { hat.setTone(amt); }

    // ── User-pattern handoff (worker thread → audio thread) ──────────────────
    // Returns the slot the loader should fill. Never the live one.
    UserPattern& writableSlot() noexcept { return slots[1 - liveSlot.load(std::memory_order_acquire)]; }
    // Publish the filled slot. Release-ordered so the audio thread cannot see
    // the new index before the pattern data it points at.
    void publishUserPattern() noexcept {
        const int next = 1 - liveSlot.load(std::memory_order_relaxed);
        slotValid[next].store(true, std::memory_order_relaxed);
        liveSlot.store(next, std::memory_order_release);
    }

    // Name of whatever pattern is currently sounding (for the UI).
    const char* currentPatternName() const noexcept {
        if (usingUserPattern()) return slots[liveSlot.load(std::memory_order_acquire)].name;
        int n = 0; return patternTable(n)[patternIdx].name;
    }

    // Steps in the current pattern's full cycle, and its grid resolution.
    int currentStepsPerBar() const noexcept {
        if (usingUserPattern()) return slots[liveSlot.load(std::memory_order_acquire)].stepsPerBar;
        int n = 0; return patternTable(n)[patternIdx].stepsPerBar;
    }
    int currentTotalSteps() const noexcept {
        if (usingUserPattern()) {
            const UserPattern& u = slots[liveSlot.load(std::memory_order_acquire)];
            return u.stepsPerBar * u.bars;
        }
        int n = 0;
        const DrumPattern& p = patternTable(n)[patternIdx];
        return p.stepsPerBar * p.bars;
    }
    // Bars in the pattern — the looper uses this to offer "loop = one pattern".
    int currentBars() const noexcept {
        if (usingUserPattern()) return slots[liveSlot.load(std::memory_order_acquire)].bars;
        int n = 0; return patternTable(n)[patternIdx].bars;
    }

    // Step index currently sounding, for the GUI playhead. -1 when stopped.
    int playheadStep() const noexcept { return lastFiredStep; }

    // ── Render ───────────────────────────────────────────────────────────────
    // ADDS the kit into out[0..n). Voices always render, even when the
    // transport is stopped, so a decaying crash isn't cut off by hitting stop.
    void render(const TransportClock& clk, float* out, int n) noexcept {
        if (n <= 0) return;
        if (size_t(n) > scratch.size()) scratch.assign(size_t(n), 0.0f);   // host grew the block
        float* buf = scratch.data();
        std::fill(buf, buf + n, 0.0f);

        if (clk.running()) {
            clockSamplePos = clk.samplePosition();
            scheduleSteps(clk, n);
            fireQueue(buf, n);
        } else {
            renderVoices(buf, 0, n);
            lastFiredStep = -1;
        }

        // Parallel compression, then room, then level. The level control comes
        // last so the compressor's operating point never moves with the fader.
        if (compAmt > 0.005f) {
            if (parallel.size() < size_t(n)) parallel.assign(size_t(n), 0.0f);
            float* par = parallel.data();
            std::copy(buf, buf + n, par);
            float* pp[1] = { par };
            comp.process(pp, pp, n, 1);
            for (int i = 0; i < n; ++i) buf[i] += par[i] * compAmt;
        }
        float* io[1] = { buf };
        if (roomAmt > 0.005f) room.process(io, io, n, 1);

        for (int i = 0; i < n; ++i) out[i] += buf[i] * master;
    }

    // Manual hit, e.g. auditioning a voice from the GUI. Fires immediately.
    void triggerNow(int inst, float velocity) noexcept {
        strike(static_cast<uint8_t>(inst), std::clamp(velocity, 0.0f, 1.0f));
    }

private:
    static constexpr int64_t kNoStep     = INT64_MIN;
    static constexpr int     kMaxQueued  = 64;
    static constexpr float   kJitterMs   = 9.0f;   // full-humanise timing spread, ±

    struct Pending { double sampleTime; int step; uint8_t inst; uint8_t vel; };

    bool userSlotValid() const noexcept {
        return slotValid[liveSlot.load(std::memory_order_acquire)].load(std::memory_order_relaxed);
    }

    // Grid position (in steps) at an offset of `sampleOffset` into this block.
    double stepPositionAt(const TransportClock& clk, double sampleOffset) const noexcept {
        const double stepsPerBeat = static_cast<double>(currentStepsPerBar()) / clk.beatsPerBar();
        return (clk.beatPosition() + clk.beatsPerSample() * sampleOffset) * stepsPerBeat;
    }

    // Walk every step boundary that could produce a hit inside this block (plus
    // a lookahead window, so an early-humanised hit is known in time) and queue
    // the hits it carries.
    void scheduleSteps(const TransportClock& clk, int n) noexcept {
        const int    totalSteps   = currentTotalSteps();
        const double stepsPerBeat = static_cast<double>(currentStepsPerBar()) / clk.beatsPerBar();
        const double samplesPerStep = clk.samplesPerBeat() / stepsPerBeat;
        if (samplesPerStep <= 0.0 || totalSteps <= 0) return;

        const double jitterSamples = kJitterMs * 1.0e-3 * fs * humanize;
        const double stepPos0      = stepPositionAt(clk, 0.0);

        // First run after a start/relocate: begin at the upcoming boundary
        // instead of replaying the whole pattern up to here.
        if (nextScanStep == kNoStep)
            nextScanStep = static_cast<int64_t>(std::ceil(stepPos0 - 1.0e-9));

        // Scan past the end of the block by the lookahead, so a hit nudged
        // early still gets queued before its own block is rendered.
        const double scanEnd = n + jitterSamples;
        for (;;) {
            const double nominalOffset = (static_cast<double>(nextScanStep) - stepPos0) * samplesPerStep;
            if (nominalOffset >= scanEnd) break;

            // Steps behind us (a big tempo jump, or a relocate) are skipped
            // rather than fired late in a burst.
            if (nominalOffset >= -samplesPerStep) {
                int64_t s = nextScanStep % totalSteps;
                if (s < 0) s += totalSteps;
                queueStep(static_cast<int>(s), nextScanStep,
                          clk.samplePosition() + nominalOffset, samplesPerStep);
            }
            ++nextScanStep;
        }
    }

    // Queue every hit on one step, with its swing and humanisation applied.
    void queueStep(int step, int64_t absStep, double nominalSample, double samplesPerStep) noexcept {
        // Swing delays the odd steps of the grid toward the triplet position.
        double offset = 0.0;
        if (swing > 0.0f && (absStep & 1) != 0)
            offset += static_cast<double>(swing) * (1.0 / 3.0) * samplesPerStep;

        const double jitterRange = kJitterMs * 1.0e-3 * fs * humanize;

        forEachHit(step, [&](uint8_t inst, uint8_t vel) {
            if (muted[inst]) return;
            double t = nominalSample + offset;
            float  v = vel * (1.0f / 127.0f);
            if (humanize > 0.0f) {
                // Independent jitter per hit: moving a whole step together just
                // sounds like a tempo wobble, whereas a real kit's limbs drift
                // against each other. Velocity follows, since a rushed hit is
                // usually a lighter one.
                t += rng.white() * jitterRange;
                v *= 1.0f - 0.28f * humanize * std::fabs(rng.white());
            }
            pushPending(t, step, inst, static_cast<uint8_t>(std::clamp(v * 127.0f, 1.0f, 127.0f)));
        });
    }

    template <typename Fn>
    void forEachHit(int step, Fn&& fn) const noexcept {
        if (usingUserPattern()) {
            const UserPattern& u = slots[liveSlot.load(std::memory_order_acquire)];
            for (int i = 0; i < u.numEvents; ++i)
                if (u.events[i].step == step) fn(u.events[i].inst, u.events[i].vel);
            return;
        }
        int n = 0;
        const DrumPattern& p = patternTable(n)[patternIdx];
        for (int l = 0; l < p.numLanes; ++l) {
            const uint8_t v = stepVelocity(p.lanes[l].steps[step]);
            if (v) fn(p.lanes[l].inst, v);
        }
    }

    void pushPending(double sampleTime, int step, uint8_t inst, uint8_t vel) noexcept {
        if (queueCount >= kMaxQueued) return;   // pathological density; drop
        queue[queueCount++] = { sampleTime, step, inst, vel };
    }

    // Render the block, stopping at each queued hit to strike its voice.
    void fireQueue(float* out, int n) noexcept {
        const double blockStart = static_cast<double>(clockSamplePos);
        int cursor = 0;

        for (;;) {
            // Earliest pending hit that falls inside this block.
            int    best     = -1;
            double bestTime = 0.0;
            for (int i = 0; i < queueCount; ++i) {
                const double rel = queue[i].sampleTime - blockStart;
                if (rel >= n) continue;
                if (best < 0 || queue[i].sampleTime < bestTime) { best = i; bestTime = queue[i].sampleTime; }
            }
            if (best < 0) break;

            int at = static_cast<int>(bestTime - blockStart);
            if (at < cursor) at = cursor;          // was scheduled before now
            if (at > n) at = n;

            renderVoices(out, cursor, at);
            cursor = at;

            lastFiredStep = queue[best].step;
            strike(queue[best].inst, queue[best].vel * (1.0f / 127.0f));
            queue[best] = queue[--queueCount];     // unordered removal
        }
        renderVoices(out, cursor, n);
    }

    // NOTE: master is deliberately absent — it is applied after the bus, so
    // the compressor's operating point does not move with the Drums fader.
    float voiceGain(int inst) const noexcept {
        return kBalance[inst] * trims[inst] * kVoiceTrim[inst];
    }

    void renderVoices(float* out, int from, int to) noexcept {
        const int n = to - from;
        if (n <= 0) return;
        float* p = out + from;

        // Analysed models are already at recorded levels, so they take the
        // balance and master gain but NOT kVoiceTrim, which exists only to
        // equalise the synthesised voices' raw output.
        if (resynth.currentKit()) {
            float g[INST_COUNT];
            for (int i = 0; i < INST_COUNT; ++i)
                g[i] = muted[i] ? 0.0f : kBalance[i] * trims[i];
            resynth.render(p, n, g);
        }

        kick    .render(p, n, voiceGain(INST_KICK));
        snare   .render(p, n, voiceGain(INST_SNARE));
        rim     .render(p, n, voiceGain(INST_SIDESTICK));
        tomHi   .render(p, n, voiceGain(INST_TOM_HI));
        tomMid  .render(p, n, voiceGain(INST_TOM_MID));
        tomFloor.render(p, n, voiceGain(INST_TOM_FLOOR));
        hat     .render(p, n, voiceGain(INST_HAT_CLOSED));
        crash   .render(p, n, voiceGain(INST_CRASH));
        ride    .render(p, n, voiceGain(INST_RIDE));
    }

    void strike(uint8_t inst, float vel) noexcept {
        // The recorded model wins wherever the kit has one; the synthesised
        // voice is the fallback, not the default.
        if (resynth.covers(inst)) { resynth.trigger(inst, vel); return; }
        switch (inst) {
            case INST_KICK:       kick.trigger(vel); break;
            case INST_SNARE:      snare.trigger(vel); break;
            case INST_SIDESTICK:  rim.trigger(vel); break;
            case INST_TOM_HI:     tomHi.trigger(vel); break;
            case INST_TOM_MID:    tomMid.trigger(vel); break;
            case INST_TOM_FLOOR:  tomFloor.trigger(vel); break;
            // One hi-hat stand: closing it silences whatever was ringing open.
            case INST_HAT_CLOSED: hat.trigger(vel, false); break;
            case INST_HAT_PEDAL:  hat.choke(); hat.trigger(vel * 0.7f, false); break;
            case INST_HAT_OPEN:   hat.trigger(vel, true); break;
            case INST_CRASH:      crash.trigger(vel); break;
            case INST_RIDE:       ride.trigger(vel); break;
            case INST_RIDE_BELL:  ride.trigger(vel); break;
            default: break;
        }
    }

    // Map the two bus knobs onto the shared Compressor and Plate blocks.
    void applyBus() noexcept {
        comp.setBypass(compAmt <= 0.005f);
        // The parallel path's settings are FIXED, and deliberately extreme —
        // its job is to be crushed. The knob controls how much of it is
        // blended in, which is also what keeps this immune to gain staging:
        // there is no threshold that has to line up with the signal's level
        // for the control to behave as labelled.
        comp.setParameter("type", 0.0f);          // VCA, not the 1176
        comp.setParameter("threshold", -38.0f);
        comp.setParameter("ratio", 3.0f);         // 20:1
        comp.setParameter("attack", 8.0f);        // ~2.5 ms, catches the hit
        comp.setParameter("release", 5.0f);       // ~220 ms, breathes with the groove
        comp.setParameter("knee", 3.0f);
        comp.setParameter("makeup", 5.0f);        // +10 dB, so the blend is audible
        comp.setParameter("progRel", 1.0f);
        comp.setParameter("scHP", 0.0f);          // the whole kit drives it

        room.setParameter("type", 0.0f);          // plate tank, used as a room
        room.setParameter("density", 1.0f);
        room.setParameter("mix", roomAmt * 0.60f);
        room.setParameter("decayTime", 0.35f + roomSize * 1.85f);
        room.setParameter("preDelayMs", 8.0f + roomSize * 14.0f);
        room.setParameter("damping", 0.45f);
        room.setParameter("monosum", 1.0f);       // mono insert: correlated wet
    }

    std::vector<float>  scratch, parallel;
    CompressorBlock     comp;
    PlateReverbBlock    room;
    float compAmt{0.0f}, roomAmt{0.0f}, roomSize{0.35f};

    // Voices
    ResynthVoices resynth;

    KickVoice   kick;
    SnareVoice  snare;
    RimVoice    rim;
    TomVoice    tomHi, tomMid, tomFloor;
    HiHatVoice  hat;
    CymbalVoice crash, ride;

    // Mixer.
    //
    // Two separate things, deliberately not merged. kVoiceTrim equalises the
    // voices' RAW synthesis output — measured peak at velocity 127, normalised
    // to about 0.8 — because a modal shell and a filtered noise burst have no
    // reason to come out at the same level. gains[] is then the musical balance
    // a user actually sets, and stays in a sane 0..1 range in the UI instead of
    // carrying calibration constants the player would have to fight.
    //
    // Re-measure with build-tools/practice_render.cpp (the "Voices" section
    // prints peak per voice) after changing any voice's synthesis.
    static constexpr float kVoiceTrim[INST_COUNT] = {
        1.08f,  // kick        measured 0.742
        0.49f,  // snare       measured 1.631
        1.72f,  // sidestick   measured 0.465
        0.79f,  // tom hi      measured 1.016
        0.81f,  // tom mid     measured 0.983
        0.82f,  // tom floor   measured 0.976
        4.60f,  // hat closed  measured 0.168  (one voice serves all three hats)
        4.60f,  // hat pedal
        4.60f,  // hat open
        3.07f,  // crash       measured 0.261
        1.91f,  // ride        measured 0.419
        1.91f,  // ride bell
    };

    // The kit's designed balance. Fixed, because it is part of how the kit
    // sounds, not a user setting: a mixer port whose 0 dB position wiped this
    // out would make every fader default to a kit nobody voiced.
    static constexpr float kBalance[INST_COUNT] = {
        1.00f, 0.90f, 0.70f, 0.80f, 0.80f, 0.85f,
        0.55f, 0.45f, 0.55f, 0.50f, 0.60f, 0.60f
    };

    // User trim ON TOP of the balance. Unity = "as voiced".
    float trims[INST_COUNT] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    bool  muted[INST_COUNT] = {};
    float master{0.45f};

    // Sequencer
    int      patternIdx{0};
    float    swing{0.0f}, humanize{0.0f};
    int64_t  nextScanStep{kNoStep};
    int64_t  clockSamplePos{0};
    int      lastFiredStep{-1};
    Pending  queue[kMaxQueued]{};
    int      queueCount{0};
    Rng      rng{0xD201CE01u};
    double   fs{48000.0};

    // User pattern double buffer
    UserPattern       slots[2];
    std::atomic<bool> slotValid[2]{ {false}, {false} };
    std::atomic<int>  liveSlot{0};
    std::atomic<bool> useUser{false};

    friend class PracticeEngine;
};

} // namespace hexdrums
