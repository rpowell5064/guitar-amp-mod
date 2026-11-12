// ─────────────────────────────────────────────────────────────────────────────
// GuitarAmp Suite — Amp LV2 plugin
//
// Five algorithmic amp models + a full power-amp stage, PLUS a NAM (Neural Amp
// Modeler) slot: model index 5 ("Neural (NAM)") runs a user-loaded .nam capture
// instead of the algorithmic amp. The .nam file is chosen in mod-ui's file
// browser (mod:fileTypes → the NAM user-files folder) and delivered as a
// patch:Set(atom:Path); it is loaded on the LV2 worker thread and swapped in with
// a single pointer assignment, so the RT thread never allocates or blocks. The
// bundled NAM core parses both legacy (WaveNet/LSTM, "a1") and the new container/
// slimmable ("a2") formats. When NAM is selected the shared power-amp is bypassed
// (a capture already includes the whole amp).
//
// Analog model switches rebuild the amp off the audio thread via the same worker.
// ─────────────────────────────────────────────────────────────────────────────
#include "lv2_util.h"
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include "NoiseGateBlock.h"
#include "HumNotchComb.h"
#ifdef HEXCHAIN_ANAGRAM
#include "MainsDetect.h"   // automatic 50/60 Hz mains (the device has no Mains control)
#endif
#include "AmpTight.h"
#include "StageFeedback.h"
#include <vector>
#include "NamModel.h"
#include "DenormalGuard.h"
#include "AnagramPortState.h"
#ifdef HEXCHAIN_ANAGRAM
#include "AnagramRt.h"
#include "kx/control-input-port-change-request.h"
#endif
#include <new>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

#define AMP_URI     "https://rpowell5064.github.io/guitaramp-suite/amp"
#define AMP_NAM_URI AMP_URI "#nammodel"
static constexpr int kPathMax = 1024;

// LV2 model index → AmpModel enum. Index 5 = NAM (handled separately). Beardo BE
// (Friedman) is index 6, AFTER NAM, so existing saved boards (which store the model
// index) keep their meaning — inserting it earlier would shift NAM and break them.
static const AmpModel kModelMap[16] = {
    AmpModel::FenderDeluxe,        // 0
    AmpModel::MarshallJCM800,      // 1
    AmpModel::EVH5150III,          // 2
    AmpModel::SunnModelT,          // 3
    AmpModel::OrangeRockerverb50,  // 4
    AmpModel::NeuralCustom,        // 5 = NAM (handled separately; placeholder, never built here)
    AmpModel::FriedmanBEDeluxe,    // 6 = Beardo BE
    AmpModel::HiwattDR103,         // 7 = Hiwatt (high-headroom British clean)
    AmpModel::VoxAC30,             // 8 = Chime Thirty (Vox AC30 Top Boost, EL84)
    AmpModel::PeaveyBackstage,     // 9 = Backline Plus (solid-state Peavey Backstage)
    AmpModel::MarshallPlexi,       // 10 = Plexiglass (Marshall 1959 Super Lead, EL34)
    AmpModel::MesaMarkV,           // 11 = Boojum V (Mesa Mark V, 9 modes, Simul-Class)
    AmpModel::MesaDualRectifier,   // 12 = Diamond Plate (Mesa Dual Rectifier, 8 modes, 6L6)
    AmpModel::PRSMT15,             // 13 = Tremont 15 (PRS MT15: Clean/Crunch/Lead + bright)
    AmpModel::AmpegSVT,            // 14 = Blue Liner (Ampeg SVT: BASS — Ultra-Lo/Hi + 3-way mid)
    AmpModel::OrangeAD200B,        // 15 = Citrus 200 (Orange AD200B Mark III: BASS — 4x 6550, one channel)
};
static const int kCanonical[16] = { 0, 1, 2, 4, 5, 3, 6, 0, 9, 0, 10, 10, 7, 8, 11, 11 };  // LV2 idx → getDefaultsForModel idx ([7] Hiwatt, [9] Backline → clean PA; [8] Vox → its own EL84 case 9; [10] Plexi, [11] Mesa → FROZEN row 10 (since the 2026-08-20 JCM800 bake re-voiced row 1); [12] Recto → its own 6L6 case; [14] SVT → its own 6550 case 11; [15] Citrus 200 inert — PA force-bypassed, own power section)
static constexpr int kSunnIdx     = 3;     // Sunn's LV2 model index
static constexpr int kNamIdx      = 5;     // NAM slot
static constexpr int kFriedmanIdx = 6;     // Beardo BE
static constexpr int kMesaIdx     = 11;    // Boojum V (Mesa Mark V)
static constexpr int kRectoIdx    = 12;    // Diamond Plate (Mesa Dual Rectifier)
static constexpr int kMt15Idx     = 13;    // Tremont 15 (PRS MT15)
static constexpr int kSvtIdx      = 14;    // Blue Liner (Ampeg SVT — bass)
static constexpr int kAd200Idx    = 15;    // Citrus 200 (Orange AD200B Mark III — bass; own power section)
static constexpr int kMaxModel    = 15;    // highest selectable model index (Citrus 200)
// Valve swap (2026-11-04, mirrors Hex Forge amp_v1tube / amp_patube): V1 = the first preamp valve,
// Power Valves = the output valves, auto re-biased (Stock = the amp's own valve, bit-identical).
static constexpr int kV1TubeMax = 4;   // 12AX7 (stock) / 5751 / 12AY7 / 12AT7 / 12AU7
static constexpr int kPaTubeMax = 5;   // Stock / EL34 / 6L6GC / 6V6GT / EL84 / KT88
// Speaker Load (2026-11-04): the cab the twin's power stage drives. The cab is a separate plugin, so
// the amp is told which one: 0 = the amp's own, 1-10 = the Cab plugin's built-in list in its order
// (Factory, Chime 2x12, American 2x12, Cashback, Hi-Volt, Doom, Bass 8x10 / 4x10 / 2x10 / 1x15),
// mapped to the CabModels::speakerRowIds() rows.
static constexpr int kSpkLoadMax = 10;
static const int kSpkLoadRow[kSpkLoadMax + 1] = { -1, 0, 3, 2, 1, 4, 5, 6, 7, 8, 9 };
static constexpr int kMaxBlock    = 512;   // internal processing chunk
static constexpr uint32_t kFbMaxN = 8192;   // Stage Feedback input buffer (a longer period runs with the loop off)
// Stage Feedback's speaker tap is the AMP's output here, not a cab's: the plugin cannot see the cab after
// it. Hex Forge feeds the loop after its cab and its Output level; in the band the string's body filter
// listens to (250 Hz) the factory cab adds +13.4 dB and the factory presets sit at a median Output of
// -15.3 dB, so the amp's own output x 0.80 (-1.9 dB) drives the loop as hard as Hex Forge's does.
static constexpr float kFbSpkTrim = 0.80f;

// 2026-08-21 tube-correctness audit (mirrors hexforge kAmpTube): Fender→6V6(4),
// EVH→6L6(0), Sunn→KT88(3, inert — PA force-bypassed). [6] Friedman EL34;
// [7] Hiwatt EL34; [8] Vox EL84; [9] Backline solid-state (nominal, PA neutral);
// [10] Plexi EL34; [11] Mesa 6L6 (Simul-Class, 6L6-dominant); [12] Recto 6L6
static const int kModelTube[16] = { 4, 1, 0, 3, 1, 0, 1, 1, 2, 0, 1, 1, 0, 0, 5, 5 };  // [14] SVT 6550, [15] Citrus 200 6550 (inert: own PA)
// Synced to hexforge kAmpMakeup for cross-plugin PARITY (2026-07-09): identical amp+power-amp DSP,
// so the same makeup levels the standalone Amp (into a cab) exactly like Hex Forge. Distorted amps
// -> equal RMS, clean amps (Fender/Hiwatt/Vox/Backline) +3 dB perceptual. Verified via amp_amplevel.
static const float kModelMakeup[16] = { 4.89f, 1.18f, 1.48f, 3.18f, 1.19f, 1.0f, 1.14f, 4.8f, 2.05f, 4.15f, 1.49f, 2.16f, 3.4f, 3.65f, 2.83f, 1.77f };  // [14] Blue Liner: measured 2026-08-28 (hexforge_amplevel −22.0 → the −13 clean parity target; standalone lands −5.2, in family)  // [0] Fender 5.20->4.89 (2026-08-21 tube audit, mirrors hexforge kAmpMakeup): correct 6V6 runs +0.54 dB hotter at the noon anchor. Previously bumped 3.78->5.20 (2026-07-28): the item #28/#25 exact-tonestack re-voice measured ~2.8dB quieter vs NAM than the old heuristic path (nam_compare loudness: old needed x1.38, new needs x1.90) -- this restores the SAME loudness parity the re-voice's own FR-matching work didn't otherwise change; [5] NAM passthrough; [11] Cali V / [12] Diamond Plate scale all modes (per-mode makeup inside the model); [12] measured via amp_amplevel

// Tight (2026-10-20, mirrors hasTight in hf_types.inc / HAS_TIGHT in script-amp.js): the high-gain amps
// Crunchy, Gainzilla, Tangerang, Beardo BE, Cali V, Diamond Plate, Tremont 15.
static inline bool hasTight(int m) noexcept {
    return m == 1 || m == 2 || m == 4 || m == 6 || m == 11 || m == 12 || m == 13;
}

