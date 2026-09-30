#pragma once
// The single timing authority shared by the drum machine and the looper.
//
// Both need to agree on where a bar starts, and they must agree EXACTLY: if
// the looper derived its own bar length from BPM while the sequencer derived
// its own step grid, a rounding difference of a fraction of a sample per bar
// would walk the loop off the groove over a few minutes of practice. So there
// is one clock, the plugin advances it once per block, and both consumers read
// their positions out of it.
//
// Position is carried in BEATS as a double rather than in samples as an
// integer: beats survive a tempo change (the musical position is unchanged
// when you turn the BPM knob), and a double holds beat positions to far better
// than sample accuracy for any session length a practice plugin will ever see.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hexdrums {

class TransportClock {
public:
    void prepare(double sampleRate) noexcept {
        fs = (sampleRate > 0.0) ? sampleRate : 48000.0;
        recalc();
        reset();
    }

    void setTempo(double newBpm) noexcept {
        newBpm = std::clamp(newBpm, 20.0, 300.0);
        if (newBpm != bpm) { bpm = newBpm; recalc(); }
    }
    void setBeatsPerBar(int n) noexcept { beatsPerBar_ = std::clamp(n, 1, 16); }

    void start() noexcept { running_ = true; }
    void stop()  noexcept { running_ = false; }
    void reset() noexcept { beatPos = 0.0; samplePos = 0; }

    bool   running()      const noexcept { return running_; }
    double tempo()        const noexcept { return bpm; }
    int    beatsPerBar()  const noexcept { return beatsPerBar_; }

    double beatsPerSample()  const noexcept { return beatsPerSample_; }
    double samplesPerBeat()  const noexcept { return fs * 60.0 / bpm; }
    double samplesPerBar()   const noexcept { return samplesPerBeat() * beatsPerBar_; }

    // Musical position at the START of the block currently being rendered.
    double  beatPosition()   const noexcept { return beatPos; }
    // Absolute sample position at the start of the block. Monotonic while
    // running; the looper schedules its quantised actions against this.
    int64_t samplePosition() const noexcept { return samplePos; }

    // Bar position, and how far into the current bar we are, in [0, 1).
    double barPosition() const noexcept { return beatPos / beatsPerBar_; }
    double barPhase()    const noexcept {
        const double b = barPosition();
        return b - std::floor(b);
    }

    // Samples from the start of this block until the next bar line. Returns 0
    // when the block starts exactly on one. The looper uses this to decide
    // whether a footswitch press belongs to this bar or the next.
    double samplesToNextBar() const noexcept {
        const double phase = barPhase();
        if (phase <= 0.0) return 0.0;
        return (1.0 - phase) * samplesPerBar();
    }

    // Advance past a rendered block. Called ONCE per run(), after every
    // consumer has read its block-start position.
    void advance(int numSamples) noexcept {
        if (!running_) return;
        beatPos   += beatsPerSample_ * numSamples;
        samplePos += numSamples;
    }

    // Host-transport sync (LV2 time:Position). Snapping the musical position
    // while leaving samplePos monotonic keeps the looper's own scheduling sane
    // across a host locate.
    void setPositionBeats(double beats) noexcept { beatPos = beats; }

private:
    void recalc() noexcept { beatsPerSample_ = bpm / (60.0 * fs); }

    double  fs{48000.0};
    double  bpm{120.0};
    double  beatPos{0.0};
    double  beatsPerSample_{120.0 / (60.0 * 48000.0)};
    int64_t samplePos{0};
    int     beatsPerBar_{4};
    bool    running_{false};
};

} // namespace hexdrums
