// ─────────────────────────────────────────────────────────────────────────────
// Hex Chain — Hex Forge LV2 plugin
//
// A single plugin that hosts the entire Hex Chain as a prewired, reorderable
// pedalboard. It owns one instance of every Hex Chain DSP block and runs them in
// series. Input Trim is locked at the head of the chain; the other nine blocks
// each carry a `pos` (slot 1..9) control so the GUI can reorder them, and a
// `bypass` control so each can be switched in/out. Disabled blocks are skipped
// entirely (true passthrough — zero CPU), so an all-off Hex Forge is unity.
//
// Stereo bus with guitar-friendly mono semantics: the signal is mono until a
// stereo block (amp/cab/mod/delay/reverb) spreads it; a mono block (gate/comp/
// fuzz/drive/input-trim) that sits after a stereo block collapses L+R to mono
// first, exactly like a real mono pedal placed after a stereo effect.
//
// Worker thread (shared, tagged): amp model switches rebuild an AmpBlockExtended
// off the RT thread (it allocates an OversamplingWrapper); cab IR loads read the
// .wav + rebuild the convolver off the RT thread. The audio thread only ever does
// a pointer swap / lock-free IR publish.
//
// Symbol isolation: like amp/drive, this .so whole-archives NamCore. The version
// script (lv2/export.map) + -Bsymbolic + --exclude-libs keep NAM/Eigen globals
// local so loading Hex Forge beside the ten standalone plugins can't interpose.
// ─────────────────────────────────────────────────────────────────────────────
#include "lv2_util.h"
#include <lv2/time/time.h>
#include "hexforge_ports.h"
#include "hexforge_factory_presets.h"   // band/song factory presets (Banks 2..6), generated

#include "BiquadFilter.h"
#include "PickupVoicer.h"
#include "NoiseGateBlock.h"
#include "CompressorBlock.h"
#include "OversamplingWrapper.h"
#include "EHXBigMuff.h"
#include "Octavia.h"
#include "ToneBenderMkII.h"
#include "OverdriveBlock.h"
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include "CabinetBlock.h"
#include "DefaultCabIR.h"
#include "CabModels.h"
#include "ModulationBlock.h"
#include "ModulationFactory.h"
#include "DelayBlock.h"
#include "DelayFactory.h"
#include "PlateReverbBlock.h"
#include "WahBlock.h"
#include "OctaveBlock.h"
#include "NailDistortion.h"
#include "NamModel.h"
#include "DenormalGuard.h"

#include <new>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <sys/stat.h>

#define HEXFORGE_URI     "https://rpowell5064.github.io/guitaramp-suite/hexforge"
#define HEXFORGE_IR_URI  HEXFORGE_URI "#irfile"
#define HEXFORGE_AMPNAM  HEXFORGE_URI "#ampnam"
#define HEXFORGE_DRNAM   HEXFORGE_URI "#drnam"
#define HEXFORGE_CABNAM  HEXFORGE_URI "#cabnam"
static constexpr int kAmpNamIdx = 5;   // amp model slot = Neural (NAM)
static constexpr int kDrNamIdx  = 3;   // drive model slot = Neural (NAM)
static constexpr int kDrDs1Idx  = 4;   // drive model slot = Grunge DS (DS-1)
static constexpr int kDrKlonIdx = 5;   // drive model slot = Gilded Horse (Klon)
static constexpr int kDrSd1Idx  = 6;   // drive model slot = Super Nova (Boss SD-1)
static constexpr int kDrMax     = 6;   // highest dr_model index

// pi-Stomp footswitches emit CC 60..63 (one per switch). A received CC in this
// range is one switch "press" → preset recall / bank combo. Change here if the
// pi-Stomp config uses a different base CC.
static constexpr int kMidiBaseCC = 60;
#define LV2_MIDI_MidiEvent_URI "http://lv2plug.in/ns/ext/midi#MidiEvent"

static constexpr int kMaxBlock = 512;
static constexpr int kPathMax  = 1024;

// Sentinel "path" for the always-available built-in Factory Cab IR. The modgui
// IR picker offers it as a static option; selecting it clears the IR back to the
// synthesized DefaultCabIR (stored as an empty path so it round-trips cleanly).
static const char* const kFactoryIR = "@factory";

// ── Preset engine ─────────────────────────────────────────────────────────────
// Hex Forge owns its own preset store: 32 banks × 4 slots (A/B/C/D) = 128 presets.
// A preset is a full snapshot of every user parameter plus the four file paths.
// (kBanks kept a power of two — the bank-wrap masks `& (kBanks-1)` depend on it.)
static constexpr int kBanks = 32, kSlots = 4;
// BUMP whenever the built-in factory presets change — on load, a saved store with an
// older rev has its FACTORY slots refreshed from the binary (user slots untouched), so
// preset fixes actually reach existing users instead of being overridden by the .dat.
static constexpr uint32_t kFactoryRev = 1;
struct Preset {
    bool  used = false;
    char  name[32] = {0};
    float vals[HF_N_PORTS] = {};
    char  irPath[kPathMax]     = {0};
    char  ampNamPath[kPathMax] = {0};
    char  drNamPath[kPathMax]  = {0};
    char  cabNamPath[kPathMax] = {0};
};
// A "param port" is a user-facing control captured by presets: everything from
// the Output level through the last block param, excluding the global Bypass,
// the Clip output, and the preset command/status ports (all ≥ HF_SW_A).
static inline bool isParamPort(int i) {
    return i >= HF_OUT_LEVEL && i < HF_SW_A && i != HF_CLIP;
}

// ── Model maps (mirror the standalone amp / drive plugins) ────────────────────
static const AmpModel kAmpMap[10] = {
    AmpModel::FenderDeluxe, AmpModel::MarshallJCM800, AmpModel::EVH5150III,
    AmpModel::SunnModelT,   AmpModel::OrangeRockerverb50,
    AmpModel::NeuralCustom,        // 5 = NAM (placeholder; not built as an algo amp)
    AmpModel::FriedmanBEDeluxe,    // 6 = Beardo BE
    AmpModel::HiwattDR103,         // 7 = Hiwatt (high-headroom British clean)
    AmpModel::VoxAC30,             // 8 = Vox AC30 Top Boost (EL84 chime)
    AmpModel::PeaveyBackstage,     // 9 = Backline Plus (solid-state Peavey Backstage)
};
static const int   kCanonical[10] = { 0, 1, 2, 4, 5, 3, 6, 0, 0, 0 }; // PowerAmp default lookup ([7] Hiwatt, [8] Vox, [9] Backline → clean PA)
static constexpr int kSunnIdx     = 3;
static constexpr int kFriedmanIdx = 6;
static constexpr int kHiwattIdx   = 7;
static constexpr int kVoxIdx      = 8;
static constexpr int kBacklineIdx = 9;
static const int   kAmpTube[10]   = { 0, 1, 1, 0, 1, 0, 1, 1, 2, 0 }; // 6L6/EL34/EL34/6L6/—/EL34/EL34/EL84/SS(n-a)
static const float kAmpMakeup[10] = { 3.3f, 1.0f, 1.4f, 3.0f, 1.15f, 1.0f, 1.0f, 1.3f, 1.6f, 2.5f };  // [7] Hiwatt was 4.9 (BUG: slammed the master limiter under any drive → mush + forced out_level to -27); high-headroom amp needs little makeup, loudness comes from out_level. [8] Vox [9] Backline (solid-state; model runs ~9 dB below the NAM, low crest so 2.5 is safe)
// Soft ceiling on the amp INPUT: x -> A*tanh(x/A). Gives the amp headroom so a hot upstream
// block (esp. the fuzz, which outputs ~0 dBFS = +14 dB over a guitar) can't slam the guitar-
// level front-end into mush. Transparent at guitar level (~-0.2 dB @ -14 dBFS), soft-limits
// hot peaks (-1.2 dB @ -6 dBFS, -3.7 dB @ 0 dBFS). Applied in the chain, NOT in the amp models,
// so every model's NAM-tuned voicing is untouched. Lower A = more headroom (more taming).
static constexpr float kAmpInputCeil = 0.75f;

// Indexed by dr_model port: 0/1/2/4/5 = algorithmic, 3 = NAM (special-cased, entry unused).
static const OverdriveType kDriveMap[7] = {
    OverdriveType::TubeScreamer808, OverdriveType::LifePedal, OverdriveType::ProcoRAT,
    OverdriveType::ProcoRAT /* [3]=NAM placeholder, never used */, OverdriveType::DS1,
    OverdriveType::Klon, OverdriveType::SuperOverdriveSD1,
};

// Binson Echorec rotary program -> playback-head bitmask (mirrors delay plugin).
static const int kEchorecProgram[12] = {
    0x01,0x02,0x04,0x08, 0x03,0x06,0x0C, 0x07,0x0E,0x0D, 0x0F,0x1F,
};

static int clampi(float v, int lo, int hi) {
    int i = static_cast<int>(v + 0.5f);
    return i < lo ? lo : (i > hi ? hi : i);
}

// ── Auto output level (clip-safe AGC) ─────────────────────────────────────────
// When engaged, it RIDES the master gain so the signal's (slowly-decaying) peak
// sits at the target level (the Output knob) — so the level sets itself and never
// has to be found by hand. It only ever turns the gain *down* (never boosts noise).
// A fast safety limiter just under the digital ceiling catches transients, so it
// also can't clip. Transparent on quiet material; turns hot amp/NAM signals down.
// dB -> linear gain. -60 dB (the port floor) is treated as a hard mute.
static inline float dbToGain(float db) noexcept {
    return db <= -59.5f ? 0.0f : std::pow(10.0f, db * 0.05f);
}
// linear gain -> dB, for migrating v2 presets (which stored out_level as 0..1).
static inline float linToDb(float lin) noexcept {
    return lin <= 1e-4f ? -60.0f : std::fmax(-60.0f, 20.0f * std::log10(lin));
}

// Master output stage: a smoothed gain (the dB-scaled Output knob, applied like
// the stock MOD gain block) followed by an optional transparent peak limiter.
// The gain is applied FIRST and smoothed (~10 ms) so knob moves never zipper;
// when limiting is on, the limiter only reduces when a post-gain peak would
// breach the ceiling — sample-accurate attack (nothing clips) with a short
// release so it doesn't dull the tone. Below the ceiling it is fully transparent.
struct AutoOutput {
    float env  = 1.0f;         // limiter gain envelope, <= 1.0
    float lvlZ = 1.0f;         // smoothed master gain
    float rel = 0.0f, lvlCoef = 0.0f;
    bool  primed = false;
    static constexpr float kCeiling = 0.98f;   // ~-0.18 dBFS: only catch true overs
    void prepare(double sr) noexcept {
        const float s = static_cast<float>(sr);
        rel     = std::exp(-1.0f / (0.045f * s));   // ~45 ms limiter release (low dulling)
        lvlCoef = std::exp(-1.0f / (0.010f * s));   // ~10 ms gain smoothing (no zipper)
    }
    void reset() noexcept { env = 1.0f; }
    void process(float* L, float* R, uint32_t n, float gain, bool limit) noexcept {
        if (!primed) { lvlZ = gain; primed = true; }
        for (uint32_t i = 0; i < n; ++i) {
            lvlZ = lvlCoef * lvlZ + (1.0f - lvlCoef) * gain;         // smoothed master gain
            float l = L[i] * lvlZ, r = R[i] * lvlZ;
            float des = 1.0f;
            if (limit) {
                const float a = std::fmax(std::fabs(l), std::fabs(r));   // stereo-linked peak
                des = (a > kCeiling) ? kCeiling / a : 1.0f;
            }
            if (des < env) env = des;                                // instant attack: never clip
            else           env = rel * env + (1.0f - rel) * des;     // release toward des (1.0 when off)
            L[i] = l * env; R[i] = r * env;
        }
    }
};

// ── RBJ notch (used by the hum notch comb below) ──────────────────────────────
static BiquadCoeffs makeNotch(double fc, double Q, double fs) noexcept {
    const double w = 2.0 * M_PI * fc / fs, a = std::sin(w) / (2.0 * Q), c = std::cos(w);
    const double a0 = 1.0 + a;
    BiquadCoeffs k;
    k.b0 = 1.0/a0; k.b1 = (-2.0*c)/a0; k.b2 = 1.0/a0;
    k.a1 = (-2.0*c)/a0; k.a2 = (1.0-a)/a0;
    return k;
}

// ── Power-line hum notch comb (Input Trim "Hum Filter") ────────────────────────
// A real humbucker nulls hum with two reverse-wound coils (a spatial differential
// null); that can't be reproduced from one already-captured signal. This instead
// *attenuates* 60 Hz mains hum with a cascade of fixed RBJ notches at the harmonic
// series (60/120/.../360 Hz). Being LTI it can ONLY remove energy, never
// synthesise a tone -> no phantom / "out of time" notes. (An adaptive-LMS version
// was tried and rejected: time-varying cancellers build weights from sustained or
// abruptly-stopped notes and then *emit* a decaying 60/120/180/240 Hz tone, which
// is exactly the artifact that showed up on the device -- see tools/hum_cancel.py.)
// 60 Hz mains (North America). Tuned + verified there: ~15 dB reduction of the hum
// stack, each line killed deeply, ~1 dB static impact on a low B1, transient ring
// no worse than the old twin-notch. Replaces the old fixed 50/60 Hz twin-notch.
struct HumNotchComb {
    static constexpr int kN = 6;
    BiquadFilter notch[kN];
    void prepare(double sr) noexcept {
        static const double f[kN] = {60.0, 120.0, 180.0, 240.0, 300.0, 360.0};
        for (int k = 0; k < kN; ++k) notch[k].setCoeffs(makeNotch(f[k], 18.0, sr));
    }
    void reset() noexcept { for (auto& b : notch) b.reset(); }
    float process(float x) noexcept {
        for (int k = 0; k < kN; ++k) x = notch[k].process(x);
        return x;
    }
};

// PickupVoicer (single-coil -> humbucker voicing) now lives in the shared header
// lv2/common/PickupVoicer.h so the standalone Input Trim (utility) plugin uses the
// exact same tuned curves. (Included at the top.)