// ── Component builds (2026-09-23, mirrors Hex Forge v48+) ───────────────────
// Amps that have a schematic-exact twin. Keep in sync with HAS_COMP in script-amp.js.
static inline bool hasComponentModel(int m) noexcept {
    return m == 0 || m == 1 || m == 2 || m == 3 || m == 4 || m == 6 || m == 7 || m == 8 || m == 9 || m == 10 || m == 11 || m == 12 || m == 13 || m == 14;
}
// TEMPORARY A/B (2026-11-05, as Hex Forge's): the four newest twins (Doom Daddy, Hi-Volt, Backline Plus,
// Tremont 15) run only while ~/.config/hexchain/newtwins_on exists (abHt bit 7); the Gainzilla twin takes
// the HT extras only while gzht_on exists (bit 8). The Anagram never polls: both stay off there.
static constexpr int kAbNewTwins = 128;
static constexpr int kAbGzHt     = 256;
static inline bool isNewTwin(int m) noexcept { return m == 3 || m == 7 || m == 9 || m == 13; }
static inline bool twinAllowed(int m, int abFlags) noexcept { return !isNewTwin(m) || (abFlags & kAbNewTwins) != 0; }
// The twins that take the valve swap, the corrected valve types and the Speaker Load: every component
// build (the Gainzilla's since 2026-11-05) but the solid-state Backline Plus, plus the Citrus 200
// (always schematic-exact). Mirrors has_valves in script-amp.js.
// The Rockerverb twin's power valves default to EL34 (2026-11-05, the user's ear on the device): its Stock
// position builds the EL34 swap; the 6V6GT choice still gives the drawing's valve.
static inline int paValveFor(int m, int pa) noexcept { return (m == 4 && pa == 0) ? 1 : pa; }
static inline bool hasValveSwap(int m, bool comp) noexcept {
    return (comp && hasComponentModel(m) && m != 9) || m == kAd200Idx;
}
static inline AmpModel componentAmpFor(int m) noexcept {
    switch (m) {
        case 0:  return AmpModel::DeluxeComp;
        case 1:  return AmpModel::JCM800Comp;
        case 2:  return AmpModel::EVH5150Comp;
        case 4:  return AmpModel::RockerverbComp;
        case 6:  return AmpModel::FriedmanBE100Comp;
        case 8:  return AmpModel::VoxComp;
        case 10: return AmpModel::PlexiComp;
        case 11: return AmpModel::MesaMarkVComp;
        case 12: return AmpModel::MesaDualRectifierComp;
        case 14: return AmpModel::SvtComp;
        case 3:  return AmpModel::SunnComp;
        case 7:  return AmpModel::HiwattComp;
        case 9:  return AmpModel::BackstageComp;
        case 13: return AmpModel::MT15Comp;
        default: return AmpModel::EVH5150Comp;
    }
}
// Loudness parity for a component build (same rows as Hex Forge): the twins carry
// their own power section and bypass the shared PA, so they miss its makeup and
// their dial-loudness curves differ. These put a twin where the shipped model sat
// at the same knobs, so the Component Build switch is loudness-neutral.
static const float kCompGainKnob[7]   = { 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f };
static const float kCompMasterKnob[4] = { 0.2f, 0.4f, 0.7f, 0.9f };
// [3]/[7]/[9]/[13]: the 2026-11-05 twins (parity with Hex Forge hf_types.inc)
static const float kCompMkDb[16] = { 10.50f, 1.90f, 3.60f, -7.35f, 7.29f, 0.0f, 1.80f, 3.60f, 4.50f, 0.70f, 1.40f, -2.95f, -3.00f, -0.05f, -2.41f, 0.0f };
static const float kCompGainDb[16][7] = {
    { 19.30f, 10.30f, 4.20f, 0.00f, -2.70f, -4.50f, -5.40f },
    { 0.00f, 0.20f, 0.05f, 0.00f, 0.00f, 0.00f, -0.05f },   // JCM800 corrected 2026-10-07 (evening) in-plugin after the knee anchor (inVolts 2.8): presets 62/1/24 at -24/-34 dBFS RMS, the twin's own gain-knob loudness law now sits within 0.2 dB of the shipped model (the old row {6.6 3.3 1.2 0 -.7 -1.2 -1.5} was compensating the wrong input scale); previously re-measured 2026-10-06 at the playing level (-24 dBFS RMS, tone knobs at noon): the rev-190 rows were taken on the Friedman's CLEAN channel (45 dB of gain-knob travel) and applied to BE/HBE (10 dB), which inverted the gain knob in use
    { 4.40f, 1.90f, 0.70f, 0.00f, -0.40f, -0.70f, -1.00f },
    { 3.35f, 1.65f, 0.67f, 0.00f, -0.53f, -0.97f, -1.37f },   // Doom Daddy twin (2026-11-05, standalone render at master .7; re-measure with the preset re-level)
    { -14.86f, -7.90f, -3.35f, 0.00f, 0.16f, 0.53f, 0.75f },   // Rockerverb twin on EL34 (2026-11-05: its Stock power valves, the user's ear); mk 10.40 -> 7.29, standalone render
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { -6.45f, -3.50f, -1.05f, 0.00f, 0.25f, 0.20f, 0.25f },   // Friedman corrected 2026-10-07 (evening) in-plugin after the knee anchor (inVolts 2.8, VR4 .25, HBE trim): BE/HBE presets 28/63/12 at -24/-34 dBFS RMS, mean; was {-4.95 -3.0 -1.35 0 -.05 -.15 -.2} with inVolts 1.0 / knee .75 (-24 dBFS, tone knobs at noon; mean of BE and HBE)
    { 4.73f, 0.35f, -0.28f, 0.00f, 0.45f, 0.84f, 1.19f },   // Hi-Volt twin (2026-11-05, same)
    { 1.60f, 0.40f, 0.20f, 0.00f, -0.10f, -0.30f, -0.30f },
    { -3.26f, -0.15f, 0.21f, 0.00f, 0.45f, 0.32f, 0.23f },   // Backline Plus twin (2026-11-05, same)
    { 3.30f, 1.60f, 0.50f, 0.00f, -0.10f, -0.20f, -0.20f },
    { -5.40f, -0.85f, -0.15f, 0.00f, 0.00f, 0.00f, -0.05f },   // Cali V: re-measured in-plugin 2026-10-19 (preset 52, wavgain 19.5) for the input scale 0.0011 -> 0.0070; Mk -2.80 -> -2.95
    { 4.70f, 3.20f, 1.60f, 0.00f, -0.40f, -0.90f, -1.60f },
    { -6.65f, -2.72f, -0.89f, 0.00f, 0.43f, 0.66f, 0.79f },   // Tremont 15 twin, Lead (2026-11-05, same)
    { 0.77f, 0.13f, -0.02f, -0.00f, 0.06f, 0.12f, 0.15f },
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
};
static const float kCompMasterDb[16][4] = {
    { -7.40f, -2.90f, 0.00f, 1.10f },
    { 0.00f, 0.00f, 0.00f, 0.00f },   // master row ZEROED 2026-10-07: the twin's own master law shows through (a master must get louder turned up); the row had cancelled it against the shipped model's level-invariant master and INVERTED the knob in use. Preset loudness is folded per preset on the twins anyway.
    { -7.50f, -0.90f, 0.00f, -1.80f },
    { 0.00f, 0.00f, 0.00f, 0.00f },
    { 0.00f, 0.00f, 0.00f, 0.00f },   // Rockerverb: master row zeroed 2026-10-07 (it inverted the master in use: Recto -6.6 dB from .5 to full, Mark V / Rockerverb -1.7)
    { 0.00f, 0.00f, 0.00f, 0.00f },
    { 0.00f, 0.00f, 0.00f, 0.00f },   // Friedman: same, 2026-10-07
    { 0.00f, 0.00f, 0.00f, 0.00f },
    { -1.30f, -0.40f, 0.00f, 0.10f },
    { 0.00f, 0.00f, 0.00f, 0.00f },
    { -3.10f, -0.90f, 0.00f, 0.20f },
    { 0.00f, 0.00f, 0.00f, 0.00f },   // Mark V: master row zeroed 2026-10-07 (it inverted the master in use: Recto -6.6 dB from .5 to full, Mark V / Rockerverb -1.7)
    { 0.00f, 0.00f, 0.00f, 0.00f },   // Recto: master row zeroed 2026-10-07 (it inverted the master in use: Recto -6.6 dB from .5 to full, Mark V / Rockerverb -1.7)
    { 0.00f, 0.00f, 0.00f, 0.00f },
    { -7.89f, -3.15f, -0.00f, 1.17f },
    { 0.00f, 0.00f, 0.00f, 0.00f },
};
static const float kCompModeDb[16][9] = {
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { -0.00f, -0.93f, -0.93f, -0.93f, -0.93f, -0.93f, -0.93f, -0.93f, -0.93f },
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { -0.00f, 11.50f, 11.50f, 11.50f, 11.50f, 11.50f, 11.50f, 11.50f, 11.50f },   // Rockerverb CLEAN channel: +23.2 dB vs the shipped at gain .5 / master .7 (re-measured 2026-10-05 after the grid fit) minus the dirty mk 11.7
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { 7.80f, 0.00f, -1.20f, -1.20f, -1.20f, -1.20f, -1.20f, -1.20f, -1.20f },   // Friedman: Clean / BE / HBE relative to BE (2026-10-07 night: Clean 9.3 -> 7.8 after the knee-width/scale change, re-measured in-plugin with its own gain row; evening: Clean re-measured in-plugin at gain .5 with inVolts 2.8, 13.6 -> 9.3; its volume law now spans 20 dB over .3-.7, not representable in a row)
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { -3.27f, -2.08f, 4.98f, 2.58f, 2.72f, 2.24f, -0.00f, 1.51f, 0.29f },
    { -6.51f, 0.68f, 4.33f, 2.63f, 0.57f, 1.42f, 0.78f, -1.81f, -1.81f },   // Diamond Plate per mode; 2026-10-19 + the 55 Hz low-resonance fit's A-weighted loss (parity with Hex Forge hf_types.inc)
    { 3.23f, 4.07f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },   // Tremont 15 twin (2026-11-05): Clean / Crunch (the real clean channel with the treble pot's pull-Boost) / Lead, relative to Lead
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
    { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
};
static inline float compLerp(const float* xs, const float* ys, int n, float x) noexcept {
    if (x <= xs[0])     return ys[0];
    if (x >= xs[n - 1]) return ys[n - 1];
    int i = 0; while (i < n - 2 && x > xs[i + 1]) ++i;
    const float t = (x - xs[i]) / (xs[i + 1] - xs[i]);
    return ys[i] + t * (ys[i + 1] - ys[i]);
}
// Friedman CLEAN channel (algo 6, sel 0): its own gain-knob loudness row. The twin's clean VOLUME is a
// real 1MA pot into a linear channel (35 dB of loudness over the knob, no compression), the shipped
// model's clean holds within 13 dB; measured in-plugin 2026-10-07 at the playing level (-24 dBFS RMS)
// on Vanishing Drive forced to Clean, shipped - twin at gain .2..  .8, added to the shared row.
static const float kCompFrCleanGainDb[7] = { 7.40f, 6.10f, 3.15f, 0.00f, -3.45f, -5.75f, -7.30f };
static inline float compParity(int algo, float gainKnob, float masterKnob, float sel) noexcept {
    const int si = (int)(sel + 0.5f);
    const float* gainRow = (algo == 6 && si == 0) ? kCompFrCleanGainDb : kCompGainDb[algo];
    const float db = kCompMkDb[algo]
                   + compLerp(kCompGainKnob,   gainRow,             7, gainKnob)
                   + compLerp(kCompMasterKnob, kCompMasterDb[algo], 4, masterKnob)
                   + kCompModeDb[algo][si < 0 ? 0 : (si > 8 ? 8 : si)];
    return (db == 0.0f) ? 1.0f : std::pow(10.0f, db / 20.0f);
}

#ifdef HEXCHAIN_ANAGRAM
// KosmOS keeps only a block's first 60 control ports (mod-connector MAX_PARAMS_PER_BLOCK, in port
// order, hidden ones included) and drops the rest: the Anagram layout (generated TTL, plugins/amp/
// spec.toml ports_last) puts every port the device shows first and the hidden ones last, before the
// atoms. Same names as the public layout below (plus P_FR_VOICE, Anagram-only); only the order
// differs. Code never relies on a port number except P_MODEL..P_CONTROL bracketing the controls
// (snapshotPorts), which still holds.
enum AmpPorts {
    P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
    P_MODEL, P_GAIN, P_BASS, P_MID, P_TREBLE, P_PRES, P_MASTER, P_SAG,
    P_RESON,
    P_FR_VOICE,                                            // Beardo: Fat / C45 / Sat as one list (2026-11-10)
    P_MV_EQPRESET,
    P_PL_VOL2,
    P_MT_BRIGHT,
    P_SV_ULTRALO, P_SV_ULTRAHI, P_SV_MIDFREQ,
    P_ECO,                                                 // Quality, on the device since 2026-11-11
    P_ENABLED,
    P_TIGHT,
    P_V1TUBE, P_PATUBE,
    P_RESET,
    P_TILE_FRAME,       // output: the chain tile's frame (TileAnim)
    // hidden on the device, past the host's 60: they keep their TTL defaults (or snapshotPorts sets them)
    P_CHANNEL, P_BYPASS,
    P_PA_BYPASS, P_PA_TUBE, P_PA_PRES, P_PA_DEPTH, P_PA_SAG, P_PA_MASTER, P_PA_NFB, P_PA_RESON, P_PA_AIR, P_PA_AUTO,
    P_FR_CHANNEL, P_MV_MODE, P_NAM_GAIN, P_NAM_VOL,
    P_RC_MODE, P_RC_VARIAC, P_RC_RECT, P_MT_MODE,
    P_PAMP_COUPL, P_PL_VARIAC, P_JCM_SIR34,
    P_COMP, P_DYNLOAD, P_MAINS,
    P_STAGE_FB, P_STAGE_FBDIST, P_STAGE_FBSUS,
    P_SUNN_V2, P_SUNN_LNK, P_SUNN_B2, P_SUNN_M2, P_SUNN_T2, P_SUNN_BR1, P_SUNN_BR2,
    P_FR_FAT, P_FR_C45, P_FR_SAT,
    P_MV_GEQ0, P_MV_GEQ1, P_MV_GEQ2, P_MV_GEQ3, P_MV_GEQ4,
    P_SPKLOAD,
    P_CONTROL, P_NOTIFY,
    P_N_PORTS
};
static constexpr int kHostTracked = P_CHANNEL;   // the first port the host does not track (no UI reports past it)
#else
enum AmpPorts {
    P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
    P_MODEL, P_GAIN, P_BASS, P_MID, P_TREBLE, P_PRES, P_MASTER, P_SAG,
    P_CHANNEL, P_RESON, P_SUNN_V2, P_SUNN_LNK, P_BYPASS,
    P_PA_BYPASS, P_PA_TUBE, P_PA_PRES, P_PA_DEPTH, P_PA_SAG, P_PA_MASTER,
    P_PA_NFB, P_PA_RESON, P_PA_AIR, P_PA_AUTO,
    P_SUNN_B2, P_SUNN_M2, P_SUNN_T2, P_SUNN_BR1, P_SUNN_BR2,  // Sunn Brite-channel
    P_FR_CHANNEL, P_FR_FAT, P_FR_C45, P_FR_SAT,               // Beardo BE (Friedman) — 3-way channel + voicing toggles
    P_MV_MODE, P_MV_GEQ0, P_MV_GEQ1, P_MV_GEQ2, P_MV_GEQ3, P_MV_GEQ4, P_MV_EQPRESET,  // Cali V (Mesa Mark V): 9-mode + 5-band graphic EQ
    P_NAM_GAIN, P_NAM_VOL,                                     // Neural (NAM): input drive + output level (dB), used only in NAM mode
    P_PL_VOL2,                                                 // Plexiglass (1959): Vol II — jumpered Normal-channel volume (0 = pre-Vol-II voicing)
    P_RC_MODE, P_RC_VARIAC, P_RC_RECT,                         // Diamond Plate (Dual Rectifier): 8-mode + Variac (Bold/Spongy) + rectifier (Silicon/Tube)
    P_MT_MODE, P_MT_BRIGHT,                                    // Tremont 15 (PRS MT15): Clean/Crunch/Lead + bright switch
    P_PAMP_COUPL,                                              // speaker-impedance coupling (2026-07-23, additive, default 0)
    P_PL_VARIAC,                                               // Plexiglass Variac (brown sound, v46, default 0 = stock)
    P_JCM_SIR34,                                               // Crunchy (JCM800) SIR #34 mod (v47, default 0 = stock)
    P_SV_ULTRALO, P_SV_ULTRAHI, P_SV_MIDFREQ,                  // Blue Liner (SVT): Ultra-Lo/Ultra-Hi + 3-way mid select (0/1/2 = 220/800/3k, default 1)
    P_COMP, P_DYNLOAD, P_ECO,                                  // 2026-09-23 (v62): Component Build (schematic-exact twin, own power section),
                                                               // Dynamic Load (the driver as the power section's load), Engine Quality (4x / 2x)
    P_MAINS,                                                   // 2026-09-25: mains frequency for the input hum comb + gate detector comb (0 = 60 Hz, 1 = 50 Hz)
    P_ENABLED,                                             // lv2:designation lv2:enabled — the host's block enable,
                                                           // INVERTED vs Bypass (1 = on).  Placed BEFORE the atoms:
                                                           // mod-host breaks if control ports follow them.
    P_TIGHT,                                               // 2026-10-20 (mv73): Tight on the high-gain amps (mirrors Hex Forge amp_tight; 0 = off)
    P_STAGE_FB, P_STAGE_FBDIST, P_STAGE_FBSUS,             // 2026-10-20 (mv74): Stage Feedback -- the speaker driving the strings
                                                           // (mirrors Hex Forge stage_fb/fbdist/fbsus; 0 = off, bit-identical)
    P_V1TUBE, P_PATUBE, P_SPKLOAD,                         // 2026-11-04 (mv77): VALVES tab -- V1 valve, power valves, the cab the
                                                           // power stage drives (mirrors Hex Forge; 0 = stock / own cab, bit-identical)
    P_CONTROL, P_NOTIFY,                                       // atom in/out (NAM file) — MUST be last: MOD/mod-host break if control ports follow the atom ports
    P_N_PORTS
};
#endif

static int clampIdx(float v, int lo, int hi) {
    int i = static_cast<int>(v + 0.5f);
    if (i < lo) i = lo;
    if (i > hi) i = hi;
    return i;
}

#ifdef HEXCHAIN_ANAGRAM
// ── The device's model list (2026-11-10) ─────────────────────────────────────────
// Every channel of a multi-channel amp is its own model on the Anagram, so there is no channel
// selector to page to: the Model port (its scalePoints in plugins/amp/spec.toml, same order) picks
// a row here, and the row sets the engine's model and that channel's port. The channel ports are
// hidden on the device and ignored for these models. Diamond Plate (Recto) sits this release out.
// The Cali V's Component Build models channel 3 only so far (MesaMarkVComponentModel: modes 0-5
// play IIC+): its channel 1 and 2 rows run the standard model, which has all nine (noTwin).
// os2 (2026-11-11, CPU): the row runs at 2x oversampling (the Engine Quality build) instead of 4x, about
// half its cost. Only rows whose aliasing at 2x stayed within 2.5 dB of 4x on a hot 3.1 kHz / 1 kHz tone
// (measured on the device, docs/NOTES.md): Clean Meanie, Tangerang Clean, Cali V Clean and Fat. Beardo
// Clean (-6 dB), Cali V Tweed (-14), Tremont Clean (-10) and Citrus 200 (-9) break up more than that
// and stay at 4x.
// stdQ (2026-11-11, the user: Coastline Tweed "sounds odd" once Eco became the default): picking the row
// sets Quality to Standard (4x); Eco stays selectable. The four rows that failed the 2x alias test (Tweed:
// its guitar render at 2x differs from 4x by -3.9 dB above 8 kHz). Leaving one for a plain row sets Eco
// back (the default). amp_run, anagramQualityFollowsRow.
struct AnaModel { int model; int port; float val; bool noTwin = false; bool os2 = false; bool stdQ = false; };   // port -1: no channel
static const AnaModel kAnaModels[] = {
    {  0, -1, 0, false, true }, // Clean Meanie
    {  1, P_JCM_SIR34, 0 },     // Crunchy McCrunchFace
    {  1, P_JCM_SIR34, 1 },     // Crunchy Hot Mod (the rental-fleet hot-rod mod)
    {  2, P_CHANNEL, 0 },       // Gain Kaiju Blue
    {  2, P_CHANNEL, 1 },       // Gain Kaiju Red
    {  3, P_SUNN_BR1, 0 },      // Doom Daddy Normal (the Normal channel alone, brights off)
    {  3, P_SUNN_BR1, 1 },      // Doom Daddy Brite (both channels jumpered, both brights on)
    {  4, P_CHANNEL, 0 },       // Tangerang Dirty
    {  4, P_CHANNEL, 1, false, true },  // Tangerang Clean
    {  6, P_FR_CHANNEL, 0, false, false, true }, // Beardo Clean (Standard)
    {  6, P_FR_CHANNEL, 1 },    // Beardo Drive
    {  6, P_FR_CHANNEL, 2 },    // Beardo Hot
    {  7, -1, 0 },              // Hi-Volt
    {  8, -1, 0 },              // Chime Thirty
    {  9, -1, 0 },              // Backline Plus
    { 10, P_PL_VARIAC, 0 },     // Plexiglass
    { 10, P_PL_VARIAC, 1 },     // Plexiglass Low Volt (the variac mod)
    { 11, P_MV_MODE, 0, true, true }, // Cali V Clean (standard model)
    { 11, P_MV_MODE, 1, true, true }, // Cali V Fat (standard model)
    { 11, P_MV_MODE, 2, true, false, true }, // Cali V Tweed (standard model; Standard)
    { 11, P_MV_MODE, 3, true }, // Cali V Edge (standard model)
    { 11, P_MV_MODE, 4, true }, // Cali V Crunch (standard model)
    { 11, P_MV_MODE, 5, true }, // Cali V Vintage Lead (standard model)
    { 11, P_MV_MODE, 6 },       // Cali V Classic Lead
    { 11, P_MV_MODE, 7 },       // Cali V Modern Lead
    { 11, P_MV_MODE, 8 },       // Cali V Extreme
    { 13, P_MT_MODE, 0, false, false, true }, // Tremont 15 Clean (Standard)
    { 13, P_MT_MODE, 1 },       // Tremont 15 Crunch
    { 13, P_MT_MODE, 2 },       // Tremont 15 Lead
    { 14, -1, 0 },              // Blue Liner
    { 15, -1, 0, false, false, true }, // Citrus 200 (Standard)
};
static constexpr int kAnaN = static_cast<int>(sizeof(kAnaModels) / sizeof(kAnaModels[0]));
static inline const AnaModel& anaModel(float portValue) noexcept {
    return kAnaModels[clampIdx(portValue, 0, kAnaN - 1)];
}
#endif

enum WorkType { WORK_LOAD, WORK_FREE, WORK_NAM_LOAD, WORK_NAM_FREE,
                WORK_AB_POLL,     // TEMPORARY physics A/B (2026-11-04, as Hex Forge's W_AB_POLL)
                WORK_BUILD };     // Anagram: a fully configured, pre-rolled amp (BuildMsg / BuildReply)   // TEMPORARY physics A/B (2026-11-04, as Hex Forge's W_AB_POLL)

struct WorkMsg {
    WorkType          type;
    AmpBlockExtended* amp = nullptr;   // amp LOAD reply / FREE target
    NamModel*         nam = nullptr;   // NAM LOAD reply / FREE target
    int               modelIdx = 0;
    bool              comp = false;    // build the component twin (when the model has one)
    bool              eco  = false;    // Engine Quality: 2x instead of 4x oversampling
    int               v1tube = 0;      // valve swap the fresh amp is built with (+ reply)
    int               patube = 0;
    int               ab     = 0;      // TEMPORARY A/B flags the fresh amp is built with (+ reply)
    char              path[kPathMax] = {0};
};

// WORK_FREE needs only the head of a WorkMsg: send that much, not the 1 KB path with it.
struct AmpFreeMsg { WorkType type; AmpBlockExtended* amp; };
static_assert(offsetof(AmpFreeMsg, amp) == offsetof(WorkMsg, amp), "AmpFreeMsg must prefix WorkMsg");

#ifdef HEXCHAIN_ANAGRAM
// ── Glitch-free amp switching (Anagram, 2026-11-09) ─────────────────────────────
// A model switch used to cost the device tens of milliseconds on the audio thread: while the new
// amp was being built the old one was handed the NEW model's settings (Cali V -> Diamond Plate
// sent the Mark V the Recto's mode, and each mode change rebuilds every stage, bias solves and
// valve tables, 40-100 ms on a twin), and once the new amp swapped in every setting was pushed
// to it again, rebuilding it a second time on the audio thread; the swap itself was a hard cut.
// Now:
//   - The worker builds the new amp COMPLETE: every port value applied (BuildMsg carries them
//     all), then pre-rolled on silence so its supplies, couplings and smoothers have settled.
//   - The running amp keeps its own settings until the swap (runKey / runV): nothing structural
//     is ever pushed to it here, so the audio thread never rebuilds stages.
//   - A switch whose setter rebuilds stages (structPorts) is a build too, not a push.
//   - The swap is a short fade out of the old amp and into the new one (kFadeN each way);
//     a build overtaken by a newer request (scrolling the model list) is dropped on arrival.
static constexpr int   kAmpSlots   = 64;     // amp pushes per block (the most is ~30, Sunn)
static constexpr int   kFadeN      = 384;    // 8 ms out, 8 ms in
static constexpr float kPrerollSec = 0.2f;   // silence run through a fresh amp on the worker
// Automatic mains handover (MainsDetect.h): the new comb's Q=18 notches ring in with tau =
// Q/(pi*f) ~ 0.11 s at 50 Hz, so 0.5 s of warm-up leaves < 1 % of its start-up transient; the
// 50 ms crossfade between two near-unity combs is inaudible (both pass the signal; only the hum
// lines they notch differ).
static constexpr double kMainsWarmSec = 0.5;
static constexpr double kMainsFadeSec = 0.05;

// What an amp object is built with: a change to any of it builds a new amp on the worker.
struct BuildKey {
    int  model = -1, v1 = 0, pa = 0, ab = 0;
    bool comp = false, eco = false;
    int  s[3] = { 0, 0, 0 };    // the model's structural switches, rounded
    bool operator==(const BuildKey& o) const noexcept {
        return model == o.model && v1 == o.v1 && pa == o.pa && ab == o.ab && comp == o.comp && eco == o.eco
            && s[0] == o.s[0] && s[1] == o.s[1] && s[2] == o.s[2];
    }
    bool operator!=(const BuildKey& o) const noexcept { return !(*this == o); }
};
// The switches whose setters rebuild the model (stage tables, bias solves, the twin's power
// section) or reset it outright (audited 2026-11-09). Lighter controls stay live pushes.
static int structPorts(int m, bool comp, int* out) noexcept {
    switch (m) {
        case kMesaIdx:     out[0] = P_MV_MODE; return 1;                                            // 10-100 ms
        case kRectoIdx:    out[0] = P_RC_MODE; out[1] = P_RC_RECT; out[2] = P_RC_VARIAC; return 3;  // 10-100 ms each
        case kMt15Idx:     out[0] = P_MT_MODE; return 1;                                            // stage tables / Boost rebuild
        case 1:            out[0] = P_JCM_SIR34; return 1;                                          // a model in the list; twin: full prepare
        case 10:           out[0] = P_PL_VARIAC; return 1;                                          // Plexiglass Variac: a model in the list
        // The channels are models in the device's list (kAnaModels): a channel change is a model
        // change, built and crossfaded like one even where the setter alone would be light.
        case kFriedmanIdx: out[0] = P_FR_CHANNEL; if (comp) { out[1] = P_FR_FAT; out[2] = P_FR_C45; return 3; } return 1;  // re-bias + state wipe
        case 4:            out[0] = P_CHANNEL; return 1;                                            // Rockerverb twin: reset()
        case 2:            out[0] = P_CHANNEL; return 1;                                            // Gain Kaiju Blue / Red
        case kSunnIdx:     out[0] = P_SUNN_BR1; out[1] = P_SUNN_LNK; return 2;                      // Normal / Brite: models in the list
        default:           return 0;
    }
}
static BuildKey buildKeyFor(const float* v, int m, int ab) noexcept {
    BuildKey k; k.model = m; k.ab = ab;
    k.comp = hasComponentModel(m) && v[P_COMP] > 0.5f && twinAllowed(m, ab);
    k.eco  = v[P_ECO] > 0.5f;
    if (hasValveSwap(m, k.comp)) {
        k.v1 = clampIdx(v[P_V1TUBE], 0, kV1TubeMax);
        k.pa = paValveFor(m, clampIdx(v[P_PATUBE], 0, kPaTubeMax));
    }
    int sp[3]; const int ns = structPorts(m, k.comp, sp);
    for (int i = 0; i < ns; ++i) k.s[i] = clampIdx(v[sp[i]], -1000, 1000);
    return k;
}
struct BuildMsg   { WorkType type; uint32_t seq; BuildKey key; float v[P_N_PORTS]; };
struct BuildReply { WorkType type; uint32_t seq; AmpBlockExtended* amp; BuildKey key; float v[P_N_PORTS];
                    int nSeed; float seed[kAmpSlots]; };   // seed: the values ampParams pushed, in order
#endif

struct URIs {
    LV2_URID atom_Object, atom_Path, atom_URID;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID nam_file;
};


struct AmpPlugin {
    double rate = 48000.0;

    AmpBlockExtended* amp = nullptr;   // double-buffered algo amp
    PowerAmpProcessor pa;
    AmpTight          tight[2];        // Tight: pre-clip high-pass + post-PA weight shelf, per channel
    StageFeedback     fb;              // Stage Feedback: speaker -> air -> string -> pickup, added to the input
    bool              fbWas = false;
    std::vector<float> fbInL, fbInR;   // the input with the string's forced vibration (the host buffers stay untouched)
    NoiseGateBlock    inGate;          // input gate BEFORE the amp's ~70 dB of gain — kills amplified
                                       // input-noise hiss on high-gain tones (engages only when driven)
    HumNotchComb      inComb[2];       // 60 Hz hum comb, also pre-gain (the user's measured idle floor is
                                       // 89% mains-hum harmonics; ~15 dB off it puts the floor under the
                                       // gate's close threshold). Engaged with the gate; always fed (warm state).
    NamModel*         nam = nullptr;   // swapped in by the worker on file load
    float             mainsCur = 60.0f; // mains the hum/detector combs are tuned to; starts at the
                                       // prepared 60 Hz so the default path re-tunes nothing

    float* ctrl[P_N_PORTS] = {};
#ifdef HEXCHAIN_ANAGRAM
    TileAnim tile;   // the chain tile's animation (P_TILE_FRAME)
#endif
    const LV2_Atom_Sequence* control = nullptr;
    LV2_Atom_Sequence*       notify  = nullptr;

    int  lastModel = -1;
    int  lastTube  = -1;
    bool lastComp  = false;   // component build the loaded amp was built with
    bool lastEco   = false;
    // Valve swap + the A/B physics: a change REBUILDS the amp on the worker (the twin's bias solves and
    // valve tables, ~10-25 ms on a Pi 5), never on this thread. last* = what the last scheduled build
    // carries; the running amp was built with its own values and is never retubed here.
    int  lastV1 = 0, lastPa = 0, lastAb = 0;
    // TEMPORARY physics A/B (2026-11-04, remove once decided; the same files as Hex Forge): the worker
    // polls <HOME>/.config/hexchain/ miller_on (bit 0), otres_on (1), pispread_on (2), rectlaw_on (3),
    // biassup_on (5), tubefix_on (6). cabload_on (4) is not read: here the Speaker Load port decides.
    std::atomic<int> abHt{0};
    uint32_t abPollCtr = 0;
    char namPath[kPathMax] = {0};
    float mono[kMaxBlock], monoOut[kMaxBlock];
    float gbufL[kMaxBlock], gbufR[kMaxBlock];   // gated-input scratch (avoids writing host input buffers)
#ifdef HEXCHAIN_ANAGRAM
    bool resetLatch    = false;   // kx:Reset edge detect
    // Automatic mains (MainsDetect.h): the detector listens to the raw input; when its verdict
    // differs from mainsCur, mainsNext (a fresh comb on the new mains) is fed alongside inComb[0]
    // for mainsWarm samples (its notches ring in), then the output crossfades to it over
    // mainsFade samples and it becomes inComb[0]. Nothing allocates, nothing jumps.
    MainsDetect  mainsDet;
    HumNotchComb mainsNext;
    int          mainsWarm = 0, mainsFade = 0;
    bool resetSwapWait = false;   // reset fired: passthrough until the fresh amp swaps in
    // AMP_PUSH state: the last value each call site pushed, and the generation of its target
    uint32_t ampGen = 1, paGen = 1, gateGen = 1;
    float    pushLast[160] = {};
    uint32_t pushGen[160]  = {};
    AnagramPortState<P_N_PORTS> ui;   // per-model control visibility reported to the host
    const LV2_ControlInputPort_Change_Request* chg = nullptr;   // asks the host to set Quality (stdQ rows)
    int rowSeen = -1;                 // the list row last seen (-1: startup / Reset, take as saved)
    // Glitch-free switching (see BuildKey)
    uint32_t   buildSeq = 0;              // the newest build asked for; older replies are dropped
    // One build at a time (2026-11-10, the user: spinning the model list made the switches lag
    // further and further behind -- every model passed queued a full build). A new choice while a
    // build is in flight marks it stale (wantSeq: the worker drops a stale build before its costly
    // steps) and is asked for when it comes back; only the latest choice is ever built.
    bool       buildBusy = false;
    std::atomic<uint32_t> wantSeq{0};     // the seq the RT side still wants (read by the worker)
    BuildKey   schedKey;                  // ... and what it carries
    BuildKey   runKey;                    // what the running amp was built with
    float      runV[P_N_PORTS] = {};      // ... its port values (structural switches are read from here)
    bool       runValid = false;          // a configured amp is running (until then: silence)
    int        runRow = 0;                // ... the list row it was built for (its values drive it)
    bool       havePend = false;          // a finished build waits for the fade-out
    BuildReply pendR {};
    float      ampLast[kAmpSlots] = {};   // ampParams dedup (slot = push order, fixed per model)
    uint32_t   ampLastGen[kAmpSlots] = {};
    float      fade = 1.0f;  int fadeDir = 0;   // swap fade: -1 out, +1 in
    float      wet  = 1.0f;                     // bypass ramp (lv2:enabled / Bypass)
    std::vector<float> dryL, dryR;              // the host input, kept for the bypass ramp
#endif

    LV2_Worker_Schedule* schedule = nullptr;
    LV2_URID_Map*        map      = nullptr;
    LV2_Atom_Forge       forge;
    URIs                 uris;
};

static void mapURIs(AmpPlugin* p) {
    LV2_URID_Map* m = p->map;
    p->uris.atom_Object    = m->map(m->handle, LV2_ATOM__Object);
    p->uris.atom_Path      = m->map(m->handle, LV2_ATOM__Path);
    p->uris.atom_URID      = m->map(m->handle, LV2_ATOM__URID);
    p->uris.patch_Set      = m->map(m->handle, LV2_PATCH__Set);
    p->uris.patch_Get      = m->map(m->handle, LV2_PATCH__Get);
    p->uris.patch_property = m->map(m->handle, LV2_PATCH__property);
    p->uris.patch_value    = m->map(m->handle, LV2_PATCH__value);
    p->uris.nam_file       = m->map(m->handle, AMP_NAM_URI);
}
static void writeNamToNotify(AmpPlugin* p) {
    const URIs& u = p->uris;
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&p->forge, 0);
    lv2_atom_forge_object(&p->forge, &frame, 0, u.patch_Set);
    lv2_atom_forge_key(&p->forge, u.patch_property);
    lv2_atom_forge_urid(&p->forge, u.nam_file);
    lv2_atom_forge_key(&p->forge, u.patch_value);
    lv2_atom_forge_path(&p->forge, p->namPath, static_cast<uint32_t>(std::strlen(p->namPath)));
    lv2_atom_forge_pop(&p->forge, &frame);
}

