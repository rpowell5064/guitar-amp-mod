#pragma once
// Four-track looper for the Practice plugin.
//
// Design notes that matter more than the code:
//
// * ONE master loop length. The first take recorded defines it; every other
//   track conforms. Independent per-track lengths turn a practice looper into
//   a phrase sampler, and make "which track is the bar line" unanswerable.
//
// * Actions are QUANTISED, not immediate. A footswitch press schedules its
//   action for the next bar line from the shared TransportClock, which is what
//   lets you hit record slightly late — as everyone does, with a guitar in
//   both hands — and still get a loop that starts on the beat.
//
// * Loop position is an internal cursor, NOT derived from the clock. The loop
//   is recorded audio: it cannot stretch. Deriving its position from the clock
//   would make a tempo change silently shift the playback point instead of
//   honestly putting the loop out of time with the drums.
//
// * A late press is recovered from a PRE-ROLL, not merely snapped forward.
//   Quantise alone cannot fix hitting record 40 ms after the bar line: the
//   audio from the bar line to the press has already gone past. So the looper
//   keeps a short rolling history of its input and, when a press lands inside
//   the snap-back window, copies the missing head of the bar out of it. Firing
//   "immediately" without that just yields a loop a few tens of milliseconds
//   short, which is exactly the drift it was supposed to prevent.
//
// * The loop seam is crossfaded. Recording stops on a bar line, but the guitar
//   is still ringing there, so the loop would click on every wrap. We keep
//   recording for a few milliseconds past the stop point and fade that
//   overhang into the head of the loop.
#include "TransportClock.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <vector>

namespace hexdrums {

class LooperBlock {
public:
    static constexpr int kNumTracks  = 4;
    static constexpr int kMaxSeconds = 120;    // 2 minutes mono per track
    static constexpr float kSeamMs   = 12.0f;  // loop-wrap crossfade
    static constexpr float kFadeMs   = 8.0f;   // mute / state-change ramp
    static constexpr float kTrimMs   = 4.0f;   // trim-edge ramp
    static constexpr float kPreRollMs = 400.0f; // late-press lookbehind
    static constexpr float kSnapMs    = 250.0f; // hard cap on the snap-back window

    enum class State : uint8_t { Empty, Recording, Overdubbing, Playing, Stopped };
    enum class Action : uint8_t { None, RecordToggle, Play, Stop };

    // ── Lifecycle ────────────────────────────────────────────────────────────
    // Allocates ~23 MB per track at 48 kHz. Called from instantiate(), never
    // from the audio thread.
    void prepare(double sampleRate) noexcept {
        fs       = (sampleRate > 0.0) ? sampleRate : 48000.0;
        capacity = static_cast<int64_t>(fs) * kMaxSeconds;
        seamLen  = static_cast<int64_t>(kSeamMs * 1.0e-3f * fs);
        fadeInc  = 1.0f / std::max(1.0f, kFadeMs * 1.0e-3f * static_cast<float>(fs));
        // Faster than the state fade: a trim edge should be inaudible but
        // must not audibly soften the attack of whatever it reveals.
        trimInc  = 1.0f / std::max(1.0f, kTrimMs * 1.0e-3f * static_cast<float>(fs));

        for (auto& t : tracks) {
            t.buf[0].assign(static_cast<size_t>(capacity), 0.0f);
            t.buf[1].assign(static_cast<size_t>(capacity), 0.0f);
            t.state = State::Empty;
            t.gain  = 0.0f;
        }
        // One shared undo buffer, not one per track: undo is "take back the
        // last overdub", so only the most recent pass ever needs a snapshot.
        undoBuf[0].assign(static_cast<size_t>(capacity), 0.0f);
        undoBuf[1].assign(static_cast<size_t>(capacity), 0.0f);

        preRollLen = static_cast<int64_t>(kPreRollMs * 1.0e-3f * fs);
        preRoll[0].assign(static_cast<size_t>(preRollLen), 0.0f);
        preRoll[1].assign(static_cast<size_t>(preRollLen), 0.0f);
        reset();
    }

    // Clear every track and forget the loop length.
    void reset() noexcept {
        for (auto& t : tracks) {
            std::fill(t.buf[0].begin(), t.buf[0].end(), 0.0f);
            std::fill(t.buf[1].begin(), t.buf[1].end(), 0.0f);
            t.state = State::Empty;
            t.gain = 0.0f; t.targetGain = 0.0f;
            t.recorded = 0;
            t.pending = Action::None;
            t.countingIn = false;
            t.trimIn = 0.0f; t.trimOut = 1.0f; t.trimGain = 1.0f;
        }
        std::fill(preRoll[0].begin(), preRoll[0].end(), 0.0f);
        std::fill(preRoll[1].begin(), preRoll[1].end(), 0.0f);
        preRollPos = 0;
        masterLen  = 0;
        loopCursor = 0;
        barsAtClose = 0;
        sealing    = 0;
        ++waveGen;
        undoTrack  = -1;
        undoReady  = false;
    }

    // ── Parameters ───────────────────────────────────────────────────────────
    void setQuantize(bool on)          noexcept { quantize = on; }
    void setCountIn(bool on)           noexcept { countIn = on; }
    void setCountMute(bool m)          noexcept { countMute = m; }

