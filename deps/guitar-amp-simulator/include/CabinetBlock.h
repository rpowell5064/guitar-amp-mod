#pragma once
#include "AudioBlock.h"
#include "BiquadFilter.h"
#include "OlaConvolver.h"
#include <array>
#include <atomic>
#include <vector>

// Cabinet IR block — partitioned FFT convolution with lock-free IR loading.
//
// Convolution: zero-latency uniform partitioned OLS (see OlaConvolver.h,
// rewritten 2026-07-23) — ~O(irLen/64) complex MACs per sample instead of a
// full-length FFT per block; long user IRs are no longer catastrophic.
//
// Thread safety:
//   setIR()  — message thread.  Writes the new IR to the back slot, then
//               publishes it with a single atomic store (release).  The audio
//               thread never touches the back slot.
//   process() — audio thread.  Loads the front slot index once per block
//               (acquire) and uses that for all channels.  Never blocks.
//
// Limitation: calling setIR() twice before the audio thread has consumed the
// first swap is safe (the second write overwrites the back slot), but the
// first IR change will be skipped.  In practice this requires two IR loads
// within one audio block (~1 ms), which does not happen via normal UI.
class CabinetBlock : public AudioBlock {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) override;
    // Mono-input fast path (2026-07-23): when the caller KNOWS both channels are
    // identical (Hex Forge tracks this), convolve + EQ ONCE and fan out to L/R —
    // the room (the only decorrelating element) still runs per channel, so the
    // result is BIT-IDENTICAL to processing two identical channels at half the
    // convolution/EQ cost. Channel 1's conv/EQ state is reset on the next true
    // stereo call (transitions only happen on user reorder = already clicky).
    void  processMonoToStereo(float* L, float* R, int numSamples) noexcept;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    void setIR(const std::vector<float>& irLeft,
               const std::vector<float>* irRight = nullptr);
    // Clear all running state: convolver tails, room combs, EQ + glue (2026-07-23,
    // seamless switching). IR spectra/coefficients are untouched.
    void reset() noexcept;