#ifndef HEXCHAIN_ANAGRAM
static LV2_Worker_Status scheduleRebuild(AmpPlugin* p, int modelIdx) {
    WorkMsg msg; msg.type = WORK_LOAD; msg.modelIdx = modelIdx;
    // gated like run()'s compOn: recorded raw, a model with no twin read as changed on EVERY block and
    // rebuilt the amp each period (Component Build is on by default) -- 2026-10-29 that storm flooded
    // mod-host's worker until it aborted on a corrupted free
    msg.comp = hasComponentModel(modelIdx) && p->ctrl[P_COMP] && *p->ctrl[P_COMP] > 0.5f
               && twinAllowed(modelIdx, p->abHt.load(std::memory_order_relaxed));
    msg.eco  = p->ctrl[P_ECO]  && *p->ctrl[P_ECO]  > 0.5f;
    if (hasValveSwap(modelIdx, msg.comp)) {
        msg.v1tube = p->ctrl[P_V1TUBE] ? clampIdx(*p->ctrl[P_V1TUBE], 0, kV1TubeMax) : 0;
        msg.patube = paValveFor(modelIdx, p->ctrl[P_PATUBE] ? clampIdx(*p->ctrl[P_PATUBE], 0, kPaTubeMax) : 0);
    }
    msg.ab = p->abHt.load(std::memory_order_relaxed);
    const LV2_Worker_Status st = p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
    if (st == LV2_WORKER_SUCCESS) { p->lastComp = msg.comp; p->lastEco = msg.eco;
                                    p->lastV1 = msg.v1tube; p->lastPa = msg.patube; p->lastAb = msg.ab; }
    return st;
}
#endif