    // The count-in is one FULL BAR, so it follows the time signature: four
    // beats in four-four, five in five-four, seven in seven-eight. It is not a
    // minimum and not a constant -- whatever this returns is exactly what the
    // player hears and sees, wherever in the bar they pressed record.
    static int countBeats(const TransportClock& clk) noexcept {
        return std::max(1, clk.beatsPerBar());
    }
    // 0 = record until you press again; otherwise the take closes itself
    // after this many bars.
    void setLoopBars(int bars)         noexcept { loopBarsWanted = std::max(0, bars); }
    // Bars the take IN PROGRESS is heading for, or 0. The panel needs this to
    // say "bar 2 of 4" while recording: the loop has no length yet, so there
    // is nothing else to count against, and a bare "bar 2" tells the player
    // nothing about when to expect the take to close.
    int  takeTargetBars() const noexcept {
        for (const auto& tr : tracks)
            if (tr.state == State::Recording)
                return (masterLen > 0) ? barsAtClose : loopBarsWanted;
        return 0;
    }

    // Samples until the counted-in take starts, or 0 when nothing is counting.
    // The plugin turns this into the beats the panel counts down, and into the
    // click on each beat.
    int64_t countInSamplesLeft(const TransportClock& clk) const noexcept {
        int64_t best = 0;
        for (const auto& tr : tracks) {
            if (!tr.countingIn || tr.pending != Action::RecordToggle) continue;
            const int64_t left = tr.applyAt - clk.samplePosition();
            if (left > 0 && (best == 0 || left < best)) best = left;
        }
        return best;
    }
    // Is a take in progress, counted-in or already rolling? The tempo is held
    // across this whole window: the grid must not move underneath a take.
    bool takeInProgress() const noexcept {
        for (const auto& tr : tracks)
            if (tr.state == State::Recording || tr.state == State::Overdubbing) return true;
        return counting();
    }
    bool counting() const noexcept {
        for (const auto& tr : tracks)
            if (tr.countingIn && tr.pending == Action::RecordToggle) return true;
        return false;
    }
    void setFeedback(float f)          noexcept { feedback = std::clamp(f, 0.0f, 1.0f); }
    void setTrackLevel(int t, float g) noexcept { if (valid(t)) tracks[t].level = std::max(0.0f, g); }
    void setTrackMuted(int t, bool m)  noexcept { if (valid(t)) tracks[t].muted = m; }
    // Non-destructive trim: the audio is untouched and the track simply plays
    // nothing outside [in, out). Widening the window brings it straight back,
    // which is why this needs no undo and can never lose a take.
    //
    // The content does NOT slide when the start moves. Trimming is a gate, not
    // a nudge: sliding it would shift the track against the bar grid and put
    // it out of step with the drums, which is the one thing a looper must not
    // do quietly.
    void setTrackTrim(int t, float in, float out) noexcept {
        if (!valid(t)) return;
        tracks[t].trimIn  = std::clamp(in,  0.0f, 1.0f);
        tracks[t].trimOut = std::clamp(out, 0.0f, 1.0f);
    }
    void setMasterLevel(float g)       noexcept { masterLevel = std::max(0.0f, g); }

    // Return every track to the top of the loop. Called when the transport is
    // restarted, so the loop and the drums both begin at bar one.
    void rewind() noexcept { loopCursor = 0; }

    // ── Persistence ──────────────────────────────────────────────────────────
    // The loop is the one thing in this plugin the user cannot rebuild from a
    // number: tempo, groove and trim all come back from control ports, but a
    // take is a performance. Saving it is what makes "reload the pedalboard"
    // stop meaning "lose your work".
    //
    // Stored as 16-BIT, not float. A take peaks at or below unity and the
    // quantisation floor lands near -90 dBFS, which is below the noise of
    // anything that reaches this plugin through a guitar amp; halving the size
    // matters more, because a two-minute take is 23 MB per track as floats and
    // the host writes this into every pedalboard save.

    int64_t loopLengthForSave() const noexcept { return masterLen; }

    // True when a track holds audio worth keeping. A take still being recorded
    // is deliberately NOT saved: the host calls save from another thread, so
    // the buffer would be read while the audio thread is still writing it, and
    // a torn half-take is worse than no take.
    bool trackSaveable(int t) const noexcept {
        return valid(t) && masterLen > 0 &&
               (tracks[t].state == State::Playing || tracks[t].state == State::Stopped);
    }

    // Saved INTERLEAVED (L,R,L,R...), so a take is one contiguous run and the
    // size is simply masterLen * 2. The state blob carries a version that says
    // which layout it is; see practice_save/practice_restore.
    void saveTrack(int t, int16_t* dst) const noexcept {
        if (!valid(t) || !dst) return;
        const float* srcL = tracks[t].buf[0].data();
        const float* srcR = tracks[t].buf[1].data();
        for (int64_t i = 0; i < masterLen; ++i) {
            const float l = std::clamp(srcL[i], -1.0f, 1.0f);
            const float r = std::clamp(srcR[i], -1.0f, 1.0f);
            dst[i * 2]     = static_cast<int16_t>(std::lrint(l * 32767.0f));
            dst[i * 2 + 1] = static_cast<int16_t>(std::lrint(r * 32767.0f));
        }
    }