// ── Output boost (Input Trim) ─────────────────────────────────────────────────
// Pickup-agnostic "make it hotter" stage: an output level boost plus a low-mid
// "beef" bump (peaking @ 250 Hz) that scales with the boost so it fattens single
// coils and thickens humbuckers. `amtDb` is the boost in dB (0..12); the beef
// reaches +3 dB at full boost. amtDb=0 -> unity gain + flat = true bypass.
struct OutputBoost {
    BiquadFilter beef;
    float  gain    = 1.0f;
    double curRate = 0.0;
    float  curAmt  = -1.0f;
    void prepare(double sr, float amtDb) noexcept {
        if (sr == curRate && amtDb == curAmt) return;
        curRate = sr; curAmt = amtDb;
        beef.setCoeffs(Filters::peaking(250.0, 3.0 * (amtDb / 12.0), 0.7, sr));
        gain = std::pow(10.0f, amtDb / 20.0f);
    }
    void reset() noexcept { beef.reset(); }
    float process(float x) noexcept { return beef.process(x) * gain; }
};

// ── Movable-block identity ────────────────────────────────────────────────────
enum Block { B_GATE, B_COMP, B_FUZZ, B_DRIVE, B_AMP, B_CAB, B_MODFX, B_DELAY, B_REVERB, B_WAH, B_OCTAVE, B_NAIL, B_COUNT };
static const int kPosPort[B_COUNT] = {
    HF_GT_POS, HF_CP_POS, HF_FZ_POS, HF_DR_POS, HF_AMP_POS,
    HF_CAB_POS, HF_MD_POS, HF_DL_POS, HF_RV_POS, HF_WH_POS, HF_OC_POS, HF_NAIL_POS,
};
static const int kEnablePort[B_COUNT] = {
    HF_GT_ENABLE, HF_CP_ENABLE, HF_FZ_ENABLE, HF_DR_ENABLE, HF_AMP_ENABLE,
    HF_CAB_ENABLE, HF_MD_ENABLE, HF_DL_ENABLE, HF_RV_ENABLE, HF_WH_ENABLE, HF_OC_ENABLE, HF_NAIL_ENABLE,
};
// enable = chain membership (1 = in chain, 0 = removed/palette); bypass = active(0)/
// bypassed(1). A block runs iff enable==1 && bypass==0. Bypassed blocks stay in the chain
// (greyed in the UI) but pass dry, keeping their settings — for live A/B.
static const int kBypassPort[B_COUNT] = {
    HF_GT_BYPASS, HF_CP_BYPASS, HF_FZ_BYPASS, HF_DR_BYPASS, HF_AMP_BYPASS,
    HF_CAB_BYPASS, HF_MD_BYPASS, HF_DL_BYPASS, HF_RV_BYPASS, HF_WH_BYPASS, HF_OC_BYPASS, HF_NAIL_BYPASS,
};

// note-division factor relative to a quarter-note beat, indexed by the *_div enum (0..7):
// 1/2, 1/4., 1/4, 1/4T, 1/8., 1/8, 1/8T, 1/16.  time(ms) = (60000/bpm)*factor; Hz = bpm/(60*factor).
static const float kDivFactor[8] = { 2.0f, 1.5f, 1.0f, 0.66667f, 0.75f, 0.5f, 0.33333f, 0.25f };

// ── Worker messaging ──────────────────────────────────────────────────────────
enum WorkType { W_AMP_LOAD, W_AMP_FREE, W_CAB_IR, W_NAM_LOAD, W_NAM_FREE };
struct WorkMsg {
    WorkType          type;
    AmpBlockExtended* amp = nullptr;   // AMP_LOAD reply / AMP_FREE target
    NamModel*         nam = nullptr;   // NAM_LOAD reply / NAM_FREE target
    int               modelIdx = 0;
    int               namSlot  = 0;    // 0=amp 1=drive 2=cab
    char              path[kPathMax] = {0};   // CAB_IR / NAM_LOAD
};

struct URIs {
    LV2_URID atom_Object, atom_Path, atom_URID, atom_String, atom_Chunk;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID ir_file, amp_nam, dr_nam, cab_nam;
    LV2_URID ps_name, ps_index, ps_apply, preset_blob, meters;
    LV2_URID midi_MidiEvent;
    LV2_URID time_Position, time_bpm, atom_Float;   // host tempo (tap-tempo / MIDI clock sync)
};

// ── Chromatic strobe tuner ── autocorrelation pitch detector on the dry input. Only runs
// when the tuner is engaged. Reports note (0..11 = C..B, -1 = no pitch) + cents (-50..+50).
struct TunerDetector {
    static constexpr int kBuf = 2048;   // ~43 ms @ 48k → > 2 periods of low E (82 Hz)
    float  buf[kBuf] = {0};
    double corr[kBuf] = {0};
    int    wr = 0, sinceCalc = 0;
    double rate = 48000.0;
    int    note = -1;
    float  cents = 0.0f;
    void prepare(double sr) noexcept {
        rate = sr; wr = 0; sinceCalc = 0; note = -1; cents = 0.0f;
        for (int i = 0; i < kBuf; ++i) buf[i] = 0.0f;
    }
    void reset() noexcept { note = -1; cents = 0.0f; }
    void process(const float* mono, int n) noexcept {
        for (int i = 0; i < n; ++i) { buf[wr] = mono[i]; wr = (wr + 1) & (kBuf - 1); }
        sinceCalc += n;
        if (sinceCalc >= 2048) { sinceCalc = 0; compute(); }
    }
    void compute() noexcept {
        float w[kBuf];
        for (int i = 0; i < kBuf; ++i) w[i] = buf[(wr + i) & (kBuf - 1)];
        double rms = 1e-12; for (int i = 0; i < kBuf; ++i) rms += (double)w[i] * w[i];
        const double c0 = rms;                 // == sum(w^2)
        rms = std::sqrt(rms / kBuf);
        if (rms < 0.004) { note = -1; return; }   // ~-48 dBFS gate → silence = no reading
        const int minLag = (int)(rate / 1050.0);  // up to ~1050 Hz
        const int maxLag = (int)(rate / 62.0);    // down to ~62 Hz (below low B)
        double best = 0.0; int bestLag = 0;
        for (int lag = minLag; lag <= maxLag && lag < kBuf; ++lag) {
            double c = 0.0;
            for (int i = 0; i + lag < kBuf; ++i) c += (double)w[i] * w[i + lag];
            const double norm = c / c0;
            corr[lag] = norm;
            if (norm > best) { best = norm; bestLag = lag; }
        }
        if (bestLag <= minLag || best < 0.35) { note = -1; return; }
        double refLag = bestLag;               // parabolic interpolation for sub-sample accuracy
        if (bestLag > minLag && bestLag < maxLag) {
            const double a = corr[bestLag - 1], b = corr[bestLag], cc = corr[bestLag + 1];
            const double den = a - 2.0 * b + cc;
            if (std::fabs(den) > 1e-12) refLag = bestLag + 0.5 * (a - cc) / den;
        }
        const double freq = rate / refLag;
        const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
        const long  nearest = std::lround(midi);
        cents = (float)((midi - nearest) * 100.0);
        note  = (int)(((nearest % 12) + 12) % 12);   // 0 = C .. 11 = B
    }
};

struct HexForge {
    double rate = 48000.0;

    // DSP blocks
    HumNotchComb      trimHum;
    PickupVoicer      trimVoice;        // single-coil -> humbucker voicing (Input Trim)
    OutputBoost       trimBoost;        // pickup-agnostic output boost + beef (Input Trim)
    NoiseGateBlock    gate;
    CompressorBlock   comp;
    std::unique_ptr<OversamplingWrapper> fuzzMuff;   // Italian Hero
    std::unique_ptr<OversamplingWrapper> fuzzBender; // Tone Bender MkII
    std::unique_ptr<OversamplingWrapper> fuzzOctavia;// Octavia (octave-up)
    OverdriveBlock    drive;
    AmpBlockExtended* amp = nullptr;                  // swapped on model change
    PowerAmpProcessor pa;
    CabinetBlock      cab;
    ModulationBlock   modfx;
    DelayBlock        delay;
    PlateReverbBlock  reverb;
    WahBlock          wah;
    OctaveBlock       octave;
    std::unique_ptr<OversamplingWrapper> nail;        // industrial distortion (NailDistortion, oversampled)
    AutoOutput        autoOut;        // auto-leveling clip protection on the master output
    NamModel*         ampNam = nullptr;   // worker-loaded neural captures
    NamModel*         drNam  = nullptr;
    NamModel*         cabNam = nullptr;

    // model-switch caches
    int lastAmpModel = 1, lastAmpTube = -1, lastDriveModel = 0, lastNailMode = 2;
    int lastModfxType = 0, lastDelayType = 0;
    float hostBpm = 120.0f;   // host tempo from time:Position events (tap-tempo / MIDI clock sync)
    TunerDetector tuner;      // strobe tuner pitch detector (runs only when engaged)
    int  lastTunerNote = -2;  // to notify the UI only on change (int output ports)

    // ports — `ports[]` are the pointers the DSP reads. For param ports they are
    // redirected to point at eff[] (the preset/override layer); for everything
    // else they point at the host buffer (hostPorts[]). See hf_run priming.
    float* ports[HF_N_PORTS]     = {};
    float* hostPorts[HF_N_PORTS] = {};
    const LV2_Atom_Sequence* control = nullptr;
    const LV2_Atom_Sequence* midiIn  = nullptr;   // footswitch CCs (pi-Stomp)
    LV2_Atom_Sequence*       notify  = nullptr;

    // preset engine
    Preset presets[kBanks][kSlots];
    int    curBank = 0, curSlot = 0;
    float  eff[HF_N_PORTS]      = {};   // effective param values the DSP runs on
    float  lastPort[HF_N_PORTS] = {};   // last host value seen (live-edit detect)
    bool   primed = false;              // ports[] redirected + eff[] seeded
    bool   pendingRecall = false;       // apply restored active preset on first run
    float  swPrev[4]  = {0,0,0,0};      // sw_a..sw_d edge state
    float  cmdPrev[7] = {0,0,0,0,0,0,0}; // bank_up/dn, save, move_up/dn, backup, restore edge state
    int    lastGoto   = -1;             // last ps_goto target serviced
    float  meterIn = 0.0f, meterOut = 0.0f;  // smoothed peak level meters (-> in_meter/out_meter)
    float  meterSentIn = -1.0f, meterSentOut = -1.0f;  // last values pushed to UI (deadband)
    uint32_t meterFrames = 0;                // throttle for the #meters notify (UI)
    // Double-tap bank nav: double-tap A = bank down, D = bank up.
    int64_t sampleClock = 0;            // running sample counter
    int64_t lastTapSample[4]  = {-100000000,-100000000,-100000000,-100000000};
    int64_t lastEdgeSample[4] = {-100000000,-100000000,-100000000,-100000000};  // sw debounce

    // file-load state
    char irPath[kPathMax]     = {0};
    char ampNamPath[kPathMax] = {0};
    char drNamPath[kPathMax]  = {0};
    char cabNamPath[kPathMax] = {0};

    // scratch
    float mono[kMaxBlock], monoOut[kMaxBlock];
    int   clipHold = 0;   // samples remaining to keep the CLIP indicator lit

    // host features
    LV2_URID_Map*        map      = nullptr;
    LV2_Worker_Schedule* schedule = nullptr;
    LV2_Atom_Forge       forge;
    URIs                 uris;
};

// ── IR file reading (mirrors the standalone Cab) ──────────────────────────────
static bool readWav(const char* path, std::vector<float>& L, std::vector<float>& R, uint32_t& outRate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    auto rd = [&](void* p, int n){ f.read(reinterpret_cast<char*>(p), n); };
    char riff[4]; rd(riff,4); if (std::strncmp(riff,"RIFF",4)!=0) return false;
    uint32_t rsz; rd(&rsz,4);
    char wave[4]; rd(wave,4); if (std::strncmp(wave,"WAVE",4)!=0) return false;
    uint16_t fmt=0, ch=0, bits=0; uint32_t sr=0;
    std::vector<uint8_t> data;
    while (f) {
        char id[4]; rd(id,4); uint32_t sz=0; rd(&sz,4); if (!f) break;
        if (std::strncmp(id,"fmt ",4)==0) {
            rd(&fmt,2); rd(&ch,2); rd(&sr,4); uint32_t br; rd(&br,4); uint16_t ba; rd(&ba,2); rd(&bits,2);
            if (sz>16) f.seekg(sz-16, std::ios::cur);
        } else if (std::strncmp(id,"data",4)==0) {
            data.resize(sz); f.read(reinterpret_cast<char*>(data.data()), sz);
        } else f.seekg(sz, std::ios::cur);
        if (sz & 1) f.seekg(1, std::ios::cur);
    }
    if (ch==0 || bits==0 || data.empty()) return false;
    outRate = sr;
    const size_t bps = bits/8, frames = data.size()/(bps*ch);
    L.assign(frames, 0.0f);
    if (ch>=2) R.assign(frames, 0.0f); else R.clear();
    const uint8_t* p = data.data();
    for (size_t i=0;i<frames;++i) for (uint16_t c=0;c<ch;++c) {
        float s=0.0f;
        if (fmt==3 && bits==32) { float v; std::memcpy(&v,p,4); s=v; }
        else if (bits==16) { int16_t v; std::memcpy(&v,p,2); s=v/32768.0f; }
        else if (bits==24) { int32_t v=(p[0])|(p[1]<<8)|(p[2]<<16); if(v&0x800000) v|=~0xFFFFFF; s=v/8388608.0f; }
        else if (bits==32) { int32_t v; std::memcpy(&v,p,4); s=v/2147483648.0f; }
        if (c==0) L[i]=s; else if (c==1 && ch>=2) R[i]=s;
        p += bps;
    }
    return true;
}
static std::vector<float> resampleLinear(const std::vector<float>& in, double sr, double dr) {
    if (in.empty() || sr<=0.0 || dr<=0.0 || std::abs(sr-dr)<1.0) return in;
    const double ratio = dr/sr; const size_t out = static_cast<size_t>(in.size()*ratio + 0.5);
    std::vector<float> o(out);
    for (size_t i=0;i<out;++i){ const double sp=i/ratio; const size_t i0=static_cast<size_t>(sp);
        const float fr=static_cast<float>(sp-i0), a=in[i0], b=(i0+1<in.size())?in[i0+1]:in[i0]; o[i]=a+fr*(b-a); }
    return o;
}
static bool loadIRFile(const char* path, double dst, std::vector<float>& L, std::vector<float>& R) {
    uint32_t sr=0; if (!readWav(path,L,R,sr) || L.empty()) return false;
    L = resampleLinear(L, sr, dst); if (!R.empty()) R = resampleLinear(R, sr, dst);
    return true;
}