static void freeAmpLater(AmpPlugin* p, AmpBlockExtended* a) {   // the worker deletes it, off the audio thread
    if (!a) return;
    AmpFreeMsg m { WORK_FREE, a };
    p->schedule->schedule_work(p->schedule->handle, sizeof(m), &m);
}

// ── Lifecycle ──────────────────────────────────────────────────────────────
static LV2_Handle amp_instantiate(const LV2_Descriptor*, double rate,
                                   const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) AmpPlugin;
    if (!p) return nullptr;
    p->schedule = static_cast<LV2_Worker_Schedule*>(lv2_find_feature(features, LV2_WORKER__schedule));
    p->map      = static_cast<LV2_URID_Map*>(lv2_find_feature(features, LV2_URID__map));
    if (!p->schedule || !p->map) { delete p; return nullptr; }
    mapURIs(p);
    lv2_atom_forge_init(&p->forge, p->map);
#ifdef HEXCHAIN_ANAGRAM
    p->ui.init(features);
    p->chg = static_cast<const LV2_ControlInputPort_Change_Request*>(
        lv2_find_feature(features, LV2_CONTROL_INPUT_PORT_CHANGE_REQUEST_URI));
    p->ui.limit = kHostTracked;   // the hidden ports past the host's 60 are never reported
#endif

    p->rate = rate;
    p->amp = new(std::nothrow) AmpBlockExtended;
    if (!p->amp) { delete p; return nullptr; }
    p->amp->prepare(rate, kMaxBlock, 2);
    p->amp->setAmpModel(AmpModel::MarshallJCM800);
    p->lastModel = 1;
    p->pa.prepare(rate, kMaxBlock, 2);
    p->tight[0].prepare(rate); p->tight[1].prepare(rate);
    p->fb.prepare(rate);
    p->fbInL.assign(kFbMaxN, 0.0f); p->fbInR.assign(kFbMaxN, 0.0f);
#ifdef HEXCHAIN_ANAGRAM
    p->dryL.assign(kFbMaxN, 0.0f); p->dryR.assign(kFbMaxN, 0.0f);
#endif
    p->inGate.prepare(rate, kMaxBlock, 2);
    p->inComb[0].prepare(rate);
    p->inComb[1].prepare(rate);
#ifdef HEXCHAIN_ANAGRAM
    p->mainsDet.prepare(rate);
#endif
    // Tuned for a fast, transparent noise-floor gate: quick open, long smooth tail so sustain rings out.
    p->inGate.setParameter("attack",     2.0f);
    p->inGate.setParameter("hold",     120.0f);
    p->inGate.setParameter("release",  250.0f);
    p->inGate.setParameter("hysteresis", 8.0f);
    return p;
}

static void amp_connect_port(LV2_Handle h, uint32_t port, void* data) {
    auto* p = static_cast<AmpPlugin*>(h);
    if (port == P_CONTROL)     p->control = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == P_NOTIFY) p->notify  = static_cast<LV2_Atom_Sequence*>(data);
    else if (port < P_N_PORTS) p->ctrl[port] = static_cast<float*>(data);
}

// ── The amp's own parameters, from a table of port values (2026-11-09) ──────────
// One mapping for both threads: run() pushes it to the running amp, and the Anagram worker
// applies it to a freshly built amp before that amp is heard (with every port's value, so the
// amp is already on the user's mode/rect/variac when it swaps in and nothing rebuilds here).
template <class Push>
static void ampParams(const float* v, int m, bool valves, int ab, Push&& push) {
    if (m == kSunnIdx) {
        push("vol1", v[P_GAIN]);
        push("vol2", v[P_SUNN_V2]);
        push("channel_link", v[P_SUNN_LNK]);
        push("bass1", v[P_BASS]);
        push("mid1", v[P_MID]);
        push("treble1", v[P_TREBLE]);
        push("bass2", v[P_SUNN_B2]);
        push("mid2", v[P_SUNN_M2]);
        push("treble2", v[P_SUNN_T2]);
        push("bright1", v[P_SUNN_BR1]);
        push("bright2", v[P_SUNN_BR2]);
    } else {
        push("gain", v[P_GAIN]);
        push("bass", v[P_BASS]);
        push("mid", v[P_MID]);
        push("treble", v[P_TREBLE]);
    }
    push("presence", v[P_PRES]);
    push("master", v[P_MASTER]);
    push("sag", v[P_SAG]);
#ifdef HEXCHAIN_ANAGRAM
    // Beardo BE takes its 3-way channel from P_FR_CHANNEL below. Pushing the generic Channel
    // first flipped it every block, and each flip re-prepares (= resets) V2a/V2b/V3a and
    // re-solves their bias: the preamp's state was wiped every period.
    if (m != kFriedmanIdx)
#endif
    push("channel", v[P_CHANNEL]);
    push("resonance", v[P_RESON]);
    push("dynload", v[P_DYNLOAD]);   // component builds only: the driver as the PA's load (small-signal transparent)
    // Component twins (2026-11-03 in Hex Forge, decided by ear): HT supply sag + ripple into the plates,
    // screens, PI and preamp rails; AC-heater hum, V1 microphony, valve spread.
    {   // TEMPORARY A/B (2026-11-05): the Gainzilla twin's only while gzht_on exists
        const float ht = (m != 2 || (ab & kAbGzHt)) ? 1.0f : 0.0f;
        push("supply", ht); push("ripple", ht);
        push("heaterhum", ht); push("micro", ht); push("spread", ht);
    }
    {   // Speaker Load: the chosen cab's speaker as the twin's power-stage load (0 = its own cab, unchanged)
        const int sl = valves ? clampIdx(v[P_SPKLOAD], 0, kSpkLoadMax) : 0;
        push("cabrow", static_cast<float>(kSpkLoadRow[sl]));
        push("cabload", sl > 0 ? 1.0f : 0.0f);
    }

    // Beardo BE (Friedman) — its own 3-way channel (Clean/BE/HBE) + voicing toggles.
    if (m == kFriedmanIdx) {
        push("channel", v[P_FR_CHANNEL]);
        push("fat", v[P_FR_FAT]);
        push("c45", v[P_FR_C45]);
        push("sat", v[P_FR_SAT]);
    }

    // Plexiglass (Marshall 1959): Vol II — the jumpered Normal channel (0 = the old single-volume voicing).
    if (m == 10) {
        push("vol2", v[P_PL_VOL2]);
        push("variac", v[P_PL_VARIAC]);   // brown sound (v46)
    }
    // Crunchy (JCM800): the SIR #34 rental-fleet hot-rod mod (v47).
    if (m == 1) push("sir34", v[P_JCM_SIR34]);
    // Cali V (Mesa Mark V) — 9-mode channel switcher + 5-band graphic EQ.
    if (m == kMesaIdx) {
        push("mode", v[P_MV_MODE]);
        push("geq0", v[P_MV_GEQ0]);
        push("geq1", v[P_MV_GEQ1]);
        push("geq2", v[P_MV_GEQ2]);
        push("geq3", v[P_MV_GEQ3]);
        push("geq4", v[P_MV_GEQ4]);
        push("eqpreset", v[P_MV_EQPRESET]);
    }
    // Diamond Plate (Dual Rectifier) — 8-mode selector + power-section feel switches.
    if (m == kRectoIdx) {
        push("mode", v[P_RC_MODE]);
        push("variac", v[P_RC_VARIAC]);
        push("rect", v[P_RC_RECT]);
    }
    // Tremont 15 (PRS MT15) - Clean/Crunch/Lead + the clean/crunch bright switch.
    if (m == kMt15Idx) {
        push("mode", v[P_MT_MODE]);
        push("bright", v[P_MT_BRIGHT]);
    }
    // Blue Liner (Ampeg SVT) - Ultra switches + the 3-position mid selector.
    if (m == kSvtIdx) {
        push("ultralo", v[P_SV_ULTRALO]);
        push("ultrahi", v[P_SV_ULTRAHI]);
        push("midfreq", v[P_SV_MIDFREQ]);
    }
}