    // Restore is called from the HOST's thread, not the audio thread. `frozen`
    // makes process() produce nothing and touch no track while a load is in
    // flight, so a host that restores while running gets silence rather than a
    // half-written buffer.
    void loadBegin() noexcept {
        frozen.store(true, std::memory_order_release);
        reset();
    }
    // `stereo` says how `src` is laid out: interleaved pairs, or a single mono
    // run written by a version of this plugin that had one channel. A mono take
    // is loaded into BOTH sides, which is exactly what it sounded like then.
    void loadTrack(int t, const int16_t* src, int64_t len, int stateCode,
                   bool stereo = true) noexcept {
        if (!valid(t) || !src || len <= 0 || len > capacity) return;
        float* dstL = tracks[t].buf[0].data();
        float* dstR = tracks[t].buf[1].data();
        for (int64_t i = 0; i < len; ++i) {
            if (stereo) {
                dstL[i] = float(src[i * 2])     * (1.0f / 32767.0f);
                dstR[i] = float(src[i * 2 + 1]) * (1.0f / 32767.0f);
            } else {
                dstL[i] = dstR[i] = float(src[i]) * (1.0f / 32767.0f);
            }
        }
        // Restored takes come back STOPPED, never playing. Loading a pedalboard
        // should not start making noise on its own.
        tracks[t].state      = State::Stopped;
        tracks[t].targetGain = 0.0f;
        tracks[t].gain       = 0.0f;
        tracks[t].recorded   = len;
        (void)stateCode;
    }
    // `bars` is what the take measured when it was SAVED. The blob does not
    // carry the tempo, so the caller works it out from the restored tempo port
    // -- which is the same board state that recorded it.
    void loadEnd(int64_t len, int bars = 0) noexcept {
        masterLen   = std::clamp<int64_t>(len, 0, capacity);
        barsAtClose = std::max(0, bars);
        loopCursor  = 0;
        ++waveGen;
        frozen.store(false, std::memory_order_release);
    }

    // ── Transport actions ────────────────────────────────────────────────────
    // Each schedules for the next bar line when quantise is on. A press on a
    // track with an action already pending REPLACES it, so double-tapping a
    // footswitch doesn't queue two conflicting state changes.
    void recordPressed(int t, const TransportClock& clk) noexcept { schedule(t, Action::RecordToggle, clk); }
    void playPressed(int t, const TransportClock& clk)   noexcept { schedule(t, Action::Play, clk); }
    void stopPressed(int t, const TransportClock& clk)   noexcept { schedule(t, Action::Stop, clk); }
    // Stop and Play are TRANSPORT commands, not per-track ones: "play" means
    // the looper plays, not that one lane of four does. Use the per-track mutes
    // to pick what you hear -- that is what they are for, and it does not lose
    // your place in the others.
    // STOP IS IMMEDIATE. Not quantised: a stop that waits for the next bar line
    // is up to a bar of the thing you just asked to stop, which reads as the
    // button not working. Everything else here is quantised because it has to
    // land in time musically; stopping does not -- you have stopped playing.
    // The gain still ramps (kFadeMs), so immediate means "this block", not a
    // click.
    void stopAllPressed(const TransportClock& clk) noexcept {
        for (int t = 0; t < kNumTracks; ++t) {
            tracks[t].pending    = Action::Stop;
            tracks[t].barLen     = static_cast<int64_t>(clk.samplesPerBar());
            tracks[t].applyAt    = clk.samplePosition();
            tracks[t].lateBy     = 0;
            // A take being counted in is cancelled outright: there is nothing
            // to stop yet, and leaving the count armed would start recording
            // a bar after the player pressed stop.
            tracks[t].countingIn = false;
        }
    }
    // PLAY starts everything together, from the top, now. Three reasons it
    // cannot just un-stop the tracks: the loop cursor keeps advancing while
    // the looper is stopped, so resuming would drop you in mid-phrase; the
    // groove restarts from bar one, so the two would be out of phase with each
    // other; and quantising the press means up to a bar of silence after
    // asking for sound. Same contract as a take: the press IS bar one.
    void playAllPressed(const TransportClock& clk) noexcept {
        for (int t = 0; t < kNumTracks; ++t) {
            tracks[t].pending    = Action::Play;
            tracks[t].barLen     = static_cast<int64_t>(clk.samplesPerBar());
            tracks[t].applyAt    = clk.samplePosition();
            tracks[t].lateBy     = 0;
            tracks[t].countingIn = false;
        }
        loopCursor = 0;
        restartReq.store(true, std::memory_order_release);
    }

    // Raised when a take begins, so the plugin can put the groove back to its
    // first step at the same instant. The looper cannot reach the drums itself.
    // True once per loop wrap. The plugin re-origins the grid and the groove
    // on it, so "in time at the start" becomes "in time, still, an hour later".
    bool consumeWrap() noexcept {
        return wrapReq.exchange(false, std::memory_order_acq_rel);
    }

    // Jump the loop to a fraction of its length. The player picking where to
    // come in is a normal thing to want and the loop is the only clock that
    // matters once one exists.
    void seekTo(float frac) noexcept {
        if (masterLen <= 0) return;
        const float f = std::clamp(frac, 0.0f, 0.9999f);
        loopCursor = static_cast<int64_t>(f * static_cast<float>(masterLen));
        if (loopCursor >= masterLen) loopCursor = masterLen - 1;
    }

    bool consumeRestartRequest() noexcept {
        return restartReq.exchange(false, std::memory_order_acq_rel);
    }

    // Destructive and immediate — clearing is never something you want to land
    // a bar later, and it is always deliberate (a long press on the hardware).
    void clearTrack(int t) noexcept {
        if (!valid(t)) return;
        std::fill(tracks[t].buf[0].begin(), tracks[t].buf[0].end(), 0.0f);
        std::fill(tracks[t].buf[1].begin(), tracks[t].buf[1].end(), 0.0f);
        tracks[t].state      = State::Empty;
        tracks[t].targetGain = 0.0f;
        tracks[t].recorded   = 0;
        tracks[t].lateBy     = 0;
        tracks[t].pending    = Action::None;
        tracks[t].countingIn = false;
        tracks[t].trimIn     = 0.0f;
        tracks[t].trimOut    = 1.0f;
        tracks[t].trimGain   = 1.0f;
        if (undoTrack == t) { undoReady = false; undoTrack = -1; }
        ++waveGen;
        // The last track standing takes the loop length with it.
        if (allEmpty()) { masterLen = 0; loopCursor = 0; barsAtClose = 0; }
    }

