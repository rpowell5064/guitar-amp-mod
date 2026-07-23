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
#include "HumNotchComb.h"
#include "IrResample.h"
#include "NoiseGateBlock.h"
#include "CompressorBlock.h"
#include "OversamplingWrapper.h"
#include "EHXBigMuff.h"
#include "Octavia.h"
#include "ToneBenderMkII.h"
#include "ZVexFuzzFactory.h"
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
static constexpr int kDrMax     = 7;   // highest dr_model index (7 = Preamp 250 / DOD 250)

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
static constexpr uint32_t kFactoryRev = 44;  // 44: Bank 1 room (2026-07-14, user: "make sure my first four presets have it") — the stock Clean/Crunch/Rhythm/Lead seeds now get roomon 1 / mix 0.12 / size 0.35 explicitly after their v3 migration (appended ports migrate in as 0). 43: ROOM ON BY DEFAULT (2026-07-14, user) — every preset now carries a subtle small-to-medium room (roomon 1, mix 0.12, size 0.35 via the seeded defaults; old user blobs zero-fill = off, their sounds untouched); full loudness re-measure baked. Cab panel rearranged: CABINET over ROOM left, mic pad right. 42: CAB ROOM AMBIENCE (2026-07-14) — cab_roomon/roommix/roomamt: a small Schroeder room (4 damped combs + allpass) after the cab convolution, toggle + mix + size, default OFF (bit-identical; presets reseeded off). 41: MIC PLACEMENTS IN PRESETS + NEVERMIND WALL REBUILD (2026-07-14) — (a) researched per-rig mic positions baked into 35 presets ('60s = backed-off warm placements: Hendrix .30/.25, Apache .20/.50, Surf .15/.40; '70s = off-cap + air: Trower/May/Gilmour ~.25-.30/.15-.20; room-heavy doom/psych: Wizard's Doom .40/.35, Sleep .35/.30, Innerspeaker .30/.35; Vig-era Nirvana .20-.25/.05-.10; modern tight metal (Mesa/EVH/Periphery/Mastodon/Gojira) + NIN direct rigs stay 0/0 = close cap edge; user-preserved presets untouched); full loudness re-measure follows. (b) Nevermind Wall REBUILT to the documented SLTS chain (user: old rework "is trash"): Mustang -> DS-1 at Kurt's settings (tone 10 o'clock, dist 4, LEVEL MAXED) -> pushed Fender Bassman (Clean Meanie gain .6 master .8), NO chorus on the wall, @american-ob, SM57 slightly off-cap. 40: CAB MIC PLACEMENT (2026-07-14) — new cab_micpos (cap edge -> cone edge: HF slide 12k->3.3k + bite recede + body) and cab_micdist (close -> ~30 cm: proximity bass falls away + air loss) on the Cabinet block; post-convolution morphs, 0/0 = bit-identical baseline (append-only, presets reseeded with 0/0 = no tone change). 39: ENRICHED FACTORY CABS (2026-07-14, user: "this is where we could really make this sound real") — all 6 built-in cabs (incl. @factory V30) gain deterministic measured-IR anatomy (CabModels::enrich: seeded cone-breakup ripple, multi-cone/baffle-edge sub-ms taps, closed-back box return / open-back dipole cancel, ringing box air mode) with an iterated DAMPED macro-correction so each cab keeps its tuned voicing (verified ≤0.9 dB drift on 4 cabs / ≤1.7 on greenback+doom, L2 loudness exact); full 54-preset loudness re-measure baked. 38: PLEXI VOL II (2026-07-14, user: "the original had Volume I and Volume II") — new amp_pl_vol2 port (append-only, default 0 = old voicing bit-identical, no blob migration): a parallel Normal-channel V1 half jumpered under the capture-anchored bright path, like the real 1959. Hendrix presets jumpered (Mauve Haze VolI .5/VolII .5/master .7; Hazy Solo — the user's "starved Octavia" — VolI .62/VolII .55/master .75/sag .45 = the DIMED stack the Octavia historically rang over; Little Feather VolII .35; Bottle Jangle VolII .3). Nevermind Wall untouched (user-preserved, vol2 0). 37: INPUT-TRIM PHASE default fixed 1->0 (2026-07-14) — default 1 meant INVERTED polarity on every instance + all factory presets (mismatched the standalone Input Trim's phase_invert default 0 and every offline NAM calibration); all presets reseeded with normal polarity; the toggle stays for DI-blend use. 36: PERIPHERY DJENT RETUNE (2026-07-14, user + research: Misha's Precision-Drive philosophy = cut bass INTO the amp, definition from mids/treble, saturation-not-mush) — Flatliner bass 0.5->0.25 mid 0.62 treble 0.62 pres 0.55 gain 0.58 TS-tone 0.7 lowcut 100; Prayer Djent bass 0.55->0.22 mid 0.65 treble 0.6 gain 0.58 TS-tone 0.72 lowcut 105. 35: user ear-pass 2 (2026-07-14) — Skye Crusher + Skye (No Mod) bass 0.78->0.48 ("way too much bass" at the new gain; Skye Soar left at 0.75 — not named, and the lead's octave/delay masks differently). 34: user ear-pass (2026-07-14) — Mastodon bank MORE GAIN (Skye Crusher/No Mod amp gain 0.45->0.62, Skye Soar 0.55->0.7, TS level 0.74->0.8) + Castaway Groove LESS BASS (0.55->0.35, low-cut 84->92). 33: CALI V DOCTRINE PASS (2026-07-14) — the user's confirmed Mark-series recipe ("the amp works like an overdrive; the tone comes from the EQ"): Cardinal Rhythm = the user's exact device dial-in BAKED (gain 0.15, Bass 0, Treble 0.38, DOD 250 level-boost in front, brightness at GEQ 6.6k — USER-PRESERVED, do not retune); the recipe translated to the other Cali V presets (Marionette Master gain 0.8->0.5 Bass 0.05 Treble 0.40 geq4 0.78; Spectrum Rhythm gain 0.76->0.35 Bass 0.08 Treble 0.40 geq4 0.72; Spectrum Lead gain 0.78->0.42 Bass 0.10 Treble 0.45 geq4 0.66) + loudness re-measured. 32: OVERNIGHT AUTHENTICITY PASS (2026-07-14, user-authorized full-permission run) — all 54 presets audited against researched real rigs by a 15-agent web-research fan-out, 28 adjusted after adjudication: historically-correct cabs across 15 presets (Ghost=V30 PPC412 not Greenbacks; Hendrix='67 G12M Greenbacks not V30s; Mastodon=Mills V30s; Nirvana verse=Bassman open-back + Come As You Are=@vox2x12; Mayer/Cure/Chic=American open-backs; Trower/Police=Greenbacks; Numb Sustain=@hiwatt), drive-pedal identities fixed (Impera rhythm=Sugar Drive/Klon, Impera lead=DOD 250, Run Like Hell=Colorsound Power Boost full-series), Cardinal Rhythm moved to the DOCUMENTED Skeleta rhythm amp (Hetfield's Mesa IIC+ = Cali V mode 6, voiced per Cali V rules), Holy Smoke rebuilt to Pike's documented no-fuzz chain (Soldano-style OD into cranked saggy Orange), Comfortably Numb delay fixed to the MXR Digital + added the missing RA-200 rotary blend, Innerspeaker fuzz=germanium Fuzz Face family (I Know It), Glide Wall reverse-verb predelay 0, Streets Chime 359 ms (measured 125.5 BPM), Bottle Jangle=Marshall 1959 platform + 330 ms Echoplex at 50/50, March Stabs master 0.55 (JMP-1 direct, no power amp), Regal Solo 800 ms Echoplex canon, Cavalier Charge 330 ms + the Manson-wired Phase 90, Trower's documented presence-0/treble-low Marshall settings, Candlelit comp=optical. Full loudness re-measure follows. 31: FULL preset overhaul (2026-07-14) — (a) LOUDNESS PARITY: all 54 presets re-measured on-device (hexforge_meas) against the NEW DSP (FF v3 re-model, Cali V retune, DNR additions) and out_level rewritten to the -12.5 dirty / -13 clean targets; the 9 never-measured presets (Gojira/Mesa/Muse banks, Regal Solo, Grunge Drop) leveled for the first time. (b) GATE FLOOR-COMPLIANCE vs the measured rig floor: Bridge Vibe/Streets Chime raised off the -60 default, Regal Solo -54->-50, Con Molars -54->-48 (fuzz/doom deep gates kept deliberately). (c) MUSE presets retuned to Bellamy's DOCUMENTED FF recipe (Drive max/Comp low/Stab max, probe-verified squeal-safe at the preset Gate; back Stab off to ~0.55-0.85 for the splatty scream). 30: FF cut-out fix — Muse chain gates OPENED (-50/-48 -> -58, hold 200/release 400; the raw-guitar-keyed gate in FRONT of the fuzz chopped sustain while the fuzz still roared — same as the old Octavia choke; the FF's own Gate knob is the noise-killer) + Spectrum Lead gate -52 -> -48 (could never close against the user's measured -45 dBFS hands-off idle floor) (2026-07-14). 29: renamed preset "With Teeth" -> "Con Molars" (bank 11/D) + fuzz pedal "Fizz Factory" -> "Fuzz Zachary" (label only, value 3 unchanged) (2026-07-13). 28: Cali V HISS RETUNE (Marionette Master/Spectrum Rhythm/Spectrum Lead) — Bass down (tight; real MarkV wants Bass 1-3 on high gain), pre-gain Treble down + brightness moved to the POST-gain GEQ 6.6 kHz (Treble is early in the circuit = adds saturation/hiss), gain nudged up to offset the model's ~3 dB high-gain trim; out_level nudged +1 for the bass loss (verify by ear) (2026-07-13). 27: PACKED presets — added Knights of Cydonia ("Cavalier Charge") + compacted so no populated bank before the last has blank slots (banks 0-13 full; MUSE = last bank, index 14, A/B only; index 15 emptied). Regal Sustain->1/A, Regal Solo->11/D, Grunge Drop->14/D, Muse->bank 14 (2026-07-13). 26: Plug-In Junior Stab 0.5->0.2 (below the osc onset — was squealing too much) (2026-07-13). 25: Plug-In Junior retune — Stab 0.72->0.5 (below the self-osc onset; PIB riff shouldn't squeal), Drive 0.92, Comp 0.5, Gate 0.42 (2026-07-13). 24: Fizz Factory — hotter drive + gentler gate (fix "cut out") + note-gated SELF-OSCILLATION above Stab 0.55 (the unruly FF scream, dies on silence); calibrated to the real Stab sweep (THD/level rise); Plug-In Junior Drive 0.85/Gate 0.35/Stab 0.72 (2026-07-12). 23: Fizz Factory Stab = SUPPLY-RAIL model (ZVEX "operating voltage" — germanium FF); UP=tight/full, DOWN=starved squishy sag/sputter (correct real-pedal direction); Plug-In Junior Stab 0->0.9 (tight) (2026-07-12). 22: Fizz Factory Stab reworked (note-gated feedback + low 480 Hz growl + late onset — no more standalone whine); Plug-In Junior Stab 0.15->0 (2026-07-12). 21: NEW Bank 16 (index 15) MUSE — "Plug-In Junior" (Plug In Baby, Fizz Factory riff), slot A; B/C/D reserved; seed-clear spares 15/0 (2026-07-12). 20: trimmed the batch to Regal Sustain/Solo + Grunge Drop (removed the other 11 + Rotary Dream/Surfing Lead); added the INPUT-TRIM clean boost (it_boost ~6 dB) to both Regal presets; clears the vacated banks 15-17 + tail slots; 52 presets (2026-07-12). 19: +14 new presets (Brown Sound/Back in Black/Appetite/Texas Flood/Iron Man Doom/Surfing Lead/Microtonal Mirage/Cinematic Swell/Nashville Twang/Warm Archtop/Funk Machine/Djent Modern/Grunge Drop/Rotary Dream); moved both Queen presets together into new Bank 15 (Royalty), backfilled Bank 10/D=Rotary Dream + Bank 13/D=Surfing Lead so no blank slots; 65 presets total (2026-07-12). 18: removed "There There" + Gojira "Quicksilver Lead"; added Brian May LEAD "Regal Solo" (treble-boosted AC30) + made "Regal Sustain" hotter; repacked the tail so NO blank slots (Gojira -> Bank 12 C/D, Mesa Mark+Regal Solo -> Bank 13); clears the vacated old Bank 15 slots (2026-07-12). 17: Quicksilver Lead octave-up moved POST-cab (pos 2->7) + boosted (up 0.45->0.65) — in front of the high-gain EVH it was masked by the amp's own 2nd harmonic ("octave didn't work") (2026-07-12). 16: parody-renamed the new presets (Winterborn/Quicksilver Lead/Castaway Groove/Marionette Master/Spectrum Rhythm+Lead); Quicksilver = former Silvera turned into an octave-up LEAD (was too close to the rhythm); Marionette Master DE-BASSED (2026-07-11). 15: Gojira Silvera/Stranded amp JCM800->EVH 5150 III (Gainzilla, red ch) per user — their actual high-gain amp (2026-07-11). 14: NEW banks — Bank 14 GOJIRA (Born in Winter clean-tap / Silvera / Stranded, JCM800+TS) + Bank 15 MESA MARK (Master of Puppets = Cali V IIC+ smiley-scoop; Colors Rhythm/Lead = Cali V Mark IV) (2026-07-11). Muse (Plug In Baby / Knights of Cydonia) deferred — needs a ZVex Fuzz Factory model. 13: REVOICED Ghost/Periphery/Mastodon for the current gear (input-clip removed + TS808/Friedman re-voiced) — normalized the TS clean-boost level (hot 0.9-1.0 down / attenuating 0.6 up, all ~0.72-0.78) + eased treble/presence/high-cut for the now-untamed brighter front-end (2026-07-11). NOTE: out_level still on the pre-revoice measurements — re-measure once tone is locked. 12: Imperial Lead tamed (was too fuzzy: dropped Sat + gain 0.72->0.56 + Klon 0.35->0.16); Hazy Solo un-starved (opened the gate in front of the Octavia + more sustain/amp gain) (2026-07-11). 11: FULL LOUDNESS PARITY re-level (2026-07-09) — amp makeup leveled (all amps equal-feel at noon) + every factory preset out_level re-measured & set to the Bank-1 target (-12.5 dirty / -13 clean); dropped the -5.9 clean pin + the +8 dB pushed-loud leads. 10: real Plexi loudness for Nevermind Wall/Mauve Haze/Hazy Solo (they were mis-loading as Backline; re-measured) + Nevermind Wall reworked to Plexiglass. 2: Gravity Lead out_level -27.5 -> -23.0. 3: Ghost Imperial/Cardinal Lead phaser -> subtle Lush-2 chorus. 4: + Angine de Poitrine bank (Bank 13, microtonal shimmer). 5: Bank 13 B/C -> Radiohead (Anyone Can Play Guitar / There There); kept Quarter-Tone Lead. 6: Cardinal Lead drive -> Preamp 250 (DOD 250). 7: Cardinal Lead re-staged (DOD drive 0.48->0.25, amp gain 0.78->0.62) — was too hot/mushy. 8: Cardinal Lead out_level re-measured on-device (-17.3->-19.2; DOD250 denser than the old RAT)
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
static const AmpModel kAmpMap[14] = {
    AmpModel::FenderDeluxe, AmpModel::MarshallJCM800, AmpModel::EVH5150III,
    AmpModel::SunnModelT,   AmpModel::OrangeRockerverb50,
    AmpModel::NeuralCustom,        // 5 = NAM (placeholder; not built as an algo amp)
    AmpModel::FriedmanBEDeluxe,    // 6 = Beardo BE
    AmpModel::HiwattDR103,         // 7 = Hiwatt (high-headroom British clean)
    AmpModel::VoxAC30,             // 8 = Vox AC30 Top Boost (EL84 chime)
    AmpModel::PeaveyBackstage,     // 9 = Backline Plus (solid-state Peavey Backstage)
    AmpModel::MarshallPlexi,       // 10 = Plexiglass (Marshall 1959 Super Lead, EL34)
    AmpModel::MesaMarkV,           // 11 = Cali V (Mesa Mark V, 9 modes, Simul-Class)
    AmpModel::MesaDualRectifier,   // 12 = Diamond Plate (Mesa Dual Rectifier, 8 modes, 6L6)
    AmpModel::PRSMT15,             // 13 = Tremont 15 (PRS MT15: Clean/Crunch/Lead + bright)
};
static const int   kCanonical[14] = { 0, 1, 2, 4, 5, 3, 6, 0, 0, 0, 1, 1, 7, 8 }; // PowerAmp default lookup ([12] Recto -> its own 6L6 case) ([10] Plexi, [11] Mesa → JCM800 EL34 PA)
static constexpr int kSunnIdx     = 3;
static constexpr int kFriedmanIdx = 6;
static constexpr int kHiwattIdx   = 7;
static constexpr int kVoxIdx      = 8;
static constexpr int kBacklineIdx = 9;
static constexpr int kPlexiIdx    = 10;   // Plexiglass (Marshall 1959 Super Lead)
static constexpr int kMesaIdx     = 11;   // Cali V (Mesa Mark V)
static constexpr int kRectoIdx    = 12;   // Diamond Plate (Mesa Dual Rectifier)
static constexpr int kMt15Idx     = 13;   // Tremont 15 (PRS MT15)
static const int   kAmpTube[14]   = { 0, 1, 1, 0, 1, 0, 1, 1, 2, 0, 1, 1, 0, 0 }; // …/EL34/EL34/6L6-EL34 Simul
// Amp-level PARITY calibration (2026-07-09, build-tools/hexforge_amplevel measured @noon, cab on):
// distorted amps → -16 dBFS RMS, clean amps (Fender/Hiwatt/Vox/Backline) → -13 dBFS (+3 dB perceptual
// boost so cleans FEEL as loud as the denser distorted models). Re-leveled after the amp re-voicings
// left Hiwatt -24 / CaliV -23 / Friedman -21.5 / Plexi -19 several dB quiet. makeup is linear post-amp gain.
static const float kAmpMakeup[14] = { 3.78f, 1.18f, 1.48f, 3.18f, 1.19f, 1.0f, 1.14f, 4.8f, 2.05f, 4.15f, 1.49f, 2.16f, 3.4f, 3.65f }; // [5] NAM passthrough; [11] Cali V scales all 9 modes (per-mode makeup is inside the model)  // [7] Hiwatt was 4.9 (BUG: slammed the master limiter under any drive → mush + forced out_level to -27); high-headroom amp needs little makeup, loudness comes from out_level. [8] Vox [9] Backline (solid-state; model runs ~9 dB below the NAM, low crest so 2.5 is safe). [10] Plexi (Marshall EL34, like JCM800)
// (REMOVED 2026-07-11) The `kAmpInputCeil = A*tanh(x/A)` "input ceiling" on the amp block was added
// 2026-07-03 and CHANGED THE AMP CHARACTER: being a nonlinearity at any A, it pre-distorted the amp
// input, and the high-gain front-ends amplified that into a fuzzy/dark (high A) or woolly (low A)
// voice. The amps are voiced for a RAW input (like the standalone Amp plugin), so it's gone. Hot
// upstream blocks are handled by preset gain-staging, not by distorting every amp's front end.