// Port values as one table (control ports only; an unconnected port reads 0).
#ifdef HEXCHAIN_ANAGRAM
static void applyRow(const AnaModel& a, float* v);
#endif
static void snapshotPorts(const AmpPlugin* p, float* v, int row = -1) {
    for (int i = 0; i < P_N_PORTS; ++i)
        v[i] = (i >= P_MODEL && i < P_CONTROL && p->ctrl[i]) ? *p->ctrl[i] : 0.0f;
#ifdef HEXCHAIN_ANAGRAM
    // the device's list row (the selected one, or `row`: the one the running amp was built for)
    applyRow(row >= 0 ? kAnaModels[row] : anaModel(v[P_MODEL]), v);
#else
    (void) row;
#endif
}

#ifdef HEXCHAIN_ANAGRAM
// A list row -> the engine's model, that model's channel and the device's fixed values.
static void applyRow(const AnaModel& a, float* v) {
    v[P_MODEL] = static_cast<float>(a.model);
    if (a.port >= 0) v[a.port] = a.val;
    if (a.noTwin) v[P_COMP] = 0.0f;
    // Doom Daddy (Sunn): Normal = the Normal channel alone, brights off; Brite = the two channels
    // jumpered in parallel with both bright switches on, the Brite channel following the page-one
    // knobs (its own seven controls are not on the device).
    if (a.model == kSunnIdx) {
        const bool brite = v[P_SUNN_BR1] > 0.5f;
        v[P_SUNN_BR2] = brite ? 1.0f : 0.0f;
        v[P_SUNN_LNK] = brite ? 1.0f : 0.0f;
        v[P_SUNN_V2] = v[P_GAIN];
        v[P_SUNN_B2] = v[P_BASS]; v[P_SUNN_M2] = v[P_MID]; v[P_SUNN_T2] = v[P_TREBLE];
    }
    // Beardo: Fat / C45 / Sat from the Voicing list (Stock, Fat, C45, Sat, Fat+C45, Fat+Sat, C45+Sat, all)
    {
        static const int kVoice[8] = { 0, 1, 2, 4, 3, 5, 6, 7 };   // list row -> bits: Fat 1, C45 2, Sat 4
        const int b = kVoice[clampIdx(v[P_FR_VOICE], 0, 7)];
        v[P_FR_FAT] = (b & 1) ? 1.0f : 0.0f; v[P_FR_C45] = (b & 2) ? 1.0f : 0.0f; v[P_FR_SAT] = (b & 4) ? 1.0f : 0.0f;
    }
    // Cali V: the presets only (the graphic EQ is not on the device; its Custom row is gone): Flat if unset
    if (v[P_MV_EQPRESET] < 0.5f) v[P_MV_EQPRESET] = 1.0f;
    // Speaker Load is not on the device: the amp's own cab
    v[P_SPKLOAD] = 0.0f;
    // Quality (2026-11-11, on the device): the player's Standard (4x) / Eco (2x); the rows marked os2
    // are always Eco. Mains is not on the device: the port reads 60 Hz, the mains actually used is
    // detected from the input (MainsDetect.h, amp_run).
    v[P_ECO] = (a.os2 || v[P_ECO] > 0.5f) ? 1.0f : 0.0f;
    v[P_MAINS] = 0.0f;
}
// The row a finished build was made for (its port values carry the row's channel): found once at
// the swap, so the running amp keeps being driven by ITS row while another one is being built.
static int rowOf(const float* v) noexcept {
    const int m = static_cast<int>(v[P_MODEL] + 0.5f);
    for (int r = 0; r < kAnaN; ++r) {
        const AnaModel& a = kAnaModels[r];
        if (a.model != m) continue;
        if (a.port >= 0 && std::fabs(v[a.port] - a.val) > 0.25f) continue;
        return r;
    }
    return 0;
}
#endif

#ifdef HEXCHAIN_ANAGRAM
// The finished build becomes the running amp. Its pushes are seeded with the values the worker
// applied, so nothing is re-pushed (= nothing rebuilt) on this thread; the old amp is freed off it.
static void anagramSwap(AmpPlugin* p) {
    AmpBlockExtended* old = p->amp;
    p->amp = p->pendR.amp;
    p->runKey = p->pendR.key;
    std::memcpy(p->runV, p->pendR.v, sizeof(p->runV));
    p->runRow = rowOf(p->runV);
    p->runValid = true; p->havePend = false;
    p->lastModel = p->runKey.model;
    ++p->ampGen;
    const int ns = std::min(p->pendR.nSeed, kAmpSlots);
    for (int k = 0; k < ns; ++k) { p->ampLast[k] = p->pendR.seed[k]; p->ampLastGen[k] = p->ampGen; }
    freeAmpLater(p, old);
}

// Every block, before anything is heard: ask the worker for the amp the ports describe, drop a
// finished build the ports no longer want, and run the swap fade. quiet = nothing of the amp is
// audible (bypassed), so a finished build swaps at once.
static void anagramSchedule(AmpPlugin* p, int modelIdx, bool quiet) {
    if (modelIdx == kNamIdx) return;   // the NAM slot builds nothing
    float v[P_N_PORTS];
    snapshotPorts(p, v);
    v[P_MODEL] = static_cast<float>(modelIdx);
    const BuildKey want = buildKeyFor(v, modelIdx, p->abHt.load(std::memory_order_relaxed));
    if (want != p->schedKey) {
        if (p->runValid && !p->resetSwapWait && want == p->runKey) {   // back to what is playing
            p->schedKey = want; ++p->buildSeq;                         // (a build in flight is stale)
            p->wantSeq.store(p->buildSeq, std::memory_order_release);
        } else if (p->buildBusy) {   // one at a time: the build in flight is for another choice
            if (p->schedKey != BuildKey{}) {
                ++p->buildSeq; p->wantSeq.store(p->buildSeq, std::memory_order_release);   // stale now
                p->schedKey = BuildKey{};   // ask for the latest choice once it comes back
            }
        } else {
            BuildMsg msg; msg.type = WORK_BUILD; msg.seq = p->buildSeq + 1; msg.key = want;
            std::memcpy(msg.v, v, sizeof(msg.v));
            p->wantSeq.store(msg.seq, std::memory_order_release);
            p->buildBusy = true;   // set first: a synchronous worker replies inside schedule_work
            if (p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg) == LV2_WORKER_SUCCESS) {
                p->buildSeq = msg.seq; p->schedKey = want;
            } else {   // the ring is full, ask again next block
                p->buildBusy = false; p->wantSeq.store(p->buildSeq, std::memory_order_release);
            }
        }
    }
    if (p->havePend && p->pendR.key != p->schedKey) {   // finished, but no longer wanted
        freeAmpLater(p, p->pendR.amp); p->havePend = false;
        if (p->fadeDir < 0) p->fadeDir = +1;
    }
    if (p->havePend) {
        if (quiet)                                   { anagramSwap(p); p->fade = 1.0f; p->fadeDir = 0; }
        else if (p->fadeDir < 0 && p->fade <= 0.0f)  { anagramSwap(p); p->fadeDir = +1; }
        else                                         p->fadeDir = -1;
    }
}
static inline float fadeShape(float x) noexcept { return x * x * (3.0f - 2.0f * x); }   // smoothstep
#endif

// ── Worker thread ────────────────────────────────────────────────────────────
static LV2_Worker_Status amp_work(LV2_Handle h, LV2_Worker_Respond_Function respond,
                                  LV2_Worker_Respond_Handle handle,
                                  uint32_t, const void* data) {
    auto* p = static_cast<AmpPlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);

    if (msg->type == WORK_FREE)     { delete msg->amp; return LV2_WORKER_SUCCESS; }
    if (msg->type == WORK_NAM_FREE) { delete msg->nam; return LV2_WORKER_SUCCESS; }
    if (msg->type == WORK_AB_POLL) {   // TEMPORARY physics A/B (2026-11-04, see AmpPlugin::abHt)
        static const char* const kAbFiles[9] = { "/miller_on", "/otres_on", "/pispread_on", "/rectlaw_on",
                                                 nullptr, "/biassup_on", "/tubefix_on",
                                                 "/newtwins_on", "/gzht_on" };   // 2026-11-05: bits 7 and 8
        const char* home = std::getenv("HOME");
        const std::string dir = (home && home[0]) ? std::string(home) + "/.config/hexchain" : std::string("/tmp");
        int v = 0;
        for (int b = 0; b < 9; ++b)
            if (kAbFiles[b])
                if (FILE* f = std::fopen((dir + kAbFiles[b]).c_str(), "r")) { v |= (1 << b); std::fclose(f); }
        p->abHt.store(v, std::memory_order_relaxed);
        return LV2_WORKER_SUCCESS;
    }

    if (msg->type == WORK_NAM_LOAD) {
        auto* nm = new(std::nothrow) NamModel;
        if (!nm) return LV2_WORKER_ERR_NO_SPACE;
        if (nm->loadFromFile(msg->path)) nm->reset(p->rate, kMaxBlock);
        WorkMsg reply; reply.type = WORK_NAM_LOAD; reply.nam = nm;
        respond(handle, sizeof(reply), &reply);
        return LV2_WORKER_SUCCESS;
    }

#ifdef HEXCHAIN_ANAGRAM
    if (msg->type == WORK_BUILD) {   // a complete amp: model, valves, every setting, pre-rolled
        const auto* bm = static_cast<const BuildMsg*>(data);
        const BuildKey& k = bm->key;
        // already passed by (the player is still turning the list): answer at once, build nothing
        auto stale = [p, bm]() { return bm->seq != p->wantSeq.load(std::memory_order_acquire); };
        auto dropped = [&]() {
            BuildReply r {}; r.type = WORK_BUILD; r.seq = bm->seq; r.amp = nullptr;
            respond(handle, sizeof(r), &r);
            return LV2_WORKER_SUCCESS;
        };
        if (stale()) return dropped();
        auto* na = new(std::nothrow) AmpBlockExtended;
        if (!na) return dropped();   // answer anyway: an unanswered build would hold buildBusy for good
        na->prepare(p->rate, kMaxBlock, 2);
        na->setEco(k.eco);   // must precede setAmpModel (the oversampling wrapper is built there)
        na->setAmpModel(k.comp ? componentAmpFor(k.model) : kModelMap[k.model]);
        if (stale()) { delete na; return dropped(); }
        if (k.v1) na->setParameter("v1tube", static_cast<float>(k.v1));
        if (k.pa) na->setParameter("patube", static_cast<float>(k.pa));
        if (const int f = k.ab) {
            na->setParameter("miller",   (f & 1)  ? 1.0f : 0.0f);
            na->setParameter("otres",    (f & 2)  ? 1.0f : 0.0f);
            na->setParameter("pispread", (f & 4)  ? 1.0f : 0.0f);
            na->setParameter("rectlaw",  (f & 8)  ? 1.0f : 0.0f);
            na->setParameter("biassup",  (f & 32) ? 1.0f : 0.0f);
            na->setParameter("tubefix",  (f & 64) ? 1.0f : 0.0f);
        }
        BuildReply r {};
        r.type = WORK_BUILD; r.seq = bm->seq; r.amp = na; r.key = k;
        std::memcpy(r.v, bm->v, sizeof(r.v));
        ampParams(bm->v, k.model, hasValveSwap(k.model, k.comp), k.ab, [&r, na](const char* id, float x) {
            if (r.nSeed < kAmpSlots) r.seed[r.nSeed] = x;
            ++r.nSeed; na->setParameter(id, x);
        });
        na->setBypass(false);
        if (stale()) { delete na; return dropped(); }   // (deleted here, on the worker)
        // Pre-roll on silence: the fresh amp's supplies, coupling caps and smoothers settle here,
        // not audibly after the swap. Deterministic, so the dual-mono contract holds.
        float z[kMaxBlock], o[kMaxBlock]; float* zi[1] = { z }; float* oo[1] = { o };
        const int blocks = static_cast<int>(p->rate * kPrerollSec) / kMaxBlock + 1;
        for (int b = 0; b < blocks; ++b) {
            if ((b & 3) == 0 && stale()) { delete na; return dropped(); }   // ~40 ms of pre-roll between checks
            std::memset(z, 0, sizeof(z)); na->process(zi, oo, kMaxBlock, 1);
        }
        respond(handle, sizeof(r), &r);
        return LV2_WORKER_SUCCESS;
    }