    void clearAll() noexcept { reset(); }

    // Swap the live buffer with the pre-overdub snapshot. Doing it as a SWAP
    // rather than a copy makes undo its own redo, which is what the second
    // press of an undo footswitch should do.
    void undo(int t) noexcept {
        if (!valid(t) || !undoReady || undoTrack != t || masterLen <= 0) return;
        for (int c = 0; c < 2; ++c) {
            float* a = tracks[t].buf[c].data();
            float* b = undoBuf[c].data();
            for (int64_t i = 0; i < masterLen; ++i) std::swap(a[i], b[i]);
        }
        ++waveGen;
    }

    // Snapshot a track for undo. This is a ~23 MB memcpy, so it must NOT run on
    // the audio thread: the plugin schedules it on the LV2 worker when an
    // overdub is ARMED, which quantisation guarantees is at least a fraction of
    // a bar before the overdub actually begins.
    void snapshotForUndo(int t) noexcept {
        if (!valid(t) || masterLen <= 0) return;
        // If an overdub begins while this copy is in flight, the snapshot is a
        // torn mix of before and after. Detecting that and declining is far
        // better than offering an undo that restores the wrong audio.
        const uint32_t gen0 = overdubGen.load(std::memory_order_acquire);
        for (int c = 0; c < 2; ++c)
            std::memcpy(undoBuf[c].data(), tracks[t].buf[c].data(),
                        static_cast<size_t>(masterLen) * sizeof(float));
        if (overdubGen.load(std::memory_order_acquire) != gen0) {
            undoReady = false; undoTrack = -1;
            return;
        }
        undoTrack = t;
        undoReady = true;
    }

    // True when the given track's next RecordToggle would start an overdub, so
    // the plugin knows to schedule a snapshot.
    bool wouldOverdub(int t) const noexcept {
        return valid(t) && (tracks[t].state == State::Playing || tracks[t].state == State::Stopped)
               && masterLen > 0;
    }

    // ── Queries (for the UI) ─────────────────────────────────────────────────
    State   trackState(int t)  const noexcept { return valid(t) ? tracks[t].state : State::Empty; }
    // Which bar is playing, or being recorded, 1-based. 0 when there is
    // nothing to count. The panel needs this to tell the player where they
    // are in the phrase; counting bars from the loop PROGRESS alone cannot do
    // it during the take that is still defining the loop, because the loop has
    // no length yet -- that take has to count the bars it has actually filled.
    int currentBar(const TransportClock& clk) const noexcept {
        const double spb = clk.samplesPerBar();
        if (spb <= 0.0) return 0;
        for (const auto& tr : tracks) {
            if (tr.state != State::Recording) continue;
            if (masterLen > 0) break;          // length known: fall through to the loop count
            return static_cast<int>(tr.recorded / spb) + 1;
        }
        if (masterLen <= 0) return 0;
        return static_cast<int>(loopCursor / spb) + 1;
    }
    bool    hasLoop()          const noexcept { return masterLen > 0; }
    int64_t loopLength()       const noexcept { return masterLen; }
    int64_t loopPosition()     const noexcept { return loopCursor; }
    float   loopProgress()     const noexcept {
        return (masterLen > 0) ? static_cast<float>(loopCursor) / static_cast<float>(masterLen) : 0.0f;
    }
    bool    undoAvailable()    const noexcept { return undoReady; }
    // Bars the loop spans at the clock's current tempo — how the UI labels it.
    // Bars in the loop. Taken from what the take MEASURED when it closed,
    // not recomputed from the current tempo -- otherwise turning the tempo knob
    // appears to change the length of audio that cannot change.
    int     loopBars(const TransportClock& clk) const noexcept {
        if (barsAtClose > 0) return barsAtClose;
        const double spb = clk.samplesPerBar();
        return (masterLen > 0 && spb > 0.0)
             ? static_cast<int>(std::lround(masterLen / spb)) : 0;
    }

    // The tempo the loop itself implies, or 0 when there is no loop. Once a
    // take exists this is the only tempo that keeps the groove with the tracks:
    // the audio is fixed, so anything else is the drums disagreeing with it.
    double loopTempo(double fs, int beatsPerBar) const noexcept {
        if (masterLen <= 0 || barsAtClose <= 0 || beatsPerBar <= 0 || fs <= 0.0) return 0.0;
        const double seconds = double(masterLen) / fs;
        if (seconds <= 0.0) return 0.0;
        return (double(barsAtClose) * double(beatsPerBar) * 60.0) / seconds;
    }