// Indexed by dr_model port: 0/1/2/4/5 = algorithmic, 3 = NAM (special-cased, entry unused).
static const OverdriveType kDriveMap[8] = {
    OverdriveType::TubeScreamer808, OverdriveType::LifePedal, OverdriveType::ProcoRAT,
    OverdriveType::ProcoRAT /* [3]=NAM placeholder, never used */, OverdriveType::DS1,
    OverdriveType::Klon, OverdriveType::SuperOverdriveSD1, OverdriveType::DOD250,
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

// ── Power-line hum notch comb (Input Trim "Hum Filter") ────────────────────────
// Now shared: lv2/common/HumNotchComb.h (also used by the standalone Input Trim and
// the standalone Amp's input stage). Included at the top.

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
enum Block { B_GATE, B_COMP, B_FUZZ, B_DRIVE, B_AMP, B_CAB, B_MODFX, B_DELAY, B_REVERB, B_WAH, B_OCTAVE, B_NAIL, B_EQ, B_COUNT };
static const int kPosPort[B_COUNT] = {
    HF_GT_POS, HF_CP_POS, HF_FZ_POS, HF_DR_POS, HF_AMP_POS,
    HF_CAB_POS, HF_MD_POS, HF_DL_POS, HF_RV_POS, HF_WH_POS, HF_OC_POS, HF_NAIL_POS,
    HF_EQ_POS,
};
static const int kEnablePort[B_COUNT] = {
    HF_GT_ENABLE, HF_CP_ENABLE, HF_FZ_ENABLE, HF_DR_ENABLE, HF_AMP_ENABLE,
    HF_CAB_ENABLE, HF_MD_ENABLE, HF_DL_ENABLE, HF_RV_ENABLE, HF_WH_ENABLE, HF_OC_ENABLE, HF_NAIL_ENABLE,
    HF_EQ_ENABLE,
};
// enable = chain membership (1 = in chain, 0 = removed/palette); bypass = active(0)/
// bypassed(1). A block runs iff enable==1 && bypass==0. Bypassed blocks stay in the chain
// (greyed in the UI) but pass dry, keeping their settings — for live A/B.
static const int kBypassPort[B_COUNT] = {
    HF_GT_BYPASS, HF_CP_BYPASS, HF_FZ_BYPASS, HF_DR_BYPASS, HF_AMP_BYPASS,
    HF_CAB_BYPASS, HF_MD_BYPASS, HF_DL_BYPASS, HF_RV_BYPASS, HF_WH_BYPASS, HF_OC_BYPASS, HF_NAIL_BYPASS,
    HF_EQ_BYPASS,
};

// ── 6-band graphic EQ block (2026-07-23) ─────────────────────────────────────
// MXR-style octave centers; a PRESET base curve is applied UNDER the sliders
// (sliders at 0 = the preset alone; they tweak on top, total clamped ±15 dB).
// Curves must mirror gen_hexforge.py's eq_preset scalePoints.
struct GraphicEQ {
    static constexpr int kBands = 6;
    BiquadFilter f[2][kBands];
    double rate = 48000.0;
    float  gain = 1.0f;
    float  curDb[kBands] = {1e9f,0,0,0,0,0};   // force first rebuild
    float  curLvl = 1e9f;
    int    curPre = -1;
    void prepare(double r) { rate = r; reset(); }
    void reset() { for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) f[c][b].reset(); }
    void update(int preset, const float* db, float lvlDb) noexcept {
        static const double kFc[kBands] = {100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0};
        // Curves per the Neural DSP electric-guitar EQ guide (warmth 80-100 Hz,
        // mud ~250, boxiness 250-500, clarity 800, presence/sharpness 1.6-3.2k):
        static const float kPre[7][kBands] = {
            { 0, 0,  0,  0,  0,  0},   // Manual
            { 3, 0, -1,  2,  1,  2},   // Clean Sparkle: warm lows + clarity + top shimmer
            {-2,-4, -3,  1,  1,  0},   // De-Mud: carve rumble/boxiness, nudge clarity
            { 3, 1, -3,  0,  2,  4},   // Classic Rock: 100 up, 400 out, 3.2k bite
            { 4,-2, -5, -2,  2,  4},   // Metal Rhythm: big tight lows, deep box cut, edge
            {-1, 0,  1,  3,  4,  5},   // Lead Cut: upper-mid push that jumps out of a mix
            {-4,-2,  2,  7,  2, -4},   // Cocked Wah: parked-wah honk
        };
        if (preset < 0) preset = 0; if (preset > 6) preset = 6;
        bool dirty = (preset != curPre) || (lvlDb != curLvl);
        for (int b = 0; b < kBands && !dirty; ++b) dirty = (db[b] != curDb[b]);
        if (!dirty) return;
        curPre = preset; curLvl = lvlDb;
        for (int b = 0; b < kBands; ++b) {
            curDb[b] = db[b];
            float d = kPre[preset][b] + db[b];
            if (d > 15.0f) d = 15.0f; else if (d < -15.0f) d = -15.0f;
            const BiquadCoeffs bc = Filters::peaking(kFc[b], d, 1.1, rate);
            f[0][b].setCoeffs(bc); f[1][b].setCoeffs(bc);
        }
        gain = std::pow(10.0f, lvlDb * (1.0f / 20.0f));
    }
    void processCh(float* x, int n, int ch) noexcept {
        for (int i = 0; i < n; ++i) {
            float v = x[i];
            for (int b = 0; b < kBands; ++b) v = f[ch][b].process(v);
            x[i] = v * gain;
        }
    }
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
    LV2_URID ps_name, ps_index, ps_apply, preset_blob, meters, tuner;
    LV2_URID midi_MidiEvent;
    LV2_URID time_Position, time_bpm, atom_Float;   // host tempo (tap-tempo / MIDI clock sync)
};