#endif
    // WORK_LOAD — build a fresh algo amp off the RT thread.
    auto* na = new(std::nothrow) AmpBlockExtended;
    if (!na) return LV2_WORKER_ERR_NO_SPACE;
    na->prepare(p->rate, kMaxBlock, 2);
    na->setEco(msg->eco);   // Engine Quality: must precede setAmpModel (the oversampling wrapper is built there)
    {   // Component Build swaps in the schematic-exact twin (own power section)
        const int idx = clampIdx(static_cast<float>(msg->modelIdx), 0, kMaxModel);
        na->setAmpModel((msg->comp && hasComponentModel(idx)) ? componentAmpFor(idx) : kModelMap[idx]);
    }
    // Valve swap + the A/B physics are set HERE, on the fresh amp: each change rebuilds the twin's
    // stages, which must stay off the audio thread. 0 = stock / off, which the amp already is.
    if (msg->v1tube) na->setParameter("v1tube", static_cast<float>(msg->v1tube));
    if (msg->patube) na->setParameter("patube", static_cast<float>(msg->patube));
    if (const int f = msg->ab) {
        na->setParameter("miller",   (f & 1)  ? 1.0f : 0.0f);
        na->setParameter("otres",    (f & 2)  ? 1.0f : 0.0f);
        na->setParameter("pispread", (f & 4)  ? 1.0f : 0.0f);
        na->setParameter("rectlaw",  (f & 8)  ? 1.0f : 0.0f);
        na->setParameter("biassup",  (f & 32) ? 1.0f : 0.0f);
        na->setParameter("tubefix",  (f & 64) ? 1.0f : 0.0f);
    }
    WorkMsg reply; reply.type = WORK_LOAD; reply.amp = na; reply.modelIdx = msg->modelIdx;
    respond(handle, sizeof(reply), &reply);
    return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status amp_work_response(LV2_Handle h, uint32_t, const void* data) {
    auto* p = static_cast<AmpPlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == WORK_NAM_LOAD) {
        NamModel* old = p->nam;
        p->nam = msg->nam;
        WorkMsg freeMsg; freeMsg.type = WORK_NAM_FREE; freeMsg.nam = old;
        p->schedule->schedule_work(p->schedule->handle, sizeof(freeMsg), &freeMsg);
        return LV2_WORKER_SUCCESS;
    }
#ifdef HEXCHAIN_ANAGRAM
    if (msg->type == WORK_BUILD) {
        const auto* r = static_cast<const BuildReply*>(data);
        p->buildBusy = false;   // the worker is free: anagramSchedule asks for the latest choice
        if (!r->amp) return LV2_WORKER_SUCCESS;                                                // dropped as stale
        if (r->seq != p->buildSeq) { freeAmpLater(p, r->amp); return LV2_WORKER_SUCCESS; }   // overtaken
        if (p->havePend) freeAmpLater(p, p->pendR.amp);
        p->pendR = *r; p->havePend = true;
        if (p->resetSwapWait || !p->runValid) {   // nothing (configured) is playing: swap now, fade in
            anagramSwap(p);
            p->resetSwapWait = false;
            p->fade = 0.0f; p->fadeDir = +1;
        }
        return LV2_WORKER_SUCCESS;
    }
#endif
    // WORK_LOAD reply — swap the algo amp.
    AmpBlockExtended* old = p->amp;
    p->amp = msg->amp;
    p->lastModel = msg->modelIdx;
#ifdef HEXCHAIN_ANAGRAM
    p->resetSwapWait = false;   // fresh amp in place — resume processing
    ++p->ampGen;                // a new amp object: every AMP_PUSH slot pushes to it again
#endif
    freeAmpLater(p, old);
    return LV2_WORKER_SUCCESS;
}

#ifdef HEXCHAIN_ANAGRAM
// The device draws the controls itself, so the modgui's visibility rules (script-amp.js
// update_model / update_amp_ctls / update_comp / refresh_tabs) are restated here: a control
// is shown only when the selected model really has it. Keep the two in sync.
static void anagramUpdateUi(AmpPlugin* p, int m, const AnaModel& sel) {
    auto on = [p](int port) { return p->ctrl[port] && *p->ctrl[port] > 0.5f; };
    const bool rvClean  = (m == 4 && sel.port == P_CHANNEL && sel.val > 0.5f);   // Tangerang Clean: no Middle / Master
    const bool twin     = hasComponentModel(m) && !isNewTwin(m);   // the 2026-11-05 twins wait for their A/B, which the device has no files for
    const bool comp     = twin && on(P_COMP) && !sel.noTwin;
    const bool hasPower = (m != kSunnIdx && m != kNamIdx);   // Sunn's own power section; a capture has its own
    const bool paFace   = hasPower && !comp;                  // a component twin runs its own power section
    const bool paManual = paFace && !on(P_PA_AUTO);
    AnagramPortState<P_N_PORTS>& ui = p->ui;

    // Core tone controls: greyed (INACTIVE) where a model lacks them, never hidden -- KosmOS only
    // hides ports declaring dg:mayUpdateBlockedState, which would also take them out of Scenes.
    ui.ui(P_MID,      true, !(m == 0 || m == 8 || rvClean));
    ui.ui(P_PRES,     true, !(m == 0 || m == 4 || m == 9 || m == 13 || m == 14 || m == 15));
    ui.ui(P_MASTER,   true, !(m == 0 || m == 8 || m == 10 || m == 14 || rvClean));
    ui.ui(P_CHANNEL,  false);   // the channel selectors: models in the device's list (kAnaModels)
    ui.ui(P_RESON,    m == 2);
    ui.ui(P_TIGHT,    hasTight(m));
    ui.ui(P_JCM_SIR34, false);   // models in the list (kAnaModels)
    ui.ui(P_PL_VOL2,  m == 10);
    ui.ui(P_PL_VARIAC, false);
    // (the Sunn's Brite-channel controls: Doom Daddy Normal / Brite in the model list)
    ui.ui(P_FR_VOICE, m == kFriedmanIdx);
    ui.ui(P_MV_EQPRESET, m == kMesaIdx);
    for (int q : { P_RC_MODE, P_RC_VARIAC, P_RC_RECT }) ui.ui(q, false);   // Diamond Plate: not in this release
    ui.ui(P_MT_MODE, false);
    ui.ui(P_MT_BRIGHT, m == kMt15Idx);
    for (int q : { P_SV_ULTRALO, P_SV_ULTRAHI, P_SV_MIDFREQ }) ui.ui(q, m == kSvtIdx);

    ui.ui(P_COMP,    twin);
    for (int q : { P_V1TUBE, P_PATUBE }) ui.ui(q, hasValveSwap(m, comp) && (comp || m == kAd200Idx));
    ui.ui(P_DYNLOAD, comp);
    // Quality (2026-11-11, on the device): every model but the ones that always run at 2x (the os2
    // rows, the Sunn) -- there it is already Eco
    ui.ui(P_ECO,     !sel.os2 && m != kSunnIdx);
    ui.ui(P_PA_AUTO,   paFace);
    ui.ui(P_PA_BYPASS, paFace);
    for (int q : { P_PA_TUBE, P_PA_PRES, P_PA_DEPTH, P_PA_SAG, P_PA_MASTER, P_PA_NFB, P_PA_RESON, P_PA_AIR })
        ui.ui(q, paManual, !on(P_PA_BYPASS));

    const bool fb = p->ctrl[P_STAGE_FB] && *p->ctrl[P_STAGE_FB] > 0.0005f;
    ui.ui(P_STAGE_FBDIST, true, fb);
    ui.ui(P_STAGE_FBSUS,  true, fb);
}
#endif

// ── Audio ─────────────────────────────────────────────────────────────────────
#ifdef HEXCHAIN_ANAGRAM
// Per-block parameter pushes, skipped when the value is unchanged (Anagram CPU pass, 2026-11-09).
// At the device's 16-frame period amp_run pushed ~40 values a block, each building a std::string
// and walking a chain of string compares: ~4 % of the amp. Each call site owns a slot; a slot
// pushes again whenever its target object is new (generation bump: amp swap, PA/gate rebuild).
// Slots are compile-time constants: a function-local static numbered at first use would run a
// thread-safe init guard, which may lock, on the audio thread.
enum : int { kPushBase = __COUNTER__ };
static constexpr int kPushSlots = 160;   // = AmpPlugin::pushLast / pushGen size
#define AMP_PUSH(obj, gen, id, val) do {                                              \
        constexpr int s_ = __COUNTER__ - kPushBase - 1;                               \
        const float v_ = (val);                                                       \
        if (s_ >= kPushSlots) { (obj).setParameter(id, v_); break; }                  \
        if (p->pushGen[s_] != (gen) || !(p->pushLast[s_] == v_)) {                    \
            p->pushLast[s_] = v_; p->pushGen[s_] = (gen); (obj).setParameter(id, v_); \
        }                                                                             \
    } while (0)
#else
#define AMP_PUSH(obj, gen, id, val) (obj).setParameter(id, val)
#endif

static void amp_run(LV2_Handle h, uint32_t n) {
#ifndef HEXCHAIN_ANAGRAM
    DenormalGuard denormalGuard;
#endif  // KosmOS forbids touching global CPU registers (FTZ) — even scoped
    auto* p = static_cast<AmpPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    p->tile.run(p->ctrl[P_TILE_FRAME], n, p->ctrl[P_ENABLED]);
#endif
    const URIs& u = p->uris;

#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset trigger (rising edge): re-init every stateful stage.
    // RECONSTRUCT the value members, not just prepare() — prepare()'s
    // same-size resize() calls keep old buffer content (measured 0.2 residue
    // on the Pi). The heap amp is rebuilt through the normal worker path
    // (fresh AmpBlockExtended swapped in, old one freed off-RT) — a few
    // blocks of stale-amp output until the swap lands, same as any model
    // change. Reconstruction allocates, but the host only fires Reset on
    // preset/bank/pairing events.
    if (p->ctrl[P_RESET] && *p->ctrl[P_RESET] > 0.5f) {
        if (!p->resetLatch) {
            p->resetLatch = true;
            p->schedKey = BuildKey{};   // anagramSchedule (below) asks the worker for a fresh, complete amp
            ++p->buildSeq; p->wantSeq.store(p->buildSeq, std::memory_order_release);   // a build in flight is stale
            if (p->havePend) { freeAmpLater(p, p->pendR.amp); p->havePend = false; }
            // Passthrough until the fresh amp lands: otherwise the still-old
            // (divergent) amp re-contaminates the just-reset PA/gate within
            // this very block (measured 0.1 post-reset divergence on the Pi).
            p->resetSwapWait = true;
            ++p->paGen; ++p->gateGen;   // rebuilt below: AMP_PUSH re-sends their values
            p->pa.~PowerAmpProcessor();
            new (&p->pa) PowerAmpProcessor{};
            p->pa.prepare(p->rate, kMaxBlock, 2);
            p->tight[0].prepare(p->rate); p->tight[1].prepare(p->rate);
            p->fb.reset();
            p->inGate = NoiseGateBlock{};
            p->inGate.prepare(p->rate, kMaxBlock, 2);
            p->inGate.setParameter("attack",     2.0f);
            p->inGate.setParameter("hold",     120.0f);
            p->inGate.setParameter("release",  250.0f);
            p->inGate.setParameter("hysteresis", 8.0f);
            // automatic mains: the detected mains survives a Reset (a preset / bank change must not
            // bring the hum back for the seconds re-detection takes): fresh detector state and fresh
            // combs, on the frequency already found (a handover in progress lands on its target).
            // Both dual-mono instances hear the same input, so they agree on it.
            {
                const float hz = p->mainsDet.hz; const int sw = p->mainsDet.switches;
                p->mainsDet = MainsDetect{};     p->mainsDet.prepare(p->rate);
                p->mainsDet.hz = hz; p->mainsDet.switches = sw;   // a later change still needs the higher bar
                p->mainsCur = hz;
            }
            p->mainsNext = HumNotchComb{};   p->mainsWarm = p->mainsFade = 0;
            p->inGate.setParameter("mainsHz", p->mainsCur);
            p->inComb[0] = HumNotchComb{};   p->inComb[0].prepare(p->rate, p->mainsCur);
            p->inComb[1] = HumNotchComb{};   p->inComb[1].prepare(p->rate, p->mainsCur);
            if (p->nam) p->nam->reset(p->rate, kMaxBlock);
            p->lastTube = -1;   // re-apply the tube type below
            p->ui.invalidate();  // re-report every control's state after the reset
            p->rowSeen = -1;     // a preset recall: its Quality is taken as saved
        }
    } else p->resetLatch = false;
    {   // anagramQualityFollowsRow: picking a stdQ row sets Quality to Standard, leaving one for a
        // plain row sets Eco (the default) back. The host applies it and reports it (the control
        // moves and is saved); the player can still change it. A row seen at startup or after a
        // Reset is taken as it is.
        const int row = clampIdx(*p->ctrl[P_MODEL], 0, kAnaN - 1);
        if (p->rowSeen < 0) p->rowSeen = row;
        else if (row != p->rowSeen) {
            const bool was = kAnaModels[p->rowSeen].stdQ, now = kAnaModels[row].stdQ;
            p->rowSeen = row;
            if (now != was && p->chg && p->ctrl[P_ECO])
                p->chg->request_change(p->chg->handle, P_ECO, now ? 0.0f : 1.0f);
        }
    }
#endif

    // Open the notify sequence + handle incoming NAM file set/get.
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
                const LV2_Atom *prop = nullptr, *val = nullptr;
                lv2_atom_object_get(obj, u.patch_property, &prop, u.patch_value, &val, 0);
                if (prop && prop->type == u.atom_URID &&
                    reinterpret_cast<const LV2_Atom_URID*>(prop)->body == u.nam_file &&
                    val && val->type == u.atom_Path) {
                    const char* path = static_cast<const char*>(LV2_ATOM_BODY_CONST(val));
                    std::strncpy(p->namPath, path, kPathMax - 1); p->namPath[kPathMax - 1] = '\0';
                    WorkMsg msg; msg.type = WORK_NAM_LOAD;
                    std::strncpy(msg.path, p->namPath, kPathMax - 1); msg.path[kPathMax - 1] = '\0';
                    p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
                }
            } else if (obj->body.otype == u.patch_Get && haveNotify) {
                writeNamToNotify(p);
            }
        }
    }