    // ── Waveform for the UI ──────────────────────────────────────────────────
    // A control port cannot carry a waveform, so the UI gets a peak envelope
    // pushed to it as a string instead. Peaks, not RMS: a DAW waveform is a
    // peak envelope, and RMS would hide exactly the transients a player is
    // looking for when they line up an edit.
    //
    // `out` receives `n` values in 0..1. Returns false when the track has
    // nothing to draw.
    bool trackPeaks(int t, float* out, int n) const noexcept {
        if (!valid(t) || masterLen <= 0 || n <= 0) return false;
        if (tracks[t].state == State::Empty) return false;

        // Drawn from whichever side is louder at each point: a lane that
        // showed only the left would look empty for anything panned hard right.
        const float* bufL = tracks[t].buf[0].data();
        const float* bufR = tracks[t].buf[1].data();
        const int64_t span = masterLen;
        for (int i = 0; i < n; ++i) {
            const int64_t a = span * i / n;
            const int64_t b = std::max(a + 1, span * (i + 1) / n);
            float pk = 0.0f;
            // Stride large loops: a two-minute take is 5.8 M samples and the
            // UI asks for a few hundred pixels, so reading every sample would
            // cost far more than the picture is worth.
            const int64_t step = std::max<int64_t>(1, (b - a) / 64);
            for (int64_t k = a; k < b; k += step)
                pk = std::max(pk, std::max(std::fabs(bufL[k]), std::fabs(bufR[k])));
            out[i] = std::min(1.0f, pk);
        }
        return true;
    }

    // Bumped whenever a track's audio changes, so the UI can redraw only when
    // there is something new rather than polling a megabyte of peaks.
    uint32_t waveformVersion() const noexcept { return waveGen; }

    // ── Render ───────────────────────────────────────────────────────────────
    // Records from `in` and ADDS loop playback into `out`. `in` and `out` may
    // be the same buffer: the plugin passes the guitar through separately, so
    // this block only ever adds its own playback on top.
    // inR/outR may be null: a mono host, or a mono source feeding a stereo
    // looper. Both channels are still recorded, so a loop taken while the
    // chain was mono still plays back correctly once it is not.
    void process(const TransportClock& clk, const float* inL, const float* inR,
                 float* outL, float* outR, int n) noexcept {
        if (n <= 0 || capacity <= 0) return;
        // A state load is rewriting the buffers from the host's thread. Produce
        // nothing and read nothing until it has finished.
        if (frozen.load(std::memory_order_acquire)) return;

        clockRunning = clk.running();

        int cursor = 0;
        while (cursor < n) {
            // Run up to the next scheduled action, so state changes land on the
            // exact sample the bar line falls on rather than at a block edge.
            int segEnd = n;
            for (int t = 0; t < kNumTracks; ++t) {
                if (tracks[t].pending == Action::None) continue;
                const int64_t rel = tracks[t].applyAt - clk.samplePosition();
                if (rel > cursor && rel < segEnd) segEnd = static_cast<int>(rel);
            }

            // Apply everything due exactly at `cursor`.
            for (int t = 0; t < kNumTracks; ++t) {
                if (tracks[t].pending == Action::None) continue;
                const int64_t rel = tracks[t].applyAt - clk.samplePosition();
                if (rel <= cursor) applyAction(t);
            }

            renderSegment(inL, inR, outL, outR, cursor, segEnd);
            cursor = segEnd;
        }
    }

private:
    struct Track {
        // One buffer per channel. The looper sits after the amp and cab,
        // so what reaches it is whatever stereo the chain made -- a mono
        // loop would throw that away at the one point it is worth keeping.
        std::vector<float> buf[2];
        State   state{State::Empty};
        Action  pending{Action::None};
        int64_t applyAt{0};
        int64_t recorded{0};      // samples captured on the take in progress
        int64_t lateBy{0};        // samples this action landed past its bar line
        int64_t targetLen{0};     // 0 = open-ended; else close the take here
        int64_t barLen{0};        // bar length in samples when the action was armed
        float   level{1.0f};
        float   gain{0.0f}, targetGain{0.0f};
        // Trim window, as FRACTIONS of the master loop. Fractions rather than
        // samples so a trim survives the loop length being set later, and so
        // the control means the same thing whatever the tempo.
        float   trimIn{0.0f}, trimOut{1.0f};
        float   trimGain{1.0f};          // ramped, so an edge never clicks
        bool    muted{false};
        bool    countingIn{false};  // this take is waiting out a count-in
    };

    static bool inRange(int t) noexcept { return t >= 0 && t < kNumTracks; }
    bool valid(int t) const noexcept { return inRange(t); }

    bool allEmpty() const noexcept {
        for (const auto& t : tracks) if (t.state != State::Empty) return false;
        return true;
    }