static void mapURIs(HexForge* p) {
    LV2_URID_Map* m = p->map;
    p->uris.atom_Object   = m->map(m->handle, LV2_ATOM__Object);
    p->uris.atom_Path     = m->map(m->handle, LV2_ATOM__Path);
    p->uris.atom_URID     = m->map(m->handle, LV2_ATOM__URID);
    p->uris.atom_String   = m->map(m->handle, LV2_ATOM__String);
    p->uris.atom_Chunk    = m->map(m->handle, LV2_ATOM__Chunk);
    p->uris.patch_Set     = m->map(m->handle, LV2_PATCH__Set);
    p->uris.patch_Get     = m->map(m->handle, LV2_PATCH__Get);
    p->uris.patch_property= m->map(m->handle, LV2_PATCH__property);
    p->uris.patch_value   = m->map(m->handle, LV2_PATCH__value);
    p->uris.ir_file       = m->map(m->handle, HEXFORGE_IR_URI);
    p->uris.amp_nam       = m->map(m->handle, HEXFORGE_AMPNAM);
    p->uris.dr_nam        = m->map(m->handle, HEXFORGE_DRNAM);
    p->uris.cab_nam       = m->map(m->handle, HEXFORGE_CABNAM);
    p->uris.ps_name       = m->map(m->handle, HEXFORGE_URI "#ps_name");
    p->uris.ps_index      = m->map(m->handle, HEXFORGE_URI "#ps_index");
    p->uris.ps_apply      = m->map(m->handle, HEXFORGE_URI "#ps_apply");
    p->uris.preset_blob   = m->map(m->handle, HEXFORGE_URI "#preset_blob");
    p->uris.meters        = m->map(m->handle, HEXFORGE_URI "#meters");
    p->uris.midi_MidiEvent= m->map(m->handle, LV2_MIDI_MidiEvent_URI);
    p->uris.time_Position = m->map(m->handle, LV2_TIME__Position);
    p->uris.time_bpm      = m->map(m->handle, LV2_TIME__beatsPerMinute);
    p->uris.atom_Float    = m->map(m->handle, LV2_ATOM__Float);
}
static void writeFileToNotify(HexForge* p, LV2_URID prop, const char* path) {
    const URIs& u = p->uris;
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&p->forge, 0);
    lv2_atom_forge_object(&p->forge, &frame, 0, u.patch_Set);
    lv2_atom_forge_key(&p->forge, u.patch_property);
    lv2_atom_forge_urid(&p->forge, prop);
    lv2_atom_forge_key(&p->forge, u.patch_value);
    lv2_atom_forge_path(&p->forge, path, static_cast<uint32_t>(std::strlen(path)));
    lv2_atom_forge_pop(&p->forge, &frame);
}

// ── Preset engine: notify emitters + recall/save/bank/move ────────────────────
// All of these run inside hf_run with the notify forge sequence already open, so
// they may append patch:Set messages to the UI.
static void forgeStringSet(HexForge* p, LV2_URID prop, const char* s) {
    const URIs& u = p->uris;
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&p->forge, 0);
    lv2_atom_forge_object(&p->forge, &frame, 0, u.patch_Set);
    lv2_atom_forge_key(&p->forge, u.patch_property);
    lv2_atom_forge_urid(&p->forge, prop);
    lv2_atom_forge_key(&p->forge, u.patch_value);
    lv2_atom_forge_string(&p->forge, s, static_cast<uint32_t>(std::strlen(s)));
    lv2_atom_forge_pop(&p->forge, &frame);
}
// "bank|slot|name0|name1|...|name31" — drives the UI bank indicator + name list.
static void emitIndex(HexForge* p) {
    if (!p->notify) return;
    char buf[6144]; int o = 0;   // 32 banks × 4 slots of names
    o += std::snprintf(buf, sizeof(buf), "%d|%d", p->curBank, p->curSlot);
    for (int b = 0; b < kBanks; ++b)
        for (int s = 0; s < kSlots; ++s) {
            const Preset& pr = p->presets[b][s];
            const char* nm = (pr.used && pr.name[0]) ? pr.name : "";
            o += std::snprintf(buf + o, sizeof(buf) - o, "|%s", nm);
            if (o >= (int)sizeof(buf) - 40) { b = kBanks; break; }
        }
    forgeStringSet(p, p->uris.ps_index, buf);
}
// "sym=val;sym=val;..." for every param port — the UI replays it via set_port_value.
static void emitApply(HexForge* p) {
    if (!p->notify) return;
    char buf[6144]; int o = 0; buf[0] = '\0';
    for (int i = 0; i < HF_N_PORTS; ++i)
        if (isParamPort(i) && o < (int)sizeof(buf) - 40)
            o += std::snprintf(buf + o, sizeof(buf) - o, "%s=%g;", HF_PORT_SYM[i], p->eff[i]);
    forgeStringSet(p, p->uris.ps_apply, buf);
}
// Schedule a worker (re)load for a file slot whose path changed (empty path =
// clear: NAM loads an unloaded model the DSP ignores; IR resets to the default).
static void schedPath(HexForge* p, char* cur, const char* want, WorkType wt, int namSlot) {
    if (std::strcmp(cur, want) == 0) return;
    std::strncpy(cur, want, kPathMax - 1); cur[kPathMax - 1] = '\0';
    WorkMsg m; m.type = wt; m.namSlot = namSlot;
    std::strncpy(m.path, want, kPathMax - 1); m.path[kPathMax - 1] = '\0';
    p->schedule->schedule_work(p->schedule->handle, sizeof(m), &m);
}
// Publish the active bank/slot/name to a status file the pi-Stomp LCD reads
// ("<bank><slot> <name>", e.g. "1A Clean"). Written only on preset changes.
static void hfWriteStatus(HexForge* p) {
    FILE* f = std::fopen("/tmp/hexforge_status", "w");
    if (!f) return;
    const Preset& pr = p->presets[p->curBank][p->curSlot];
    std::fprintf(f, "%d%c %s\n", p->curBank + 1, 'A' + p->curSlot,
                 (pr.used && pr.name[0]) ? pr.name : "(empty)");
    std::fclose(f);
}
// ── Preset store backup file (survives delete/re-add + bundle updates) ─────────
// The preset store also lives in a single file OUTSIDE the plugin instance, so a
// fresh instance can recover all 32 presets after the plugin is deleted+re-added
// or the bundle is updated. Same self-describing layout as the State blob
// (version 3) but with raw absolute file paths (no LV2 mapPath outside State).
static std::string hfBackupDir() {
    const char* home = std::getenv("HOME");
    return (home && home[0]) ? std::string(home) + "/.config/hexchain" : std::string("/tmp");
}
// Reconstruct an old positional value array (the npc old values sit at the start
// of vals[], rest zero) onto the CURRENT (v7) port layout, inserting defaults for
// ports that didn't exist in `srcVer`. Two insertion regions, both mid-enum:
//   * Input-Trim voicing/boost block, 5 contiguous ports [HF_IT_HUMBK..HF_IT_BOOSTAMT],
//     added incrementally: v4 humbk+hbamt, v5 hbmodel, v6 boost+boostamt.
//   * Delay Seraph block, 4 contiguous ports [HF_DL_PATTERN..HF_DL_MODRATE], added v7.
// Done as a single left-to-right walk over the new layout: gap slots get defaults,
// every other slot consumes the next old value (preserves order across both gaps).
static_assert(HF_IT_HBAMT == HF_IT_HUMBK + 1 && HF_IT_HBMODEL == HF_IT_HUMBK + 2 &&
              HF_IT_BOOST == HF_IT_HUMBK + 3 && HF_IT_BOOSTAMT == HF_IT_HUMBK + 4,
              "voicing/boost ports must be contiguous for the preset migration");
static_assert(HF_DL_DUCKING == HF_DL_PATTERN + 1 && HF_DL_MODDEPTH == HF_DL_PATTERN + 2 &&
              HF_DL_MODRATE == HF_DL_PATTERN + 3 && HF_DL_PATTERN > HF_IT_BOOSTAMT,
              "Seraph delay ports must be contiguous and after the IT block");
//   * Wah + Octave blocks, 13 contiguous ports [HF_WH_POS..HF_OC_DRY], added v8.
static_assert(HF_OC_DRY == HF_WH_POS + 12 && HF_WH_POS > HF_DL_MODRATE,
              "Wah+Octave ports must be contiguous and after the Delay block");
//   * Per-block bypass, 11 contiguous toggles [HF_GT_BYPASS..HF_OC_BYPASS], added v9.
static_assert(HF_OC_BYPASS == HF_GT_BYPASS + 10 && HF_GT_BYPASS > HF_OC_DRY && HF_OC_BYPASS < HF_SW_A,
              "bypass ports must be contiguous, after the param blocks and before the commands");
//   * Nail block, 8 contiguous ports [HF_NAIL_POS..HF_NAIL_BYPASS], added v12.
static_assert(HF_NAIL_BYPASS == HF_NAIL_POS + 7 && HF_NAIL_POS == HF_OC_BYPASS + 1 && HF_NAIL_BYPASS < HF_SW_A,
              "Nail ports must be contiguous, right after the bypass toggles and before the commands");
//   * Tempo-sync ports, 4 contiguous [HF_DL_SYNC..HF_MD_DIV], added v13.
static_assert(HF_MD_DIV == HF_DL_SYNC + 3 && HF_DL_SYNC == HF_NAIL_BYPASS + 1 && HF_MD_DIV < HF_SW_A,
              "tempo-sync ports must be contiguous, after the Nail block and before the commands");