#if defined(HEXCHAIN_ANAGRAM)
    // The device's list (kAnaModels): no Neural slot (no NAM on this target), no Diamond Plate.
    const AnaModel& anaSel = anaModel(*p->ctrl[P_MODEL]);
    const int  modelIdx    = anaSel.model;
#elif defined(HEXCHAIN_ANAGRAM_NONAM)
    // NAM is not built into this target (the device has its own neural loader): the Neural
    // slot is absent from the TTL, and a stray 5 runs the default amp instead of silence.
    const int  modelRaw    = clampIdx(*p->ctrl[P_MODEL], 0, kMaxModel);
    const int  modelIdx    = (modelRaw == kNamIdx) ? 1 : modelRaw;
#else
    const int  modelIdx   = clampIdx(*p->ctrl[P_MODEL], 0, kMaxModel);
#endif
    const bool isNam       = (modelIdx == kNamIdx);
#ifdef HEXCHAIN_ANAGRAM
    anagramUpdateUi(p, modelIdx, anaSel);
#endif
    // Bypassed when EITHER this plugin's own Bypass port is on OR the host's
    // designated lv2:enabled port is off.  Mind the inverted sense of enabled.
    const bool fullBypass  = (*p->ctrl[P_BYPASS] > 0.5f) ||
                             (p->ctrl[P_ENABLED] && *p->ctrl[P_ENABLED] <= 0.5f);
    float* inL  = p->ctrl[P_IN_L];  float* inR  = p->ctrl[P_IN_R];
    float* outL = p->ctrl[P_OUT_L]; float* outR = p->ctrl[P_OUT_R];