    void schedule(int t, Action a, const TransportClock& clk) noexcept {
        if (!valid(t)) return;
        tracks[t].pending = a;
        tracks[t].barLen  = static_cast<int64_t>(clk.samplesPerBar());
        // Free mode, or a stopped transport (nothing to quantise TO), acts now.
        if (!quantize || !clk.running()) {
            tracks[t].applyAt = clk.samplePosition();
            return;
        }
        const double spb   = clk.samplesPerBar();
        const double toBar = clk.samplesToNextBar();

        // COUNT-IN. Only for the take that DEFINES the loop: once a loop
        // exists you can hear where beat one is, and counting in over the top
        // of it would just be in the way.
        //
        // The count starts IMMEDIATELY and runs to the first bar line at least
        // a bar away, so the take still begins exactly on a bar line and stays
        // in step with the drums. An earlier version waited out the current
        // bar in silence before counting, which at 72 bpm meant staring at
        // nothing for up to seven seconds before the first number appeared.
        // Everything from the press onwards is now part of the count: it is
        // visible and clicking from the instant the button goes down.
        // Count in before ANY take that starts recording, not just the one that
        // sets the loop length. A press that CLOSES a take is excluded: that is
        // a stop, and counting into it would be nonsense.
        const bool startsRecording = (tracks[t].state == State::Empty    ||
                                      tracks[t].state == State::Playing  ||
                                      tracks[t].state == State::Stopped);
        if (a == Action::RecordToggle && countIn && startsRecording) {
            const double spBeat = clk.samplesPerBeat();
            // A WHOLE BAR of count, every time: "4 3 2 1" in four-four,
            // "5 4 3 2 1" in five-four. The count is aligned to the next BEAT,
            // not to a bar line. It used to wait out the bar with half a beat
            // of slack, so what you got depended on where in the bar you
            // happened to press -- a press just after a downbeat counted three
            // and a half, and the panel showed three. There is nothing left to
            // meet on the old grid anyway: a take now rewinds the loop and
            // restarts the groove when it begins, so the take IS the bar line.
            // A press a hair AFTER a beat was meant for THAT beat. Without this
            // the count waits out most of a beat before its first click, while
            // the panel already shows the full number -- so the first count
            // lasts nearly twice as long as the rest and the whole thing reads
            // as an extra beat. Snapping back makes the first click immediate.
            // The count starts ON a beat, like a drummer counting you in. Only
            // a press that landed a HAIR after one is taken as belonging to it
            // -- a quarter beat, so the first click is never more than that off
            // the grid. Snapping to the nearest beat instead was tried and is
            // wrong: a press half a beat late drags the first click half a beat
            // off, and a count whose own first click is out of time is exactly
            // the thing that "feels off".
            double toBeat = clk.samplesToNextBeat();
            if (toBeat > 0.75 * spBeat) toBeat -= spBeat;   // negative: just gone
            const double wait = toBeat + countBeats(clk) * spBeat;
            tracks[t].applyAt    = clk.samplePosition() + static_cast<int64_t>(wait);
            tracks[t].lateBy     = 0;
            tracks[t].countingIn = true;
            return;
        }
        // How far past the bar line just gone this press landed.
        const double sinceBar = (toBar <= 0.0) ? 0.0 : spb - toBar;

        // A press a hair after the bar line was meant for THAT line, not the
        // next one. The window is capped in real time as well as in bars: at a
        // slow tempo a bar is long enough that 9% of it would exceed the
        // pre-roll we can actually recover from.
        const double window = std::min(0.09 * spb, kSnapMs * 1.0e-3 * fs);

        if (sinceBar <= window) {
            tracks[t].applyAt = clk.samplePosition();
            tracks[t].lateBy  = std::min(static_cast<int64_t>(sinceBar), preRollLen);
        } else {
            tracks[t].applyAt = clk.samplePosition() + static_cast<int64_t>(toBar);
            tracks[t].lateBy  = 0;
        }
    }

    void applyAction(int t) noexcept {
        Track& tr = tracks[t];
        const Action a = tr.pending;
        tr.pending = Action::None;
        tr.countingIn = false;

        switch (a) {
            case Action::RecordToggle: recordToggle(t); break;
            case Action::Play:
                if (tr.state == State::Recording || tr.state == State::Overdubbing) closeRecording(t);
                if (tr.state != State::Empty) { tr.state = State::Playing; tr.targetGain = 1.0f; }
                break;
            case Action::Stop:
                if (tr.state == State::Recording || tr.state == State::Overdubbing) closeRecording(t);
                if (tr.state != State::Empty) { tr.state = State::Stopped; tr.targetGain = 0.0f; }
                break;
            default: break;
        }
    }

    void recordToggle(int t) noexcept {
        Track& tr = tracks[t];
        // EVERY take starts from the top, whatever it is recording onto: a
        // fresh track, or an overdub on one that already has audio. The rewind
        // and the groove restart used to live inside the Empty case only, so
        // overdubbing -- the common case once there is anything to play along
        // to -- dropped you in wherever the cursor happened to be, with the
        // drums mid-pattern. The whole looper moves together or the count-in
        // is counting you in to the middle of a phrase.
        if (tr.state == State::Empty || tr.state == State::Playing ||
            tr.state == State::Stopped) {
            loopCursor = 0;
            restartReq.store(true, std::memory_order_release);
            // And bring the REST of the looper back with it. Starting a take
            // after a stop used to arm only the track being recorded, so the
            // new part was played over silence -- you were overdubbing onto a
            // looper that had nothing playing. Anything with audio on it comes
            // back; MUTE is how you choose not to hear a track, and it is left
            // alone here because that is a decision the player already made.
            for (int o = 0; o < kNumTracks; ++o) {
                if (o == t) continue;
                Track& ot = tracks[o];
                if (ot.state == State::Stopped) {
                    ot.state      = State::Playing;
                    ot.targetGain = 1.0f;
                }
            }
        }
        switch (tr.state) {
            case State::Empty: {
                tr.recorded   = 0;        // counts THIS take, master or punch-in
                tr.state      = State::Recording;
                // Freeze the target NOW: changing the length control halfway
                // through a take must not retune the take already running.
                tr.targetLen  = (loopBarsWanted > 0 && masterLen == 0)
                              ? static_cast<int64_t>(loopBarsWanted) * tr.barLen
                              : 0;
                tr.targetGain = 1.0f;
                // The first take also defines where loop position zero is.
                if (masterLen == 0) loopCursor = 0;
                // Recover the head of the bar the player already played over.
                fillFromPreRoll(t, tr.lateBy);
                break;
            }

            case State::Recording:
            case State::Overdubbing:
                closeRecording(t);
                tr.state      = State::Playing;
                tr.targetGain = 1.0f;
                break;

            case State::Playing:
            case State::Stopped:
                tr.state      = State::Overdubbing;
                tr.targetGain = 1.0f;
                overdubGen.fetch_add(1, std::memory_order_release);
                ++waveGen;
                break;
        }
    }