static void migratePorts(float* vals, uint32_t srcVer) noexcept {
    static const float vdef[5] = {0.0f, 1.0f, 0.0f, 0.0f, 4.0f};  // humbk,hbamt,hbmodel,boost,boostamt
    static const float ddef[4] = {1.0f, 0.0f, 0.0f, 0.3f};        // pattern,ducking,moddepth,modrate
    static const float wodef[13] = {10.0f, 0.0f, 0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f,   // Wah: pos,en,type,freq,depth,sens,q,mix
                                    11.0f, 0.0f, 0.0f, 0.5f, 1.0f};                     // Octave: pos,en,up,down,dry
    const int itExisting = (srcVer < 4) ? 0 : (srcVer == 4) ? 2 : (srcVer == 5) ? 3 : 5;
    const int itAt = HF_IT_HUMBK + itExisting, itEnd = HF_IT_HUMBK + 5;   // IT gap [itAt,itEnd)
    const bool dlGap = (srcVer < 7);
    const int dlAt = HF_DL_PATTERN, dlEnd = HF_DL_PATTERN + 4;            // DL gap [dlAt,dlEnd)
    const bool woGap = (srcVer < 8);
    const int woAt = HF_WH_POS, woEnd = HF_WH_POS + 13;                   // Wah+Octave gap [woAt,woEnd)
    // v9 inserted 11 per-block bypass toggles [HF_GT_BYPASS..HF_OC_BYPASS] BEFORE the command
    // ports, so a v8 blob's command/status values shift up by 11; the new bypass slots default
    // to 0 (active). This is a positional insert (not a trailing append) — the gap is required.
    // The bypass group is a FIXED 11 toggles (gt..oc); the Nail block (v12) has its OWN bypass in
    // its own group, so this gap stays 11 even though B_COUNT is now 12 (don't use B_COUNT here).
    const bool byGap = (srcVer < 9);
    const int byAt = HF_GT_BYPASS, byEnd = HF_OC_BYPASS + 1;              // 11 bypass ports [byAt,byEnd), default 0
    // v12 inserted the Nail block — 8 contiguous ports [HF_NAIL_POS..HF_NAIL_BYPASS], right after
    // the bypass toggles and before the command ports; a pre-v12 blob's command/status values shift
    // up by 8 and the new nail slots default to OFF at slot 12.
    static const float naildef[8] = {12.0f, 0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f}; // pos,en,mode,drive,tone,texture,level,byp
    const bool nailGap = (srcVer < 12);
    const int nailAt = HF_NAIL_POS, nailEnd = HF_NAIL_POS + 8;            // Nail gap [nailAt,nailEnd)
    // v13 inserted 4 tempo-sync ports [HF_DL_SYNC..HF_MD_DIV] before the commands; defaults =
    // sync OFF, Delay div 1/8 (5), Mod div 1/4 (2).
    static const float syncdef[4] = {0.0f, 5.0f, 0.0f, 2.0f};             // dl_sync,dl_div,md_sync,md_div
    const bool syncGap = (srcVer < 13);
    const int syncAt = HF_DL_SYNC, syncEnd = HF_DL_SYNC + 4;              // sync gap [syncAt,syncEnd)

    float old[HF_N_PORTS];
    std::memcpy(old, vals, sizeof(old));   // snapshot (old values at front, tail zero)
    int o = 0;
    for (int i = 0; i < HF_N_PORTS; ++i) {
        if      (i >= itAt && i < itEnd)                 vals[i] = vdef[i - HF_IT_HUMBK];
        else if (dlGap && i >= dlAt && i < dlEnd)        vals[i] = ddef[i - dlAt];
        else if (woGap && i >= woAt && i < woEnd)        vals[i] = wodef[i - woAt];
        else if (byGap && i >= byAt && i < byEnd)        vals[i] = 0.0f;       // new bypass = active
        else if (nailGap && i >= nailAt && i < nailEnd)  vals[i] = naildef[i - nailAt];
        else if (syncGap && i >= syncAt && i < syncEnd)  vals[i] = syncdef[i - syncAt];
        else                                             vals[i] = old[o++];
    }
}
static void seedFactoryPresets(HexForge* p);   // fwd decl (defined after the factory arrays)
static void hfSerialize(HexForge* p, std::vector<uint8_t>& blob) {
    auto putBytes = [&](const void* d, size_t n){ const uint8_t* b=(const uint8_t*)d; blob.insert(blob.end(), b, b+n); };
    auto putU32   = [&](uint32_t v){ putBytes(&v, 4); };
    auto putPath  = [&](const char* s){ uint32_t len=(uint32_t)std::strlen(s); putU32(len); putBytes(s, len); };
    putU32(13); putU32(kBanks); putU32(kSlots); putU32(HF_N_PORTS); putU32(kFactoryRev);   // v13: + tempo-sync ports (factoryRev still after N_PORTS)
    for (int b=0;b<kBanks;++b) for (int s=0;s<kSlots;++s) {
        const Preset& pr = p->presets[b][s];
        putU32(pr.used ? 1u : 0u);
        putBytes(pr.name, sizeof(pr.name));
        putBytes(pr.vals, sizeof(pr.vals));
        putPath(pr.irPath); putPath(pr.ampNamPath); putPath(pr.drNamPath); putPath(pr.cabNamPath);
    }
    putU32(static_cast<uint32_t>(p->curBank));
    putU32(static_cast<uint32_t>(p->curSlot));
}
static bool hfDeserialize(HexForge* p, const uint8_t* d, size_t size) {
    size_t off = 0;
    auto getU32  = [&](uint32_t& v)->bool { if (off+4>size) return false; std::memcpy(&v, d+off, 4); off+=4; return true; };
    auto getPath = [&](char* dst){ uint32_t len=0; dst[0]='\0';
        if (!getU32(len) || off+len>size) { off=size; return; }
        uint32_t m = len < kPathMax-1 ? len : kPathMax-1;
        std::memcpy(dst, d+off, m); dst[m]='\0'; off += len; };
    uint32_t ver=0, nb=0, ns=0, np=0;
    if (!getU32(ver)) return false; getU32(nb); getU32(ns);
    if (ver < 2 || ver > 13) return false;
    const bool migrateOutDb = (ver == 2);
    const bool needMigrate  = (ver < 13);  // voicing/boost (v4-6) + Seraph (v7) + Wah/Octave (v8) + bypass (v9) + Nail (v12) + tempo sync (v13)
    getU32(np);
    uint32_t factoryRev = 0; if (ver >= 11) getU32(factoryRev);   // v11+: factory-preset revision
    const uint32_t npc = np < (uint32_t)HF_N_PORTS ? np : (uint32_t)HF_N_PORTS;
    for (uint32_t b=0;b<nb;++b) for (uint32_t s=0;s<ns;++s) {
        uint32_t used=0; getU32(used);
        char name[32] = {0}; if (off+sizeof(name)<=size){ std::memcpy(name,d+off,sizeof(name)); off+=sizeof(name); }
        float vals[HF_N_PORTS]; for (int i=0;i<HF_N_PORTS;++i) vals[i]=0.0f;
        if (off + (size_t)np*4 <= size) { std::memcpy(vals, d+off, (size_t)npc*4); off += (size_t)np*4; }
        else off = size;
        if (needMigrate) migratePorts(vals, ver);   // insert voicing/boost + Seraph ports (defaults)
        if (migrateOutDb) vals[HF_OUT_LEVEL] = linToDb(vals[HF_OUT_LEVEL]);
        if (ver < 10) vals[HF_OUT_MONO] = 1.0f;   // pre-v10 saves default to MONO
        char ir[kPathMax],an[kPathMax],dn[kPathMax],cn[kPathMax];
        getPath(ir); getPath(an); getPath(dn); getPath(cn);
        if (b<kBanks && s<kSlots) {
            Preset& pr = p->presets[b][s];
            // Don't let an empty backup slot wipe a factory-seeded preset: this is how
            // new factory presets (e.g. the Banks 2..6 band/song set) reach users whose
            // saved store predates them. A user's own saved preset (used=1) still wins.
            if (used == 0 && pr.used) continue;
            pr.used = (used != 0);
            std::memcpy(pr.name, name, sizeof(pr.name)); pr.name[sizeof(pr.name)-1]='\0';
            std::memcpy(pr.vals, vals, sizeof(pr.vals));
            std::strncpy(pr.irPath,ir,kPathMax-1);     pr.irPath[kPathMax-1]='\0';
            std::strncpy(pr.ampNamPath,an,kPathMax-1); pr.ampNamPath[kPathMax-1]='\0';
            std::strncpy(pr.drNamPath,dn,kPathMax-1);  pr.drNamPath[kPathMax-1]='\0';
            std::strncpy(pr.cabNamPath,cn,kPathMax-1); pr.cabNamPath[kPathMax-1]='\0';
        }
    }
    uint32_t cb=0, cs=0; getU32(cb); getU32(cs);
    p->curBank = cb < kBanks ? cb : 0;
    p->curSlot = cs < kSlots ? cs : 0;
    if (factoryRev < kFactoryRev) seedFactoryPresets(p);   // refresh updated factory slots (user slots kept)
    return true;
}
// Write the store to the backup file (atomic: tmp + rename). Called on every
// save/rename/move and on board save — light, user-triggered file I/O.
static void hfWriteBackup(HexForge* p) {
    std::string dir = hfBackupDir();
    const char* home = std::getenv("HOME");
    if (home && home[0]) { std::string cfg = std::string(home) + "/.config"; ::mkdir(cfg.c_str(), 0755); }
    ::mkdir(dir.c_str(), 0755);
    const std::string path = dir + "/hexforge-presets.dat";
    const std::string tmp  = path + ".tmp";
    std::vector<uint8_t> blob; hfSerialize(p, blob);
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    std::fwrite(blob.data(), 1, blob.size(), f);
    std::fclose(f);
    std::rename(tmp.c_str(), path.c_str());
}
// Load the store from the backup file into presets[]. Returns true on success.
static bool hfLoadBackup(HexForge* p) {
    const std::string path = hfBackupDir() + "/hexforge-presets.dat";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return false; }
    std::vector<uint8_t> blob(static_cast<size_t>(sz));
    size_t rd = std::fread(blob.data(), 1, static_cast<size_t>(sz), f);
    std::fclose(f);
    if (rd != static_cast<size_t>(sz)) return false;
    return hfDeserialize(p, blob.data(), blob.size());
}

// Recall: move the active position to (bank,slot). If the slot holds a preset,
// load its params into eff[] and (re)load its files; if empty, just move the
// cursor (sound unchanged) so the UI can Save the current sound into it.
static void psRecall(HexForge* p, int bank, int slot) {
    p->curBank = bank & (kBanks - 1);
    p->curSlot = slot & (kSlots - 1);
    const Preset& pr = p->presets[p->curBank][p->curSlot];
    hfWriteStatus(p);
    if (pr.used) {
        for (int i = 0; i < HF_N_PORTS; ++i) if (isParamPort(i)) {
            p->eff[i] = pr.vals[i];
            p->lastPort[i] = p->hostPorts[i] ? *p->hostPorts[i] : pr.vals[i];
        }
        schedPath(p, p->irPath,     pr.irPath,     W_CAB_IR,   0);
        schedPath(p, p->ampNamPath, pr.ampNamPath, W_NAM_LOAD, 0);
        schedPath(p, p->drNamPath,  pr.drNamPath,  W_NAM_LOAD, 1);
        schedPath(p, p->cabNamPath, pr.cabNamPath, W_NAM_LOAD, 2);
        if (p->notify) {   // best-effort UI sync; headless recall still changes sound
            emitApply(p);
            writeFileToNotify(p, p->uris.ir_file, p->irPath);
            writeFileToNotify(p, p->uris.amp_nam, p->ampNamPath);
            writeFileToNotify(p, p->uris.dr_nam,  p->drNamPath);
            writeFileToNotify(p, p->uris.cab_nam, p->cabNamPath);
        }
    }
    emitIndex(p);
}
// Save: overwrite the active slot with the live (edited) settings + current files.
static void psSave(HexForge* p) {
    Preset& pr = p->presets[p->curBank][p->curSlot];
    for (int i = 0; i < HF_N_PORTS; ++i) if (isParamPort(i)) pr.vals[i] = p->eff[i];
    std::strncpy(pr.irPath,     p->irPath,     kPathMax - 1); pr.irPath[kPathMax - 1] = '\0';
    std::strncpy(pr.ampNamPath, p->ampNamPath, kPathMax - 1); pr.ampNamPath[kPathMax - 1] = '\0';
    std::strncpy(pr.drNamPath,  p->drNamPath,  kPathMax - 1); pr.drNamPath[kPathMax - 1] = '\0';
    std::strncpy(pr.cabNamPath, p->cabNamPath, kPathMax - 1); pr.cabNamPath[kPathMax - 1] = '\0';
    if (pr.name[0] == '\0')
        std::snprintf(pr.name, sizeof(pr.name), "%c%d", 'A' + p->curSlot, p->curBank + 1);
    pr.used = true;
    emitIndex(p);
    hfWriteStatus(p);
    hfWriteBackup(p);   // keep the off-instance backup current
}
static void psBankDelta(HexForge* p, int d) { psRecall(p, p->curBank + d, p->curSlot); }
// A footswitch tap. Single tap recalls that slot immediately (no delay).
// Double-tapping A or D within ~0.4 s navigates banks — A = down, D = up — and
// lands on that same slot letter in the new bank.
static void psSwitchPress(HexForge* p, int sw) {
    const int64_t now = p->sampleClock;
    const int64_t win = static_cast<int64_t>(p->rate * 0.4);
    const bool dbl = (now - p->lastTapSample[sw]) < win;
    p->lastTapSample[sw] = now;
    if      (sw == 0 && dbl) psRecall(p, p->curBank - 1, 0);   // double-tap A → bank down, slot A
    else if (sw == 3 && dbl) psRecall(p, p->curBank + 1, 3);   // double-tap D → bank up, slot D
    else                     psRecall(p, p->curBank, sw);      // single tap → recall this slot
}
// Move the active preset earlier/later across the flat 32-slot order (= "sort").
// The cursor follows the moved preset; no audio change (same preset content).
static void psMoveDelta(HexForge* p, int d) {
    int flat = p->curBank * kSlots + p->curSlot, tgt = flat + d;
    if (tgt < 0 || tgt >= kBanks * kSlots) return;
    Preset tmp = p->presets[p->curBank][p->curSlot];
    p->presets[p->curBank][p->curSlot] = p->presets[tgt / kSlots][tgt % kSlots];
    p->presets[tgt / kSlots][tgt % kSlots] = tmp;
    p->curBank = tgt / kSlots; p->curSlot = tgt % kSlots;
    emitIndex(p);
    hfWriteBackup(p);
}