#ifdef HEXCHAIN_ANAGRAM
    // Mains detection on the raw host input, every block (bypassed or not), before anything
    // below can overwrite an in-place buffer.
    p->mainsDet.feed(inL, inR, n);
    // Bypass ramps (kFadeN) instead of cutting: the amp keeps running until it is faded out.
    const bool bypassCut = fullBypass && (p->wet <= 0.0f || n > kFbMaxN);
    if (bypassCut) p->wet = 0.0f;
    else if (!fullBypass && n > kFbMaxN) p->wet = 1.0f;
    anagramSchedule(p, modelIdx, bypassCut);
    if (bypassCut || p->resetSwapWait || !p->runValid) {
        if (!bypassCut) {   // no amp yet (startup, Reset): silence -- the amp fades in from it, the DI
            std::memset(outL, 0, sizeof(float) * n);   // must not reach the cab and then be cut off
            if (outR != outL) std::memset(outR, 0, sizeof(float) * n);
            if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
            p->fbWas = false;
            return;
        }
#else
    if (fullBypass) {
#endif
        if (outL != inL) std::memcpy(outL, inL, sizeof(float) * n);
        if (outR != inR) std::memcpy(outR, inR, sizeof(float) * n);
        if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
        p->fbWas = false;
        return;
    }

#ifdef HEXCHAIN_ANAGRAM
    const bool wetRamp = fullBypass || p->wet < 1.0f;   // (n <= kFbMaxN here)
    if (wetRamp) {   // the host input, before anything below can overwrite it (in-place buffers)
        std::memcpy(p->dryL.data(), inL, sizeof(float) * n);
        std::memcpy(p->dryR.data(), inR, sizeof(float) * n);
    }
#endif

    // ── Stage Feedback (as Hex Forge): the raw input + the string's forced vibration, built in a
    // plugin-owned buffer that everything below reads. The speaker side is fed once the block is out
    // (feedSpeaker), so the loop latency is the acoustic delay + one period.
    {
#ifdef HEXCHAIN_ANAGRAM
        const float amt = 0.0f;   // Stage Feedback is not on the device (2026-11-10): its ports are hidden
#else
        const float amt = p->ctrl[P_STAGE_FB] ? *p->ctrl[P_STAGE_FB] : 0.0f;
#endif
        if (amt > 0.0005f && n <= kFbMaxN) {
            if (!p->fbWas) { p->fb.reset(); p->fbWas = true; }
            p->fb.setAmount(amt);
            p->fb.setDistance(p->ctrl[P_STAGE_FBDIST] ? *p->ctrl[P_STAGE_FBDIST] : 1.0f);
            p->fb.setSustain(p->ctrl[P_STAGE_FBSUS] ? *p->ctrl[P_STAGE_FBSUS] : 0.5f);
            for (uint32_t i = 0; i < n; ++i) {
                const float x = 0.5f * (inL[i] + inR[i]);
                const float s = p->fb.process(x) - x;
                p->fbInL[i] = inL[i] + s; p->fbInR[i] = inR[i] + s;
            }
            inL = p->fbInL.data(); inR = p->fbInR.data();
        } else {
            p->fbWas = false;
        }
    }
    auto feedSpeaker = [p, outL, outR, n]() {
        if (p->fbWas) for (uint32_t i = 0; i < n; ++i) p->fb.feedSample(0.5f * (outL[i] + outR[i]) * kFbSpkTrim);
    };

    if (isNam) {
        // ── Neural (NAM) path ── mono capture; bypass the algo amp + power amp.
        // NAM Gain drives the model harder (input trim); NAM Level trims the output. Both dB.
        const float inGain  = std::pow(10.0f, *p->ctrl[P_NAM_GAIN] / 20.0f);
        const float outGain = std::pow(10.0f, *p->ctrl[P_NAM_VOL]  / 20.0f);
        if (p->nam && p->nam->isLoaded()) {
            for (uint32_t off = 0; off < n; off += kMaxBlock) {
                const int len = static_cast<int>((n - off > (uint32_t)kMaxBlock) ? kMaxBlock : (n - off));
                for (int i = 0; i < len; ++i) p->mono[i] = inGain * 0.5f * (inL[off + i] + inR[off + i]);
                p->nam->processBuffer(p->mono, p->monoOut, len);
                for (int i = 0; i < len; ++i) { const float y = p->monoOut[i] * outGain; outL[off + i] = y; outR[off + i] = y; }
            }
        } else {  // no model loaded yet → pass through
            if (outL != inL) std::memcpy(outL, inL, sizeof(float) * n);
            if (outR != inR) std::memcpy(outR, inR, sizeof(float) * n);
        }
        feedSpeaker();
        if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
        return;
    }

    // ── Algorithmic amp path ──
    // v = the port values the RUNNING amp is driven with, m = its model. On the Anagram that is
    // the amp that was built (runKey): its structural switches come from the build, never from
    // ports that a pending build is still being made for.
    float v[P_N_PORTS];
#ifdef HEXCHAIN_ANAGRAM
    snapshotPorts(p, v, p->runRow);   // the running amp's own row: nothing of the next one leaks in
    const int m = p->runKey.model;
    {
        v[P_MODEL] = static_cast<float>(m);
        int sp[3]; const int ns = structPorts(m, p->runKey.comp, sp);
        for (int i = 0; i < ns; ++i) v[sp[i]] = p->runV[sp[i]];
    }
    const bool compOn = p->runKey.comp;
#else
    snapshotPorts(p, v);
    const int m = modelIdx;
    const bool compOn = hasComponentModel(m) && v[P_COMP] > 0.5f
                        && twinAllowed(m, p->abHt.load(std::memory_order_relaxed));
    const bool ecoOn  = v[P_ECO] > 0.5f;
#endif
    AmpBlockExtended* amp = p->amp;
    amp->setBypass(false);
#ifdef HEXCHAIN_ANAGRAM
    // Mains: the detector's verdict, not the (hidden, forced) port. A change starts the comb
    // handover (warm-up, crossfade: see the mono loop); the gate's detector comb follows when
    // it completes.
    if (p->mainsDet.hz != p->mainsCur && p->mainsWarm == 0 && p->mainsFade == 0) {
        p->mainsNext = HumNotchComb{};
        p->mainsNext.prepare(p->rate, p->mainsDet.hz);
        p->mainsWarm = static_cast<int>(kMainsWarmSec * p->rate);
    }
#else
    const float mainsHz = (p->ctrl[P_MAINS] && v[P_MAINS] > 0.5f) ? 50.0f : 60.0f;
    if (mainsHz != p->mainsCur) {   // 50/60 Hz mains: re-tune the input hum comb + the gate's detector comb
        p->mainsCur = mainsHz;
        p->inComb[0].prepare(p->rate, mainsHz);
        p->inComb[1].prepare(p->rate, mainsHz);
        p->inGate.setParameter("mainsHz", mainsHz);
    }
#endif
    const bool valves = hasValveSwap(m, compOn);
#ifdef HEXCHAIN_ANAGRAM
    const int  abWant = p->runKey.ab;
#else
    const int  v1Want = (valves && p->ctrl[P_V1TUBE]) ? clampIdx(v[P_V1TUBE], 0, kV1TubeMax) : 0;
    const int  paWant = valves ? paValveFor(m, p->ctrl[P_PATUBE] ? clampIdx(v[P_PATUBE], 0, kPaTubeMax) : 0) : 0;
    const int  abWant = p->abHt.load(std::memory_order_relaxed);
    if (m != p->lastModel || compOn != p->lastComp || ecoOn != p->lastEco
        || v1Want != p->lastV1 || paWant != p->lastPa || abWant != p->lastAb) {
        if (scheduleRebuild(p, m) == LV2_WORKER_SUCCESS) p->lastModel = m;
    }

    // TEMPORARY physics A/B (2026-11-04, remove once decided): ask the worker twice a second.
    p->abPollCtr += n;
    if (p->abPollCtr >= static_cast<uint32_t>(p->rate * 0.5)) {
        p->abPollCtr = 0;
        WorkMsg am; am.type = WORK_AB_POLL;
        p->schedule->schedule_work(p->schedule->handle, sizeof(am), &am);
    }
#endif

    {
        AmpBlockExtended* a = amp;
#ifdef HEXCHAIN_ANAGRAM
        int k = 0;   // the push's slot: the sequence is fixed per model (the worker seeded it at the build)
        ampParams(v, m, valves, abWant, [p, a, &k](const char* id, float x) {
            if (k < kAmpSlots) {
                if (p->ampLastGen[k] == p->ampGen && p->ampLast[k] == x) { ++k; return; }
                p->ampLast[k] = x; p->ampLastGen[k] = p->ampGen;
            }
            ++k; a->setParameter(id, x);
        });
#else
        ampParams(v, m, valves, abWant, [a](const char* id, float x) { a->setParameter(id, x); });
#endif
    }
    // Recto Modern modes (4, 7) disconnect the power-amp NFB loop on the real amp.
    const bool rectoModern = (m == kRectoIdx) && (v[P_RC_MODE] > 3.5f) &&
                             (v[P_RC_MODE] < 4.5f || v[P_RC_MODE] > 6.5f);

    int desiredTube;
    if (v[P_PA_AUTO] > 0.5f) {
        const auto d = PowerAmpProcessor::getDefaultsForModel(kCanonical[m]);
        AMP_PUSH(p->pa, p->paGen, "master",   d.master);
        AMP_PUSH(p->pa, p->paGen, "presence", d.presence);
        AMP_PUSH(p->pa, p->paGen, "depth",    d.depth);
        AMP_PUSH(p->pa, p->paGen, "nfb",      rectoModern ? 0.05f : d.nfb);
        AMP_PUSH(p->pa, p->paGen, "sag",      d.sag);
        AMP_PUSH(p->pa, p->paGen, "tracklo",  d.trackLo); AMP_PUSH(p->pa, p->paGen, "trackhi", d.trackHi);   // post-PA gain-level tracking (the reference rig)
        AMP_PUSH(p->pa, p->paGen, "resonance", v[P_PA_RESON]);
    AMP_PUSH(p->pa, p->paGen, "coupling", v[P_PAMP_COUPL]);   // speaker-impedance coupling (v24 fidelity)
        AMP_PUSH(p->pa, p->paGen, "airFeel",   v[P_PA_AIR]);
        desiredTube = kModelTube[m];
    } else {
        AMP_PUSH(p->pa, p->paGen, "presence",  v[P_PA_PRES]);
        AMP_PUSH(p->pa, p->paGen, "depth",     v[P_PA_DEPTH]);
        AMP_PUSH(p->pa, p->paGen, "sag",       v[P_PA_SAG]);
        AMP_PUSH(p->pa, p->paGen, "master",    v[P_PA_MASTER]);
        AMP_PUSH(p->pa, p->paGen, "nfb",       v[P_PA_NFB]);
        AMP_PUSH(p->pa, p->paGen, "resonance", v[P_PA_RESON]);
        AMP_PUSH(p->pa, p->paGen, "airFeel",   v[P_PA_AIR]);
        desiredTube = clampIdx(v[P_PA_TUBE], 0, 5);   // 5 = 6550 (2026-08-28 Blue Liner)
    }
    // Post-saturation sag-VCA depth is a per-amp voicing value with no user port.
    const auto paDef = PowerAmpProcessor::getDefaultsForModel(kCanonical[m]);   // once per block
    AMP_PUSH(p->pa, p->paGen, "bloomvca", paDef.bloomVca);
    AMP_PUSH(p->pa, p->paGen, "trackgain", v[P_GAIN]);   // post-PA gain-level tracking follows the amp gain knob
    AMP_PUSH(p->pa, p->paGen, "duty",     paDef.duty);
    AMP_PUSH(p->pa, p->paGen, "evengen",  paDef.evenDepth);
    AMP_PUSH(p->pa, p->paGen, "ripplesag",paDef.rippleSagCoupling);
    AMP_PUSH(p->pa, p->paGen, "ltptail",  paDef.ltpTail);
    AMP_PUSH(p->pa, p->paGen, "fluxOT",   paDef.fluxOT ? 1.0f : 0.0f);
    // Per-amp PA drive/makeup (2026-07-31 fix): mirror the hexforge main-rig fix --
    // these were never applied on the standalone amp, so EVH/Rockerverb/Vox ran
    // over-driven. 1.0/1.0 for every other model = bit-identical.
    AMP_PUSH(p->pa, p->paGen, "padrive",  paDef.paDrive);
    AMP_PUSH(p->pa, p->paGen, "pamakeup", paDef.paMakeup);
    // HG round 2 per-amp bakes (2026-08-20): knee/tilt from AmpDefaults (tilt
    // setters early-out on unchanged values, so per-run pushes are cheap).
    AMP_PUSH(p->pa, p->paGen, "pakneecurve",  paDef.kneeCurve);
    AMP_PUSH(p->pa, p->paGen, "shapertilt",   paDef.shaperTiltDb);
    AMP_PUSH(p->pa, p->paGen, "shapertiltlo", paDef.shaperTiltLoDb);
    if (desiredTube != p->lastTube) { p->lastTube = desiredTube; p->pa.setTubeType(static_cast<TubeType>(desiredTube)); }

    const bool paBypass = (v[P_PA_BYPASS] > 0.5f) || (m == kSunnIdx)
                       || (m == kAd200Idx)   // Citrus 200 carries its own power section
                       || compOn;                   // every component twin carries its own
    p->pa.setBypass(paBypass);

    // Input hum comb + noise gate — BEFORE the amp's huge gain (the only place signal and its amplified
    // noise floor are still separable). "Driven" = gain past halfway on any model, OR a Cali V high-gain
    // lead mode (6-8) at ANY gain — those modes carry ~70 dB whatever the knob says, and the old gain>0.5
    // test left the gate wide open on them. Measured against the user's rig (2026-07-14): idle floor peaks
    // -45 dBFS hands-off (89% mains hum) / -58 hands-on. The comb takes ~15 dB of hum off, putting the
    // hands-off floor near -57; the gate at -50 (close -54) then shuts in BOTH states, while strums
    // (-20..-6 dBFS) open it instantly and ring-out tails hold it open down to -54. -90 = effectively open.
    const float driveKnob = (m == kSunnIdx) ? v[P_SUNN_V2] : v[P_GAIN];
    const bool  mesaLead  = (m == kMesaIdx) && (v[P_MV_MODE] >= 5.5f);
    // Measured against the rig's idle floor (2026-07-14): EVH's RED channel pins it to -18 dBFS out
    // even at gain 0.35 (-5 at 0.7 — the suite's worst), and the JCM800 is already at -25 by gain
    // 0.35 — both leak badly below the generic gain>0.5 test, so they get their own conditions.
    const bool  evhRed    = (m == 2) && (v[P_CHANNEL] >= 0.5f);
    const bool  jcmDriven = (m == 1) && (driveKnob > 0.3f);
    // Recto: CH2/CH3 Vintage+Modern (modes 3,4,6,7) carry high gain at any knob setting.
    const bool  rectoHot  = (m == kRectoIdx) && (v[P_RC_MODE] >= 2.5f) &&
                            !(v[P_RC_MODE] >= 4.5f && v[P_RC_MODE] < 5.5f);
    // Tremont 15 Lead carries high gain at any knob setting.
    const bool  mt15Hot   = (m == kMt15Idx) && (v[P_MT_MODE] >= 1.5f);
    const bool  driven    = (driveKnob > 0.5f) || mesaLead || evhRed || jcmDriven || rectoHot || mt15Hot;
    p->inGate.setBypass(false);
    AMP_PUSH(p->inGate, p->gateGen, "threshold", driven ? -50.0f : -90.0f);
    {   // Tight: high-gain amps only; 0 (and every other model) = hard bypass
        const float t = (hasTight(m) && p->ctrl[P_TIGHT]) ? v[P_TIGHT] : 0.0f;
        p->tight[0].setAmount(t); p->tight[1].setAmount(t);
    }
    const bool tightOn = p->tight[0].active();

#ifdef HEXCHAIN_ANAGRAM
    // One amp, one guitar (Anagram): the stereo pair is summed and the whole model runs ONCE,
    // then fans out to both outputs. A guitar on a stereo chain arrives as L == R, and then
    // 0.5 * (L + R) == L exactly and the result is bit-identical to running both channels
    // (they carry no per-channel variation: valve spread is between the push-pull halves).
    // Halves the cost; on the device's 16-frame period the stereo amp did not fit.
    for (uint32_t off = 0; off < n; off += kMaxBlock) {
        const int len = static_cast<int>((n - off > (uint32_t)kMaxBlock) ? kMaxBlock : (n - off));
        float* g = p->gbufL;
        {
            const float* l = inL + off;
            const float* r = inR + off;
            HumNotchComb& comb = p->inComb[0];
            if (p->mainsWarm == 0 && p->mainsFade == 0) {
                for (int i = 0; i < len; ++i) {
                    const float x = 0.5f * (l[i] + r[i]);
                    const float hc = comb.process(x);
                    g[i] = driven ? hc : x;
                }
            } else {   // mains handover: both combs run; warm-up, then a linear crossfade
                const int xfN = static_cast<int>(kMainsFadeSec * p->rate);
                bool done = false;
                for (int i = 0; i < len; ++i) {
                    const float x  = 0.5f * (l[i] + r[i]);
                    const float hc = comb.process(x);
                    const float hn = p->mainsNext.process(x);
                    float y = hc;
                    if (done) y = hn;
                    else if (p->mainsWarm > 0) { if (--p->mainsWarm == 0) p->mainsFade = xfN; }
                    else {
                        const float t = 1.0f - static_cast<float>(p->mainsFade) / static_cast<float>(xfN);
                        y = hc + t * (hn - hc);
                        if (--p->mainsFade == 0) done = true;
                    }
                    g[i] = driven ? y : x;
                }
                if (done) {
                    comb = p->mainsNext;   // (trivially copyable: coefficients + state)
                    p->mainsCur = static_cast<float>(p->mainsNext.mainsHz);
                    p->inGate.setParameter("mainsHz", p->mainsCur);
                }
            }
        }
        float* gbuf[1] = { g };
        p->inGate.process(gbuf, gbuf, len, 1);
        float* outs[1] = { outL + off };
        if (tightOn) { p->tight[0].update(len); p->tight[0].pre(g, len); }
        amp->process(gbuf, outs, len, 1);
        p->pa.process(outs, outs, len, 1);
        if (tightOn) p->tight[0].post(outs[0], len);
        amp->setExternalSag(p->pa.getSagEnvNorm());
        std::memcpy(outR + off, outL + off, sizeof(float) * (size_t) len);
    }
#else
    for (uint32_t off = 0; off < n; off += kMaxBlock) {
        const int len = static_cast<int>((n - off > (uint32_t)kMaxBlock) ? kMaxBlock : (n - off));
        float* ins[2]  = { inL  + off, inR  + off };
        float* gbuf[2] = { p->gbufL, p->gbufR };
        // Hum comb into the scratch (always fed so its state stays warm -> no click on engage);
        // the raw input passes through when not driven.
        for (int c = 0; c < 2; ++c) {
            const float* s = ins[c];
            float*       d = gbuf[c];
            HumNotchComb& comb = p->inComb[c];
            for (int i = 0; i < len; ++i) { const float hc = comb.process(s[i]); d[i] = driven ? hc : s[i]; }
        }
        p->inGate.process(gbuf, gbuf, len, 2);  // gate the (de-hummed) input in place, then amplify
        float* outs[2] = { outL + off, outR + off };
        if (tightOn) for (int c = 0; c < 2; ++c) { p->tight[c].update(len); p->tight[c].pre(gbuf[c], len); }
        amp->process(gbuf, outs, len, 2);
        p->pa.process(outs, outs, len, 2);
        if (tightOn) for (int c = 0; c < 2; ++c) p->tight[c].post(outs[c], len);
        // Voicing constants below are fitted values; the derivation is not public.
        amp->setExternalSag(p->pa.getSagEnvNorm());
    }
#endif
    // the selector the parity row is indexed by: channel for EVH/Rockerverb, BE channel
    // for Beardo, the mode rotary for Cali V / Diamond Plate, nothing for the rest
    const float compSel = (m == 2 || m == 4) ? v[P_CHANNEL]
                        : (m == kFriedmanIdx)       ? v[P_FR_CHANNEL]
                        : (m == kMesaIdx)           ? v[P_MV_MODE]
                        : (m == kRectoIdx)          ? v[P_RC_MODE]
                        : (m == kMt15Idx)           ? v[P_MT_MODE] : 0.0f;
    const float mk = kModelMakeup[m]
                   * (compOn ? compParity(m, v[P_GAIN], v[P_MASTER], compSel) : 1.0f);
    if (mk != 1.0f) for (uint32_t i = 0; i < n; ++i) { outL[i] *= mk; outR[i] *= mk; }
#ifdef HEXCHAIN_ANAGRAM
    if (p->fadeDir != 0) {   // amp swap: out of the old amp, (swap at silence,) into the new one
        const float step = static_cast<float>(p->fadeDir) / kFadeN;
        float f = p->fade;
        for (uint32_t i = 0; i < n; ++i) {
            f = std::min(1.0f, std::max(0.0f, f + step));
            const float g = fadeShape(f);
            outL[i] *= g; outR[i] *= g;
        }
        p->fade = f;
        if (p->fadeDir > 0 && f >= 1.0f) p->fadeDir = 0;
    }
    if (wetRamp) {           // bypass: amp <-> dry input
        const float step = (fullBypass ? -1.0f : 1.0f) / kFadeN;
        float w = p->wet;
        const float* dl = p->dryL.data(); const float* dr = p->dryR.data();
        for (uint32_t i = 0; i < n; ++i) {
            w = std::min(1.0f, std::max(0.0f, w + step));
            const float g = fadeShape(w);
            outL[i] = dl[i] + g * (outL[i] - dl[i]);
            outR[i] = dr[i] + g * (outR[i] - dr[i]);
        }
        p->wet = w;
    }
#endif
    feedSpeaker();

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

static void amp_cleanup(LV2_Handle h) {
    auto* p = static_cast<AmpPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    if (p->havePend) delete p->pendR.amp;   // a finished build still waiting for its fade-out
#endif
    delete p->amp;
    delete p->nam;
    delete p;
}

// ── State (persist the loaded NAM path) ───────────────────────────────────────
static LV2_State_Status amp_save(LV2_Handle h, LV2_State_Store_Function store,
                                 LV2_State_Handle handle, uint32_t flags,
                                 const LV2_Feature* const* features) {
    auto* p = static_cast<AmpPlugin*>(h);
    if (p->namPath[0] == '\0') return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    char* ap = mapPath ? mapPath->abstract_path(mapPath->handle, p->namPath) : p->namPath;
    store(handle, p->uris.nam_file, ap, std::strlen(ap) + 1, p->uris.atom_Path,
          flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    if (mapPath && ap != p->namPath) free(ap);
    return LV2_STATE_SUCCESS;
}
static LV2_State_Status amp_restore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                                    LV2_State_Handle handle, uint32_t,
                                    const LV2_Feature* const* features) {
    auto* p = static_cast<AmpPlugin*>(h);
    size_t size = 0; uint32_t type = 0, vflags = 0;
    const void* val = retrieve(handle, p->uris.nam_file, &size, &type, &vflags);
    if (!val || type != p->uris.atom_Path) return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    const char* ap = static_cast<const char*>(val);
    char* path = mapPath ? mapPath->absolute_path(mapPath->handle, ap) : const_cast<char*>(ap);
    auto* nm = new(std::nothrow) NamModel;   // restore() is not RT — load directly
    if (nm && nm->loadFromFile(path)) {
        nm->reset(p->rate, kMaxBlock);
        delete p->nam; p->nam = nm;
        std::strncpy(p->namPath, path, kPathMax - 1); p->namPath[kPathMax - 1] = '\0';
    } else delete nm;
    if (mapPath && path != ap) free(path);
    return LV2_STATE_SUCCESS;
}

static const void* amp_extension_data(const char* uri) {
    static const LV2_Worker_Interface worker = { amp_work, amp_work_response, nullptr };
    static const LV2_State_Interface  state  = { amp_save, amp_restore };
    if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
    if (!std::strcmp(uri, LV2_STATE__interface))  return &state;
    return nullptr;
}

LV2_EXPORT_DESCRIPTOR(AMP_URI,
    amp_instantiate, amp_connect_port,
    nullptr, amp_run, nullptr, amp_cleanup, amp_extension_data)