    // End a take. The FIRST take to close sets the master loop length and opens
    // the seam window; later takes simply stop adding.
    void closeRecording(int t) noexcept {
        Track& tr = tracks[t];
        if (masterLen != 0 || tr.state != State::Recording) return;

        // The take began on a bar line and ran to `lateBy` past the closing
        // one, so the loop is everything except that overshoot.
        int64_t len = tr.recorded - tr.lateBy;
        len = std::clamp(len, seamLen * 2, capacity - seamLen);
        masterLen = len;
        // Remember how long the take was IN BARS. tr.barLen is the bar length
        // captured when this take was SCHEDULED, which is the tempo the player
        // actually recorded against -- deriving it from the current tempo would
        // make the answer change later, which is the whole bug.
        barsAtClose = (tr.barLen > 0)
                    ? int(std::lround(double(len) / double(tr.barLen))) : 0;

        // The overshoot IS the seam material: it is the player still ringing
        // over the top of bar one. Fold whatever we already captured, and only
        // keep recording live for the part we are short of.
        const int64_t have = std::min(tr.lateBy, seamLen);
        for (int64_t i = 0; i < have; ++i) {
            const float w = static_cast<float>(i) / static_cast<float>(seamLen);
            for (int c = 0; c < 2; ++c) {
                float& head = tr.buf[c][static_cast<size_t>(i)];
                head = head * w + tr.buf[c][static_cast<size_t>(masterLen + i)] * (1.0f - w);
            }
        }

        if (have < seamLen) {
            sealing    = seamLen;
            sealTrack  = t;
            sealCursor = have;
        } else {
            sealing   = 0;
            sealTrack = -1;
        }

        // Playback resumes where we actually are: `lateBy` past the bar line.
        loopCursor = (masterLen > 0) ? (tr.lateBy % masterLen) : 0;
        ++waveGen;
    }

    // Copy the last `count` samples of input history into the take, so a take
    // that was started late still begins on the bar line.
    void fillFromPreRoll(int t, int64_t count) noexcept {
        count = std::min(count, preRollLen);
        if (count <= 0) return;
        Track& tr = tracks[t];

        for (int64_t i = 0; i < count; ++i) {
            // preRollPos is the next slot to be written, so the oldest sample
            // we want sits `count` slots behind it.
            int64_t src = preRollPos - count + i;
            src %= preRollLen;
            if (src < 0) src += preRollLen;

            int64_t dst;
            if (masterLen == 0) {
                dst = i;                                   // master take: linear
            } else {
                dst = loopCursor - count + i;              // punch-in: wraps
                dst %= masterLen;
                if (dst < 0) dst += masterLen;
            }
            if (dst >= 0 && dst < capacity)
                tr.buf[0][static_cast<size_t>(dst)] = preRoll[0][static_cast<size_t>(src)];
                tr.buf[1][static_cast<size_t>(dst)] = preRoll[1][static_cast<size_t>(src)];
        }
        tr.recorded = count;
    }