// ── Factory presets (Bank 1 / A–D) ────────────────────────────────────────────
// A fresh Hex Forge instance starts with Bank 1 pre-filled with these four
// sounds (Clean / Crunch / Rhythm / Lead) so it's a usable rig the moment it
// loads — the same starting point as the factory pedalboard. Values were
// captured from the dialed-in rig; the IR slot is left empty so they use the
// built-in Factory Cab (no licensed IR file is referenced). hf_restore still
// overwrites these from saved State, so a user's own presets always win.
static const char* const kFactoryName[kSlots] = { "Clean", "Crunch", "Rhythm", "Lead" };
static const float kFactoryVals[kSlots][HF_N_PORTS] = {
// Clean  (out_level -24.0 dB: user's hand-dialed master Output, 2026-06-29)
{ 0, 0, 0, 0, 0, 0, 0, -24, 0, 1, 0, 0, 1, 1, 0, -60, 5, 50, 100, 6, 2, 0, 0, -20, 1, 5, 5, 3, 0, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 1, 0, 0.19, 0.5, 0.58, 1, 0.3, 5, 1, 0, 0.5775, 0.615, 0.635, 0.605, 0.5, 0.7525, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 16000, 1, 7, 0, 0, 0.5, 0.5, 0.5, 0.5, 8, 1, 2, 250, 0.21315, 0.3, 0.5, 0.003, 0.001, 3, 9, 1, 10, 1.5, 0.3, 0, 0.01, 0.335, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
// Crunch
{ 0, 0, 0, 0, 0, 0, 0, -20.58, 0, 1, 0, 0, 1, 1, 1, -60, 5, 50, 100, 6, 2, 1, 0, -20, 1, 5, 5, 3, 0, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 0, 0, 0.145, 0.555, 0.6525, 1, 0.3, 5, 1, 1, 0.2275, 0.365, 0.6825, 0.66, 0.3975, 0.495, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 8660, 1, 7, 0, 0, 0.5, 0.5, 0.5, 0.5, 8, 0, 0, 250, 0.4, 0.3, 0.5, 0.003, 0.001, 10, 9, 1, 10, 1.5, 0.3, 0.5, 0.8, 0.3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
// Rhythm
{ 0, 0, 0, 0, 0, 0, 0, -20, 0, 1, 0, 1, 1, 1, 1, -52, 0.1, 101.25, 213.975, 6, 2, 1, 0, -20, 0, 1.725, 6.85, 3, 2.05, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 1, 0, 0.02, 0.5775, 1, 1, 0.3, 5, 1, 2, 0.6225, 0.415, 0.755, 0.72, 0.5, 0.3525, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 8705, 1, 7, 0, 0, 0.5, 0.5, 0.5, 0.5, 8, 0, 0, 250, 0.4, 0.3, 0.5, 0.003, 0.001, 10, 9, 1, 10, 1.5, 0.3, 0.0175, 0.01, 0.1175, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
// Lead
{ 0, 0, 0, 0, 0, 0, 0, -20.58, 0, 1, 0, 1, 1, 1, 1, -61.6, 0.1, 67.5, 238.85, 6, 2, 1, 0, -18, 0, 2.775, 6.175, 3, 0.55, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 1, 0, 0.0625, 0.6, 0.7175, 1, 0.3, 5, 1, 4, 0.6075, 0.45, 0.7475, 0.7675, 0.6025, 0.47, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 9470, 1, 7, 1, 0, 0.12, 0.6125, 0.5, 0.5, 8, 1, 0, 445.777, 0.31605, 0.17, 0.5, 0.003, 0.001, 10, 9, 1, 10, 1.5, 0.3, 0, 0.01, 0.1475, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
// Write the built-in factory presets over their slots (leaves every non-factory
// slot untouched — so user presets survive). Used both for a fresh instance AND
// to REFRESH factory slots when a saved store predates the current kFactoryRev
// (otherwise updated factory presets in the binary never reach an existing user —
// the persisted .dat / pedalboard state would keep overriding them forever).
static void seedFactoryPresets(HexForge* p) {
    // Bank 1 (index 0): stock Clean/Crunch/Rhythm/Lead — captured as v3-layout rows
    // (before the IT voicing/boost + Seraph ports), so migrate them to the current layout.
    for (int s = 0; s < kSlots; ++s) {
        Preset& pr = p->presets[0][s];
        pr.used = true;
        std::snprintf(pr.name, sizeof(pr.name), "%s", kFactoryName[s]);
        std::memcpy(pr.vals, kFactoryVals[s], sizeof(pr.vals));
        migratePorts(pr.vals, 3);
        pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0] = '\0';
    }
    // Band/song presets (generated in the current layout).
    for (int i = 0; i < kFactoryExtraCount; ++i) {
        const HfFactoryPreset& fp = kFactoryExtra[i];
        if (fp.bank < 0 || fp.bank >= kBanks || fp.slot < 0 || fp.slot >= kSlots) continue;
        Preset& pr = p->presets[fp.bank][fp.slot];
        pr.used = true;
        std::snprintf(pr.name, sizeof(pr.name), "%s", fp.name);
        std::memcpy(pr.vals, fp.vals, sizeof(pr.vals));
        pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0] = '\0';
        if (fp.cabIr && fp.cabIr[0]) { std::strncpy(pr.irPath, fp.cabIr, kPathMax-1); pr.irPath[kPathMax-1]='\0'; }
    }
}
static void psInitDefaults(HexForge* p) {
    seedFactoryPresets(p);
    p->curBank = 0; p->curSlot = 0;
    p->pendingRecall = true;   // a fresh instance starts on Bank 1 / A (Clean)
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
static LV2_Handle hf_instantiate(const LV2_Descriptor*, double rate,
                                 const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) HexForge;
    if (!p) return nullptr;
    p->map      = static_cast<LV2_URID_Map*>(lv2_find_feature(features, LV2_URID__map));
    p->schedule = static_cast<LV2_Worker_Schedule*>(lv2_find_feature(features, LV2_WORKER__schedule));
    if (!p->map || !p->schedule) { delete p; return nullptr; }
    mapURIs(p);
    lv2_atom_forge_init(&p->forge, p->map);

    p->rate = rate;
    p->trimHum.prepare(rate);
    p->trimVoice.reset();   // coeffs are set lazily in run() from the live HB Amount port
    p->trimBoost.reset();
    p->gate.prepare(rate, kMaxBlock, 1);
    p->comp.prepare(rate, kMaxBlock, 1);
    // Build fuzz models directly (NOT via OverdriveFactory) — same as the fuzz plugin.
    p->fuzzMuff   = std::make_unique<OversamplingWrapper>(std::make_unique<EHXBigMuff>());
    p->fuzzBender = std::make_unique<OversamplingWrapper>(std::make_unique<ToneBenderMkII>());
    p->fuzzOctavia= std::make_unique<OversamplingWrapper>(std::make_unique<Octavia>());
    if (!p->fuzzMuff || !p->fuzzBender || !p->fuzzOctavia) { delete p; return nullptr; }
    p->fuzzMuff->prepare(rate, kMaxBlock, 1);
    p->fuzzBender->prepare(rate, kMaxBlock, 1);
    p->fuzzOctavia->prepare(rate, kMaxBlock, 1);
    p->fuzzMuff->setParameter("era", 2.0f);
    p->drive.prepare(rate, kMaxBlock, 1);
    p->drive.setType(kDriveMap[0]);
    // Nail — industrial distortion (oversampled, like the fuzzes)
    p->nail = std::make_unique<OversamplingWrapper>(std::make_unique<NailDistortion>());
    if (!p->nail) { delete p; return nullptr; }
    p->nail->prepare(rate, kMaxBlock, 1);
    p->tuner.prepare(rate);
    p->nail->setParameter("mode", 2.0f);
    p->amp = new(std::nothrow) AmpBlockExtended;
    if (!p->amp) { delete p; return nullptr; }
    p->amp->prepare(rate, kMaxBlock, 2);
    p->amp->setAmpModel(kAmpMap[1]);   // default Crunchy McCrunchFace
    p->pa.prepare(rate, kMaxBlock, 2);
    p->cab.prepare(rate, kMaxBlock, 2);
    p->cab.setIR(DefaultCabIR::generate(rate));
    p->modfx.prepare(rate, kMaxBlock, 2);
    p->modfx.setType(ModulationFactory::fromIndex(0));
    p->delay.prepare(rate, kMaxBlock, 2);
    p->delay.setType(DelayFactory::fromIndex(0));
    p->reverb.prepare(rate, kMaxBlock, 2);
    p->wah.prepare(rate, kMaxBlock, 2);
    p->octave.prepare(rate, kMaxBlock, 2);
    p->autoOut.prepare(rate);
    psInitDefaults(p);   // Bank 1 / A–D pre-filled (overwritten by hf_restore if state exists)
    hfLoadBackup(p);     // recover the user's full preset store across delete/re-add + updates
    return p;
}

static void hf_connect_port(LV2_Handle h, uint32_t port, void* data) {
    auto* p = static_cast<HexForge*>(h);
    if (port == HF_CONTROL)      p->control = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == HF_NOTIFY)  p->notify  = static_cast<LV2_Atom_Sequence*>(data);
    else if (port == HF_MIDI_IN) p->midiIn  = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port < HF_N_PORTS) {
        p->hostPorts[port] = static_cast<float*>(data);
        // Once primed, param ports stay pointed at eff[]; only re-point others.
        if (!p->primed || !isParamPort(static_cast<int>(port)))
            p->ports[port] = static_cast<float*>(data);
    }
}

// ── Worker ────────────────────────────────────────────────────────────────────
static LV2_Worker_Status hf_work(LV2_Handle h, LV2_Worker_Respond_Function respond,
                                 LV2_Worker_Respond_Handle handle, uint32_t, const void* data) {
    auto* p = static_cast<HexForge*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == W_AMP_FREE) { delete msg->amp; return LV2_WORKER_SUCCESS; }
    if (msg->type == W_NAM_FREE) { delete msg->nam; return LV2_WORKER_SUCCESS; }
    if (msg->type == W_CAB_IR) {
        std::vector<float> L, R;
        if (msg->path[0] == '@')                                          // built-in synthetic cab
            p->cab.setIR(CabModels::generate(msg->path, p->rate));
        else if (msg->path[0] && loadIRFile(msg->path, p->rate, L, R)) p->cab.setIR(L, R.empty()?nullptr:&R);
        else p->cab.setIR(DefaultCabIR::generate(p->rate));   // empty path = clear to Factory Cab
        return LV2_WORKER_SUCCESS;
    }
    if (msg->type == W_NAM_LOAD) {
        auto* nm = new(std::nothrow) NamModel;
        if (!nm) return LV2_WORKER_ERR_NO_SPACE;
        if (nm->loadFromFile(msg->path)) nm->reset(p->rate, kMaxBlock);
        WorkMsg reply; reply.type = W_NAM_LOAD; reply.nam = nm; reply.namSlot = msg->namSlot;
        respond(handle, sizeof(reply), &reply);
        return LV2_WORKER_SUCCESS;
    }
    // W_AMP_LOAD — build a fresh amp off the RT thread.
    auto* na = new(std::nothrow) AmpBlockExtended;
    if (!na) return LV2_WORKER_ERR_NO_SPACE;
    na->prepare(p->rate, kMaxBlock, 2);
    na->setAmpModel(kAmpMap[clampi(static_cast<float>(msg->modelIdx), 0, kBacklineIdx)]);
    WorkMsg reply; reply.type = W_AMP_LOAD; reply.amp = na; reply.modelIdx = msg->modelIdx;
    respond(handle, sizeof(reply), &reply);
    return LV2_WORKER_SUCCESS;
}
static LV2_Worker_Status hf_work_response(LV2_Handle h, uint32_t, const void* data) {
    auto* p = static_cast<HexForge*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == W_NAM_LOAD) {
        NamModel** slot = (msg->namSlot == 0) ? &p->ampNam
                        : (msg->namSlot == 1) ? &p->drNam : &p->cabNam;
        NamModel* old = *slot;
        *slot = msg->nam;
        WorkMsg freeMsg; freeMsg.type = W_NAM_FREE; freeMsg.nam = old;
        p->schedule->schedule_work(p->schedule->handle, sizeof(freeMsg), &freeMsg);
        return LV2_WORKER_SUCCESS;
    }
    if (msg->type != W_AMP_LOAD) return LV2_WORKER_SUCCESS;
    AmpBlockExtended* old = p->amp;
    p->amp = msg->amp;
    p->lastAmpModel = msg->modelIdx;
    WorkMsg freeMsg; freeMsg.type = W_AMP_FREE; freeMsg.amp = old;
    p->schedule->schedule_work(p->schedule->handle, sizeof(freeMsg), &freeMsg);
    return LV2_WORKER_SUCCESS;
}

// ── Audio helpers ─────────────────────────────────────────────────────────────
// Run a mono block on the chunk: collapse to mono per real-pedal semantics, then
// mirror the result back to both channels.
static inline void runMono(AudioBlock& b, float* L, float* R, int len, float* s, bool stereo) {
    if (!stereo) for (int i=0;i<len;++i) s[i] = L[i];
    else         for (int i=0;i<len;++i) s[i] = 0.5f*(L[i]+R[i]);
    float* io[1] = { s };
    b.process(io, io, len, 1);
    for (int i=0;i<len;++i) { L[i] = s[i]; R[i] = s[i]; }
}
static inline void runStereo(AudioBlock& b, float* L, float* R, int len, bool& stereo) {
    if (!stereo) for (int i=0;i<len;++i) R[i] = L[i];
    float* io[2] = { L, R };
    b.process(io, io, len, 2);
    stereo = true;
}