// ── Chromatic strobe tuner ── autocorrelation pitch detector on the dry input. Only runs
// when the tuner is engaged. Reports note (0..11 = C..B, -1 = no pitch) + cents (-50..+50).
struct TunerDetector {
    // Runs the autocorrelation at a 4:1-DECIMATED rate (48k→12k) — 16x fewer mults than at
    // full rate, which is what kept CPU high. 12 kHz still covers guitar (fundamentals+lows).
    static constexpr int kDec = 4;
    static constexpr int kBuf = 1024;   // ~85 ms window at the decimated 12 kHz rate
    float  buf[kBuf] = {0};
    double corr[kBuf] = {0};
    int    wr = 0, sinceCalc = 0, decCnt = 0;
    double rate = 12000.0;              // DECIMATED rate
    float  lp = 0.0f, lpCoef = 0.0f;    // one-pole anti-alias LP (runs at the full input rate)
    int    note = -1;
    float  cents = 0.0f;
    void prepare(double sr) noexcept {
        rate = sr / kDec;
        lpCoef = (float)std::exp(-2.0 * M_PI * 3200.0 / sr);   // ~3.2 kHz LP before decimation
        wr = sinceCalc = decCnt = 0; lp = 0.0f; note = -1; cents = 0.0f;
        for (int i = 0; i < kBuf; ++i) buf[i] = 0.0f;
    }
    void reset() noexcept { note = -1; cents = 0.0f; }
    void process(const float* mono, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            lp = mono[i] + lpCoef * (lp - mono[i]);            // anti-alias then decimate
            if (++decCnt >= kDec) {
                decCnt = 0;
                buf[wr] = lp; wr = (wr + 1) & (kBuf - 1);
                if (++sinceCalc >= 1024) { sinceCalc = 0; compute(); }   // recompute ~12x/s
            }
        }
    }
    void compute() noexcept {
        float w[kBuf];
        for (int i = 0; i < kBuf; ++i) w[i] = buf[(wr + i) & (kBuf - 1)];
        double e0 = 1e-12; for (int i = 0; i < kBuf; ++i) e0 += (double)w[i] * w[i];
        const double meanPow = e0 / kBuf;
        if (std::sqrt(meanPow) < 0.004) { note = -1; return; }   // ~-48 dBFS gate → silence
        const int minLag = (int)(rate / 1050.0);   // up to ~1050 Hz
        int maxLag = (int)(rate / 62.0);            // down to ~62 Hz (below low E)
        if (maxLag >= kBuf) maxLag = kBuf - 1;
        // Normalize each lag by BOTH the overlap length (kills the (N-lag) envelope that
        // biased the peak toward shorter lags = sharp) and mean power → ~1.0 at the true period.
        double gmax = 0.0;
        for (int lag = minLag; lag <= maxLag; ++lag) {
            double c = 0.0; const int ov = kBuf - lag;
            for (int i = 0; i < ov; ++i) c += (double)w[i] * w[i + lag];
            const double norm = (c / ov) / (meanPow + 1e-12);
            corr[lag] = norm;
            if (norm > gmax) gmax = norm;
        }
        if (gmax < 0.5) { note = -1; return; }
        // Pick the FIRST local peak within 88% of the global max = the fundamental period
        // (a plain global-max can land on 2x the period → an octave-down error).
        const double pt = gmax * 0.88;
        int bestLag = 0;
        for (int lag = minLag + 1; lag < maxLag; ++lag)
            if (corr[lag] >= pt && corr[lag] >= corr[lag-1] && corr[lag] > corr[lag+1]) { bestLag = lag; break; }
        if (bestLag <= minLag) { note = -1; return; }
        double refLag = bestLag;               // parabolic interpolation for sub-sample accuracy
        const double a = corr[bestLag - 1], b = corr[bestLag], cc = corr[bestLag + 1];
        const double den = a - 2.0 * b + cc;
        if (std::fabs(den) > 1e-12) refLag = bestLag + 0.5 * (a - cc) / den;
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
    std::unique_ptr<OversamplingWrapper> fuzzFactory;// Fizz Factory (ZVex-style chaos/gated octave)
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
    GraphicEQ         eq;              // 6-band graphic EQ block (movable, v23)
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
    // Output doubler (fake double-track): loose-timing tap, three slow wander phases.
    std::vector<float> dblBuf;
    int    dblW = 0;
    double dblPhase = 0.0;   // wander sine 1 (0.11 Hz)
    double dblPh2 = 2.0;     // wander sine 2 (0.23 Hz) — offset starts: no aligned zero
    double dblPh3 = 4.0;     // wander sine 3 (0.047 Hz)
    // Mono-blend phase diffusers (Schroeder allpasses): scramble the take's phase vs
    // the dry so the mono sum thickens instead of comb-cancelling (measured -1.2 dB
    // coherent loss without them). Stereo keeps the raw take — no diffusion.
    std::vector<float> dblApA, dblApB;
    int dblApAw = 0, dblApBw = 0;

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
// IR resampling upgraded linear -> windowed-sinc 2026-07-14 (lv2/common/IrResample.h):
// linear interp baked ~-1 dB @ 10 kHz droop + imaging aliases into every 44.1 kHz IR.
static bool loadIRFile(const char* path, double dst, std::vector<float>& L, std::vector<float>& R) {
    uint32_t sr=0; if (!readWav(path,L,R,sr) || L.empty()) return false;
    L = irresample::resampleSinc(L, sr, dst); if (!R.empty()) R = irresample::resampleSinc(R, sr, dst);
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
    p->uris.tuner         = m->map(m->handle, HEXFORGE_URI "#tuner");
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
//   * Octave microtonal shimmer, 2 contiguous [HF_OC_MICRO, HF_OC_INTERVAL], added v14.
static_assert(HF_OC_INTERVAL == HF_OC_MICRO + 1 && HF_OC_MICRO == HF_MD_DIV + 1 && HF_OC_INTERVAL < HF_SW_A,
              "octave shimmer ports must be contiguous, after tempo-sync and before the commands");
//   * Cali V (Mesa Mark V) mode selector [HF_AMP_MV_MODE] added v15, then the 5-band graphic EQ
//     [HF_AMP_MV_GEQ0..4] added v16 — all 6 contiguous, after the octave ports, before the commands.
static_assert(HF_AMP_MV_MODE == HF_OC_INTERVAL + 1 && HF_AMP_MV_GEQ0 == HF_AMP_MV_MODE + 1
              && HF_AMP_MV_GEQ4 == HF_AMP_MV_GEQ0 + 4 && HF_AMP_MV_EQPRESET == HF_AMP_MV_GEQ4 + 1,
              "amp_mv_mode + 5 graphic-EQ + eqpreset must be contiguous, before md_offset");
//   * Modulation Center Delay [HF_MD_OFFSET] added v18 — right after the Cali V EQ preset.
static_assert(HF_MD_OFFSET == HF_AMP_MV_EQPRESET + 1 && HF_AMP_NAM_GAIN == HF_MD_OFFSET + 1,
              "md_offset must sit right after the Cali V EQ preset, before the NAM trims");
//   * NAM input/output trims — 6 contiguous ports [HF_AMP_NAM_GAIN..HF_CAB_NAM_VOL] added v19;
//     then the Plexi Vol II (amp_pl_vol2) and the cab mic placement (cab_micpos/cab_micdist),
//     all append-only default-0 (2026-07-14), as the last preset params before the commands.
static_assert(HF_AMP_NAM_VOL == HF_AMP_NAM_GAIN + 1 && HF_DR_NAM_GAIN == HF_AMP_NAM_GAIN + 2
              && HF_DR_NAM_VOL == HF_AMP_NAM_GAIN + 3 && HF_CAB_NAM_GAIN == HF_AMP_NAM_GAIN + 4
              && HF_CAB_NAM_VOL == HF_AMP_NAM_GAIN + 5 && HF_AMP_PL_VOL2 == HF_CAB_NAM_VOL + 1
              && HF_CAB_MICPOS == HF_AMP_PL_VOL2 + 1 && HF_CAB_MICDIST == HF_CAB_MICPOS + 1
              && HF_CAB_ROOMON == HF_CAB_MICDIST + 1 && HF_CAB_ROOMMIX == HF_CAB_ROOMON + 1
              && HF_CAB_ROOMAMT == HF_CAB_ROOMMIX + 1,
              "NAM trims, Plexi Vol II, mic placement, then cab room must sit before the Recto block");
//   * Diamond Plate (Mesa Dual Rectifier) mode + Variac + rectifier — 3 contiguous ports
//     [HF_AMP_RC_MODE..HF_AMP_RC_RECT], added v20, as the last preset params before the commands.
static_assert(HF_AMP_RC_MODE == HF_CAB_ROOMAMT + 1 && HF_AMP_RC_VARIAC == HF_AMP_RC_MODE + 1
              && HF_AMP_RC_RECT == HF_AMP_RC_MODE + 2,
              "Recto ports must be contiguous after the cab room");
//   * Tremont 15 (PRS MT15) channel + bright — 2 contiguous ports [HF_AMP_MT_MODE,
//     HF_AMP_MT_BRIGHT], added v21, as the last preset params before the commands.
static_assert(HF_AMP_MT_MODE == HF_AMP_RC_RECT + 1 && HF_AMP_MT_BRIGHT == HF_AMP_MT_MODE + 1,
              "MT15 ports must be contiguous after the Recto block");
//   * Cab Voice + Output Doubler — 2 contiguous ports, added v22, last before the commands.
static_assert(HF_CAB_VOICE == HF_AMP_MT_BRIGHT + 1 && HF_OUT_DOUBLER == HF_CAB_VOICE + 1,
              "cab voice + doubler must be contiguous");
//   * EQ block — 11 contiguous ports [EQ_POS..EQ_BYPASS], added v23, last before the commands.
static_assert(HF_EQ_POS == HF_OUT_DOUBLER + 1 && HF_EQ_ENABLE == HF_EQ_POS + 1
              && HF_EQ_PRESET == HF_EQ_POS + 2 && HF_EQ_100 == HF_EQ_POS + 3
              && HF_EQ_LEVEL == HF_EQ_POS + 9 && HF_EQ_BYPASS == HF_EQ_POS + 10
              && HF_EQ_BYPASS == HF_SW_A - 1,
              "EQ block ports must be contiguous, right before the commands");
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
    // v14 inserted the Octave microtonal shimmer [HF_OC_MICRO, HF_OC_INTERVAL] before the
    // commands; defaults = shimmer OFF (0), interval 0 (quarter-tone up).
    static const float ocdef[2] = {0.0f, 0.0f};                          // oc_micro, oc_interval
    const bool ocGap = (srcVer < 14);
    const int ocAt = HF_OC_MICRO, ocEnd = HF_OC_MICRO + 2;               // oc gap [ocAt,ocEnd)
    // v15 inserted the Cali V (Mesa Mark V) mode selector [HF_AMP_MV_MODE] before the commands;
    // default 6 (Mark IIC+, the hero lead mode).
    const bool mvGap = (srcVer < 15);
    const int mvAt = HF_AMP_MV_MODE, mvEnd = HF_AMP_MV_MODE + 1;         // mode gap [mvAt,mvEnd)
    // v16 inserted the Cali V 5-band graphic EQ [HF_AMP_MV_GEQ0..4]; default 0.5 (flat) each.
    const bool gvGap = (srcVer < 16);
    const int gvAt = HF_AMP_MV_GEQ0, gvEnd = HF_AMP_MV_GEQ0 + 5;         // geq gap [gvAt,gvEnd)
    // v17 inserted the Cali V graphic-EQ preset selector [HF_AMP_MV_EQPRESET]; default 0 (Custom).
    const bool eqGap = (srcVer < 17);
    const int eqAt = HF_AMP_MV_EQPRESET, eqEnd = HF_AMP_MV_EQPRESET + 1;
    // v18 appended the Modulation Center Delay [HF_MD_OFFSET] before the commands; default 0 ms
    // (= stock chorus/flanger/small-clone voicing).
    const bool mdoGap = (srcVer < 18);
    const int mdoAt = HF_MD_OFFSET, mdoEnd = HF_MD_OFFSET + 1;
    // v19 appended 6 NAM input/output trims [HF_AMP_NAM_GAIN..HF_CAB_NAM_VOL] before the commands;
    // default 0 dB (unity) for each.
    const bool namGap = (srcVer < 19);
    const int namAt = HF_AMP_NAM_GAIN, namEnd = HF_AMP_NAM_GAIN + 6;
    // v20 appended the Diamond Plate (Dual Rectifier) ports [HF_AMP_RC_MODE..HF_AMP_RC_RECT];
    // defaults = CH3 Modern (7), Bold (0), Silicon (0) — plain zero-fill would recall CH1 Clean.
    static const float rcdef[3] = {7.0f, 0.0f, 0.0f};
    const bool rcGap = (srcVer < 20);
    const int rcAt = HF_AMP_RC_MODE, rcEnd = HF_AMP_RC_MODE + 3;
    // v21 appended the Tremont 15 (PRS MT15) ports [HF_AMP_MT_MODE, HF_AMP_MT_BRIGHT];
    // defaults = Lead (2), bright off — plain zero-fill would recall the Clean channel.
    static const float mtdef[2] = {2.0f, 0.0f};
    const bool mtGap = (srcVer < 21);
    const int mtAt = HF_AMP_MT_MODE, mtEnd = HF_AMP_MT_MODE + 2;
    // v22 appended cab voice + output doubler; defaults Room (0) / off (0).
    const bool cvGap = (srcVer < 22);
    const int cvAt = HF_CAB_VOICE, cvEnd = HF_CAB_VOICE + 2;
    // v23 appended the EQ block group [EQ_POS..EQ_BYPASS]: pos 6 (after the cab),
    // palette (enable 0), Manual preset, all bands/level 0 dB, active (bypass 0).
    static const float eqbdef[11] = {6.0f, 0,0,0,0,0,0,0,0,0,0};
    const bool eqbGap = (srcVer < 23);
    const int eqbAt = HF_EQ_POS, eqbEnd = HF_EQ_POS + 11;

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
        else if (ocGap && i >= ocAt && i < ocEnd)        vals[i] = ocdef[i - ocAt];
        else if (mvGap && i >= mvAt && i < mvEnd)        vals[i] = 6.0f;   // Mesa mode default = Mark IIC+
        else if (gvGap && i >= gvAt && i < gvEnd)        vals[i] = 0.5f;   // Mesa graphic-EQ band = flat
        else if (eqGap && i >= eqAt && i < eqEnd)        vals[i] = 0.0f;   // Mesa EQ preset = Custom
        else if (mdoGap && i >= mdoAt && i < mdoEnd)     vals[i] = 0.0f;   // Mod Center Delay = 0 ms
        else if (namGap && i >= namAt && i < namEnd)     vals[i] = 0.0f;   // NAM gain/level trims = 0 dB
        else if (rcGap && i >= rcAt && i < rcEnd)        vals[i] = rcdef[i - rcAt];  // Recto: CH3 Modern/Bold/Silicon
        else if (mtGap && i >= mtAt && i < mtEnd)        vals[i] = mtdef[i - mtAt];  // MT15: Lead/bright off
        else if (cvGap && i >= cvAt && i < cvEnd)        vals[i] = 0.0f;             // cab voice Room / doubler off
        else if (eqbGap && i >= eqbAt && i < eqbEnd)     vals[i] = eqbdef[i - eqbAt];  // EQ: palette, flat
        else                                             vals[i] = old[o++];
    }
}
static void seedFactoryPresets(HexForge* p);   // fwd decl (defined after the factory arrays)
static void hfSerialize(HexForge* p, std::vector<uint8_t>& blob) {
    auto putBytes = [&](const void* d, size_t n){ const uint8_t* b=(const uint8_t*)d; blob.insert(blob.end(), b, b+n); };
    auto putU32   = [&](uint32_t v){ putBytes(&v, 4); };
    auto putPath  = [&](const char* s){ uint32_t len=(uint32_t)std::strlen(s); putU32(len); putBytes(s, len); };
    putU32(23); putU32(kBanks); putU32(kSlots); putU32(HF_N_PORTS); putU32(kFactoryRev);   // v23: + EQ block; v22: + cab voice/doubler; v21: + Tremont 15
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
    if (ver < 2 || ver > 23) return false;
    const bool migrateOutDb = (ver == 2);
    const bool needMigrate  = (ver < 23);  // …EQ preset (v17) + Center Delay (v18) + NAM trims (v19)
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
    uint32_t cb=0, cs=0; getU32(cb); getU32(cs);   // (saved cursor read for byte alignment, then discarded)
    p->curBank = 0; p->curSlot = 0;   // ALWAYS land on the first preset (Bank 1 / A) on load
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
        // Ports appended after the v3 capture migrate in as 0 — give Bank 1 the same subtle
        // default room every other preset carries (user 2026-07-14: "make sure my first four
        // presets have it").
        pr.vals[HF_CAB_ROOMON]  = 1.0f;
        pr.vals[HF_CAB_ROOMMIX] = 0.12f;
        pr.vals[HF_CAB_ROOMAMT] = 0.35f;
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
    // 2026-07-13 (rev 27): packed the collection so no populated bank before the last has blank slots (user
    // request). Banks 0-13 are now FULL; the MUSE bank (index 14) is the LAST populated bank and holds only
    // A/B (Plug In Baby + Knights of Cydonia); index 15 was emptied (its Muse presets moved to 14). Clear the
    // now-vacated factory slots so stale presets from older revs don't linger (a user is extremely unlikely
    // to have saved over former-factory slots).
    { auto clr=[&](int b,int s){ Preset& o=p->presets[b][s]; o.used=false; o.name[0]='\0'; };
      clr(14,2); clr(14,3);                                   // MUSE bank (last) = A/B only
      for (int s=0; s<kSlots; ++s) clr(15,s);                 // index 15 emptied (Muse moved to 14)
      for (int b=16; b<=17; ++b) for (int s=0; s<kSlots; ++s) clr(b,s); }
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
    p->fuzzFactory= std::make_unique<OversamplingWrapper>(std::make_unique<ZVexFuzzFactory>());
    if (!p->fuzzMuff || !p->fuzzBender || !p->fuzzOctavia || !p->fuzzFactory) { delete p; return nullptr; }
    p->fuzzMuff->prepare(rate, kMaxBlock, 1);
    p->fuzzBender->prepare(rate, kMaxBlock, 1);
    p->fuzzOctavia->prepare(rate, kMaxBlock, 1);
    p->fuzzFactory->prepare(rate, kMaxBlock, 1);
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
    p->cab.setIR(CabModels::generate("@factory", rate));   // enriched Factory Cab (2026-07-14)
    p->modfx.prepare(rate, kMaxBlock, 2);
    p->modfx.setType(ModulationFactory::fromIndex(0));
    p->delay.prepare(rate, kMaxBlock, 2);
    p->delay.setType(DelayFactory::fromIndex(0));
    p->reverb.prepare(rate, kMaxBlock, 2);
    p->wah.prepare(rate, kMaxBlock, 2);
    p->octave.prepare(rate, kMaxBlock, 2);
    p->autoOut.prepare(rate);
    p->eq.prepare(rate);
    p->dblBuf.assign(size_t(rate * 0.045) + 4, 0.0f);   // doubler: 28 ms base ± 7 ms wander + margin
    p->dblApA.assign(size_t(rate * 0.0053) + 2, 0.0f);  // mono-blend phase diffusers
    p->dblApB.assign(size_t(rate * 0.0089) + 2, 0.0f);
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
        else p->cab.setIR(CabModels::generate("@factory", p->rate));   // empty path = clear to (enriched) Factory Cab
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
    na->setAmpModel(kAmpMap[clampi(static_cast<float>(msg->modelIdx), 0, kMt15Idx)]);
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
    const int fuzzPedal = clampi(*p->ports[HF_FZ_PEDAL], 0, 3);
    p->fuzzMuff->setBypass(false); p->fuzzBender->setBypass(false); p->fuzzOctavia->setBypass(false); p->fuzzFactory->setBypass(false);
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
    // Fizz Factory: Sustain→Drive, Bias→Comp, Trim→Gate, Temp→Stab, Volume→Level
    p->fuzzFactory->setParameter("sustain",   *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzFactory->setParameter("bias",      *p->ports[HF_FZ_BIAS]);
    p->fuzzFactory->setParameter("inputtrim", *p->ports[HF_FZ_INPUTTRIM]);
    p->fuzzFactory->setParameter("getemp",    *p->ports[HF_FZ_GETEMP]);
    p->fuzzFactory->setParameter("level",     *p->ports[HF_FZ_VOLUME]);
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
    const int ampModel = clampi(*p->ports[HF_AMP_MODEL], 0, kMt15Idx);
    const int ampAlgo  = (ampModel == 5) ? 1 : ampModel;   // NAM(5)→1 safe; 6=Beardo,7=Hiwatt,8=Vox identity
    const bool ampIsAlgo = (ampModel <= 4) || (ampModel == kFriedmanIdx) || (ampModel == kHiwattIdx) || (ampModel == kVoxIdx) || (ampModel == kBacklineIdx) || (ampModel == kPlexiIdx) || (ampModel == kMesaIdx) || (ampModel == kRectoIdx) || (ampModel == kMt15Idx);
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
    // Plexiglass (Marshall 1959): Vol II — the jumpered Normal-channel volume (0 = pre-Vol-II voicing).
    if (ampModel == 10) amp->setParameter("vol2", *p->ports[HF_AMP_PL_VOL2]);
    // Cali V (Mesa Mark V): 9-mode selector (0..8) + 5-band graphic EQ (each 0..1, 0.5 = flat).
    if (ampModel == kMesaIdx) {
        amp->setParameter("mode", *p->ports[HF_AMP_MV_MODE]);
        amp->setParameter("geq0", *p->ports[HF_AMP_MV_GEQ0]);
        amp->setParameter("geq1", *p->ports[HF_AMP_MV_GEQ1]);
        amp->setParameter("geq2", *p->ports[HF_AMP_MV_GEQ2]);
        amp->setParameter("geq3", *p->ports[HF_AMP_MV_GEQ3]);
        amp->setParameter("geq4", *p->ports[HF_AMP_MV_GEQ4]);
        amp->setParameter("eqpreset", *p->ports[HF_AMP_MV_EQPRESET]);   // 0 Custom (sliders) or baked "V"
    }
    // Diamond Plate (Dual Rectifier): 8-mode selector + power-section feel switches.
    if (ampModel == kRectoIdx) {
        amp->setParameter("mode",   *p->ports[HF_AMP_RC_MODE]);
        amp->setParameter("variac", *p->ports[HF_AMP_RC_VARIAC]);
        amp->setParameter("rect",   *p->ports[HF_AMP_RC_RECT]);
    }
    // Tremont 15 (PRS MT15): Clean/Crunch/Lead + the clean/crunch bright switch.
    if (ampModel == kMt15Idx) {
        amp->setParameter("mode",   *p->ports[HF_AMP_MT_MODE]);
        amp->setParameter("bright", *p->ports[HF_AMP_MT_BRIGHT]);
    }
    // Recto Modern modes (4, 7) disconnect the power-amp NFB loop on the real amp.
    const float rcMode = *p->ports[HF_AMP_RC_MODE];
    const bool rectoModern = (ampModel == kRectoIdx) &&
                             ((rcMode > 3.5f && rcMode < 4.5f) || rcMode > 6.5f);
    int desiredTube;
    if (*p->ports[HF_AMP_PAMP_AUTO] > 0.5f) {
        const auto d = PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]);
        p->pa.setParameter("master", d.master); p->pa.setParameter("presence", d.presence);
        p->pa.setParameter("depth", d.depth);   p->pa.setParameter("nfb", rectoModern ? 0.05f : d.nfb);
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
    p->cab.setParameter("micpos",    *p->ports[HF_CAB_MICPOS]);   // mic placement (2026-07-14; 0/0 = as-voiced)
    p->cab.setParameter("micdist",   *p->ports[HF_CAB_MICDIST]);
    p->cab.setParameter("roomon",    *p->ports[HF_CAB_ROOMON]);   // room ambience (2026-07-14; off = bit-identical)
    p->cab.setParameter("roommix",   *p->ports[HF_CAB_ROOMMIX]);
    p->cab.setParameter("roomamt",   *p->ports[HF_CAB_ROOMAMT]);
    p->cab.setParameter("voice",     *p->ports[HF_CAB_VOICE]);   // Room / Studio (recorded chain)
    {   // EQ block: preset base curve + slider offsets + level (rebuilds only on change)
        const float eqDb[GraphicEQ::kBands] = {
            *p->ports[HF_EQ_100], *p->ports[HF_EQ_200], *p->ports[HF_EQ_400],
            *p->ports[HF_EQ_800], *p->ports[HF_EQ_1K6], *p->ports[HF_EQ_3K2],
        };
        p->eq.update((int)*p->ports[HF_EQ_PRESET], eqDb, *p->ports[HF_EQ_LEVEL]);
    }
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
    p->modfx.setParameter("centerDelay", *p->ports[HF_MD_OFFSET]);   // ms (delay-line types only)
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
    p->octave.setParameter("up",       *p->ports[HF_OC_UP]);
    p->octave.setParameter("down",     *p->ports[HF_OC_DOWN]);
    p->octave.setParameter("dry",      *p->ports[HF_OC_DRY]);
    p->octave.setParameter("micro",    *p->ports[HF_OC_MICRO]);
    p->octave.setParameter("interval", *p->ports[HF_OC_INTERVAL]);

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
                case B_FUZZ:  runMono(fuzzPedal==0 ? *p->fuzzMuff : (fuzzPedal==1 ? *p->fuzzBender : (fuzzPedal==2 ? *p->fuzzOctavia : *p->fuzzFactory)), L, R, len, p->mono, stereo); break;
                case B_DRIVE:
                    if (driveModel == kDrNamIdx && p->drNam && p->drNam->isLoaded()) {
                        // Neural drive: mono; NAM Gain (input drive) + NAM Level (output), both dB; Mix dry/wet.
                        const float ig=std::pow(10.0f,*p->ports[HF_DR_NAM_GAIN]/20.0f);
                        const float og=std::pow(10.0f,*p->ports[HF_DR_NAM_VOL] /20.0f);
                        for (int i=0;i<len;++i) p->mono[i]=ig*(stereo?0.5f*(L[i]+R[i]):L[i]);
                        p->drNam->processBuffer(p->mono, p->monoOut, len);
                        const float mix=*p->ports[HF_DR_MIX], wet=og*mix, dry=1.0f-mix;
                        for (int i=0;i<len;++i){ float d=stereo?0.5f*(L[i]+R[i]):L[i]; float o=dry*d+wet*p->monoOut[i]; L[i]=o; R[i]=o; }
                    } else runMono(p->drive, L, R, len, p->mono, stereo);
                    break;
                case B_AMP: {
                    if (!stereo) for (int i=0;i<len;++i) R[i]=L[i];
                    // NOTE: the amp gets the RAW input (as it did before 2026-07-03 and as the
                    // standalone Amp plugin still does). The old `kAmpInputCeil` tanh "input ceiling"
                    // was a nonlinearity at ANY setting — even when barely attenuating, it added
                    // harmonics that the high-gain front-ends then amplified into a fuzzy/dark/woolly
                    // voice. Removed. A hot upstream fuzz is handled by preset gain-staging (clean
                    // amps + tamed fuzz volume), not by pre-distorting every amp's input.
                    if (ampModel == kAmpNamIdx && p->ampNam && p->ampNam->isLoaded()) {
                        // Neural amp: mono capture -> both; NAM Gain (input drive) + NAM Level (output), dB; no power amp.
                        const float ig=std::pow(10.0f,*p->ports[HF_AMP_NAM_GAIN]/20.0f);
                        const float og=std::pow(10.0f,*p->ports[HF_AMP_NAM_VOL] /20.0f);
                        for (int i=0;i<len;++i) p->mono[i]=ig*0.5f*(L[i]+R[i]);
                        p->ampNam->processBuffer(p->mono, p->monoOut, len);
                        for (int i=0;i<len;++i){ float y=p->monoOut[i]*og; L[i]=y; R[i]=y; }
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
                        // Neural cab/rig overrides the IR convolver; NAM Gain (input) + NAM Level (output), dB; Mix = dry/wet.
                        if (!stereo) for (int i=0;i<len;++i) R[i]=L[i];
                        const float ig=std::pow(10.0f,*p->ports[HF_CAB_NAM_GAIN]/20.0f);
                        const float og=std::pow(10.0f,*p->ports[HF_CAB_NAM_VOL] /20.0f);
                        for (int i=0;i<len;++i) p->mono[i]=ig*0.5f*(L[i]+R[i]);
                        p->cabNam->processBuffer(p->mono, p->monoOut, len);
                        const float mix=*p->ports[HF_CAB_MIX], dry=1.0f-mix;
                        for (int i=0;i<len;++i){ float w=p->monoOut[i]*og*mix; L[i]=dry*L[i]+w; R[i]=dry*R[i]+w; }
                        stereo = true;
                    } else runStereo(p->cab, L, R, len, stereo);
                    break;
                case B_MODFX:  runStereo(p->modfx,  L, R, len, stereo); break;
                case B_DELAY:  runStereo(p->delay,  L, R, len, stereo); break;
                case B_REVERB: runStereo(p->reverb, L, R, len, stereo); break;
                case B_WAH:    runMono(p->wah, L, R, len, p->mono, stereo); break;
                case B_OCTAVE: runMono(p->octave, L, R, len, p->mono, stereo); break;
                case B_NAIL:   runMono(*p->nail, L, R, len, p->mono, stereo); break;
                case B_EQ:     p->eq.processCh(L, len, 0);
                               if (stereo) p->eq.processCh(R, len, 1); break;
            }
        }
        // If nothing ever spread to stereo, R already mirrors L (mono blocks wrote both;
        // if the whole chain was empty, copy L→R for a mono-correct output).
        if (!stereo) for (int i=0;i<len;++i) R[i] = L[i];
    }

    // ── Doubler (v3, 2026-07-23): LOOSE-TIMING double-track. v2's constant -6 cent
    // detune was still a chorus by construction — a steady pitch offset beats against
    // the dry at a fixed rate (that's the micropitch effect, user heard it). A real
    // second take holds NO pitch offset: its timing wanders slowly around a ~28 ms
    // lag, so pitch deviation drifts through ZERO (peaks ±4-9 cents, mean 0) and the
    // wander is NON-periodic (three incommensurate slow sines ≈ human sloppiness,
    // dominant periods 4-21 s — far below any chorus LFO). Deterministic, no RNG.
    if (p->ports[HF_OUT_DOUBLER] && *p->ports[HF_OUT_DOUBLER] > 0.5f && !p->dblBuf.empty()) {
        const int len = (int)p->dblBuf.size();
        const bool monoRig = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
        constexpr double kTwoPi = 6.283185307179586;
        const double w1 = kTwoPi * 0.11  / p->rate;       // timing-wander rates (Hz)
        const double w2 = kTwoPi * 0.23  / p->rate;
        const double w3 = kTwoPi * 0.047 / p->rate;
        for (uint32_t i = 0; i < n; ++i) {
            p->dblBuf[(size_t)p->dblW] = 0.5f * (outL[i] + outR[i]);
            p->dblPhase += w1; if (p->dblPhase >= kTwoPi) p->dblPhase -= kTwoPi;
            p->dblPh2   += w2; if (p->dblPh2   >= kTwoPi) p->dblPh2   -= kTwoPi;
            p->dblPh3   += w3; if (p->dblPh3   >= kTwoPi) p->dblPh3   -= kTwoPi;
            const double wob = 0.0035 * std::sin(p->dblPhase)
                             + 0.0014 * std::sin(p->dblPh2)
                             + 0.0025 * std::sin(p->dblPh3);      // ±6.9 ms, non-periodic
            double rp = (double)p->dblW - (0.028 + wob) * p->rate; // 21-35 ms behind the dry
            while (rp < 0.0) rp += (double)len;
            const int   i0 = (int)rp;
            const float fr = (float)(rp - (double)i0);
            const int   i1 = (i0 + 1 == len) ? 0 : i0 + 1;
            const float w  = p->dblBuf[(size_t)i0] * (1.0f - fr) + p->dblBuf[(size_t)i1] * fr;
            if (monoRig) {
                // Mono rig: keep the dry take intact on BOTH sides and tuck the second
                // take ~7.5 dB under. Two allpasses scramble its phase first, so the
                // sum reads as thickness, not a harmonic comb against the dry.
                float v = w;
                { float& cell = p->dblApA[(size_t)p->dblApAw];
                  const float y = cell - 0.55f * v; cell = v + 0.55f * y;
                  if (++p->dblApAw >= (int)p->dblApA.size()) p->dblApAw = 0; v = y; }
                { float& cell = p->dblApB[(size_t)p->dblApBw];
                  const float y = cell - 0.55f * v; cell = v + 0.55f * y;
                  if (++p->dblApBw >= (int)p->dblApB.size()) p->dblApBw = 0; v = y; }
                outL[i] += 0.42f * v;
                outR[i] += 0.42f * v;
            } else {
                // Stereo rig: the right channel becomes the second take (hard double).
                outR[i] = 0.30f * outR[i] + 0.85f * w;
            }
            if (++p->dblW >= len) p->dblW = 0;
        }
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
        // Strobe tuner readout → same atom channel (output ports don't reach the modgui).
        if (tunerOn) {
            char tbuf[24]; std::snprintf(tbuf, sizeof(tbuf), "%d|%.1f", p->tuner.note, p->tuner.cents);
            forgeStringSet(p, p->uris.tuner, tbuf);
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
    putU32(23);                 // version (23: + EQ block; 22: + cab voice/doubler; 21: + Tremont 15 channel/bright; 19: + NAM gain/level trims; 18: + Mod Center Delay; 17: + Cali V EQ preset; 16: + Cali V graphic EQ; 15: + Cali V Mesa mode; 14: + Octave shimmer; 13: + tempo-sync; 12: + Nail; 11: + factory rev; 10: + Output Mono Sum; 9: + per-block bypass; 8: + Wah/Octave; 7: + Seraph; 6: + Boost; 5: + HB Model; 4: + HB voicing; 3: dB; 2: linear)
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
        if (ver < 2 || ver > 23) return LV2_STATE_SUCCESS;    // unknown layout — start fresh
        const bool migrateOutDb = (ver == 2);     // v2 stored out_level as 0..1 linear
        const bool needMigrate  = (ver < 23);     // …Mod Center Delay (v18) + NAM trims (v19)
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
        uint32_t cb=0, cs=0; getU32(cb); getU32(cs);   // (saved cursor read for byte alignment, then discarded)
        p->curBank = 0; p->curSlot = 0;   // ALWAYS land on the first preset (Bank 1 / A) on load
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