    void renderSegment(const float* inL, const float* inR,
                       float* outL, float* outR, int from, int to) noexcept {
        const int len = to - from;
        if (len <= 0) return;

        for (int i = from; i < to; ++i) {
            const float xL = inL ? inL[i] : 0.0f;
            const float xR = inR ? inR[i] : xL;      // mono source: both sides alike
            float mixL = 0.0f, mixR = 0.0f;

            // Rolling input history for late-press recovery. Written for every
            // sample regardless of state: the whole point is having audio from
            // before anything was armed.
            preRoll[0][static_cast<size_t>(preRollPos)] = xL;
            preRoll[1][static_cast<size_t>(preRollPos)] = xR;
            if (++preRollPos >= preRollLen) preRollPos = 0;

            for (int t = 0; t < kNumTracks; ++t) {
                Track& tr = tracks[t];

                // Smooth every gain change: state transitions and mutes land on
                // arbitrary samples and would otherwise click.
                // countMute silences the whole looper for the duration of a
                // count-in: the click is the only thing that should be audible
                // while the player is counted in, and existing loops playing
                // underneath are exactly the pulse the count is there to
                // replace. It rides the same ramp as a mute, so it cannot
                // click, and nothing is recording yet.
                const float want = ((tr.muted || countMute) ? 0.0f : tr.targetGain);
                if (tr.gain < want)      tr.gain = std::min(want, tr.gain + fadeInc);
                else if (tr.gain > want) tr.gain = std::max(want, tr.gain - fadeInc);

                // Trim gate. Ramped over a few milliseconds: a hard gate on an
                // arbitrary sample of a ringing guitar is a click, and the
                // whole point of trimming the head of a loop is to remove a
                // noise, not to swap it for a different one.
                float tw = 1.0f;
                if (masterLen > 0 && tr.trimOut > tr.trimIn) {
                    const int64_t lo = static_cast<int64_t>(tr.trimIn  * float(masterLen));
                    const int64_t hi = (tr.trimOut >= 0.9995f)
                                     ? masterLen
                                     : static_cast<int64_t>(tr.trimOut * float(masterLen));
                    if (loopCursor < lo || loopCursor >= hi) tw = 0.0f;
                }
                if (tr.trimGain < tw)      tr.trimGain = std::min(tw, tr.trimGain + trimInc);
                else if (tr.trimGain > tw) tr.trimGain = std::max(tw, tr.trimGain - trimInc);
                const float trimG = tr.trimGain;

                switch (tr.state) {
                    case State::Recording: {
                        // Before a master length exists the take grows; after
                        // one exists a punch-in wraps with the loop.
                        const int64_t pos = (masterLen > 0) ? loopCursor : tr.recorded;
                        if (pos < capacity) {
                            tr.buf[0][static_cast<size_t>(pos)] = xL;
                            tr.buf[1][static_cast<size_t>(pos)] = xR;
                        }
                        ++tr.recorded;
                        if (masterLen == 0) {
                            // A fixed-length take closes itself on the bar it
                            // was asked for, so you can start a loop and play
                            // into it without a hand free for the footswitch.
                            if (tr.targetLen > 0 && tr.recorded >= tr.targetLen) {
                                closeRecording(t);
                                tr.state      = State::Playing;
                                tr.targetGain = 1.0f;
                            } else if (tr.recorded >= capacity) {   // out of room
                                closeRecording(t);
                                tr.state = State::Playing;
                            }
                        } else if (tr.recorded >= masterLen) {
                            // A full lap is captured regardless of where in the
                            // loop the punch-in started.
                            tr.state      = State::Playing;
                            tr.targetGain = 1.0f;
                            // The take that SETS the loop bumps this inside
                            // closeRecording(), which returns early once a
                            // master length exists -- so without this line
                            // tracks 2-4 finished recording and the editor was
                            // never told, leaving their lanes blank forever.
                            ++waveGen;
                        }
                        break;
                    }
                    case State::Overdubbing: {
                        if (masterLen > 0 && loopCursor < masterLen) {
                            const float g = tr.level * tr.gain * trimG;
                            float& sL = tr.buf[0][static_cast<size_t>(loopCursor)];
                            float& sR = tr.buf[1][static_cast<size_t>(loopCursor)];
                            sL = sL * feedback + xL;
                            sR = sR * feedback + xR;
                            mixL += sL * g;
                            mixR += sR * g;
                        }
                        break;
                    }
                    case State::Playing: {
                        if (masterLen > 0 && loopCursor < masterLen) {
                            const float g = tr.level * tr.gain * trimG;
                            mixL += tr.buf[0][static_cast<size_t>(loopCursor)] * g;
                            mixR += tr.buf[1][static_cast<size_t>(loopCursor)] * g;
                        }
                        break;
                    }
                    case State::Stopped:
                        // Still fading out from the last ramp; keep feeding it
                        // so the stop isn't a hard cut.
                        if (tr.gain > 0.0f && masterLen > 0 && loopCursor < masterLen) {
                            const float g = tr.level * tr.gain * trimG;
                            mixL += tr.buf[0][static_cast<size_t>(loopCursor)] * g;
                            mixR += tr.buf[1][static_cast<size_t>(loopCursor)] * g;
                        }
                        break;
                    case State::Empty:
                    default: break;
                }
            }

            // Fold the post-stop overhang into the head of the loop so the wrap
            // is continuous instead of a step.
            if (sealing > 0 && sealTrack >= 0) {
                const float w = static_cast<float>(sealCursor) / static_cast<float>(seamLen);
                float& headL = tracks[sealTrack].buf[0][static_cast<size_t>(sealCursor)];
                float& headR = tracks[sealTrack].buf[1][static_cast<size_t>(sealCursor)];
                headL = headL * w + xL * (1.0f - w);
                headR = headR * w + xR * (1.0f - w);
                if (++sealCursor >= seamLen) { sealing = 0; sealTrack = -1; }
            }

            if (outL) outL[i] += mixL * masterLevel;
            if (outR) outR[i] += mixR * masterLevel;

            // Advance the loop once per sample, shared by every track -- but
            // only while the transport is RUNNING. It used to advance
            // regardless, so after a stop the playhead and the bar counter
            // carried on sweeping through a loop that was not playing, which
            // is a display saying the opposite of what you can hear.
            if (masterLen > 0 && clockRunning) {
                if (++loopCursor >= masterLen) {
                    loopCursor = 0;
                    // The loop wrapped. The groove runs on a CONTINUOUS clock
                    // while the loop is an integer number of samples, so the
                    // two disagree by the rounding every single lap and the
                    // error accumulates -- which is how the drums drift away
                    // from the tracks over a few minutes. Telling the plugin
                    // each wrap lets it put the groove back on the loop's own
                    // downbeat, so the error can never add up.
                    wrapReq.store(true, std::memory_order_release);
                }
            }
        }
    }

    Track   tracks[kNumTracks];
    std::vector<float> undoBuf[2];
    std::vector<float> preRoll[2];
    int64_t preRollLen{0}, preRollPos{0};

    double  fs{48000.0};
    int64_t capacity{0};
    int64_t masterLen{0};
    int64_t loopCursor{0};
    int     barsAtClose{0};   // bars the take measured when it closed
    int64_t seamLen{0};
    int64_t sealing{0}, sealCursor{0};
    int     sealTrack{-1};
    std::atomic<uint32_t> overdubGen{0};
    uint32_t waveGen{0};
    int     undoTrack{-1};
    bool    undoReady{false};
    bool    quantize{true};
    bool  countIn{true};   // a bar of count-in before the take that sets the loop
    bool  countMute{false};// loop playback held silent while a count runs
    bool  clockRunning{false};  // transport state for this block (see process)
    int   loopBarsWanted{0};  // 0 = free; else the take is this many bars
    float   feedback{1.0f};
    float   masterLevel{1.0f};
    float   fadeInc{0.001f};
    float   trimInc{0.0f};
    std::atomic<bool> frozen{false};   // a state load is in flight
    std::atomic<bool> restartReq{false};
    std::atomic<bool> wrapReq{false};   // a take has just begun
};

} // namespace hexdrums