static void hf_run(LV2_Handle h, uint32_t n) {
    DenormalGuard denormalGuard;
    auto* p = static_cast<HexForge*>(h);
    const URIs& u = p->uris;

    // ── Prime the preset override layer (once, after all ports are connected) ──
    // Param ports are redirected to read eff[]; everything else reads the host
    // buffer. Done before the atom loop so a first-cycle patch:Get sees real values.
    if (!p->primed) {
        for (int i=0;i<HF_N_PORTS;++i) {
            if (i==HF_CONTROL || i==HF_NOTIFY || i==HF_MIDI_IN) continue;
            if (isParamPort(i)) {
                p->eff[i]      = p->hostPorts[i] ? *p->hostPorts[i] : 0.0f;
                p->lastPort[i] = p->eff[i];
                p->ports[i]    = &p->eff[i];
            } else {
                p->ports[i]    = p->hostPorts[i];
            }
        }
        p->primed = true;
    }

    // ── Atom: IR file set / get, + open notify sequence ──
    const bool haveNotify = (p->notify != nullptr);
    LV2_Atom_Forge_Frame seqFrame;
    if (haveNotify) {
        lv2_atom_forge_set_buffer(&p->forge, reinterpret_cast<uint8_t*>(p->notify), p->notify->atom.size);
        lv2_atom_forge_sequence_head(&p->forge, &seqFrame, 0);
    }
    if (p->control) {
        LV2_ATOM_SEQUENCE_FOREACH(p->control, ev) {
            if (ev->body.type != u.atom_Object) continue;
            const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(&ev->body);
            if (obj->body.otype == u.patch_Set) {
                const LV2_Atom *prop=nullptr, *val=nullptr;
                lv2_atom_object_get(obj, u.patch_property, &prop, u.patch_value, &val, 0);
                if (!prop || prop->type!=u.atom_URID || !val) continue;
                const LV2_URID which = reinterpret_cast<const LV2_Atom_URID*>(prop)->body;
                // Rename the active preset (UI -> plugin, String value).
                if (val->type == u.atom_String) {
                    if (which == u.ps_name) {
                        const char* s = static_cast<const char*>(LV2_ATOM_BODY_CONST(val));
                        Preset& pr = p->presets[p->curBank][p->curSlot];
                        std::strncpy(pr.name, s, sizeof(pr.name)-1); pr.name[sizeof(pr.name)-1]='\0';
                        for (char* c=pr.name; *c; ++c) if (*c=='|') *c=' ';   // keep index delimiter clean
                        pr.used = true;
                        if (haveNotify) emitIndex(p);
                        hfWriteStatus(p);
                        hfWriteBackup(p);
                    }
                    continue;
                }
                if (val->type != u.atom_Path) continue;
                const char* path = static_cast<const char*>(LV2_ATOM_BODY_CONST(val));
                char* dst = nullptr; WorkMsg msg;
                if      (which == u.ir_file) { dst = p->irPath;     msg.type = W_CAB_IR; }
                else if (which == u.amp_nam) { dst = p->ampNamPath; msg.type = W_NAM_LOAD; msg.namSlot = 0; }
                else if (which == u.dr_nam)  { dst = p->drNamPath;  msg.type = W_NAM_LOAD; msg.namSlot = 1; }
                else if (which == u.cab_nam) { dst = p->cabNamPath; msg.type = W_NAM_LOAD; msg.namSlot = 2; }
                if (dst) {
                    // "Factory Cab" sentinel → clear IR to the built-in default.
                    const char* eff = (dst == p->irPath && std::strcmp(path, kFactoryIR) == 0) ? "" : path;
                    std::strncpy(dst, eff, kPathMax-1); dst[kPathMax-1]='\0';
                    std::strncpy(msg.path, dst, kPathMax-1); msg.path[kPathMax-1]='\0';
                    p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
                }
            } else if (obj->body.otype == u.patch_Get && haveNotify) {
                writeFileToNotify(p, u.ir_file, p->irPath);
                writeFileToNotify(p, u.amp_nam, p->ampNamPath);
                writeFileToNotify(p, u.dr_nam, p->drNamPath);
                writeFileToNotify(p, u.cab_nam, p->cabNamPath);
                forgeStringSet(p, u.ps_name, p->presets[p->curBank][p->curSlot].name);
                emitIndex(p);
                emitApply(p);   // sync knobs to the active preset's effective values
            } else if (obj->body.otype == u.time_Position) {
                // Host tempo (tap-tempo / MIDI clock) → cache BPM for the synced Delay/Mod.
                const LV2_Atom* bpmA = nullptr;
                lv2_atom_object_get(obj, u.time_bpm, &bpmA, 0);
                if (bpmA && bpmA->type == u.atom_Float) {
                    const float b = reinterpret_cast<const LV2_Atom_Float*>(bpmA)->body;
                    if (b >= 20.0f && b <= 400.0f) p->hostBpm = b;
                }
            }
        }
    }

    // ── Preset engine: detect live edits, run commands ──
    p->sampleClock += static_cast<int64_t>(n);   // for double-tap timing
    // A knob/host move on any param port overrides its recalled value.
    for (int i=0;i<HF_N_PORTS;++i) if (isParamPort(i)) {
        const float hv = p->hostPorts[i] ? *p->hostPorts[i] : p->lastPort[i];
        if (hv != p->lastPort[i]) { p->eff[i] = hv; p->lastPort[i] = hv; }
    }
    // Apply a preset restored from State (first run after hf_restore).
    if (p->pendingRecall) { p->pendingRecall = false; psRecall(p, p->curBank, p->curSlot); }
    // Command edges (rising 0->1). The four A/B/C/D switches feed the combo
    // detector (single press → recall; two within the window → bank). They reach
    // the plugin either as MIDI-bound control ports (pi-Stomp footswitches send
    // CC 127 on press → port 1) or pulsed by the modgui buttons. The bank/save/
    // move pulses come from the custom modgui.
    auto rose = [&](int port, float& prev)->bool {
        const float v = p->hostPorts[port] ? *p->hostPorts[port] : 0.0f;
        const bool r = (prev < 0.5f && v >= 0.5f); prev = v; return r;
    };
    // A/B/C/D switches recall on EITHER edge (so each press selects its preset,
    // whether the MIDI-bound toggle flips on or off). An ~80 ms debounce drops the
    // modgui button's quick 1->0 release pulse so it counts as one press.
    auto swEdge = [&](int port, float& prev, int sw)->bool {
        const float v = p->hostPorts[port] ? *p->hostPorts[port] : 0.0f;
        const bool flipped = (v >= 0.5f) != (prev >= 0.5f); prev = v;
        if (!flipped) return false;
        if (p->sampleClock - p->lastEdgeSample[sw] < static_cast<int64_t>(p->rate * 0.08)) return false;
        p->lastEdgeSample[sw] = p->sampleClock;
        return true;
    };
    if (swEdge(HF_SW_A, p->swPrev[0], 0)) psSwitchPress(p, 0);
    if (swEdge(HF_SW_B, p->swPrev[1], 1)) psSwitchPress(p, 1);
    if (swEdge(HF_SW_C, p->swPrev[2], 2)) psSwitchPress(p, 2);
    if (swEdge(HF_SW_D, p->swPrev[3], 3)) psSwitchPress(p, 3);
    if (rose(HF_PS_BANK_UP, p->cmdPrev[0])) psBankDelta(p, +1);
    if (rose(HF_PS_BANK_DN, p->cmdPrev[1])) psBankDelta(p, -1);
    if (rose(HF_PS_SAVE,    p->cmdPrev[2])) psSave(p);
    if (rose(HF_PS_MOVE_UP, p->cmdPrev[3])) psMoveDelta(p, -1);
    if (rose(HF_PS_MOVE_DN, p->cmdPrev[4])) psMoveDelta(p, +1);
    if (rose(HF_PS_BACKUP,  p->cmdPrev[5])) hfWriteBackup(p);              // snapshot all 32 to disk
    if (rose(HF_PS_RESTORE, p->cmdPrev[6]) && hfLoadBackup(p))            // pull the backup into this instance
        psRecall(p, p->curBank, p->curSlot);                             // re-applies sound + refreshes the UI list
    // Direct jump from the UI list: recall when ps_goto changes to a valid index.
    {
        const float gf = p->hostPorts[HF_PS_GOTO] ? *p->hostPorts[HF_PS_GOTO] : -1.0f;
        const int g = static_cast<int>(std::lround(gf));
        if (g >= 0 && g < kBanks * kSlots && g != p->lastGoto) { p->lastGoto = g; psRecall(p, g / kSlots, g % kSlots); }
        else if (g < 0) p->lastGoto = -1;
    }
    // ── Footswitch MIDI (pi-Stomp CC 60..63): each CC message = one switch press ──
    if (p->midiIn) {
        LV2_ATOM_SEQUENCE_FOREACH(p->midiIn, ev) {
            if (ev->body.type != u.midi_MidiEvent || ev->body.size < 3) continue;
            const uint8_t* m = static_cast<const uint8_t*>(LV2_ATOM_BODY_CONST(&ev->body));
            if ((m[0] & 0xF0) != 0xB0) continue;            // Control Change, any channel
            if (m[2] < 64) continue;                        // press-down only (ignore the release = value 0)
            const int sw = static_cast<int>(m[1]) - kMidiBaseCC;
            if (sw >= 0 && sw <= 3) psSwitchPress(p, sw);
        }
    }
    // Mirror the active bank/slot to the UI output ports.
    if (p->ports[HF_PS_BANK]) *p->ports[HF_PS_BANK] = static_cast<float>(p->curBank);
    if (p->ports[HF_PS_SLOT]) *p->ports[HF_PS_SLOT] = static_cast<float>(p->curSlot);

    float* inL  = p->ports[HF_IN_L];
    float* inR  = p->ports[HF_IN_R];
    float* outL = p->ports[HF_OUT_L];
    float* outR = p->ports[HF_OUT_R];

    // ── Strobe tuner: detect the dry input pitch when engaged; publish note + cents ──
    const bool tunerOn   = p->ports[HF_TUNER_ON]   && *p->ports[HF_TUNER_ON]   > 0.5f;
    const bool tunerMute = tunerOn && p->ports[HF_TUNER_MUTE] && *p->ports[HF_TUNER_MUTE] > 0.5f;
    if (tunerOn) {
        for (uint32_t off = 0; off < n; off += kMaxBlock) {
            const int len = (int)((n - off > (uint32_t)kMaxBlock) ? kMaxBlock : (n - off));
            for (int i = 0; i < len; ++i) p->mono[i] = 0.5f * (inL[off+i] + inR[off+i]);
            p->tuner.process(p->mono, len);
        }
        if (p->ports[HF_TUNER_NOTE])  *p->ports[HF_TUNER_NOTE]  = (float)p->tuner.note;
        if (p->ports[HF_TUNER_CENTS]) *p->ports[HF_TUNER_CENTS] = p->tuner.cents;
    } else if (p->tuner.note != -1) {
        p->tuner.reset();
        if (p->ports[HF_TUNER_NOTE])  *p->ports[HF_TUNER_NOTE]  = -1.0f;
        if (p->ports[HF_TUNER_CENTS]) *p->ports[HF_TUNER_CENTS] = 0.0f;
    }

    // ── Global bypass: unity passthrough (or silence while tuning-muted) ──
    if (*p->ports[HF_BYPASS] > 0.5f) {
        if (tunerMute) { std::memset(outL, 0, sizeof(float)*n); std::memset(outR, 0, sizeof(float)*n); }
        else {
            if (outL != inL) std::memcpy(outL, inL, sizeof(float)*n);
            if (outR != inR) std::memcpy(outR, inR, sizeof(float)*n);
        }
        if (p->ports[HF_CLIP]) *p->ports[HF_CLIP] = 0.0f;
        if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
        return;
    }

    // ── Configure every block from its ports (once per run) ──
    // Input trim
    const bool  itEnabled = (*p->ports[HF_IT_ENABLE] > 0.5f);
    const float itGain    = std::pow(10.0f, *p->ports[HF_IT_GAIN]/20.0f)
                            * ((*p->ports[HF_IT_PHASE] > 0.5f) ? -1.0f : 1.0f);
    const bool  itHum     = *p->ports[HF_IT_HUM] > 0.5f;
    const bool  itHB      = *p->ports[HF_IT_HUMBK] > 0.5f;
    const int   itHBModel = (int)(*p->ports[HF_IT_HBMODEL] + 0.5f);
    if (itHB) p->trimVoice.prepare(p->rate, itHBModel, *p->ports[HF_IT_HBAMT]);   // recompute on model/amount change
    const bool  itBoost   = *p->ports[HF_IT_BOOST] > 0.5f;
    if (itBoost) p->trimBoost.prepare(p->rate, *p->ports[HF_IT_BOOSTAMT]);
    // Gate
    p->gate.setBypass(false);
    p->gate.setParameter("threshold",  *p->ports[HF_GT_THRESH]);
    p->gate.setParameter("attack",     *p->ports[HF_GT_ATTACK]);
    p->gate.setParameter("hold",       *p->ports[HF_GT_HOLD]);
    p->gate.setParameter("release",    *p->ports[HF_GT_RELEASE]);
    p->gate.setParameter("hysteresis", *p->ports[HF_GT_HYST]);
    // Comp
    p->comp.setBypass(false);
    p->comp.setParameter("type",      *p->ports[HF_CP_TYPE]);
    p->comp.setParameter("threshold", *p->ports[HF_CP_THRESH]);
    p->comp.setParameter("ratio",     *p->ports[HF_CP_RATIO]);
    p->comp.setParameter("attack",    *p->ports[HF_CP_ATTACK]);
    p->comp.setParameter("release",   *p->ports[HF_CP_RELEASE]);
    p->comp.setParameter("knee",      *p->ports[HF_CP_KNEE]);
    p->comp.setParameter("makeup",    *p->ports[HF_CP_MAKEUP]);
    // Fuzz (which pedal chosen at process time)
    const int fuzzPedal = clampi(*p->ports[HF_FZ_PEDAL], 0, 2);
    p->fuzzMuff->setBypass(false); p->fuzzBender->setBypass(false); p->fuzzOctavia->setBypass(false);
    p->fuzzMuff->setParameter("era",   *p->ports[HF_FZ_MODE]);
    p->fuzzMuff->setParameter("drive", *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzMuff->setParameter("tone",  *p->ports[HF_FZ_TONE]);
    p->fuzzMuff->setParameter("level", *p->ports[HF_FZ_VOLUME]);
    p->fuzzBender->setParameter("attack",    *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzBender->setParameter("level",     *p->ports[HF_FZ_VOLUME]);
    p->fuzzBender->setParameter("bias",      *p->ports[HF_FZ_BIAS]);
    p->fuzzBender->setParameter("inputtrim", *p->ports[HF_FZ_INPUTTRIM]);
    p->fuzzBender->setParameter("getemp",    *p->ports[HF_FZ_GETEMP]);
    p->fuzzOctavia->setParameter("drive", *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzOctavia->setParameter("tone",  *p->ports[HF_FZ_TONE]);
    p->fuzzOctavia->setParameter("level", *p->ports[HF_FZ_VOLUME]);
    // Drive
    p->drive.setBypass(false);
    const int driveModel = clampi(*p->ports[HF_DR_MODEL], 0, kDrMax);
    if (driveModel != kDrNamIdx && driveModel != p->lastDriveModel) { p->lastDriveModel = driveModel; p->drive.setType(kDriveMap[driveModel]); }
    p->drive.setParameter("drive",  *p->ports[HF_DR_DRIVE]);
    p->drive.setParameter("tone",   *p->ports[HF_DR_TONE]);
    p->drive.setParameter("level",  *p->ports[HF_DR_LEVEL]);
    p->drive.setParameter("mix",    *p->ports[HF_DR_MIX]);
    p->drive.setParameter("octave", *p->ports[HF_DR_OCTAVE]);
    // Nail — industrial distortion (mode + drive/tone/texture/level)
    p->nail->setBypass(false);
    const int nailMode = clampi(*p->ports[HF_NAIL_MODE], 0, NailDistortion::kNumModes - 1);
    if (nailMode != p->lastNailMode) { p->lastNailMode = nailMode; p->nail->setParameter("mode", (float)nailMode); }
    p->nail->setParameter("drive",   *p->ports[HF_NAIL_DRIVE]);
    p->nail->setParameter("tone",    *p->ports[HF_NAIL_TONE]);
    p->nail->setParameter("texture", *p->ports[HF_NAIL_TEXTURE]);
    p->nail->setParameter("level",   *p->ports[HF_NAIL_LEVEL]);
    // Amp
    const int ampModel = clampi(*p->ports[HF_AMP_MODEL], 0, kBacklineIdx);
    const int ampAlgo  = (ampModel == 5) ? 1 : ampModel;   // NAM(5)→1 safe; 6=Beardo,7=Hiwatt,8=Vox identity
    const bool ampIsAlgo = (ampModel <= 4) || (ampModel == kFriedmanIdx) || (ampModel == kHiwattIdx) || (ampModel == kVoxIdx) || (ampModel == kBacklineIdx);
    if (ampIsAlgo && ampModel != p->lastAmpModel) {   // rebuild only for algo models
        WorkMsg msg; msg.type=W_AMP_LOAD; msg.modelIdx=ampModel;
        if (p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg) == LV2_WORKER_SUCCESS)
            p->lastAmpModel = ampModel;
    }
    AmpBlockExtended* amp = p->amp;
    amp->setBypass(false);
    if (ampModel == kSunnIdx) {
        amp->setParameter("vol1",         *p->ports[HF_AMP_GAIN]);
        amp->setParameter("vol2",         *p->ports[HF_AMP_SUNN_VOL2]);
        amp->setParameter("channel_link", *p->ports[HF_AMP_SUNN_LINK]);
        amp->setParameter("bass1",        *p->ports[HF_AMP_BASS]);
        amp->setParameter("mid1",         *p->ports[HF_AMP_MID]);
        amp->setParameter("treble1",      *p->ports[HF_AMP_TREBLE]);
        amp->setParameter("bass2",        *p->ports[HF_AMP_SUNN_BASS2]);
        amp->setParameter("mid2",         *p->ports[HF_AMP_SUNN_MID2]);
        amp->setParameter("treble2",      *p->ports[HF_AMP_SUNN_TREBLE2]);
        amp->setParameter("bright1",      *p->ports[HF_AMP_SUNN_BRIGHT1]);
        amp->setParameter("bright2",      *p->ports[HF_AMP_SUNN_BRIGHT2]);
    } else {
        amp->setParameter("gain",   *p->ports[HF_AMP_GAIN]);
        amp->setParameter("bass",   *p->ports[HF_AMP_BASS]);
        amp->setParameter("mid",    *p->ports[HF_AMP_MID]);
        amp->setParameter("treble", *p->ports[HF_AMP_TREBLE]);
    }
    amp->setParameter("presence",  *p->ports[HF_AMP_PRESENCE]);
    amp->setParameter("master",    *p->ports[HF_AMP_MASTER]);
    amp->setParameter("sag",       *p->ports[HF_AMP_SAG]);
    amp->setParameter("channel",   *p->ports[HF_AMP_CHANNEL]);
    amp->setParameter("resonance", *p->ports[HF_AMP_RESONANCE]);
    // Beardo BE (Friedman): its own 3-way channel (Clean/BE/HBE) + voicing toggles.
    if (ampModel == kFriedmanIdx) {
        amp->setParameter("channel", *p->ports[HF_AMP_FR_CHANNEL]);
        amp->setParameter("fat",     *p->ports[HF_AMP_FR_FAT]);
        amp->setParameter("c45",     *p->ports[HF_AMP_FR_C45]);
        amp->setParameter("sat",     *p->ports[HF_AMP_FR_SAT]);
    }
    int desiredTube;
    if (*p->ports[HF_AMP_PAMP_AUTO] > 0.5f) {
        const auto d = PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]);
        p->pa.setParameter("master", d.master); p->pa.setParameter("presence", d.presence);
        p->pa.setParameter("depth", d.depth);   p->pa.setParameter("nfb", d.nfb);
        p->pa.setParameter("sag", d.sag);
        p->pa.setParameter("resonance", *p->ports[HF_AMP_PAMP_RESONANCE]);
        p->pa.setParameter("airFeel",   *p->ports[HF_AMP_PAMP_AIRFEEL]);
        desiredTube = kAmpTube[ampAlgo];
    } else {
        p->pa.setParameter("presence",  *p->ports[HF_AMP_PAMP_PRESENCE]);
        p->pa.setParameter("depth",     *p->ports[HF_AMP_PAMP_DEPTH]);
        p->pa.setParameter("sag",       *p->ports[HF_AMP_PAMP_SAG]);
        p->pa.setParameter("master",    *p->ports[HF_AMP_PAMP_MASTER]);
        p->pa.setParameter("nfb",       *p->ports[HF_AMP_PAMP_NFB]);
        p->pa.setParameter("resonance", *p->ports[HF_AMP_PAMP_RESONANCE]);
        p->pa.setParameter("airFeel",   *p->ports[HF_AMP_PAMP_AIRFEEL]);
        desiredTube = clampi(*p->ports[HF_AMP_PAMP_TUBE], 0, 3);
    }
    // Post-saturation sag-VCA depth is a per-amp voicing value with no user port.
    p->pa.setParameter("bloomvca", PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).bloomVca);
    if (desiredTube != p->lastAmpTube) { p->lastAmpTube = desiredTube; p->pa.setTubeType(static_cast<TubeType>(desiredTube)); }
    const bool paBypass = (*p->ports[HF_AMP_PAMP_BYPASS] > 0.5f) || (ampModel == kSunnIdx);
    p->pa.setBypass(paBypass);
    const float ampMakeup = kAmpMakeup[ampAlgo];
    // Cab
    p->cab.setBypass(false);
    p->cab.setParameter("lowCutHz",  *p->ports[HF_CAB_LOWCUT]);
    p->cab.setParameter("highCutHz", *p->ports[HF_CAB_HIGHCUT]);
    p->cab.setParameter("mix",       *p->ports[HF_CAB_MIX]);
    // Modfx
    p->modfx.setBypass(false);
    const int modfxType = clampi(*p->ports[HF_MD_TYPE], 0, 6);
    if (modfxType != p->lastModfxType) { p->lastModfxType = modfxType; p->modfx.setType(ModulationFactory::fromIndex(modfxType)); }
    // Mod clock sync: lock the LFO to host BPM x division (0 => free-run from the Rate knob).
    if (*p->ports[HF_MD_SYNC] > 0.5f) {
        const int mv = clampi(*p->ports[HF_MD_DIV], 0, 7);
        const float periodSec = (60.0f / p->hostBpm) * kDivFactor[mv];
        p->modfx.setSyncHz(periodSec > 0.0f ? (1.0f / periodSec) : 0.0f);
    } else {
        p->modfx.setSyncHz(0.0f);
    }
    p->modfx.setParameter("rate",        *p->ports[HF_MD_RATE]);
    p->modfx.setParameter("depth",       *p->ports[HF_MD_DEPTH]);
    p->modfx.setParameter("mix",         *p->ports[HF_MD_MIX]);
    // Mono mode: force EVERY width-based stereo effect to CENTERED (width 0) so a summed-
    // mono rig keeps full-level content. Otherwise the delay's pan (Seraph) loses ~6 dB to
    // pan law, the Digital delay's L/R time offset combs, and the chorus/Uni-Vibe LFO-phase
    // spread partially cancels — all read as the effect "cutting out" in mono. Reverb has no
    // width knob (decorrelated tails just narrow, never null) so the output sum covers it.
    const bool monoOut = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
    p->modfx.setParameter("stereoWidth", monoOut ? 0.0f : *p->ports[HF_MD_WIDTH]);
    // Delay
    p->delay.setBypass(false);
    const int delayType = clampi(*p->ports[HF_DL_TYPE], 0, 3);
    if (delayType != p->lastDelayType) { p->lastDelayType = delayType; p->delay.setType(DelayFactory::fromIndex(delayType)); }
    // Delay clock sync: delay time = host BPM x division (else the manual Time knob).
    float dlMs = *p->ports[HF_DL_TIME];
    if (*p->ports[HF_DL_SYNC] > 0.5f) {
        const int dv = clampi(*p->ports[HF_DL_DIV], 0, 7);
        dlMs = (60000.0f / p->hostBpm) * kDivFactor[dv];
        if (dlMs < 1.0f) dlMs = 1.0f; else if (dlMs > 2000.0f) dlMs = 2000.0f;
    }
    p->delay.setParameter("timeMs",       dlMs);
    p->delay.setParameter("feedback",     *p->ports[HF_DL_FEEDBACK]);
    p->delay.setParameter("mix",          *p->ports[HF_DL_MIX]);
    p->delay.setParameter("stereoWidth",  monoOut ? 0.0f : *p->ports[HF_DL_WIDTH]);
    p->delay.setParameter("wowDepth",     *p->ports[HF_DL_WOW]);
    p->delay.setParameter("flutterDepth", *p->ports[HF_DL_FLUTTER]);
    p->delay.setParameter("headMask", static_cast<float>(kEchorecProgram[clampi(*p->ports[HF_DL_HEADS],0,11)]));
    p->delay.setParameter("pattern",  *p->ports[HF_DL_PATTERN]);    // Seraph
    p->delay.setParameter("ducking",  *p->ports[HF_DL_DUCKING]);    // Seraph
    p->delay.setParameter("modDepth", *p->ports[HF_DL_MODDEPTH]);   // Seraph
    p->delay.setParameter("modRate",  *p->ports[HF_DL_MODRATE]);    // Seraph
    // Reverb
    p->reverb.setBypass(false);
    p->reverb.setParameter("preDelayMs", *p->ports[HF_RV_PREDELAY]);
    p->reverb.setParameter("decayTime",  *p->ports[HF_RV_DECAY]);
    p->reverb.setParameter("damping",    *p->ports[HF_RV_DAMPING]);
    p->reverb.setParameter("modDepth",   *p->ports[HF_RV_MODDEPTH]);
    p->reverb.setParameter("modRate",    *p->ports[HF_RV_MODRATE]);
    p->reverb.setParameter("mix",        *p->ports[HF_RV_MIX]);
    // Wah
    p->wah.setBypass(false);
    p->wah.setParameter("type",  *p->ports[HF_WH_TYPE]);
    p->wah.setParameter("freq",  *p->ports[HF_WH_FREQ]);
    p->wah.setParameter("depth", *p->ports[HF_WH_DEPTH]);
    p->wah.setParameter("sens",  *p->ports[HF_WH_SENS]);
    p->wah.setParameter("q",     *p->ports[HF_WH_Q]);
    p->wah.setParameter("mix",   *p->ports[HF_WH_MIX]);
    // Octave
    p->octave.setBypass(false);
    p->octave.setParameter("up",   *p->ports[HF_OC_UP]);
    p->octave.setParameter("down", *p->ports[HF_OC_DOWN]);
    p->octave.setParameter("dry",  *p->ports[HF_OC_DRY]);

    // ── Resolve chain order (Input Trim locked first; rest sorted by pos) ──
    int order[B_COUNT];
    for (int i=0;i<B_COUNT;++i) order[i] = i;
    int posv[B_COUNT];
    for (int i=0;i<B_COUNT;++i) posv[i] = clampi(*p->ports[kPosPort[i]], 1, 12);
    // stable selection sort by (pos, canonical index)
    for (int a=0;a<B_COUNT-1;++a) {
        int best=a;
        for (int b=a+1;b<B_COUNT;++b)
            if (posv[order[b]] < posv[order[best]]) best=b;
        if (best!=a) { int t=order[a]; order[a]=order[best]; order[best]=t; }
    }
    bool enabled[B_COUNT];   // "run this block": in the chain AND not bypassed
    for (int i=0;i<B_COUNT;++i)
        enabled[i] = (*p->ports[kEnablePort[i]] > 0.5f) && (*p->ports[kBypassPort[i]] <= 0.5f);

    // ── Process in <= kMaxBlock chunks; each chunk runs the whole chain ──
    for (uint32_t off=0; off<n; off+=kMaxBlock) {
        const int len = static_cast<int>((n-off > (uint32_t)kMaxBlock) ? kMaxBlock : (n-off));
        float* L = outL + off; float* R = outR + off;
        // seed chunk from input
        for (int i=0;i<len;++i) { L[i] = inL[off+i]; R[i] = inR[off+i]; }
        bool stereo = false;

        // Input Trim (locked head of chain)
        if (itEnabled) {
            for (int i=0;i<len;++i) {
                float x = L[i];
                if (itHum) x = p->trimHum.process(x);
                if (itHB)    x = p->trimVoice.process(x);   // single-coil -> humbucker voicing
                if (itBoost) x = p->trimBoost.process(x);   // output boost + beef
                x *= itGain;
                L[i] = x; R[i] = x;
            }
        }

        for (int oi=0; oi<B_COUNT; ++oi) {
            const int id = order[oi];
            if (!enabled[id]) continue;
            switch (id) {
                case B_GATE:  runMono(p->gate, L, R, len, p->mono, stereo); break;
                case B_COMP:  runMono(p->comp, L, R, len, p->mono, stereo); break;
                case B_FUZZ:  runMono(fuzzPedal==0 ? *p->fuzzMuff : (fuzzPedal==1 ? *p->fuzzBender : *p->fuzzOctavia), L, R, len, p->mono, stereo); break;
                case B_DRIVE:
                    if (driveModel == kDrNamIdx && p->drNam && p->drNam->isLoaded()) {
                        // Neural drive: mono; Level (x2) + Mix dry/wet.
                        if (!stereo) for (int i=0;i<len;++i) p->mono[i]=L[i];
                        else         for (int i=0;i<len;++i) p->mono[i]=0.5f*(L[i]+R[i]);
                        p->drNam->processBuffer(p->mono, p->monoOut, len);
                        const float mix=*p->ports[HF_DR_MIX], wet=*p->ports[HF_DR_LEVEL]*2.0f*mix, dry=1.0f-mix;
                        for (int i=0;i<len;++i){ float o=dry*p->mono[i]+wet*p->monoOut[i]; L[i]=o; R[i]=o; }
                    } else runMono(p->drive, L, R, len, p->mono, stereo);
                    break;
                case B_AMP: {
                    if (!stereo) for (int i=0;i<len;++i) R[i]=L[i];
                    // Input headroom (see kAmpInputCeil): soft ceiling so a hot upstream block
                    // can't slam the amp front-end. Applies to both NAM + algorithmic amps.
                    { const float A = kAmpInputCeil, iA = 1.0f/A;
                      for (int i=0;i<len;++i){ L[i]=A*std::tanh(L[i]*iA); R[i]=A*std::tanh(R[i]*iA); } }
                    if (ampModel == kAmpNamIdx && p->ampNam && p->ampNam->isLoaded()) {
                        // Neural amp: mono capture -> both; Master = output trim; no power amp.
                        for (int i=0;i<len;++i) p->mono[i]=0.5f*(L[i]+R[i]);
                        p->ampNam->processBuffer(p->mono, p->monoOut, len);
                        const float trim=*p->ports[HF_AMP_MASTER];
                        for (int i=0;i<len;++i){ float y=p->monoOut[i]*trim; L[i]=y; R[i]=y; }
                    } else {
                        float* io[2] = { L, R };
                        amp->process(io, io, len, 2);
                        p->pa.process(io, io, len, 2);
                        if (ampMakeup != 1.0f) for (int i=0;i<len;++i){ L[i]*=ampMakeup; R[i]*=ampMakeup; }
                    }
                    stereo = true;
                    break;
                }
                case B_CAB:
                    if (p->cabNam && p->cabNam->isLoaded()) {
                        // Neural cab/rig overrides the IR convolver; Mix = dry/wet.
                        if (!stereo) for (int i=0;i<len;++i) R[i]=L[i];
                        for (int i=0;i<len;++i) p->mono[i]=0.5f*(L[i]+R[i]);
                        p->cabNam->processBuffer(p->mono, p->monoOut, len);
                        const float mix=*p->ports[HF_CAB_MIX], dry=1.0f-mix;
                        for (int i=0;i<len;++i){ float w=p->monoOut[i]*mix; L[i]=dry*L[i]+w; R[i]=dry*R[i]+w; }
                        stereo = true;
                    } else runStereo(p->cab, L, R, len, stereo);
                    break;
                case B_MODFX:  runStereo(p->modfx,  L, R, len, stereo); break;
                case B_DELAY:  runStereo(p->delay,  L, R, len, stereo); break;
                case B_REVERB: runStereo(p->reverb, L, R, len, stereo); break;
                case B_WAH:    runMono(p->wah, L, R, len, p->mono, stereo); break;
                case B_OCTAVE: runMono(p->octave, L, R, len, p->mono, stereo); break;
                case B_NAIL:   runMono(*p->nail, L, R, len, p->mono, stereo); break;
            }
        }
        // If nothing ever spread to stereo, R already mirrors L (mono blocks wrote both;
        // if the whole chain was empty, copy L→R for a mono-correct output).
        if (!stereo) for (int i=0;i<len;++i) R[i] = L[i];
    }

    // ── Mono sum (default ON): collapse L+R -> 0.5*(L+R) so a MONO rig (pi-Stomp
    // into one amp) keeps ALL panned/stereo-widened content (Seraph engines,
    // choruses, wide delay/reverb) instead of losing whatever's on the unused jack.
    // Our width is pan/decorrelation (never anti-phase), so the sum can't null. ──
    if (p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f)
        for (uint32_t i = 0; i < n; ++i) { const float mo = 0.5f*(outL[i]+outR[i]); outL[i]=mo; outR[i]=mo; }

    // ── Master output level (the "Output" stage — last in the chain) ──
    // The knob is in dB (0 dB = unity, up to +12 dB boost); convert to a linear
    // gain and apply it smoothed. Auto-Limit only adds the clip-safe limiter on
    // top — below the ceiling both modes sound identical.
    const float outGain = dbToGain(*p->ports[HF_OUT_LEVEL]);
    const bool  outLimit = *p->ports[HF_OUT_AUTO] > 0.5f;
    p->autoOut.process(outL, outR, n, outGain, outLimit);

    // Silence the output while tuning (mute engaged) — the tuner still reads the dry input.
    if (tunerMute) { std::memset(outL, 0, sizeof(float)*n); std::memset(outR, 0, sizeof(float)*n); }

    // ── Clip indicator: latch for ~250 ms whenever the output hits full scale ──
    float peak = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float a = std::fabs(outL[i]); if (a > peak) peak = a;
        a = std::fabs(outR[i]);       if (a > peak) peak = a;
    }
    if (peak >= 0.999f) p->clipHold = static_cast<int>(p->rate * 0.25);
    if (p->ports[HF_CLIP]) *p->ports[HF_CLIP] = (p->clipHold > 0) ? 1.0f : 0.0f;
    if (p->clipHold > 0)   p->clipHold -= static_cast<int>(n);

    // ── Input / output level meters: smoothed peak, mapped -60..0 dB -> 0..1 ──
    float ipk = 0.0f;
    if (inL && inR) for (uint32_t i = 0; i < n; ++i) {
        float a = std::fabs(inL[i]); if (a > ipk) ipk = a;
        a = std::fabs(inR[i]);       if (a > ipk) ipk = a;
    }
    p->meterIn  = std::fmax(p->meterIn  * 0.82f, ipk);
    p->meterOut = std::fmax(p->meterOut * 0.82f, peak);   // 'peak' = output peak from the clip scan
    auto mnorm = [](float pk) -> float {
        if (pk < 1.0e-4f) return 0.0f;
        float v = (20.0f * std::log10(pk) + 60.0f) / 60.0f;
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    };
    const float mIn  = mnorm(p->meterIn);
    const float mOut = mnorm(p->meterOut);
    if (p->ports[HF_IN_METER])  *p->ports[HF_IN_METER]  = mIn;
    if (p->ports[HF_OUT_METER]) *p->ports[HF_OUT_METER] = mOut;
    // Push to the custom modgui via the notify atom (~25 Hz) — output control ports
    // don't reliably reach the icon's change callback on MODEP, so reuse the preset
    // UI's atom channel.
    p->meterFrames += n;
    if (haveNotify && p->meterFrames >= (uint32_t)(p->rate / 14.0f)) {
        p->meterFrames = 0;
        // Deadband: only push when a bar moved >~1.5% — steady levels/silence cost nothing,
        // which keeps mod-ui's browser UI from churning on a constant atom stream.
        if (std::fabs(mIn - p->meterSentIn) > 0.015f || std::fabs(mOut - p->meterSentOut) > 0.015f) {
            p->meterSentIn = mIn; p->meterSentOut = mOut;
            char mbuf[24]; std::snprintf(mbuf, sizeof(mbuf), "%.3f|%.3f", mIn, mOut);
            forgeStringSet(p, p->uris.meters, mbuf);
        }
    }

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