private:
    float lowCutHz_  =  80.0f;
    float highCutHz_ = 16000.0f;
    float mix_       =   1.0f;
    // Mic placement (2026-07-14). Post-convolution filter morphs, one-sided from the
    // voiced baseline so 0/0 is BIT-IDENTICAL to before (old presets/saves untouched):
    //   micPos  0 = as-voiced (cap-edge close mic) → 1 = cone EDGE: progressive HF loss,
    //             the 4 kHz bite recedes, a touch more low-mid body.
    //   micDist 0 = as-voiced (close) → 1 = ~30 cm back: proximity bass falls away,
    //             slight HF air loss. Tone-only — loudness parity is preserved elsewhere.
    float micPos_  = 0.0f;
    float micDist_ = 0.0f;
    bool  micActive_ = false;
    // Room ambience (2026-07-14): a compact Schroeder small-room (4 combs + 1 allpass, damped)
    // AFTER the convolution + mic filters — the "amp in a room" layer. Toggleable (off =
    // bit-identical), Mix = wet blend, Amount = room size/decay (~0.12 s tight booth → ~0.45 s
    // live room). Preset-savable like every other cab param.
    bool  roomOn_   = false;
    float roomMix_  = 0.15f;
    float roomAmt_  = 0.35f;
    // Room Density (2026-07-23, opt-in like the reverb's): 0 = the classic 4-comb/
    // 1-allpass room every preset was voiced on (bit-identical), 1 = 6 combs +
    // 2 allpasses — smoother "amp in the room" wash, level-matched.
    bool  roomDense_ = false;
    // Cab Voice (2026-07-22): 0 = Room (bit-identical legacy path), 1 = Studio — the
    // "recorded" sound: a second fixed virtual mic (darker ribbon-at-edge character)
    // blended 35% against the primary mic path, bracketing HPF/LPF (78 Hz / 10.5 kHz),
    // a fixed console curve (-1.5 dB @ 400, +2.2 dB @ 3.2k) and gentle level-invariant
    // bus glue (2:1 toward the signal's own sliding average, <= 3 dB GR). Room ambience
    // is forced OFF in Studio voice (a close-mic'd record is dry; ambience belongs to
    // the mix, not the cab).
    bool  studio_   = false;
    bool  monoActive_ = false;   // processMonoToStereo ran (ch1 conv/EQ state stale)

    // Speaker drive compression (item #40, 2026-07-28): a phenomenological, PRE-
    // convolution model of how a real cone/coil responds to being driven hard —
    // envelope-driven ~1.2:1 program compression + level-dependent LF soft
    // saturation below ~120 Hz (Bl force-factor droop under excursion) + a very
    // slow thermal HF tilt (voice-coil resistance rises under sustained heat,
    // dulling the top). All three are LEVEL-INVARIANT by design (same reasoning
    // as the Studio-voice bus glue below): they react to how loud the signal is
    // relative to its OWN recent/long-term average, never an absolute threshold,
    // since the cab's insertion point can run at very different absolute levels
    // depending on what's upstream (a hot fuzz vs. a clean boost). Off (default)
    // is bit-identical -- the per-channel state below is only touched when on.
    bool  spkDriveOn_  = false;
    float spkDriveAmt_ = 0.5f;   // [0,1] overall depth of all three sub-mechanisms

    static constexpr int kMaxCh    = 2;
    static constexpr int kNumSlots = 2;  // double-buffer: front / back

    // Double-buffered convolvers: audio thread reads [frontSlot_][ch],
    // message thread writes [1 - frontSlot_][ch] then swaps the index.
    OlaConvolver            convolvers_[kNumSlots][kMaxCh];
    std::atomic<int>        frontSlot_{0};

    // Stored raw IR for re-applying when prepare() is called (e.g. sample-rate change).
    struct StoredIR {
        std::vector<float> ch[kMaxCh];
        bool valid = false;
    } storedIR_;

    // Scratch buffer for capturing the dry signal before in-place convolution.
    std::vector<float> dryBuf_;

    // Per-channel post-convolution EQ (audio thread only).
    struct EQState {
        BiquadFilter lowCut;
        BiquadFilter highCut;
        BiquadFilter micLP;      // mic position: off-axis/cone-edge HF loss
        BiquadFilter micBite;    // mic position: 4 kHz bite recedes toward the edge
        BiquadFilter micBody;    // mic position: slight low-mid gain off-center
        BiquadFilter distProx;   // mic distance: proximity bass falls away
        BiquadFilter distAir;    // mic distance: HF air loss with distance
        // Studio voice (fixed coefficients; see studio_ comment above)
        BiquadFilter mic2LP, mic2Bite, mic2Body;   // second virtual mic (ribbon/edge)
        BiquadFilter stHP, stLP;                   // bracketing filters
        BiquadFilter conA, conB;                   // console curve
        float compEnv = 0.0f;                      // bus glue: fast envelope
        float compRef = 0.0f;                      // bus glue: slow sliding reference
    };
    std::array<EQState, kMaxCh> eqState_;

    // Item #40 pre-convolution speaker-drive state (per channel).
    struct SpkDriveState {
        float compEnv = 0.0f, compRef = 0.0f;      // 1.2:1 program compression (fast vs ~1.5s ref)
        BiquadFilter lfSplit;                       // ~120 Hz LP: isolates the LF band
        float lfFastEnv = 0.0f, lfRef = 0.0f;       // LF band level vs its own slower reference
        BiquadFilter hfShelf;                       // fixed high-shelf cut (max thermal-hot tilt)
        float thermalEnv = 0.0f, thermalRef = 0.0f; // seconds-scale envelope vs tens-of-seconds baseline
    };
    std::array<SpkDriveState, kMaxCh> spkState_;
    // Coefficients (set in prepare()): reuses compAtt_/compRel_/compSlow_ (30 ms /
    // 180 ms / 1.5 s, already tuned for the Studio-voice glue) for the program
    // compressor; the LF-band and thermal envelopes get their own, purpose-scaled
    // time constants below.
    float spkLfAtt_ = 0.0f, spkLfRel_ = 0.0f, spkLfRefCoef_ = 0.0f;
    float spkThAtt_ = 0.0f, spkThRel_ = 0.0f, spkThRefCoef_ = 0.0f;
    // One sample of the speaker-drive chain (in place, pre-convolution).
    float spkDriveTick(SpkDriveState& s, float x) noexcept;

    // Per-channel small-room state (short prime-spaced combs + one allpass).
    struct RoomState {   // sized for Dense (6 combs + 2 APs); Classic uses the first 4 / 1
        static constexpr int kCombs = 6;
        static constexpr int kClassicCombs = 4;
        std::vector<float> comb[kCombs];
        std::vector<float> ap, ap2;
        int   cw[kCombs] = {};
        int   aw = 0, aw2 = 0;
        int   clen[kCombs] = {1,1,1,1,1,1};   // active delay lengths (follow roomAmt_)
        int   alen = 1, alen2 = 1;
        float damp[kCombs] = {};              // per-comb damping LP state
    };
    std::array<RoomState, kMaxCh> room_;
    float roomFb_ = 0.6f;
    void  rebuildRoom();
    float roomTick(RoomState& rs, float x) noexcept;

    float compAtt_ = 0.0f, compRel_ = 0.0f, compSlow_ = 0.0f;   // bus glue coeffs (set in prepare)

    void rebuildEQ();
    void loadIRIntoSlot(int slot);
};