static void hf_cleanup(LV2_Handle h) {
    auto* p = static_cast<HexForge*>(h);
    delete p->amp;
    delete p->ampNam; delete p->drNam; delete p->cabNam;
    delete p;
}

// ── State (persist the loaded IR path) ────────────────────────────────────────
static LV2_State_Status hf_save(LV2_Handle h, LV2_State_Store_Function store,
                                LV2_State_Handle handle, uint32_t flags,
                                const LV2_Feature* const* features) {
    auto* p = static_cast<HexForge*>(h);
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    auto saveOne = [&](LV2_URID prop, const char* raw) {
        if (raw[0] == '\0') return;
        char* ap = mapPath ? mapPath->abstract_path(mapPath->handle, const_cast<char*>(raw)) : const_cast<char*>(raw);
        store(handle, prop, ap, std::strlen(ap)+1, p->uris.atom_Path,
              flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
        if (mapPath && ap != raw) free(ap);
    };
    saveOne(p->uris.ir_file, p->irPath);
    saveOne(p->uris.amp_nam, p->ampNamPath);
    saveOne(p->uris.dr_nam,  p->drNamPath);
    saveOne(p->uris.cab_nam, p->cabNamPath);

    // ── Preset store: one self-describing Chunk holding all 8×4 presets ──
    std::vector<uint8_t> blob;
    auto putBytes = [&](const void* d, size_t n){ const uint8_t* b=(const uint8_t*)d; blob.insert(blob.end(), b, b+n); };
    auto putU32   = [&](uint32_t v){ putBytes(&v, 4); };
    auto putPath  = [&](const char* raw){   // stored portable (abstract)
        char* ap = (raw[0] && mapPath) ? mapPath->abstract_path(mapPath->handle, const_cast<char*>(raw)) : nullptr;
        const char* s = ap ? ap : raw;
        uint32_t len = static_cast<uint32_t>(std::strlen(s));
        putU32(len); putBytes(s, len);
        if (ap) free(ap);
    };
    putU32(13);                 // version (13: + tempo-sync; 12: + Nail block; 11: + factory rev; 10: + Output Mono Sum; 9: + per-block bypass; 8: + Wah/Octave; 7: + Seraph delay; 6: + Boost; 5: + HB Model; 4: + HB voicing; 3: dB; 2: linear)
    putU32(kBanks); putU32(kSlots); putU32(HF_N_PORTS); putU32(kFactoryRev);
    for (int b=0;b<kBanks;++b) for (int s=0;s<kSlots;++s) {
        const Preset& pr = p->presets[b][s];
        putU32(pr.used ? 1u : 0u);
        putBytes(pr.name, sizeof(pr.name));
        putBytes(pr.vals, sizeof(pr.vals));
        putPath(pr.irPath); putPath(pr.ampNamPath); putPath(pr.drNamPath); putPath(pr.cabNamPath);
    }
    putU32(static_cast<uint32_t>(p->curBank));
    putU32(static_cast<uint32_t>(p->curSlot));
    store(handle, p->uris.preset_blob, blob.data(), blob.size(), p->uris.atom_Chunk,
          flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    hfWriteBackup(p);   // mirror the store to the off-instance backup on every board save
    return LV2_STATE_SUCCESS;
}
static LV2_State_Status hf_restore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                                   LV2_State_Handle handle, uint32_t,
                                   const LV2_Feature* const* features) {
    auto* p = static_cast<HexForge*>(h);
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    size_t size=0; uint32_t type=0, vflags=0;

    auto absOf = [&](const void* val) -> char* {
        const char* ap = static_cast<const char*>(val);
        return mapPath ? mapPath->absolute_path(mapPath->handle, ap) : const_cast<char*>(ap);
    };
    // IR
    const void* irv = retrieve(handle, p->uris.ir_file, &size, &type, &vflags);
    if (irv && type == p->uris.atom_Path) {
        char* path = absOf(irv);
        std::vector<float> L, R;
        if (loadIRFile(path, p->rate, L, R)) {
            p->cab.setIR(L, R.empty()?nullptr:&R);
            std::strncpy(p->irPath, path, kPathMax-1); p->irPath[kPathMax-1]='\0';
        }
        if (mapPath && path != irv) free(path);
    }
    // NAM x3
    auto restoreNam = [&](LV2_URID prop, NamModel** slot, char* pathDst) {
        const void* v = retrieve(handle, prop, &size, &type, &vflags);
        if (!v || type != p->uris.atom_Path) return;
        char* path = absOf(v);
        auto* nm = new(std::nothrow) NamModel;
        if (nm && nm->loadFromFile(path)) {
            nm->reset(p->rate, kMaxBlock);
            delete *slot; *slot = nm;
            std::strncpy(pathDst, path, kPathMax-1); pathDst[kPathMax-1]='\0';
        } else delete nm;
        if (mapPath && path != v) free(path);
    };
    restoreNam(p->uris.amp_nam, &p->ampNam, p->ampNamPath);
    restoreNam(p->uris.dr_nam,  &p->drNam,  p->drNamPath);
    restoreNam(p->uris.cab_nam, &p->cabNam, p->cabNamPath);

    // ── Preset store ──
    const void* bv = retrieve(handle, p->uris.preset_blob, &size, &type, &vflags);
    if (bv && type == p->uris.atom_Chunk && size >= 12) {
        const uint8_t* d = static_cast<const uint8_t*>(bv); size_t off = 0;
        auto getU32 = [&](uint32_t& v)->bool { if (off+4>size) return false; std::memcpy(&v, d+off, 4); off+=4; return true; };
        auto getPath = [&](char* dst){
            uint32_t len=0; dst[0]='\0';
            if (!getU32(len) || off+len>size) { off = size; return; }
            char tmp[kPathMax]; uint32_t m = len < kPathMax-1 ? len : kPathMax-1;
            std::memcpy(tmp, d+off, m); tmp[m]='\0'; off += len;
            if (tmp[0] && mapPath) { char* ab = mapPath->absolute_path(mapPath->handle, tmp);
                std::strncpy(dst, ab, kPathMax-1); dst[kPathMax-1]='\0'; if (ab != tmp) free(ab); }
            else { std::strncpy(dst, tmp, kPathMax-1); dst[kPathMax-1]='\0'; }
        };
        uint32_t ver=0, nb=0, ns=0, np=0; getU32(ver); getU32(nb); getU32(ns);
        if (ver < 2 || ver > 13) return LV2_STATE_SUCCESS;    // unknown layout — start fresh
        const bool migrateOutDb = (ver == 2);     // v2 stored out_level as 0..1 linear
        const bool needMigrate  = (ver < 13);     // voicing/boost (v4-6) + Seraph (v7) + Wah/Octave (v8) + bypass (v9) + Nail (v12) + tempo sync (v13)
        getU32(np);                                 // param-port count at save time
        uint32_t factoryRev = 0; if (ver >= 11) getU32(factoryRev);   // v11+: factory-preset revision
        const uint32_t npc = np < (uint32_t)HF_N_PORTS ? np : (uint32_t)HF_N_PORTS;
        for (uint32_t b=0;b<nb;++b) for (uint32_t s=0;s<ns;++s) {
            uint32_t used=0; getU32(used);
            char name[32] = {0}; if (off+sizeof(name)<=size){ std::memcpy(name,d+off,sizeof(name)); off+=sizeof(name); }
            float vals[HF_N_PORTS]; for (int i=0;i<HF_N_PORTS;++i) vals[i]=0.0f;
            if (off + (size_t)np*4 <= size) { std::memcpy(vals, d+off, (size_t)npc*4); off += (size_t)np*4; }
            else off = size;
            if (needMigrate) migratePorts(vals, ver);                 // insert voicing/boost + Seraph ports
            if (migrateOutDb) vals[HF_OUT_LEVEL] = linToDb(vals[HF_OUT_LEVEL]);  // 0..1 -> dB
            if (ver < 10) vals[HF_OUT_MONO] = 1.0f;   // pre-v10 saves default to MONO
            char ir[kPathMax],an[kPathMax],dn[kPathMax],cn[kPathMax];
            getPath(ir); getPath(an); getPath(dn); getPath(cn);
            if (b<kBanks && s<kSlots) {     // ignore extras if a future build grows the grid
                Preset& pr = p->presets[b][s];
                if (used == 0 && pr.used) continue;   // keep factory-seeded preset in an empty restored slot
                pr.used = (used != 0);
                std::memcpy(pr.name, name, sizeof(pr.name)); pr.name[sizeof(pr.name)-1]='\0';
                std::memcpy(pr.vals, vals, sizeof(pr.vals));
                std::strncpy(pr.irPath,ir,kPathMax-1);     pr.irPath[kPathMax-1]='\0';
                std::strncpy(pr.ampNamPath,an,kPathMax-1); pr.ampNamPath[kPathMax-1]='\0';
                std::strncpy(pr.drNamPath,dn,kPathMax-1);  pr.drNamPath[kPathMax-1]='\0';
                std::strncpy(pr.cabNamPath,cn,kPathMax-1); pr.cabNamPath[kPathMax-1]='\0';
            }
        }
        uint32_t cb=0, cs=0; getU32(cb); getU32(cs);
        p->curBank = cb < kBanks ? cb : 0;
        p->curSlot = cs < kSlots ? cs : 0;
        if (factoryRev < kFactoryRev) seedFactoryPresets(p);   // refresh updated factory slots (user slots kept)
        p->pendingRecall = true;   // first run applies the active preset to the DSP
    }
    return LV2_STATE_SUCCESS;
}

static const void* hf_extension_data(const char* uri) {
    static const LV2_Worker_Interface worker = { hf_work, hf_work_response, nullptr };
    static const LV2_State_Interface  state  = { hf_save, hf_restore };
    if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
    if (!std::strcmp(uri, LV2_STATE__interface))  return &state;
    return nullptr;
}

LV2_EXPORT_DESCRIPTOR(HEXFORGE_URI,
    hf_instantiate, hf_connect_port,
    nullptr, hf_run, nullptr, hf_cleanup, hf_extension_data)
