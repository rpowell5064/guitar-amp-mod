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
#include <ctime>
#include "hexforge_ports.h"
#include "hexforge_factory_presets.h"   // band/song factory presets (Banks 2..6), generated

#include "BiquadFilter.h"
#include "PickupVoicer.h"
#include "HumNotchComb.h"
#include "CalMeasure.h"
#include "EvhCaptureFit.h"
#include "RectoCaptureFit.h"
#include "PickupLoadSim.h"
#include "IrResample.h"
#include "AdaaSoftClip.h"
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
#define HEXFORGE_AMP2NAM HEXFORGE_URI "#amp2nam"
#define HEXFORGE_IR2_URI HEXFORGE_URI "#ir2file"
#define HEXFORGE_DR2NAM  HEXFORGE_URI "#dr2nam"
static constexpr int kAmpNamIdx = 5;   // amp model slot = Neural (NAM)
static constexpr int kDrNamIdx  = 3;   // drive model slot = Neural (NAM)
static constexpr int kDrDs1Idx  = 4;   // drive model slot = Grunge DS (DS-1)
static constexpr int kDrKlonIdx = 5;   // drive model slot = Gilded Horse (Klon)
static constexpr int kDrSd1Idx  = 6;   // drive model slot = Super Nova (Boss SD-1)
static constexpr int kDrMax     = 8;   // highest dr_model index (8 = Echo Primer / EP-3)

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
static constexpr uint32_t kFactoryRev = 113;  // 113: + parity restore for the blend drop. 112: Periphery rb_blend -> 0.10 (pump scales with blend; 0.10 = measured flutter floor). 111: + measured parity restore (+1.2/+1.6 out_level). 110: Periphery rb_locut 150 Hz (kills the dual-rig low-mid cancellation pump; measured via woosh2). 109: Green Man gain-floor fix re-level (real x11.85 TS feedback floor at drive 0 = the boost trick works; 22 presets carry the TS, 9 measured >=0.5 dB re-leveled to parity -- the hotter pushed-amp tone is the intended change). 108: Solar Monolith = user's on-device dial-in baked VERBATIM (less drive/more octave, brighter, parallel link, Vol II up). 107: Wizard's Doom -> "Solar Monolith" Sunn O))) drone (New Dawn/Life Pedal + cranked Doom Daddy/Sunn, dark monolith, Hex Ambient wash); Regal Sustain/Solo (Brian May/Vox) tuned for the Vox re-voice (top tamed, violin mids, Solo delay -> EP-3). 106: Periphery hiss tame (Flatliner/Prayer Djent -- treble/presence/TS + cab highcut 7.8k->6.3k, kill the amplified Red HF hiss); Berlin Wall Pulse (b4/1) drive New Dawn->Dear Rodent Boy (RAT, Gilmour), baked verbatim to survive the reseed. 105: Brown Sound '84 = user's overvolt-variac dial-in baked VERBATIM from store (gain .655/EQ off/EP-3 clean); Periphery/Gojira EVH presets reworked to songs (Flatliner=Flatline, Prayer Djent=Prayer Position, Castaway Groove=Silvera) on the new EVH. (104 was a briefly-live rev, superseded.) 103: USER bank-0 reworks baked (Clean gain up; Crunch SIR #34 ON; Rhythm full rebuild around the corrected EVH; Lead phase normal) -- store-diffed, DO NOT RETUNE. 102: padrive/pamakeup now applied on the MAIN rig (+ standalone amp), was Rig-B-only -- EVH/Rockerverb/Vox were 2-3x over-driven on the main rig; 18 affected presets re-leveled to loudness parity (measured before/after). 101: Brown Sound '84 mod Phaser -> Script Phaser (user swap, md type 8). 100: REVERTED the Fender PA compression re-voice (rev 99) -- user: bitcrush-like distortion on Fender, click on Chime Thirty when pushed, nothing felt right. Bumped past 99 to force the re-seed that restores the original Fender out_levels (the store was already reseeded to 99). Everything back to the rev-98 state. 98: Jungle Sleaze = USER's SIR #34 build (mod ON, re-dialed around the bite). DO NOT RETUNE. 97: USER round 2 baked verbatim (Brown Sound '84 -> REAL EP-3 chain: Echo Primer + EP-3 Echo slap + second variac'd Plexi B rig; Jungle Sleaze hotter/brighter + Cab 2 prep). DO NOT RETUNE. 96: Plexi Variac v2 physics audit (s=V/120 exact-transform knees, gm~s^(1/3), swing~s, sag~s^-1.5 + 15ms fast RC) -- Brown Sound '84 re-trimmed -22.5 (net -0.2 dB shift). 95: bank 18 loudness parity (user request) -- out_levels re-trimmed to the -16.8 Crunch anchor after the rev-94 user finals (-17.3/-22.0/-23.0/-20.6). 94: bank 18 B/C/D = USER's on-device finals baked verbatim (Brown Sound re-staged: IT boost +7.05 into eased EP-3, gain .73 / treble+master dimed, Vol II out; Acca Dacca -> 'Sister's Singer', everything on 10, gate off; Jungle intro hotter w/ dimed Green Man). DO NOT RETUNE. 93: Brown Sound '84 round 3 -- EP-3 Echoplex-preamp stand-in (Preamp 250, drive .25 / level .78 hot into V1) fixes 'plexi short on gain for 1984'; out re-trimmed -22.7 (clean-HOME measured to the -16.8 anchor). 92: Frayed Justice = USER's on-device rework baked verbatim (amp-first chain, treble .17/presence up, Mid-Boost EQ post-cab, spkdrive Full, Cali V mode-7 B rig + Greenback Cab 2, DO NOT RETUNE); Jungle Sleaze -> the WTTJ DELAY INTRO (gain .45, 470ms digital, fb .55, mix .42); Brown Sound '84 pass 2 (gain/VolII/sag up, treble<->presence inverted, mids .68, phaser 100%-wet .45, echo eased). 91: NEW BANK 18 (idx 17) CLASSIC ROCK/METAL (user): Frayed Justice (AJFA Cali V deep-V, dry) / Brown Sound '84 (Plexi VARIAC + pre-amp phaser + tape echo) / Acca Dacca (jumpered non-master Plexi) / Jungle Sleaze (Appetite modded-1959 ~= JCM800). 90: NIN presets = the USER'S on-device final reworks baked verbatim (March direct/no-cab + octave + inverted Fender rig; Broken -> Recto + tape slap + Cali V rig + MID-CHAIN Gate 2; Con Molars starved Fuzz Zachary front end). DO NOT RETUNE. 89: NIN EQ retune round 4 (user) -- March scoop eased (+1 net), Broken hiss-band boost removed + Wish mids, Con Molars regains the lost 1.9k mid-bite with the EQ MOVED PRE-AMP (Reaktor-then-reamp order). 88: NIN round 3 (user) -- March Stabs +2.5 dB more, Broken Crush white-noise tamed at the crusher (texture .30, tone .38, highcut 7500). 87: NIN ear-pass round 2 (user): March Stabs hotter+louder (+~2.7 dB total), Broken Crush de-noised (gate -40 at the rig floor + DS stage trim), Con Molars low end + gain; WWA locked user-approved. 86: NIN topology upgrades (user) -- March Stabs Nail moves POST-amp (JMP-1 -> Zoom -> cab, the documented TDS order), Broken Crush gains the 9030 stacked-distortion stage (Grunge DS -> Nail Broke), Con Molars drops the double cab-sim (real amp does the re-amp), WWA wall deepened; all parity-measured. 85: JCM800 stage-2 cap 2.0->1.4 (h2@223 15->10, whole profile overlays both knob-documented captures; the 2026-07-28 fix shipped 2.0 while its comment said 1.6) + measured per-preset re-level on the 6 affected JCM800 presets. 84: Skye (No Mod) + Cardinal Rhythm/Lead = the USER'S on-device dual-rig dial-ins baked verbatim (store-diffed; Cardinal Rhythm pol back to NORMAL, Skye B = major voice at blend .57; placed AFTER the rev-72 JCM800 loop which had been stacking +0.10 on the rev-83 absolute gain). 83: Skye (No Mod) heavier (A gain .82, Orange B .65 crunchy) + Cardinal B layers crunched (.62/.60) and +1 dB over parity (user: too clean / a little quiet). 82: Cardinal Rhythm/Lead dual rigs (user: "those are ghost too") -- Orange layer, BOTH inverted (Cali V + Friedman-lead phase), parity measured. 81: Skye (No Mod) riff gain 0.62->0.72 (user) + Streets Chime parity trim (+1.45->+2.0; the baked dl2 shifted the joint delta). 80: dual-rig round 2 (user: Ghost + Periphery + Skye (No Mod)) -- Imperial pair (Orange under Friedman; rhythm coherent, lead INVERTED -- the pedal chain flips net phase), djent parallel clean blends, Streets Chime = Edge dual amp + FIRST dl2 presets (Streets quarter-note second delay, Numb Sustain rack delay); all phase+parity measured (rig_meas2, wiggle-safe writes). 79: Gilmour dual-rig pol/parity fix (the rev-78 first-pass measured a wrong B amp via the zero-write port trap). 78: DUAL RIGS (2026-07-30, user): 7 documented multi-amp presets gain a measured Amp 2/Cab 2 layer (Muse x2 clean-Vox-under-fuzz, Gilmour x2 Twin-under-Hiwatt INVERTED polarity, QOTSA Orange layer inverted, NIN Sunn wall, Mastodon Orange) with on-device phase + RMS-parity verification baked into out_level. 77: factory rows now MIGRATED at seed time (2026-07-30): the 224-port v30-era kFactoryExtra literals were memcpy'd raw into the grown layout, so the old command/meter tail landed on every param appended since (quality/dr2/rig-B = garbage; rig B silent everywhere). kFactoryTableVer=30 + migratePorts at seed; reseed flushes the corrupted slots. 76: Vox preset gates raised to -44 (2026-07-29 whine fix: hysteresis close point sat below the real hands-off floor -> latch-open -> constant amplified hum partials; -44 puts close at -48, above the ~-54 post-comb detector floor). 75: Bank 1 Crunch = the user's on-device rework baked verbatim (2026-07-29): the rev-72 +0.10 JCM800 bump still read too tame after the re-voice, so they rebuilt it -- chain Drive->Amp->Cab->Reverb (comp parked), Green Man clean-boost (drive .07, level max), amp gain .61 / bass .2525 / mid .475 / master .6825; store-diffed on-device against a fresh factory seed. 74: Moondust Glam overhauled to Mick Ronson's documented Ziggy rig (2026-07-29, user request): germanium Tone Bender (hot, tamed volume) -> cocked Cry Baby (kept) -> CRANKED jumpered Plexiglass (was a clean-ish Hiwatt platform -- the Major 'Pig' is Marshall lineage) -> Greenbacks; out_level re-measured on device. 73: fz_gvol=1.0 re-seed on every factory preset (2026-07-29 hotfix -- older-layout factory tables zero-filled the new port; 0 = guitar vol rolled off = I Know It / Tone Bender inaudible, user-reported). 72: +0.10 gain on every factory JCM800 preset (2026-07-28, user request after the fuzzy-fix re-voice -- the stage-2 duty-collapse cap tamed its drive contribution; factory-scoped, idempotent). 71: Speaker Drive per-amp defaults (2026-07-28, item #40, user request) -- every factory preset now gets Speaker Drive seeded from its own amp choice: Subtle for Fender/Clean Meanie + Vox/Chime Thirty, Full for JCM800 + the suite's high-gain amps (EVH/Gainzilla, Friedman/Beardo BE, Recto/Diamond Plate, MT15/Tremont 15); every other amp (Sunn, Rockerverb, Hiwatt, Backline, Plexiglass, Cali V, NAM) left at Off -- the user specified only these two categories, nothing guessed for the rest. 70: Cydonia Sunrise -> "Knights of Fuzz" = the USER'S full-arc KOC build baked verbatim (slap INTO near-max Fuzz Zachary + SD-1 -> low-gain JCM800, eased trem, Ambient wash, Lead Cut EQ; out locked -18.6; parked comp stripped per the no-residue rule) (2026-07-25). 69: Cavalier Charge REPLACED by "Cydonia Sunrise" (user 2026-07-25) — the KOC INTRO/VERSE retro sci-fi surf: bright scooped Chime Thirty at the edge, fast deep tremolo, 140 ms slap, small plate. 68: I Saw a Deer — leftover bypassed Gilded Horse cleaned to defaults (user: "there shouldn't be a klon"; block was disabled = zero audio change). 67: Bank 17 parody names (Homesick Saucer / Hand in Cloud) + Cloudforge -> "I Saw a Deer" = the USER'S on-device rework baked verbatim (octave-up->Nail->maxed phaser->hot 76 ms tape slap->max-bloom ambient->Clean Sparkle EQ; out locked -20.94) (2026-07-25). 66: HEX AMBIENT (2026-07-25) — new reverb type 2 (Cloudburst/SLO-family blooming tail, rv_bloom port, blob v27) + Bank 17 AMBIENT: Sweet Dispersion (Temper Trap) / Homesick Alien (Radiohead) / Hand in Mine (EITS) / Cloudforge (showcase, bloom max). 65: Cardinal Lead joins the Lead Cut set (user request — EQ appends end-of-chain, their preserved amp/drive dial-in untouched; re-measured + re-leveled). 64: Lead Cut on every lead preset (2026-07-24, user): end-chain EQ block with the stock Lead Cut curve (-1/0/+1/+3/+4/+5) baked into the sliders + eq_preset=5 retained selection, on Regal Sustain/Sermon Solo/Imperial Lead/Numb Sustain/Gravity Lead/Hazy Solo/Skye Soar/Regal Solo/Quarter-Tone Lead/Spectrum Lead (Cardinal Lead user-preserved + Bank-1 Lead excluded); all 10 re-measured + parity re-leveled. 63: EQ-block polish pass (2026-07-24): Sweet Soy Stabs de-boomed/de-muffled (bass .45/treble .66/pres .58/highcut 9200 + end-chain EQ), end-chain EQ curves on all 5 nu metal presets + Flatliner/Prayer Djent (documented per-preset intents, boosts capped +2 dB), One Step Deeper -1.5 dB perceptual trim; all 7 re-measured + parity re-leveled. 62: NU METAL — first Diamond Plate presets (2026-07-24, drop-D voiced): 14/D Duality Crush (Slipknot Iowa, CH3 Modern+silicon+TS clamp) + Bank 16 Blind Squall (Korn, CH2 Vintage+tube sag)/Quiet Drive (Deftones shimmer-clean)/One Step Deeper (LP tight CH3 Modern)/Sweet Soy Stabs (SOAD mid-forward staccato); seed-clear list shrunk (14/D + bank 15 now seeded). 61: Surf Splash retuned to the user's surf recipe — mid SCOOP (.55->.35, the honk), bass/treble/presence into the Fender-surf bands, reverb mix .62->.5, predelay 4->15 ms (clean staccato attack ahead of the drip) (2026-07-24). 60: Surf Splash rebuilt FROM SCRATCH (user dropped the edge-of-breakup idea: pure clean high-headroom Showman + fast comp glue + cranked spring tank) + March Stabs/Con Molars -2.5 dB perceptual trim (EQ presence push reads louder at RMS parity) (2026-07-24). 59: Surf Splash REDO (edge-of-breakup: Green Man push + Clean Meanie gain up, @american-ob, spring tank kept wet) + the 4 NIN presets gain the user's end-of-chain 6-band EQ curves (eq block pos 13; 6.4k intent folded into cab highcut) (2026-07-24). 58: Dense cab room on every factory preset + Spring reverb where the rig calls for it (Surf Splash / Apache Echo — the genuinely surf/early-60s presets); residual loudness shifts absorbed via ROOMDENSE_MEAS_DELTA. 57: Dense-tank parity re-level (24 presets measured; Surf Splash -4.0 dB the extreme; bank-1 inline outs adjusted). 56: Dense reverb tank on every factory preset (user A/B'd: 'it sounds great'). 55: peak-capped presets re-leveled too (Come As Water / Innerspeaker were pinned by the peak cap). 54: polish loudness parity — 15 coupling-lifted presets re-leveled from on-device measurement (+0.3..+1.0 dB absorbed into out_level). 53: fidelity polish — per-rig Speaker Coupling + Pickup Load across all factory presets (user-preserved slots untouched). 52: Retro Poland = the USER'S on-device dial-in baked verbatim (Preamp 250 drive, reordered chain, bypassed tape slap) — user-preserved, do not retune. 51: renamed to Retro Poland + a touch more queasy (depth .75, mix .68). 50: Maromaro Retro — Klon (Gilded Horse) push + audible seasick (rate ~0.5 Hz, depth .7, mix .6). 49: Maromaro Retro retuned to the user's tone blueprint (slow 0.25 Hz seasick + tape drift + blueprint amp dial-in). 48: Maromaro Retro moved to the new Seasick Vibe mod type (deep sweep + dry/wet crossfade = true pitch heave). 47: + Maromaro Retro (bank 15/C, 2026-07-23): compressed edge-of-break-up clean + deep seasick Small Clone, for the Retro Poland covers. 46: re-seed to flush transient store churn (2026-07-23): a partially-migrated Bank 1 Clean (~5 dB quiet) was found live in a user store; fresh seeds measure correct (-13.9 dBFS DI at out -24 = parity), so the seed VALUES are unchanged - the bump just guarantees every store gets a clean Bank 1. 44: Bank 1 room (2026-07-14, user: "make sure my first four presets have it") — the stock Clean/Crunch/Rhythm/Lead seeds now get roomon 1 / mix 0.12 / size 0.35 explicitly after their v3 migration (appended ports migrate in as 0). 43: ROOM ON BY DEFAULT (2026-07-14, user) — every preset now carries a subtle small-to-medium room (roomon 1, mix 0.12, size 0.35 via the seeded defaults; old user blobs zero-fill = off, their sounds untouched); full loudness re-measure baked. Cab panel rearranged: CABINET over ROOM left, mic pad right. 42: CAB ROOM AMBIENCE (2026-07-14) — cab_roomon/roommix/roomamt: a small Schroeder room (4 damped combs + allpass) after the cab convolution, toggle + mix + size, default OFF (bit-identical; presets reseeded off). 41: MIC PLACEMENTS IN PRESETS + NEVERMIND WALL REBUILD (2026-07-14) — (a) researched per-rig mic positions baked into 35 presets ('60s = backed-off warm placements: Hendrix .30/.25, Apache .20/.50, Surf .15/.40; '70s = off-cap + air: Trower/May/Gilmour ~.25-.30/.15-.20; room-heavy doom/psych: Wizard's Doom .40/.35, Sleep .35/.30, Innerspeaker .30/.35; Vig-era Nirvana .20-.25/.05-.10; modern tight metal (Mesa/EVH/Periphery/Mastodon/Gojira) + NIN direct rigs stay 0/0 = close cap edge; user-preserved presets untouched); full loudness re-measure follows. (b) Nevermind Wall REBUILT to the documented SLTS chain (user: old rework "is trash"): Mustang -> DS-1 at Kurt's settings (tone 10 o'clock, dist 4, LEVEL MAXED) -> pushed Fender Bassman (Clean Meanie gain .6 master .8), NO chorus on the wall, @american-ob, SM57 slightly off-cap. 40: CAB MIC PLACEMENT (2026-07-14) — new cab_micpos (cap edge -> cone edge: HF slide 12k->3.3k + bite recede + body) and cab_micdist (close -> ~30 cm: proximity bass falls away + air loss) on the Cabinet block; post-convolution morphs, 0/0 = bit-identical baseline (append-only, presets reseeded with 0/0 = no tone change). 39: ENRICHED FACTORY CABS (2026-07-14, user: "this is where we could really make this sound real") — all 6 built-in cabs (incl. @factory V30) gain deterministic measured-IR anatomy (CabModels::enrich: seeded cone-breakup ripple, multi-cone/baffle-edge sub-ms taps, closed-back box return / open-back dipole cancel, ringing box air mode) with an iterated DAMPED macro-correction so each cab keeps its tuned voicing (verified ≤0.9 dB drift on 4 cabs / ≤1.7 on greenback+doom, L2 loudness exact); full 54-preset loudness re-measure baked. 38: PLEXI VOL II (2026-07-14, user: "the original had Volume I and Volume II") — new amp_pl_vol2 port (append-only, default 0 = old voicing bit-identical, no blob migration): a parallel Normal-channel V1 half jumpered under the capture-anchored bright path, like the real 1959. Hendrix presets jumpered (Mauve Haze VolI .5/VolII .5/master .7; Hazy Solo — the user's "starved Octavia" — VolI .62/VolII .55/master .75/sag .45 = the DIMED stack the Octavia historically rang over; Little Feather VolII .35; Bottle Jangle VolII .3). Nevermind Wall untouched (user-preserved, vol2 0). 37: INPUT-TRIM PHASE default fixed 1->0 (2026-07-14) — default 1 meant INVERTED polarity on every instance + all factory presets (mismatched the standalone Input Trim's phase_invert default 0 and every offline NAM calibration); all presets reseeded with normal polarity; the toggle stays for DI-blend use. 36: PERIPHERY DJENT RETUNE (2026-07-14, user + research: Misha's Precision-Drive philosophy = cut bass INTO the amp, definition from mids/treble, saturation-not-mush) — Flatliner bass 0.5->0.25 mid 0.62 treble 0.62 pres 0.55 gain 0.58 TS-tone 0.7 lowcut 100; Prayer Djent bass 0.55->0.22 mid 0.65 treble 0.6 gain 0.58 TS-tone 0.72 lowcut 105. 35: user ear-pass 2 (2026-07-14) — Skye Crusher + Skye (No Mod) bass 0.78->0.48 ("way too much bass" at the new gain; Skye Soar left at 0.75 — not named, and the lead's octave/delay masks differently). 34: user ear-pass (2026-07-14) — Mastodon bank MORE GAIN (Skye Crusher/No Mod amp gain 0.45->0.62, Skye Soar 0.55->0.7, TS level 0.74->0.8) + Castaway Groove LESS BASS (0.55->0.35, low-cut 84->92). 33: CALI V DOCTRINE PASS (2026-07-14) — the user's confirmed Mark-series recipe ("the amp works like an overdrive; the tone comes from the EQ"): Cardinal Rhythm = the user's exact device dial-in BAKED (gain 0.15, Bass 0, Treble 0.38, DOD 250 level-boost in front, brightness at GEQ 6.6k — USER-PRESERVED, do not retune); the recipe translated to the other Cali V presets (Marionette Master gain 0.8->0.5 Bass 0.05 Treble 0.40 geq4 0.78; Spectrum Rhythm gain 0.76->0.35 Bass 0.08 Treble 0.40 geq4 0.72; Spectrum Lead gain 0.78->0.42 Bass 0.10 Treble 0.45 geq4 0.66) + loudness re-measured. 32: OVERNIGHT AUTHENTICITY PASS (2026-07-14, user-authorized full-permission run) — all 54 presets audited against researched real rigs by a 15-agent web-research fan-out, 28 adjusted after adjudication: historically-correct cabs across 15 presets (Ghost=V30 PPC412 not Greenbacks; Hendrix='67 G12M Greenbacks not V30s; Mastodon=Mills V30s; Nirvana verse=Bassman open-back + Come As You Are=@vox2x12; Mayer/Cure/Chic=American open-backs; Trower/Police=Greenbacks; Numb Sustain=@hiwatt), drive-pedal identities fixed (Impera rhythm=Sugar Drive/Klon, Impera lead=DOD 250, Run Like Hell=Colorsound Power Boost full-series), Cardinal Rhythm moved to the DOCUMENTED Skeleta rhythm amp (Hetfield's Mesa IIC+ = Cali V mode 6, voiced per Cali V rules), Holy Smoke rebuilt to Pike's documented no-fuzz chain (Soldano-style OD into cranked saggy Orange), Comfortably Numb delay fixed to the MXR Digital + added the missing RA-200 rotary blend, Innerspeaker fuzz=germanium Fuzz Face family (I Know It), Glide Wall reverse-verb predelay 0, Streets Chime 359 ms (measured 125.5 BPM), Bottle Jangle=Marshall 1959 platform + 330 ms Echoplex at 50/50, March Stabs master 0.55 (JMP-1 direct, no power amp), Regal Solo 800 ms Echoplex canon, Cavalier Charge 330 ms + the Manson-wired Phase 90, Trower's documented presence-0/treble-low Marshall settings, Candlelit comp=optical. Full loudness re-measure follows. 31: FULL preset overhaul (2026-07-14) — (a) LOUDNESS PARITY: all 54 presets re-measured on-device (hexforge_meas) against the NEW DSP (FF v3 re-model, Cali V retune, DNR additions) and out_level rewritten to the -12.5 dirty / -13 clean targets; the 9 never-measured presets (Gojira/Mesa/Muse banks, Regal Solo, Grunge Drop) leveled for the first time. (b) GATE FLOOR-COMPLIANCE vs the measured rig floor: Bridge Vibe/Streets Chime raised off the -60 default, Regal Solo -54->-50, Con Molars -54->-48 (fuzz/doom deep gates kept deliberately). (c) MUSE presets retuned to Bellamy's DOCUMENTED FF recipe (Drive max/Comp low/Stab max, probe-verified squeal-safe at the preset Gate; back Stab off to ~0.55-0.85 for the splatty scream). 30: FF cut-out fix — Muse chain gates OPENED (-50/-48 -> -58, hold 200/release 400; the raw-guitar-keyed gate in FRONT of the fuzz chopped sustain while the fuzz still roared — same as the old Octavia choke; the FF's own Gate knob is the noise-killer) + Spectrum Lead gate -52 -> -48 (could never close against the user's measured -45 dBFS hands-off idle floor) (2026-07-14). 29: renamed preset "With Teeth" -> "Con Molars" (bank 11/D) + fuzz pedal "Fizz Factory" -> "Fuzz Zachary" (label only, value 3 unchanged) (2026-07-13). 28: Cali V HISS RETUNE (Marionette Master/Spectrum Rhythm/Spectrum Lead) — Bass down (tight; real MarkV wants Bass 1-3 on high gain), pre-gain Treble down + brightness moved to the POST-gain GEQ 6.6 kHz (Treble is early in the circuit = adds saturation/hiss), gain nudged up to offset the model's ~3 dB high-gain trim; out_level nudged +1 for the bass loss (verify by ear) (2026-07-13). 27: PACKED presets — added Knights of Cydonia ("Cavalier Charge") + compacted so no populated bank before the last has blank slots (banks 0-13 full; MUSE = last bank, index 14, A/B only; index 15 emptied). Regal Sustain->1/A, Regal Solo->11/D, Grunge Drop->14/D, Muse->bank 14 (2026-07-13). 26: Plug-In Junior Stab 0.5->0.2 (below the osc onset — was squealing too much) (2026-07-13). 25: Plug-In Junior retune — Stab 0.72->0.5 (below the self-osc onset; PIB riff shouldn't squeal), Drive 0.92, Comp 0.5, Gate 0.42 (2026-07-13). 24: Fizz Factory — hotter drive + gentler gate (fix "cut out") + note-gated SELF-OSCILLATION above Stab 0.55 (the unruly FF scream, dies on silence); calibrated to the real Stab sweep (THD/level rise); Plug-In Junior Drive 0.85/Gate 0.35/Stab 0.72 (2026-07-12). 23: Fizz Factory Stab = SUPPLY-RAIL model (ZVEX "operating voltage" — germanium FF); UP=tight/full, DOWN=starved squishy sag/sputter (correct real-pedal direction); Plug-In Junior Stab 0->0.9 (tight) (2026-07-12). 22: Fizz Factory Stab reworked (note-gated feedback + low 480 Hz growl + late onset — no more standalone whine); Plug-In Junior Stab 0.15->0 (2026-07-12). 21: NEW Bank 16 (index 15) MUSE — "Plug-In Junior" (Plug In Baby, Fizz Factory riff), slot A; B/C/D reserved; seed-clear spares 15/0 (2026-07-12). 20: trimmed the batch to Regal Sustain/Solo + Grunge Drop (removed the other 11 + Rotary Dream/Surfing Lead); added the INPUT-TRIM clean boost (it_boost ~6 dB) to both Regal presets; clears the vacated banks 15-17 + tail slots; 52 presets (2026-07-12). 19: +14 new presets (Brown Sound/Back in Black/Appetite/Texas Flood/Iron Man Doom/Surfing Lead/Microtonal Mirage/Cinematic Swell/Nashville Twang/Warm Archtop/Funk Machine/Djent Modern/Grunge Drop/Rotary Dream); moved both Queen presets together into new Bank 15 (Royalty), backfilled Bank 10/D=Rotary Dream + Bank 13/D=Surfing Lead so no blank slots; 65 presets total (2026-07-12). 18: removed "There There" + Gojira "Quicksilver Lead"; added Brian May LEAD "Regal Solo" (treble-boosted AC30) + made "Regal Sustain" hotter; repacked the tail so NO blank slots (Gojira -> Bank 12 C/D, Mesa Mark+Regal Solo -> Bank 13); clears the vacated old Bank 15 slots (2026-07-12). 17: Quicksilver Lead octave-up moved POST-cab (pos 2->7) + boosted (up 0.45->0.65) — in front of the high-gain EVH it was masked by the amp's own 2nd harmonic ("octave didn't work") (2026-07-12). 16: parody-renamed the new presets (Winterborn/Quicksilver Lead/Castaway Groove/Marionette Master/Spectrum Rhythm+Lead); Quicksilver = former Silvera turned into an octave-up LEAD (was too close to the rhythm); Marionette Master DE-BASSED (2026-07-11). 15: Gojira Silvera/Stranded amp JCM800->EVH 5150 III (Gainzilla, red ch) per user — their actual high-gain amp (2026-07-11). 14: NEW banks — Bank 14 GOJIRA (Born in Winter clean-tap / Silvera / Stranded, JCM800+TS) + Bank 15 MESA MARK (Master of Puppets = Cali V IIC+ smiley-scoop; Colors Rhythm/Lead = Cali V Mark IV) (2026-07-11). Muse (Plug In Baby / Knights of Cydonia) deferred — needs a ZVex Fuzz Factory model. 13: REVOICED Ghost/Periphery/Mastodon for the current gear (input-clip removed + TS808/Friedman re-voiced) — normalized the TS clean-boost level (hot 0.9-1.0 down / attenuating 0.6 up, all ~0.72-0.78) + eased treble/presence/high-cut for the now-untamed brighter front-end (2026-07-11). NOTE: out_level still on the pre-revoice measurements — re-measure once tone is locked. 12: Imperial Lead tamed (was too fuzzy: dropped Sat + gain 0.72->0.56 + Klon 0.35->0.16); Hazy Solo un-starved (opened the gate in front of the Octavia + more sustain/amp gain) (2026-07-11). 11: FULL LOUDNESS PARITY re-level (2026-07-09) — amp makeup leveled (all amps equal-feel at noon) + every factory preset out_level re-measured & set to the Bank-1 target (-12.5 dirty / -13 clean); dropped the -5.9 clean pin + the +8 dB pushed-loud leads. 10: real Plexi loudness for Nevermind Wall/Mauve Haze/Hazy Solo (they were mis-loading as Backline; re-measured) + Nevermind Wall reworked to Plexiglass. 2: Gravity Lead out_level -27.5 -> -23.0. 3: Ghost Imperial/Cardinal Lead phaser -> subtle Lush-2 chorus. 4: + Angine de Poitrine bank (Bank 13, microtonal shimmer). 5: Bank 13 B/C -> Radiohead (Anyone Can Play Guitar / There There); kept Quarter-Tone Lead. 6: Cardinal Lead drive -> Preamp 250 (DOD 250). 7: Cardinal Lead re-staged (DOD drive 0.48->0.25, amp gain 0.78->0.62) — was too hot/mushy. 8: Cardinal Lead out_level re-measured on-device (-17.3->-19.2; DOD250 denser than the old RAT)
struct Preset {
    bool  used = false;
    char  name[32] = {0};
    float vals[HF_N_PORTS] = {};
    char  irPath[kPathMax]     = {0};
    char  ir2Path[kPathMax]    = {0};   // Cab 2 user IR (v39; empty = built-in rb_cab selection)
    char  amp2NamPath[kPathMax]= {0};   // Amp 2 NAM capture (v39)
    char  dr2NamPath[kPathMax] = {0};   // Drive 2 NAM capture (v40)
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
static const int   kCanonical[14] = { 0, 1, 2, 4, 5, 3, 6, 0, 9, 0, 10, 10, 7, 8 }; // PowerAmp default lookup ([8] Vox -> its own EL84 case 9) ([12] Recto -> its own 6L6 case) ([10] Plexi, [11] Mesa -> FROZEN row 10 since the 2026-08-20 JCM800 HG-round-2 bake re-voiced row 1)
static constexpr int kSunnIdx     = 3;
static constexpr int kFriedmanIdx = 6;
static constexpr int kHiwattIdx   = 7;
static constexpr int kVoxIdx      = 8;
static constexpr int kBacklineIdx = 9;
static constexpr int kPlexiIdx    = 10;   // Plexiglass (Marshall 1959 Super Lead)
static constexpr int kMesaIdx     = 11;   // Cali V (Mesa Mark V)
static constexpr int kRectoIdx    = 12;   // Diamond Plate (Mesa Dual Rectifier)
static constexpr int kMt15Idx     = 13;   // Tremont 15 (PRS MT15)
// 2026-08-21 tube-correctness audit: Fender 6L6→6V6 (real AB763 = 6V6GT, new
// TubeType), EVH EL34→6L6 (real 5150III = 6L6GC), Sunn 6L6→KT88 (real Model T
// = 6550-class; inert — its PA is force-bypassed, the model has its own power
// section). Backline stays nominal 6L6 (solid-state; PA deliberately neutral,
// capture-tuned through it). The rest verified correct: JCM800/Plexi/Hiwatt/
// Rockerverb/Friedman EL34, Vox EL84, MarkV/Recto/MT15 6L6.
static const int   kAmpTube[14]   = { 4, 1, 0, 3, 1, 0, 1, 1, 2, 0, 1, 1, 0, 0 };
// Amp-level PARITY calibration (2026-07-09, build-tools/hexforge_amplevel measured @noon, cab on):
// distorted amps → -16 dBFS RMS, clean amps (Fender/Hiwatt/Vox/Backline) → -13 dBFS (+3 dB perceptual
// boost so cleans FEEL as loud as the denser distorted models). Re-leveled after the amp re-voicings
// left Hiwatt -24 / CaliV -23 / Friedman -21.5 / Plexi -19 several dB quiet. makeup is linear post-amp gain.
static const float kAmpMakeup[14] = { 4.89f, 1.18f, 1.48f, 3.18f, 1.19f, 1.0f, 1.14f, 4.8f, 2.05f, 4.15f, 1.49f, 2.16f, 3.4f, 3.65f }; // [0] Fender 5.20->4.89 (2026-08-21 tube audit): the correct 6V6 runs +0.54 dB hotter at the hf_amplevel noon anchor; x0.94 restores the -13 dBFS clean parity exactly. Previously bumped 3.78->5.20 (2026-07-28): item #28/#25 exact-tonestack re-voice measured ~2.8dB quieter vs NAM (nam_compare loudness: old x1.38, new x1.90) -- restores the loudness the FR re-voice didn't otherwise preserve; [5] NAM passthrough; [11] Cali V scales all 9 modes (per-mode makeup is inside the model)  // [7] Hiwatt was 4.9 (BUG: slammed the master limiter under any drive → mush + forced out_level to -27); high-headroom amp needs little makeup, loudness comes from out_level. [8] Vox [9] Backline (solid-state; model runs ~9 dB below the NAM, low crest so 2.5 is safe). [10] Plexi (Marshall EL34, like JCM800)
// (REMOVED 2026-07-11) The `kAmpInputCeil = A*tanh(x/A)` "input ceiling" on the amp block was added
// 2026-07-03 and CHANGED THE AMP CHARACTER: being a nonlinearity at any A, it pre-distorted the amp
// input, and the high-gain front-ends amplified that into a fuzzy/dark (high A) or woolly (low A)
// voice. The amps are voiced for a RAW input (like the standalone Amp plugin), so it's gone. Hot
// upstream blocks are handled by preset gain-staging, not by distorting every amp's front end.

// Indexed by dr_model port: 0/1/2/4/5 = algorithmic, 3 = NAM (special-cased, entry unused).
static const OverdriveType kDriveMap[9] = {
    OverdriveType::TubeScreamer808, OverdriveType::LifePedal, OverdriveType::ProcoRAT,
    OverdriveType::ProcoRAT /* [3]=NAM placeholder, never used */, OverdriveType::DS1,
    OverdriveType::Klon, OverdriveType::SuperOverdriveSD1, OverdriveType::DOD250,
    OverdriveType::EchoplexPreamp,   // 8 = Echo Primer (EP-3 JFET, v42)
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
    // Subsonic DC blocker on the master (ACTIVE): removes DC/rumble that fuzz bias
    // shift, octave-down or a DC-offset user IR can push into the limiter/meters.
    // ~5 Hz 1-pole — inaudible on guitar (<0.05 dB above 80 Hz), pure hygiene.
    BiquadFilter dcHP[2];
    // ADAA soft-knee limiter (item 14). soft=false (default) keeps the exact
    // instant-attack gain-ride below; soft=true swaps in a zero-latency, low-alias
    // ADAA soft clip per channel. Enabling it shifts loudness on presets that hit
    // the ceiling, so it stays OFF until a Phase-2 hfmeas re-measure turns it on.
    bool soft = true;   // ADAA soft-knee limiter ON (2026-07-26 Phase-2): zero-latency,
                        // low-alias vs the old hard gain-snap. Knee 0.85 so it only
                        // touches signals near the ceiling; loudness shift is <0.5 dB.
    AdaaSoftClip clip[2];
    void prepare(double sr) noexcept {
        const float s = static_cast<float>(sr);
        rel     = std::exp(-1.0f / (0.045f * s));   // ~45 ms limiter release (low dulling)
        lvlCoef = std::exp(-1.0f / (0.010f * s));   // ~10 ms gain smoothing (no zipper)
        const auto hp = Filters::highpass1pole(5.0, sr);
        for (auto& b : dcHP) b.setCoeffs(hp);
        for (auto& c : clip) c.set(0.85f, kCeiling);
    }
    void reset() noexcept {
        env = 1.0f;
        for (auto& b : dcHP) b.reset();
        for (auto& c : clip) c.reset();
    }
    void process(float* L, float* R, uint32_t n, float gain, bool limit) noexcept {
        if (!primed) { lvlZ = gain; primed = true; }
        for (uint32_t i = 0; i < n; ++i) {
            lvlZ = lvlCoef * lvlZ + (1.0f - lvlCoef) * gain;         // smoothed master gain
            float l = dcHP[0].process(L[i]) * lvlZ;
            float r = dcHP[1].process(R[i]) * lvlZ;
            if (limit && soft) {
                // Zero-latency ADAA soft clip (no envelope → no attack-snap alias).
                L[i] = clip[0].process(l);
                R[i] = clip[1].process(r);
                continue;
            }
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
enum Block { B_GATE, B_COMP, B_FUZZ, B_DRIVE, B_AMP, B_CAB, B_MODFX, B_DELAY, B_REVERB, B_WAH, B_OCTAVE, B_NAIL, B_EQ, B_DRIVE2,
             // X2 second instances (2026-07-30): a clone of every movable effect
             B_GATE2, B_COMP2, B_FUZZ2, B_NAIL2, B_MODFX2, B_DELAY2, B_REVERB2, B_WAH2, B_OCTAVE2, B_EQ2, B_COUNT };
static const int kPosPort[B_COUNT] = {
    HF_GT_POS, HF_CP_POS, HF_FZ_POS, HF_DR_POS, HF_AMP_POS,
    HF_CAB_POS, HF_MD_POS, HF_DL_POS, HF_RV_POS, HF_WH_POS, HF_OC_POS, HF_NAIL_POS,
    HF_EQ_POS, HF_DR2_POS,
    HF_GT2_POS, HF_CP2_POS, HF_FZ2_POS, HF_NAIL2_POS, HF_MD2_POS,
    HF_DL2_POS, HF_RV2_POS, HF_WH2_POS, HF_OC2_POS, HF_EQ2_POS,
};
static const int kEnablePort[B_COUNT] = {
    HF_GT_ENABLE, HF_CP_ENABLE, HF_FZ_ENABLE, HF_DR_ENABLE, HF_AMP_ENABLE,
    HF_CAB_ENABLE, HF_MD_ENABLE, HF_DL_ENABLE, HF_RV_ENABLE, HF_WH_ENABLE, HF_OC_ENABLE, HF_NAIL_ENABLE,
    HF_EQ_ENABLE, HF_DR2_ENABLE,
    HF_GT2_ENABLE, HF_CP2_ENABLE, HF_FZ2_ENABLE, HF_NAIL2_ENABLE, HF_MD2_ENABLE,
    HF_DL2_ENABLE, HF_RV2_ENABLE, HF_WH2_ENABLE, HF_OC2_ENABLE, HF_EQ2_ENABLE,
};
// enable = chain membership (1 = in chain, 0 = removed/palette); bypass = active(0)/
// bypassed(1). A block runs iff enable==1 && bypass==0. Bypassed blocks stay in the chain
// (greyed in the UI) but pass dry, keeping their settings — for live A/B.
static const int kBypassPort[B_COUNT] = {
    HF_GT_BYPASS, HF_CP_BYPASS, HF_FZ_BYPASS, HF_DR_BYPASS, HF_AMP_BYPASS,
    HF_CAB_BYPASS, HF_MD_BYPASS, HF_DL_BYPASS, HF_RV_BYPASS, HF_WH_BYPASS, HF_OC_BYPASS, HF_NAIL_BYPASS,
    HF_EQ_BYPASS, HF_DR2_BYPASS,
    HF_GT2_BYPASS, HF_CP2_BYPASS, HF_FZ2_BYPASS, HF_NAIL2_BYPASS, HF_MD2_BYPASS,
    HF_DL2_BYPASS, HF_RV2_BYPASS, HF_WH2_BYPASS, HF_OC2_BYPASS, HF_EQ2_BYPASS,
};

// ── 6-band graphic EQ block (2026-07-23) ─────────────────────────────────────
// MXR-style octave centers. The PRESET port is a RETAINED SELECTION only: the
// modgui loads the chosen curve into the slider ports on click (so the values
// live on the controls and persist in presets/pedalboards) — the DSP reads the
// sliders alone, so the retained selection never double-applies.
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
        // Preset curves live in script-hexforge.js (EQ_PRE, from the Neural DSP
        // electric-guitar EQ guide) — the UI loads them into the slider ports.
        (void)preset;   // retained UI selection; not part of the audio path
        bool dirty = (lvlDb != curLvl);
        for (int b = 0; b < kBands && !dirty; ++b) dirty = (db[b] != curDb[b]);
        if (!dirty) return;
        curLvl = lvlDb;
        for (int b = 0; b < kBands; ++b) {
            curDb[b] = db[b];
            float d = db[b];
            if (d > 12.0f) d = 12.0f; else if (d < -12.0f) d = -12.0f;
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

// Enum-valued ports whose changes are audible steps (model/type/mode switches) —
// any change triggers the seamless-switch mute ramp.
static const int kSwWatch[] = {
    HF_AMP_MODEL, HF_DR_MODEL, HF_DL_TYPE, HF_MD_TYPE, HF_FZ_PEDAL, HF_FZ_MODE, HF_RV_DENSITY,
    HF_RV_TYPE, HF_CAB_ROOMDENSE, HF_CAB_SPKDRIVE, HF_MD_SHAPE,
    HF_NAIL_MODE, HF_CP_TYPE, HF_WH_TYPE, HF_AMP_MV_MODE, HF_AMP_RC_MODE,
    HF_AMP_MT_MODE, HF_AMP_FR_CHANNEL, HF_AMP_CHANNEL, HF_CAB_VOICE, HF_QUALITY, HF_DR_ECO, HF_DR2_MODEL, HF_DR2_ECO, HF_RB_ENABLE, HF_RB_AMP, HF_RB_CAB, HF_RB_ECO,
    HF_RB_MV_MODE, HF_RB_RC_MODE, HF_RB_MT_MODE, HF_RB_FR_CHANNEL, HF_RB_CABVOICE, HF_RB_CABROOMDENSE, HF_RB_CABSPKDRIVE,
    // X2 clones (v38): the same audible-step switches as their parents
    HF_FZ2_PEDAL, HF_FZ2_MODE, HF_DL2_TYPE, HF_MD2_TYPE, HF_MD2_SHAPE,
    HF_RV2_DENSITY, HF_RV2_TYPE, HF_NAIL2_MODE, HF_CP2_TYPE, HF_WH2_TYPE,
    // v40: fuzz/nail Eco rebuild at the mute zero point
    HF_FZ_ECO, HF_NAIL_ECO, HF_FZ2_ECO, HF_NAIL2_ECO,
    // v41: Plexi Variac (drive/sag topology step)
    HF_AMP_PL_VARIAC, HF_RB_PL_VARIAC,
    // v43: SIR #34 mod (adds/removes a whole gain stage)
    HF_AMP_SIR34, HF_RB_SIR34,
};
static constexpr int kSwWatchN = int(sizeof(kSwWatch) / sizeof(kSwWatch[0]));
static_assert(kSwWatchN <= 64, "grow swWatchPrev[]");

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
    bool              eco      = false;   // AMP_LOAD: build with 2x oversampling (Engine Quality)
    int               namSlot  = 0;    // 0=amp 1=drive 2=cab
    char              path[kPathMax] = {0};   // CAB_IR / NAM_LOAD
};

struct URIs {
    LV2_URID atom_Object, atom_Path, atom_URID, atom_String, atom_Chunk;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID ir_file, amp_nam, dr_nam, cab_nam, amp2_nam, ir2_file, dr2_nam;
    LV2_URID ps_name, ps_index, ps_apply, preset_blob, meters, tuner, cal;
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
    PickupLoadSim     trimLoad;         // pickup/cable/input-impedance sim (v24, default off)
    OutputBoost       trimBoost;        // pickup-agnostic output boost + beef (Input Trim)
    NoiseGateBlock    gate;
    CompressorBlock   comp;
    std::unique_ptr<OversamplingWrapper> fuzzMuff;   // Italian Hero
    std::unique_ptr<OversamplingWrapper> fuzzBender; // Tone Bender MkII
    std::unique_ptr<OversamplingWrapper> fuzzOctavia;// Octavia (octave-up)
    std::unique_ptr<OversamplingWrapper> fuzzFactory;// Fizz Factory (ZVex-style chaos/gated octave)
    OverdriveBlock    drive;
    OverdriveBlock    drive2;           // Drive B (first multi-instance block, 2026-07-30)
    int               lastDrive2Model = -1;
    AmpBlockExtended* amp = nullptr;                  // swapped on model change
    AmpBlockExtended* amp2 = nullptr;                 // Rig B amp (2026-07-30 dual rig)
    AmpBlockExtended* pendAmp2 = nullptr;
    int               pendAmp2Model = -1;
    PowerAmpProcessor pa2;                            // Rig B power amp
    CabinetBlock      cab2;                           // Rig B cab (built-ins only)
    int    lastAmp2Model = -1;
    bool   lastAmp2Eco   = false;
    bool   amp2Requested = false;       // W_AMP_LOAD(B) in flight -- DO NOT re-schedule (allocation storm)
    int    lastCab2Model = -1;
    bool   rigBHeld      = false;                     // rigBBuf holds this chunk's amp-input tap
    float  rigBBuf[1024] = {};                        // kMaxBlock; mono B-path scratch
    BiquadFilter rbLoCut;                             // rb_locut (v44): HP on the blend layer
    float  rbLoCutLast   = -1.0f;
    double cpuRigB       = 0.0;                       // Rig B CPU meter accumulator (whole B path)
    double cpuCab2       = 0.0;                       // Cab 2 slice of cpuRigB (UI shows Amp 2 = rigb - cab2)
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
    // ── X2 second instances (2026-07-30, v38): a clone of every movable effect.
    // Always constructed (matches the drive2 precedent); processed only when the
    // block is enabled, so idle cost is zero and RAM is the only standing price.
    NoiseGateBlock    gate2;
    CompressorBlock   comp2;
    std::unique_ptr<OversamplingWrapper> fuzz2Muff;
    std::unique_ptr<OversamplingWrapper> fuzz2Bender;
    std::unique_ptr<OversamplingWrapper> fuzz2Octavia;
    std::unique_ptr<OversamplingWrapper> fuzz2Factory;
    std::unique_ptr<OversamplingWrapper> nail2;
    ModulationBlock   modfx2;
    DelayBlock        delay2;
    PlateReverbBlock  reverb2;
    WahBlock          wah2;
    OctaveBlock       octave2;
    GraphicEQ         eq2;
    int lastNail2Mode = 2, lastModfx2Type = 0, lastDelay2Type = 0;
    NamModel*         ampNam = nullptr;   // worker-loaded neural captures
    NamModel*         drNam  = nullptr;
    NamModel*         cabNam = nullptr;
    NamModel*         amp2Nam = nullptr;  // Amp 2 (Rig B) neural capture (v39)
    NamModel*         dr2Nam  = nullptr;  // Drive 2 neural capture (v40)

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
    double cpuAcc[B_COUNT + 1] = {};    // per-block + [B_COUNT]=whole-chunk seconds (CPU meters)
    long   cpuSamps   = 0;              // samples since last meter publish
    bool   lastEco    = false;          // last Engine Quality the amp was built with
    int    lastGoto   = -1;             // last ps_goto target serviced
    float  meterIn = 0.0f, meterOut = 0.0f;  // smoothed peak level meters (-> in_meter/out_meter)
    float  meterSentIn = -1.0f, meterSentOut = -1.0f;  // last values pushed to UI (deadband)
    uint32_t meterFrames = 0;                // throttle for the #meters notify (UI)
    // ── Auto-calibration wizard (tail ports, see gen_hexforge.py) ──
    int        calState = 0;                 // mirrors cal_state: 0 idle/1-3 phase/4 done/5 error
    uint32_t   calSamplesLeft = 0, calSamplesTotal = 0;   // phase countdown (sample-counted)
    float      calCmdPrev = 0.0f;            // cal_cmd edge detect
    CalMeasure calMeas;                      // shared measurement core (lv2/common/CalMeasure.h)
    CalPhaseStats calPh[3];                  // hands-off / hands-on / play-hard results
    bool       calPhDone[3] = {false,false,false};
    CalRecommend  calRec{};
    int        calNotifyTicks = 0;           // keep pushing #cal briefly after completion
    // Applied offset layer, slewed toward the cal_trim_offs/cal_floor_offs ports
    // (~10 dB/s) so an Apply never clicks; snapped to the ports on the first run.
    float      calTrimOffsSm = 0.0f, calFloorOffsSm = 0.0f;
    bool       calOffsInit = false;
    // ── EVH capture-fit voicing (BAKED 2026-08-19, blend 0.8875 chosen by ear
    // on the live lab; loudness-neutral). See lv2/common/EvhCaptureFit.h.
    EvhCaptureFit evhFit[2];
    // ── Recto CH3-Modern capture-fit voicing (BAKED 2026-08-20, user blend 1.0
    // from the live lab; loudness-neutral). See lv2/common/RectoCaptureFit.h.
    RectoCaptureFit rectoFit[2];
    // ── Output Voice: FRFR de-close-mic EQ. Permanent USER knobs (mv189);
    // defaults = the user's 2026-08-21 FRFR-10 in-room reference bake (locut
    // 100.75 Hz / prox 1.815 dB / pres 3.225 dB / fizz 2.70 dB). Coeffs re-fit
    // only when a knob moves (change-detect); filter state carries.
    BiquadFilter fvHP[2], fvProx[2], fvPres[2], fvFizz[2];
    float fvLastCut = -1.0f, fvLastProx = -1.0f, fvLastPres = -1.0f, fvLastFizz = -1.0f;
    // Double-tap bank nav: double-tap A = bank down, D = bank up.
    int64_t sampleClock = 0;            // running sample counter
    int64_t lastTapSample[4]  = {-100000000,-100000000,-100000000,-100000000};
    int64_t lastEdgeSample[4] = {-100000000,-100000000,-100000000,-100000000};  // sw debounce

    // file-load state
    char irPath[kPathMax]     = {0};
    char ir2Path[kPathMax]    = {0};    // Cab 2 user IR override (v39)
    char amp2NamPath[kPathMax]= {0};    // Amp 2 NAM capture (v39)
    char dr2NamPath[kPathMax] = {0};    // Drive 2 NAM capture (v40)
    char ampNamPath[kPathMax] = {0};
    char drNamPath[kPathMax]  = {0};
    char cabNamPath[kPathMax] = {0};

    // scratch
    float mono[kMaxBlock], monoOut[kMaxBlock];
    int   clipHold = 0;   // samples remaining to keep the CLIP indicator lit
    // ── Seamless switching (2026-07-23) ──────────────────────────────────────
    // A short output mute-ramp (~2.5 ms out / 10 ms in) wraps every discontinuous
    // event: preset recall, block engage/bypass, model/type/mode changes, and
    // worker-loaded amp/NAM swaps. The event itself is DEFERRED to the ramp's
    // zero point, where stale tails are also cleared — so nothing pops and no
    // leftover delay/reverb/room audio ever replays into the new sound.
    int   swFadeState = 0;              // 0 idle, 1 fading out, 2 fading in
    float swFadeGain  = 1.0f;
    bool  swPendRecall = false;
    int   swPendBank = 0, swPendSlot = 0;
    bool  swHold = false;               // chain topology frozen during fade-out
    bool  swAcceptNext = false;         // adopt the new state silently next block
    bool  swEnabledHeld[B_COUNT] = {};
    bool  swEnabledPrev[B_COUNT] = {};
    bool  swPrevInit = false;
    float swWatchPrev[64] = {};
    AmpBlockExtended* pendAmp = nullptr;   // worker-built amp awaiting the zero point
    int   pendAmpModel = -1;
    NamModel* pendNam[5] = {};             // worker-built NAM models awaiting swap (0 amp, 1 drive, 2 cab, 3 amp2, 4 drive2)
    // Fuzz/Nail Eco (v40): last-applied state -- the wrappers are REBUILT at the
    // mute-ramp zero point when these flip (OverdriveBlock's accepted pattern).
    bool lastFzEco = false, lastFz2Eco = false, lastNailEco = false, lastNail2Eco = false;

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
    irresample::conditionIr(L, dst); if (!R.empty()) irresample::conditionIr(R, dst);   // trim tail + 1 s cap
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
    p->uris.amp2_nam      = m->map(m->handle, HEXFORGE_AMP2NAM);
    p->uris.ir2_file      = m->map(m->handle, HEXFORGE_IR2_URI);
    p->uris.dr2_nam       = m->map(m->handle, HEXFORGE_DR2NAM);
    p->uris.ps_name       = m->map(m->handle, HEXFORGE_URI "#ps_name");
    p->uris.ps_index      = m->map(m->handle, HEXFORGE_URI "#ps_index");
    p->uris.ps_apply      = m->map(m->handle, HEXFORGE_URI "#ps_apply");
    p->uris.preset_blob   = m->map(m->handle, HEXFORGE_URI "#preset_blob");
    p->uris.meters        = m->map(m->handle, HEXFORGE_URI "#meters");
    p->uris.tuner         = m->map(m->handle, HEXFORGE_URI "#tuner");
    p->uris.cal           = m->map(m->handle, HEXFORGE_URI "#cal");
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
              && HF_EQ_LEVEL == HF_EQ_POS + 9 && HF_EQ_BYPASS == HF_EQ_POS + 10,
              "EQ block ports must be contiguous");
//   * Fidelity pair — pickup load + speaker coupling, added v24, last before the commands.
static_assert(HF_IT_LOAD == HF_EQ_BYPASS + 1 && HF_AMP_PAMP_COUPL == HF_IT_LOAD + 1,
              "fidelity ports must be contiguous");
static_assert(HF_RV_DENSITY == HF_AMP_PAMP_COUPL + 1, "reverb density after the fidelity pair");
//   * Reverb Type + Cab Room Density — added v26, last before the commands.
static_assert(HF_RV_TYPE == HF_RV_DENSITY + 1 && HF_CAB_ROOMDENSE == HF_RV_TYPE + 1,
              "v26 ports must be contiguous");
//   * Hex Ambient bloom — 1 port, added v27.
static_assert(HF_RV_BLOOM == HF_CAB_ROOMDENSE + 1,
              "v27 port must be contiguous");
//   * Cab Speaker Drive (item #40) — 1 port, added v28.
static_assert(HF_CAB_SPKDRIVE == HF_RV_BLOOM + 1,
              "v28 port must be contiguous");
//   * Tremolo Shape (roadmap #34) — 1 port, added v29.
static_assert(HF_MD_SHAPE == HF_CAB_SPKDRIVE + 1,
              "v29 port must be contiguous");
//   * Fuzz Guitar Vol (roadmap #45) — 1 port, added v30, last before the commands.
static_assert(HF_FZ_GVOL == HF_MD_SHAPE + 1 && HF_QUALITY == HF_FZ_GVOL + 1
              && HF_DR_ECO == HF_QUALITY + 1 && HF_DR2_POS == HF_DR_ECO + 1
              && HF_DR2_BYPASS == HF_DR2_POS + 9 && HF_RB_ENABLE == HF_DR2_BYPASS + 1
              && HF_RB_POL == HF_RB_ENABLE + 16 && HF_RB_RESONANCE == HF_RB_POL + 1
              && HF_RB_CABSPKDRIVE == HF_RB_RESONANCE + 44 && HF_RB_CAB2ON == HF_RB_CABSPKDRIVE + 1,
              "params: Drive 2, Rig B core, the 45-port parity family, Cab 2 presence (v37)");
//   * X2 second instances (v38): ten contiguous clone families end the param range.
static_assert(HF_GT2_POS == HF_RB_CAB2ON + 1 && HF_GT2_BYPASS == HF_GT2_POS + 7
              && HF_CP2_POS == HF_GT2_BYPASS + 1 && HF_FZ2_POS == HF_CP2_POS + 10
              && HF_NAIL2_POS == HF_FZ2_POS + 12 && HF_MD2_POS == HF_NAIL2_POS + 8
              && HF_DL2_POS == HF_MD2_POS + 12 && HF_RV2_POS == HF_DL2_POS + 17
              && HF_WH2_POS == HF_RV2_POS + 12 && HF_OC2_POS == HF_WH2_POS + 9
              && HF_EQ2_POS == HF_OC2_POS + 8 && HF_EQ2_BYPASS == HF_EQ2_POS + 10
              && HF_RB_NAM_GAIN == HF_EQ2_BYPASS + 1 && HF_RB_NAM_VOL == HF_RB_NAM_GAIN + 1,
              "v38 X2 clone families, then the v39 Amp 2 NAM trims");
//   * v40 quick wins: fuzz/nail Eco x4 + Drive 2 NAM trims end the param range.
static_assert(HF_FZ_ECO == HF_RB_NAM_VOL + 1 && HF_NAIL_ECO == HF_FZ_ECO + 1
              && HF_FZ2_ECO == HF_NAIL_ECO + 1 && HF_NAIL2_ECO == HF_FZ2_ECO + 1
              && HF_DR2_NAM_GAIN == HF_NAIL2_ECO + 1 && HF_DR2_NAM_VOL == HF_DR2_NAM_GAIN + 1,
              "v40: eco x4 + Drive 2 NAM trims");
//   * v41: Plexi Variac (brown sound), both amps, end the param range.
static_assert(HF_AMP_PL_VARIAC == HF_DR2_NAM_VOL + 1 && HF_RB_PL_VARIAC == HF_AMP_PL_VARIAC + 1,
              "v41: the Plexi Variac pair");
//   * v42: EP-3 Echo Age, both delay instances.
static_assert(HF_DL_AGE == HF_RB_PL_VARIAC + 1 && HF_DL2_AGE == HF_DL_AGE + 1,
              "v42: the EP-3 Age pair");
//   * v43: SIR #34 mod toggles, both amps.
static_assert(HF_AMP_SIR34 == HF_DL2_AGE + 1 && HF_RB_SIR34 == HF_AMP_SIR34 + 1,
              "v43: the SIR #34 pair");
//   * v44: rig-B blend low cut ends the param range.
static_assert(HF_RB_LOCUT == HF_RB_SIR34 + 1 && HF_RB_LOCUT == HF_SW_A - 1,
              "v44: rb_locut ends the param range");
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
    // v24 appended pickup load + speaker coupling; both default 0 (off).
    const bool fidGap = (srcVer < 24);
    const int fidAt = HF_IT_LOAD, fidEnd = HF_IT_LOAD + 2;
    // v25 appended reverb density; default 0 (Classic tank).
    const bool rdGap = (srcVer < 25);
    const int rdAt = HF_RV_DENSITY;
    // v26 appended reverb type + cab room density; both default 0.
    const bool rtGap = (srcVer < 26);
    const int rtAt = HF_RV_TYPE, rtEnd = HF_RV_TYPE + 2;
    // v27 appended the Hex Ambient bloom; default 0.5 (inert unless type = Ambient).
    const bool blGap = (srcVer < 27);
    const int blAt = HF_RV_BLOOM;
    // v28 appended Cab Speaker Drive (item #40); default 0 (Off).
    const bool spkGap = (srcVer < 28);
    const int spkAt = HF_CAB_SPKDRIVE;
    // v29 appended Tremolo Shape (roadmap #34); default 0 (Bias = shipped voicing).
    const bool shpGap = (srcVer < 29);
    const int shpAt = HF_MD_SHAPE;
    // v30 appended Fuzz Guitar Vol (roadmap #45); default 1.0 (full = bit-identical).
    const bool gvGap2 = (srcVer < 30);
    const int gvAt2 = HF_FZ_GVOL;
    // v31 inserted 14 CPU-meter OUTPUT ports [HF_CPU_GT..HF_CPU_TOTAL] before HF_MIDI_IN
    // (2026-07-30). Outputs are meaningless in blobs; zero-fill.
    const bool cpuGap = (srcVer < 31);
    const int cpuAt = HF_CPU_GT, cpuEnd = HF_CPU_GT + 14;
    // v32 appended Engine Quality (Eco oversampling switch); default 0 = Standard.
    const bool qGap = (srcVer < 32);
    const int qAt = HF_QUALITY;
    // v33 appended Drive Eco; default 0 = Standard.
    const bool deGap = (srcVer < 33);
    const int deAt = HF_DR_ECO;
    // v34 appended the Drive B family (10 ports: pos..bypass) + its CPU meter
    // (after cpu_total). Defaults: parked at slot 14, disabled, Green Man, knobs
    // at noon-ish, mix full.
    static const float dr2def[10] = {14.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f};
    const bool dr2Gap = (srcVer < 34);
    const int dr2At = HF_DR2_POS, dr2End = HF_DR2_POS + 10;
    // v35 appended the Rig B family (17 ports: enable, amp core, cab, blend) +
    // its CPU meter. Disabled by default; knobs at their port defaults.
    static const float rbdef[17] = {0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.7f, 0.3f, 0.0f, 0.0f,
                                    0.0f, 80.0f, 16000.0f, 0.5f, 0.0f, 0.0f};
    const bool rbGap = (srcVer < 35);
    const int rbAt = HF_RB_ENABLE, rbEnd = HF_RB_ENABLE + 17;
    // v36 appended the 45-port Rig B parity family (amp extras + PA + cab extras);
    // defaults cloned from the A-side ports.
    static const float rb2def[45] = {0,0.5f,0,0.5f,0.5f,0.5f,0,0, 1,0,0,0, 6,0.5f,0.5f,0.5f,0.5f,0.5f,0,
                                     7,0,0, 2,0, 0, 0,1,1,0.55f,0.18f,0.33f,0.62f,0.42f,0.5f,0,0,
                                     1,0,0,1,0.12f,0.35f,0,0,0};
    const bool rb2Gap = (srcVer < 36);
    const int rb2At = HF_RB_RESONANCE, rb2End = HF_RB_RESONANCE + 45;
    // v37 appended Cab 2 presence (rb_cab2on); default 0 (Cab 2 out of the chain).
    // ARCHAEOLOGY (2026-07-30): this gap was missed when the port landed, so the
    // deployed build stamped 318-param blobs as v36 — the loaders disambiguate by
    // the saved param count (np >= 318 -> treated as v37) before calling this.
    const bool c2Gap = (srcVer < 37);
    const int c2At = HF_RB_CAB2ON;
    // v38 inserted the ten X2 clone families [HF_GT2_POS..HF_EQ2_BYPASS] (107
    // ports) + their 10 CPU meters at the tail. Defaults: parked slots 15..24,
    // OUT of the chain (enable 0), params at their port defaults, active.
    static const float x2def[107] = {
        15.0f, 0.0f, -60.0f, 2.0f, 120.0f, 250.0f, 8.0f, 0.0f,   // gt2
        16.0f, 0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f, 3.0f, 0.0f, 0.0f,   // cp2
        17.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f, 0.65f, 0.5f, 0.5f, 0.4f, 1.0f, 0.0f,   // fz2
        18.0f, 0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f,   // nail2
        19.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,   // md2
        20.0f, 0.0f, 0.0f, 250.0f, 0.4f, 0.15f, 0.5f, 0.003f, 0.001f, 10.0f, 1.0f, 0.0f, 0.0f, 0.3f, 0.0f, 5.0f, 0.0f,   // dl2
        21.0f, 0.0f, 10.0f, 1.5f, 0.3f, 0.0f, 0.8f, 0.15f, 0.0f, 0.0f, 0.5f, 0.0f,   // rv2
        22.0f, 0.0f, 0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f, 0.0f,   // wh2
        23.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,   // oc2
        24.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,   // eq2
    };
    const bool x2Gap = (srcVer < 38);
    const int x2At = HF_GT2_POS, x2End = HF_GT2_POS + 107;
    // v39 appended the Amp 2 NAM Gain/Level trims; default 0 dB each.
    const bool rnGap = (srcVer < 39);
    const int rnAt = HF_RB_NAM_GAIN, rnEnd = HF_RB_NAM_GAIN + 2;
    // v40 appended fuzz/nail Eco x4 + Drive 2 NAM trims; all default 0.
    const bool qwGap = (srcVer < 40);
    const int qwAt = HF_FZ_ECO, qwEnd = HF_FZ_ECO + 6;
    // v41 appended the Plexi Variac pair; default 0 (stock wall voltage).
    const bool vrGap = (srcVer < 41);
    const int vrAt = HF_AMP_PL_VARIAC, vrEnd = HF_AMP_PL_VARIAC + 2;
    // v42 appended the EP-3 Echo Age pair; default 0.35 (NON-zero -- a lightly
    // played deck, matching the port default so the knob recalls where it sits).
    const bool agGap = (srcVer < 42);
    const int agAt = HF_DL_AGE, agEnd = HF_DL_AGE + 2;
    // v43 appended the SIR #34 mod toggles; default 0 (stock JCM800).
    const bool smGap = (srcVer < 43);
    const int smAt = HF_AMP_SIR34, smEnd = HF_AMP_SIR34 + 2;
    // v44 appended the rig-B blend low cut; default 0 = off (bit-identical).
    const bool rlGap = (srcVer < 44);
    const int rlAt = HF_RB_LOCUT;

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
        else if (fidGap && i >= fidAt && i < fidEnd)     vals[i] = 0.0f;             // pickup load / coupling off
        else if (rdGap && i == rdAt)                     vals[i] = 0.0f;             // reverb density Classic
        else if (rtGap && i >= rtAt && i < rtEnd)        vals[i] = 0.0f;             // plate / classic room
        else if (blGap && i == blAt)                     vals[i] = 0.5f;             // ambient bloom (inert on plate/spring)
        else if (spkGap && i == spkAt)                   vals[i] = 0.0f;             // speaker drive Off
        else if (shpGap && i == shpAt)                   vals[i] = 0.0f;             // tremolo shape Bias
        else if (gvGap2 && i == gvAt2)                   vals[i] = 1.0f;             // fuzz guitar vol FULL
        else if (cpuGap && i >= cpuAt && i < cpuEnd)     vals[i] = 0.0f;
        else if (qGap && i == qAt)                       vals[i] = 0.0f;             // quality Standard
        else if (deGap && i == deAt)                     vals[i] = 0.0f;             // drive eco Standard
        else if (dr2Gap && i >= dr2At && i < dr2End)     vals[i] = dr2def[i - dr2At]; // Drive B parked
        else if (dr2Gap && i == HF_CPU_DR2)              vals[i] = 0.0f;              // Drive B meter
        else if (rbGap && i >= rbAt && i < rbEnd)        vals[i] = rbdef[i - rbAt];   // Rig B off
        else if (rbGap && i == HF_CPU_RIGB)              vals[i] = 0.0f;              // Rig B meter
        else if (rb2Gap && i >= rb2At && i < rb2End)     vals[i] = rb2def[i - rb2At]; // Rig B parity
        else if (c2Gap && i == c2At)                     vals[i] = 0.0f;              // Cab 2 out of the chain
        else if (x2Gap && i >= x2At && i < x2End)        vals[i] = x2def[i - x2At];   // X2 clones parked
        else if (x2Gap && i >= HF_CPU_GT2 && i <= HF_CPU_EQ2) vals[i] = 0.0f;         // X2 meters (outputs)
        else if (rnGap && i >= rnAt && i < rnEnd)        vals[i] = 0.0f;              // Amp 2 NAM trims 0 dB
        else if (qwGap && i >= qwAt && i < qwEnd)        vals[i] = 0.0f;              // v40 eco/trims
        else if (vrGap && i >= vrAt && i < vrEnd)        vals[i] = 0.0f;              // v41 variac stock
        else if (agGap && i >= agAt && i < agEnd)        vals[i] = 0.35f;             // v42 EP-3 age
        else if (smGap && i >= smAt && i < smEnd)        vals[i] = 0.0f;              // v43 SIR #34 stock
        else if (rlGap && i == rlAt)                     vals[i] = 0.0f;              // v44 rig-B low cut off
        else                                             vals[i] = old[o++];
    }
}
static void seedFactoryPresets(HexForge* p);   // fwd decl (defined after the factory arrays)
// Layout version the generated hexforge_factory_presets.h rows were captured at.
// MUST be bumped when gen_hexforge_presets.py regenerates the table on a newer
// layout. 30 = the fz_gvol-era 224-port layout.
static constexpr uint32_t kFactoryTableVer = 30;
static void hfSerialize(HexForge* p, std::vector<uint8_t>& blob) {
    auto putBytes = [&](const void* d, size_t n){ const uint8_t* b=(const uint8_t*)d; blob.insert(blob.end(), b, b+n); };
    auto putU32   = [&](uint32_t v){ putBytes(&v, 4); };
    auto putPath  = [&](const char* s){ uint32_t len=(uint32_t)std::strlen(s); putU32(len); putBytes(s, len); };
    putU32(44); putU32(kBanks); putU32(kSlots); putU32(HF_N_PORTS); putU32(kFactoryRev);   // v44: + rb_locut; v43: + SIR #34 pair; v42: + EP-3 Age pair; v41: + Plexi Variac; v40: + eco x4 + Drive 2 NAM; v39: + Amp 2 NAM + Cab 2 IR paths; v38: + X2 clone families; v37: + Cab 2 presence; v31: + 14 CPU meter outputs (tail, pre-MIDI); v30: + fuzz guitar vol; v29: + tremolo shape; v28: + speaker drive; v27: + ambient bloom; v26: + reverb type / room density; v25: + reverb density
    for (int b=0;b<kBanks;++b) for (int s=0;s<kSlots;++s) {
        const Preset& pr = p->presets[b][s];
        putU32(pr.used ? 1u : 0u);
        putBytes(pr.name, sizeof(pr.name));
        putBytes(pr.vals, sizeof(pr.vals));
        putPath(pr.irPath); putPath(pr.ampNamPath); putPath(pr.drNamPath); putPath(pr.cabNamPath);
        putPath(pr.amp2NamPath); putPath(pr.ir2Path);   // v39
        putPath(pr.dr2NamPath);                         // v40
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
    if (ver < 2 || ver > 44) return false;
    const bool migrateOutDb = (ver == 2);
    const bool needMigrate  = (ver < 44);  // ...v44 rb_locut
    getU32(np);
    if (ver == 36 && np >= 318) ver = 37;   // deployed v36 stamps already carry rb_cab2on (318-param layout)
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
        char ir[kPathMax],an[kPathMax],dn[kPathMax],cn[kPathMax],a2[kPathMax],i2[kPathMax],d2n[kPathMax];
        getPath(ir); getPath(an); getPath(dn); getPath(cn);
        a2[0] = i2[0] = d2n[0] = '\0';
        if (ver >= 39) { getPath(a2); getPath(i2); }   // v39: Amp 2 NAM + Cab 2 IR
        if (ver >= 40) getPath(d2n);                   // v40: Drive 2 NAM
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
            std::strncpy(pr.amp2NamPath,a2,kPathMax-1); pr.amp2NamPath[kPathMax-1]='\0';
            std::strncpy(pr.ir2Path,i2,kPathMax-1);     pr.ir2Path[kPathMax-1]='\0';
            std::strncpy(pr.dr2NamPath,d2n,kPathMax-1); pr.dr2NamPath[kPathMax-1]='\0';
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
        schedPath(p, p->amp2NamPath, pr.amp2NamPath, W_NAM_LOAD, 3);
        schedPath(p, p->dr2NamPath, pr.dr2NamPath, W_NAM_LOAD, 4);
        if (std::strcmp(p->ir2Path, pr.ir2Path) != 0) {   // Cab 2 IR: user file or back to the rb_cab built-in
            std::strncpy(p->ir2Path, pr.ir2Path, kPathMax-1); p->ir2Path[kPathMax-1]='\0';
            p->lastCab2Model = -1;                        // forces the run() logic to (re)load next block
        }
        if (p->notify) {   // best-effort UI sync; headless recall still changes sound
            emitApply(p);
            writeFileToNotify(p, p->uris.ir_file, p->irPath);
            writeFileToNotify(p, p->uris.amp_nam, p->ampNamPath);
            writeFileToNotify(p, p->uris.dr_nam,  p->drNamPath);
            writeFileToNotify(p, p->uris.cab_nam, p->cabNamPath);
            writeFileToNotify(p, p->uris.amp2_nam, p->amp2NamPath);
            writeFileToNotify(p, p->uris.ir2_file, p->ir2Path);
            writeFileToNotify(p, p->uris.dr2_nam, p->dr2NamPath);
        }
    }
    emitIndex(p);
}
// Save: overwrite the active slot with the live (edited) settings + current files.
static void psSave(HexForge* p) {
    Preset& pr = p->presets[p->curBank][p->curSlot];
    for (int i = 0; i < HF_N_PORTS; ++i) if (isParamPort(i)) pr.vals[i] = p->eff[i];
    std::strncpy(pr.irPath,     p->irPath,     kPathMax - 1); pr.irPath[kPathMax - 1] = '\0';
    std::strncpy(pr.ir2Path,    p->ir2Path,    kPathMax - 1); pr.ir2Path[kPathMax - 1] = '\0';
    std::strncpy(pr.amp2NamPath, p->amp2NamPath, kPathMax - 1); pr.amp2NamPath[kPathMax - 1] = '\0';
    std::strncpy(pr.dr2NamPath, p->dr2NamPath, kPathMax - 1); pr.dr2NamPath[kPathMax - 1] = '\0';
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
// RT trigger: the actual recall runs at the mute-ramp's zero point (seamless).
static void psRecallRequest(HexForge* p, int bank, int slot) {
    p->swPendBank = bank; p->swPendSlot = slot; p->swPendRecall = true;
    if (p->swFadeState == 0) p->swFadeState = 1;
}
static void psBankDelta(HexForge* p, int d) { psRecallRequest(p, p->curBank + d, p->curSlot); }
// A footswitch tap. Single tap recalls that slot immediately (no delay).
// Double-tapping A or D within ~0.4 s navigates banks — A = down, D = up — and
// lands on that same slot letter in the new bank.
static void psSwitchPress(HexForge* p, int sw) {
    const int64_t now = p->sampleClock;
    const int64_t win = static_cast<int64_t>(p->rate * 0.4);
    const bool dbl = (now - p->lastTapSample[sw]) < win;
    p->lastTapSample[sw] = now;
    if      (sw == 0 && dbl) psRecallRequest(p, p->curBank - 1, 0);   // double-tap A → bank down, slot A
    else if (sw == 3 && dbl) psRecallRequest(p, p->curBank + 1, 3);   // double-tap D → bank up, slot D
    else                     psRecallRequest(p, p->curBank, sw);      // single tap → recall this slot
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
{ 0, 0, 0, 0, 0, 0, 0, -23.5, 0, 1, 0, 0, 1, 1, 0, -60, 5, 50, 100, 6, 2, 0, 0, -20, 1, 5, 5, 3, 0, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 1, 0, 0.19, 0.5, 0.58, 1, 0.3, 5, 1, 0, 0.5775, 0.615, 0.635, 0.605, 0.5, 0.7525, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 16000, 1, 7, 0, 0, 0.5, 0.5, 0.5, 0.5, 8, 1, 2, 250, 0.21315, 0.3, 0.5, 0.003, 0.001, 3, 9, 1, 10, 1.5, 0.3, 0, 0.01, 0.335, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
// Crunch
{ 0, 0, 0, 0, 0, 0, 0, -20.2, 0, 1, 0, 0, 1, 1, 1, -60, 5, 50, 100, 6, 2, 1, 0, -20, 1, 5, 5, 3, 0, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 0, 0, 0.145, 0.555, 0.6525, 1, 0.3, 5, 1, 1, 0.2275, 0.365, 0.6825, 0.66, 0.3975, 0.495, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 8660, 1, 7, 0, 0, 0.5, 0.5, 0.5, 0.5, 8, 0, 0, 250, 0.4, 0.3, 0.5, 0.003, 0.001, 10, 9, 1, 10, 1.5, 0.3, 0.5, 0.8, 0.3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
// Rhythm
{ 0, 0, 0, 0, 0, 0, 0, -20.5, 0, 1, 0, 1, 1, 1, 1, -52, 0.1, 101.25, 213.975, 6, 2, 1, 0, -20, 0, 1.725, 6.85, 3, 2.05, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 1, 0, 0.02, 0.5775, 1, 1, 0.3, 5, 1, 2, 0.6225, 0.415, 0.755, 0.72, 0.5, 0.3525, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 8705, 1, 7, 0, 0, 0.5, 0.5, 0.5, 0.5, 8, 0, 0, 250, 0.4, 0.3, 0.5, 0.003, 0.001, 10, 9, 1, 10, 1.5, 0.3, 0.0175, 0.01, 0.1175, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
// Lead
{ 0, 0, 0, 0, 0, 0, 0, -21.3, 0, 1, 0, 1, 1, 1, 1, -61.6, 0.1, 67.5, 238.85, 6, 2, 1, 0, -18, 0, 2.775, 6.175, 3, 0.55, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 1, 0, 0.0625, 0.6, 0.7175, 1, 0.3, 5, 1, 4, 0.6075, 0.45, 0.7475, 0.7675, 0.6025, 0.47, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 9470, 1, 7, 1, 0, 0.12, 0.6125, 0.5, 0.5, 8, 1, 0, 445.777, 0.31605, 0.17, 0.5, 0.003, 0.001, 10, 9, 1, 10, 1.5, 0.3, 0, 0.01, 0.1475, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
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
        // Fidelity polish (2026-07-23): speaker coupling + pickup load per stock rig
        // (Clean=Fender, Crunch=JCM800, Rhythm=EVH tight, Lead=Orange).
        static const float kSeedCoupl[kSlots] = {0.20f, 0.30f, 0.10f, 0.25f};
        static const float kSeedLoad [kSlots] = {0.12f, 0.08f, 0.00f, 0.05f};
        pr.vals[HF_AMP_PAMP_COUPL] = kSeedCoupl[s];
        pr.vals[HF_IT_LOAD]        = kSeedLoad[s];
        pr.vals[HF_RV_DENSITY]     = 1.0f;   // dense tank everywhere (user 2026-07-23)
        pr.vals[HF_CAB_ROOMDENSE]  = 1.0f;   // dense cab room everywhere (user 2026-07-23)
        pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0] = pr.amp2NamPath[0] = pr.ir2Path[0] = pr.dr2NamPath[0] = '\0';
    }
    // Band/song presets (generated in the current layout).
    for (int i = 0; i < kFactoryExtraCount; ++i) {
        const HfFactoryPreset& fp = kFactoryExtra[i];
        if (fp.bank < 0 || fp.bank >= kBanks || fp.slot < 0 || fp.slot >= kSlots) continue;
        Preset& pr = p->presets[fp.bank][fp.slot];
        pr.used = true;
        std::snprintf(pr.name, sizeof(pr.name), "%s", fp.name);
        std::memcpy(pr.vals, fp.vals, sizeof(pr.vals));
        // The kFactoryExtra rows are literals captured at their GENERATION layout
        // (v30, 224 ports). Ports appended before HF_SW_A since then shift the
        // command/meter tail onto the new params -- a raw memcpy hands every
        // factory preset garbage for quality/dr2/rig-B values (found 2026-07-30:
        // rig B silent everywhere because rb_master inherited a stale zero).
        // Migrate the row like any old store blob.
        migratePorts(pr.vals, kFactoryTableVer);
        pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0] = pr.amp2NamPath[0] = pr.ir2Path[0] = pr.dr2NamPath[0] = '\0';
        if (fp.cabIr && fp.cabIr[0]) { std::strncpy(pr.irPath, fp.cabIr, kPathMax-1); pr.irPath[kPathMax-1]='\0'; }
    }
    // DUAL RIGS (rev 78, 2026-07-30 user request: "figure out which ones would
    // benefit from dual amps and cabs"): presets whose documented artist rigs ran
    // multiple amps at once gain a parallel Amp 2/Cab 2 layer -- Muse (Bellamy's
    // clean AC30 under the fuzz rig), Gilmour (Hiwatt DR103 + Fender Twin),
    // QOTSA (solid-state + tube blend -> Orange layer), NIN Fragile-era (Sunn
    // low wall), Mastodon (Marshall + Orange backline). PHASE + PARITY measured
    // on-device per preset (rig_meas chord-burst RMS + rig_dbg wiggle passes,
    // 2026-07-30): the Desert Robot Orange layer reinforces INVERTED; the
    // Fender-under-Hiwatt pair blends at NORMAL polarity (the first measurement
    // hit the zero-write port trap -- rb_amp=0 never landed -- and measured a
    // Crunchy B side; inverted CANCELS with the real Clean Meanie: -9 dB). dOut
    // = measured out_level adjustment restoring RMS parity (+-0.2 dB).
    {
        struct RbSeed { int bank, slot;
                        float amp, gain, bass, mid, treble, pres, master, sag, cab, blend, pol, dOut; };
        static const RbSeed kRbSeeds[] = {
            {14,0,  8, 0.32f, 0.50f, 0.55f, 0.60f, 0.50f, 0.75f, 0.40f, 1, 0.30f, 0, +2.9f},  // Plug-In Junior: clean Chime Thirty under the FF
            {14,1,  8, 0.30f, 0.50f, 0.55f, 0.60f, 0.50f, 0.75f, 0.40f, 1, 0.25f, 0, +2.1f},  // Knights of Fuzz: same, subtler (user's build -- A side untouched)
            { 4,0,  0, 0.28f, 0.55f, 0.45f, 0.60f, 0.55f, 0.80f, 0.30f, 2, 0.35f, 0,  0.0f},  // Dark Side Air: Twin under the Hiwatt (normal pol; re-measured with the REAL Clean Meanie B)
            { 4,2,  0, 0.30f, 0.50f, 0.45f, 0.55f, 0.50f, 0.80f, 0.30f, 2, 0.25f, 0, +1.2f},  // Numb Sustain: Twin body under the lead (normal pol, re-measured)
            { 9,2,  4, 0.50f, 0.60f, 0.55f, 0.45f, 0.40f, 0.70f, 0.45f, 0, 0.30f, 1, +2.3f},  // Desert Robot: Orange girth under the Backline SS
            {11,1,  3, 0.55f, 0.65f, 0.40f, 0.40f, 0.40f, 0.70f, 0.50f, 5, 0.28f, 0, +2.0f},  // World Went Away: Sunn low wall (Fragile layering)
            { 6,0,  4, 0.48f, 0.50f, 0.60f, 0.50f, 0.40f, 0.70f, 0.40f, 0, 0.25f, 0,  0.0f},  // Skye Crusher: Orange beside the JCM800 (coherent, parity exact)
            // Round 2 (rev 80, user: "the ghost and periphery presets"; + Skye (No Mod) per user):
            { 3,0,  4, 0.50f, 0.55f, 0.50f, 0.50f, 0.45f, 0.70f, 0.40f, 0, 0.30f, 0, -1.2f},  // Imperial Rhythm: Orange under the Friedman (coherent: 10.9 dB pol swing)
            { 3,1,  4, 0.45f, 0.55f, 0.50f, 0.50f, 0.45f, 0.70f, 0.40f, 0, 0.28f, 1, +1.1f},  // Imperial Lead: same pair -- but the lead's pedal chain flips the net phase (inverted reinforces)
            { 7,2,  0, 0.30f, 0.30f, 0.50f, 0.60f, 0.50f, 0.80f, 0.15f, 0, 0.20f, 1, +1.7f},  // Flatliner: djent parallel CLEAN blend (tight low-bass Fender for pick attack)
            { 7,3,  0, 0.30f, 0.30f, 0.50f, 0.60f, 0.50f, 0.80f, 0.15f, 0, 0.20f, 1, +1.7f},  // Prayer Djent: same clean blend
            {10,2,  0, 0.30f, 0.50f, 0.45f, 0.60f, 0.55f, 0.80f, 0.30f, 2, 0.30f, 0, +2.0f},  // Streets Chime: the Edge's Fender side beside the AC30 (dOut covers amp2 + dl2 jointly)
            { 6,1,  4, 0.65f, 0.50f, 0.60f, 0.50f, 0.40f, 0.70f, 0.40f, 0, 0.25f, 0, -0.9f},  // Skye (No Mod): CRUNCHY Orange layer (rev 83 -- a clean layer diluted the riffs) + hotter A gain
            // Round 3 (rev 82, user: "the cardinal presets too, those are ghost"):
            { 3,2,  4, 0.62f, 0.55f, 0.50f, 0.50f, 0.45f, 0.75f, 0.40f, 0, 0.25f, 1, -1.1f},  // Cardinal Rhythm: CRUNCHY Orange under the Cali V (inverted; user dial-in untouched)
            { 3,3,  4, 0.60f, 0.55f, 0.50f, 0.50f, 0.45f, 0.70f, 0.40f, 0, 0.28f, 1, +1.8f},  // Cardinal Lead: CRUNCHY Orange under the Friedman (inverted)
        };
        for (const RbSeed& rs : kRbSeeds) {
            Preset& pr = p->presets[rs.bank][rs.slot];
            if (!pr.used) continue;
            pr.vals[HF_RB_ENABLE]   = 1.0f;
            pr.vals[HF_RB_AMP]      = rs.amp;
            pr.vals[HF_RB_GAIN]     = rs.gain;
            pr.vals[HF_RB_BASS]     = rs.bass;
            pr.vals[HF_RB_MID]      = rs.mid;
            pr.vals[HF_RB_TREBLE]   = rs.treble;
            pr.vals[HF_RB_PRESENCE] = rs.pres;
            pr.vals[HF_RB_MASTER]   = rs.master;
            pr.vals[HF_RB_SAG]      = rs.sag;
            pr.vals[HF_RB_CAB2ON]   = 1.0f;
            pr.vals[HF_RB_CAB]      = rs.cab;
            pr.vals[HF_RB_BLEND]    = rs.blend;
            pr.vals[HF_RB_LEVEL]    = 0.0f;
            pr.vals[HF_RB_POL]      = rs.pol;
            pr.vals[HF_OUT_LEVEL]  += rs.dOut;   // measured loudness-parity compensation
        }
        // X2 SECOND DELAYS (rev 80): documented two-delay rigs. Slotted at the same
        // pos as Delay 1 -- the canonical tie-break runs dl2 right after dl.
        struct Dl2Seed { int bank, slot; float time, fb, mix; };
        static const Dl2Seed kDl2Seeds[] = {
            {10, 2, 478.0f, 0.35f, 0.22f},   // Streets Chime: Edge's second delay (quarter note at 125.5 BPM beside the 359 ms dotted eighth)
            { 4, 2, 380.0f, 0.30f, 0.14f},   // Numb Sustain: second rack delay under the lead (parity-neutral, +0.05 dB measured)
        };
        // Skye (No Mod) = the RIFF preset (user 2026-07-30: "add more gain...
        // it should be for the riffs"): JCM800 gain 0.62 -> 0.72 (the same step
        // as the rev-34 ear-pass), out_level re-measured for parity below.
        p->presets[6][1].vals[HF_AMP_GAIN] = 0.82f;   // rev 83: "too clean... heavy mastodon riffs"
        for (const Dl2Seed& ds : kDl2Seeds) {
            Preset& pr = p->presets[ds.bank][ds.slot];
            if (!pr.used) continue;
            pr.vals[HF_DL2_ENABLE]   = 1.0f;
            pr.vals[HF_DL2_POS]      = pr.vals[HF_DL_POS];
            pr.vals[HF_DL2_TYPE]     = 0.0f;    // Digital
            pr.vals[HF_DL2_TIME]     = ds.time;
            pr.vals[HF_DL2_FEEDBACK] = ds.fb;
            pr.vals[HF_DL2_MIX]      = ds.mix;
            pr.vals[HF_DL2_WIDTH]    = 0.6f;
            pr.vals[HF_DL2_BYPASS]   = 0.0f;
        }
    }

    // 2026-07-13 (rev 27): packed the collection so no populated bank before the last has blank slots (user
    // request). Banks 0-13 are now FULL; the MUSE bank (index 14) is the LAST populated bank and holds only
    // A/B (Plug In Baby + Knights of Cydonia); index 15 was emptied (its Muse presets moved to 14). Clear the
    // now-vacated factory slots so stale presets from older revs don't linger (a user is extremely unlikely
    // to have saved over former-factory slots).
    // BANK 18 (index 17) -- CLASSIC ROCK / METAL (rev 91, user request):
    // A Frayed Justice (Metallica ...And Justice for All: Cali V IIC+ deep-V
    //   GEQ scoop, tight gate, V30s, famously DRY), B Brown Sound '84 (Van
    //   Halen: Plexi + VARIAC, Vol I dimed + jumpered Vol II, Phase 90 IN
    //   FRONT of the amp, tape echo after the cab, Greenbacks), C Acca Dacca
    //   (AC/DC: non-master Plexi at the edge, jumpered, bright, dry, no
    //   pedals), D Jungle Sleaze (GnR Appetite: the S.I.R.-modded 1959 ~= our
    //   JCM800, mid-forward, Greenbacks, darker top, small plate).
    // Donor row = stock Crunch (migrated, simple chain), every optional block
    // reset OFF, then per-preset builds. out_levels measured on-device.
    {
        auto mk = [&](int slot, const char* name) -> Preset& {
            Preset& pr = p->presets[17][slot];
            pr = p->presets[0][1];
            pr.used = true;
            std::snprintf(pr.name, sizeof(pr.name), "%s", name);
            pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0]
                = pr.amp2NamPath[0] = pr.ir2Path[0] = pr.dr2NamPath[0] = '\0';
            static const int offs[] = { HF_CP_ENABLE, HF_FZ_ENABLE, HF_DR_ENABLE,
                HF_MD_ENABLE, HF_DL_ENABLE, HF_RV_ENABLE, HF_WH_ENABLE, HF_OC_ENABLE,
                HF_NAIL_ENABLE, HF_EQ_ENABLE, HF_DR2_ENABLE, HF_RB_ENABLE, HF_RB_CAB2ON,
                HF_GT2_ENABLE, HF_CP2_ENABLE, HF_FZ2_ENABLE, HF_NAIL2_ENABLE, HF_MD2_ENABLE,
                HF_DL2_ENABLE, HF_RV2_ENABLE, HF_WH2_ENABLE, HF_OC2_ENABLE, HF_EQ2_ENABLE };
            for (int o : offs) pr.vals[o] = 0.0f;
            pr.vals[HF_AMP_PL_VARIAC] = 0.0f; pr.vals[HF_AMP_PL_VOL2] = 0.0f;
            pr.vals[HF_CAB_MICPOS] = 0.0f; pr.vals[HF_CAB_MICDIST] = 0.0f;
            pr.vals[HF_CAB_ROOMON] = 1.0f; pr.vals[HF_CAB_ROOMMIX] = 0.10f;
            pr.vals[HF_CAB_ROOMAMT] = 0.35f; pr.vals[HF_CAB_ROOMDENSE] = 1.0f;
            pr.vals[HF_RV_DENSITY] = 1.0f;
            pr.vals[HF_OUT_LEVEL] = -16.0f;   // provisional, measured below
            return pr;
        };
        {   // A: Frayed Justice
            Preset& pr = mk(0, "Frayed Justice");
            pr.vals[HF_GT_THRESH] = -42.0f; pr.vals[HF_GT_ATTACK] = 1.0f;
            pr.vals[HF_GT_HOLD] = 120.0f; pr.vals[HF_GT_RELEASE] = 200.0f;
            pr.vals[HF_AMP_MODEL] = 11.0f; pr.vals[HF_AMP_GAIN] = 0.52f;
            pr.vals[HF_AMP_BASS] = 0.08f; pr.vals[HF_AMP_MID] = 0.5f;
            pr.vals[HF_AMP_TREBLE] = 0.40f; pr.vals[HF_AMP_PRESENCE] = 0.45f;
            pr.vals[HF_AMP_MASTER] = 0.6f; pr.vals[HF_AMP_SAG] = 0.2f;
            pr.vals[HF_AMP_MV_MODE] = 6.0f;   // IIC+
            pr.vals[HF_AMP_MV_GEQ0] = 0.75f; pr.vals[HF_AMP_MV_GEQ1] = 0.54f;
            pr.vals[HF_AMP_MV_GEQ2] = 0.08f; pr.vals[HF_AMP_MV_GEQ3] = 0.42f;
            pr.vals[HF_AMP_MV_GEQ4] = 0.75f;   // deep-V faders (mirrors preset 3)
            pr.vals[HF_AMP_MV_EQPRESET] = 3.0f;
            pr.vals[HF_CAB_LOWCUT] = 95.0f; pr.vals[HF_CAB_HIGHCUT] = 7800.0f;
            pr.vals[HF_CAB_ROOMMIX] = 0.06f;   // AJFA is DRY
        }
        {   // B: Brown Sound '84
            Preset& pr = mk(1, "Brown Sound '84");
            // Second pass (user: "try another pass at the van halen tone"):
            // MORE amp, LESS top -- gain/Vol II up, sag deeper (the variac'd
            // supply moving under the note is the brown in brown sound), the
            // treble/presence tilt inverted (treble down .55, presence UP .65
            // -- warm plates, present cab), mids pushed .68. The Phase 90 is
            // a real 100%-wet script pedal: mix up .45, rate slowed .35.
            // Tape echo eased to .15 so the swirl leads, not the repeats.
            pr.vals[HF_GT_THRESH] = -48.0f;
            // Round 3 (user: "the plexi seems short on gain"): the model is
            // capture-anchored to a REAL 1959, which is short on gain for VH
            // by itself -- Ed's push was the Echoplex EP-3 preamp slamming
            // the front end. Preamp 250 as the EP-3 stand-in: barely any
            // drive of its own, level .78 hot into V1.
            pr.vals[HF_DR_ENABLE] = 1.0f; pr.vals[HF_DR_MODEL] = 7.0f;
            pr.vals[HF_DR_DRIVE] = 0.25f; pr.vals[HF_DR_TONE] = 0.55f;
            pr.vals[HF_DR_LEVEL] = 0.78f;
            pr.vals[HF_AMP_MODEL] = 10.0f; pr.vals[HF_AMP_GAIN] = 0.95f;
            pr.vals[HF_AMP_PL_VOL2] = 0.50f; pr.vals[HF_AMP_PL_VARIAC] = 1.0f;
            pr.vals[HF_AMP_BASS] = 0.50f; pr.vals[HF_AMP_MID] = 0.68f;
            pr.vals[HF_AMP_TREBLE] = 0.55f; pr.vals[HF_AMP_PRESENCE] = 0.65f;
            pr.vals[HF_AMP_MASTER] = 0.90f; pr.vals[HF_AMP_SAG] = 0.50f;
            pr.vals[HF_MD_ENABLE] = 1.0f; pr.vals[HF_MD_POS] = 3.0f;   // Phase 90 IN FRONT
            pr.vals[HF_MD_TYPE] = 2.0f; pr.vals[HF_MD_RATE] = 0.35f;
            pr.vals[HF_MD_DEPTH] = 0.50f; pr.vals[HF_MD_MIX] = 0.45f;
            pr.vals[HF_DL_ENABLE] = 1.0f; pr.vals[HF_DL_TYPE] = 1.0f;   // tape echo
            pr.vals[HF_DL_TIME] = 310.0f; pr.vals[HF_DL_FEEDBACK] = 0.32f;
            pr.vals[HF_DL_MIX] = 0.15f; pr.vals[HF_DL_WIDTH] = 0.55f;
            pr.vals[HF_RV_ENABLE] = 1.0f; pr.vals[HF_RV_TYPE] = 0.0f;
            pr.vals[HF_RV_MIX] = 0.14f; pr.vals[HF_RV_DECAY] = 1.6f;
            pr.vals[HF_RV_PREDELAY] = 15.0f;
            pr.vals[HF_CAB_LOWCUT] = 85.0f; pr.vals[HF_CAB_HIGHCUT] = 8000.0f;
            pr.vals[HF_CAB_MICPOS] = 0.10f; pr.vals[HF_CAB_MICDIST] = 0.10f;
            pr.vals[HF_CAB_ROOMMIX] = 0.12f;
        }
        {   // C: Acca Dacca
            Preset& pr = mk(2, "Sister's Singer");
            pr.vals[HF_GT_THRESH] = -50.0f;
            pr.vals[HF_AMP_MODEL] = 10.0f; pr.vals[HF_AMP_GAIN] = 0.55f;
            pr.vals[HF_AMP_PL_VOL2] = 0.35f;
            pr.vals[HF_AMP_BASS] = 0.5f; pr.vals[HF_AMP_MID] = 0.58f;
            pr.vals[HF_AMP_TREBLE] = 0.60f; pr.vals[HF_AMP_PRESENCE] = 0.6f;
            pr.vals[HF_AMP_MASTER] = 0.80f; pr.vals[HF_AMP_SAG] = 0.30f;
            pr.vals[HF_CAB_LOWCUT] = 90.0f; pr.vals[HF_CAB_HIGHCUT] = 8000.0f;
        }
        {   // D: Jungle Sleaze
            Preset& pr = mk(3, "Jungle Sleaze");
            // Rev 92 (user): this slot IS the Welcome to the Jungle DELAY
            // INTRO, not the rhythm tone -- gain eased to .45 (driven single
            // notes, not the wall), and the famous cascading echo: clean
            // DIGITAL repeats at 470 ms (~quarter note at the song's ~124
            // bpm), feedback .55 for the 4-5 trailing images, mix .42 so the
            // echoes answer at nearly full voice. Small plate behind it.
            pr.vals[HF_GT_THRESH] = -48.0f;
            pr.vals[HF_AMP_MODEL] = 1.0f; pr.vals[HF_AMP_GAIN] = 0.45f;
            pr.vals[HF_AMP_BASS] = 0.55f; pr.vals[HF_AMP_MID] = 0.62f;
            pr.vals[HF_AMP_TREBLE] = 0.55f; pr.vals[HF_AMP_PRESENCE] = 0.5f;
            pr.vals[HF_AMP_MASTER] = 0.70f; pr.vals[HF_AMP_SAG] = 0.35f;
            pr.vals[HF_DL_ENABLE] = 1.0f; pr.vals[HF_DL_TYPE] = 0.0f;   // clean digital
            pr.vals[HF_DL_TIME] = 470.0f; pr.vals[HF_DL_FEEDBACK] = 0.55f;
            pr.vals[HF_DL_MIX] = 0.42f; pr.vals[HF_DL_WIDTH] = 0.45f;
            pr.vals[HF_CAB_LOWCUT] = 85.0f; pr.vals[HF_CAB_HIGHCUT] = 7500.0f;
            pr.vals[HF_CAB_MICPOS] = 0.15f; pr.vals[HF_CAB_MICDIST] = 0.10f;
            pr.vals[HF_RV_ENABLE] = 1.0f; pr.vals[HF_RV_TYPE] = 0.0f;
            pr.vals[HF_RV_MIX] = 0.12f; pr.vals[HF_RV_DECAY] = 1.4f;
        }
        // Cab IRs: Greenbacks for the Marshalls, factory V30 for the Mesa.
        std::strncpy(p->presets[17][1].irPath, "@greenback", kPathMax-1);
        std::strncpy(p->presets[17][2].irPath, "@greenback", kPathMax-1);
        std::strncpy(p->presets[17][3].irPath, "@greenback", kPathMax-1);
        // Loudness parity, measured on-device vs the Bank-1 Crunch anchor
        // (chord-burst domain: anchor -16.8): all four seeded hot, trimmed to it.
        p->presets[17][0].vals[HF_OUT_LEVEL] = -19.0f;
        p->presets[17][1].vals[HF_OUT_LEVEL] = -22.7f;   // re-trimmed after the EP-3 boost (round 3, clean-HOME measured)
        p->presets[17][2].vals[HF_OUT_LEVEL] = -19.4f;
        p->presets[17][3].vals[HF_OUT_LEVEL] = -18.0f;   // re-trimmed for the delay intro
        // USER FINAL Frayed Justice (rev 92, 2026-07-31, store-diffed
        // on-device -- DO NOT RETUNE): the user's rework, baked verbatim.
        // Chain reordered amp-first (amp 2 / cab 3 / EQ 4 / reverb 5),
        // treble dumped to .17 with presence up, the Mid-Boost EQ curve
        // post-cab, Speaker Drive Full, a touch of plate -- and a full B
        // rig: second Cali V in mode 7 with its own GEQ hump + Greenback
        // Cab 2, blended .60. (Cali V is untouched by the spkdrive loop,
        // so the baked cab_spkdrive survives.)
        {
            Preset& fj = p->presets[17][0];
            fj.vals[HF_CP_POS] = 6.0f;  fj.vals[HF_FZ_POS] = 7.0f;
            fj.vals[HF_DR_POS] = 8.0f;  fj.vals[HF_AMP_POS] = 2.0f;
            fj.vals[HF_AMP_BASS] = 0.09f;     fj.vals[HF_AMP_MID] = 0.54f;
            fj.vals[HF_AMP_TREBLE] = 0.1725f; fj.vals[HF_AMP_PRESENCE] = 0.55f;
            fj.vals[HF_CAB_POS] = 3.0f; fj.vals[HF_MD_POS] = 9.0f;
            fj.vals[HF_DL_POS] = 10.0f; fj.vals[HF_RV_POS] = 5.0f;
            fj.vals[HF_RV_ENABLE] = 1.0f; fj.vals[HF_RV_MIX] = 0.2075f;
            fj.vals[HF_WH_POS] = 11.0f; fj.vals[HF_OC_POS] = 12.0f;
            fj.vals[HF_NAIL_POS] = 13.0f; fj.vals[HF_EQ_POS] = 4.0f;
            fj.vals[HF_EQ_ENABLE] = 1.0f; fj.vals[HF_EQ_PRESET] = 4.0f;
            fj.vals[HF_EQ_100] = 4.0f;  fj.vals[HF_EQ_200] = -2.0f;
            fj.vals[HF_EQ_400] = -5.0f; fj.vals[HF_EQ_800] = -2.0f;
            fj.vals[HF_EQ_1K6] = 2.0f;  fj.vals[HF_EQ_3K2] = 4.0f;
            fj.vals[HF_CAB_SPKDRIVE] = 2.0f;
            fj.vals[HF_RB_ENABLE] = 1.0f; fj.vals[HF_RB_AMP] = 11.0f;
            fj.vals[HF_RB_GAIN] = 0.335f;   fj.vals[HF_RB_BASS] = 0.6875f;
            fj.vals[HF_RB_MID] = 0.665f;    fj.vals[HF_RB_TREBLE] = 0.6425f;
            fj.vals[HF_RB_BLEND] = 0.6025f; fj.vals[HF_RB_LEVEL] = 0.06f;
            fj.vals[HF_RB_MV_MODE] = 7.0f;
            fj.vals[HF_RB_MV_GEQ0] = 0.416667f; fj.vals[HF_RB_MV_GEQ1] = 0.583333f;
            fj.vals[HF_RB_MV_GEQ2] = 0.708333f; fj.vals[HF_RB_MV_GEQ3] = 0.625f;
            fj.vals[HF_RB_MV_GEQ4] = 0.458333f;
            fj.vals[HF_RB_CABROOMDENSE] = 1.0f; fj.vals[HF_RB_CABSPKDRIVE] = 2.0f;
            fj.vals[HF_RB_CAB2ON] = 1.0f;
            std::strncpy(fj.ir2Path, "@greenback", kPathMax-1);
            fj.ir2Path[kPathMax-1] = '\0';
        }
        // USER FINAL B/C/D (rev 94, 2026-07-31, store-diffed on-device at
        // exact float32 precision -- DO NOT RETUNE):
        //  - Brown Sound '84: fully re-staged -- Input Trim boost +7.05 dB
        //    into the EP-3 drive eased to .135, amp gain DOWN .73 with
        //    treble/master DIMED + presence .9975 (the real dimed-Plexi
        //    move: gain from the front, top from the tone stack), Vol II
        //    OUT, sag .34, phaser slowed to .13 mono, echo up .22, an EQ
        //    parked bypassed at pos 8.
        //  - Sister's Singer: everything on 10 -- gain 1.0 / Vol II 1.0 /
        //    master 1.0, bass down .33, Deep-V-based chain EQ pre-cab,
        //    gate moved post-EQ and switched OFF (bypassed).
        //  - Jungle Sleaze: the intro goes hotter -- donor Green Man boost
        //    ON dimed (.9975) into gain .77, master .825, mic pushed in.
        {
            Preset& bs = p->presets[17][1];
            bs.vals[HF_IT_BOOST] = 1.0f;    bs.vals[HF_IT_BOOSTAMT] = 7.05f;
            bs.vals[HF_CP_POS] = 9.0f;      bs.vals[HF_FZ_POS] = 10.0f;
            bs.vals[HF_DR_POS] = 3.0f;      bs.vals[HF_DR_DRIVE] = 0.135f;
            bs.vals[HF_DR_TONE] = 0.5425f;  bs.vals[HF_DR_LEVEL] = 0.775f;
            bs.vals[HF_AMP_POS] = 4.0f;     bs.vals[HF_AMP_GAIN] = 0.7325f;
            bs.vals[HF_AMP_BASS] = 0.3775f; bs.vals[HF_AMP_MID] = 0.7775f;
            bs.vals[HF_AMP_TREBLE] = 1.0f;  bs.vals[HF_AMP_PRESENCE] = 0.9975f;
            bs.vals[HF_AMP_MASTER] = 1.0f;  bs.vals[HF_AMP_SAG] = 0.3425f;
            bs.vals[HF_CAB_POS] = 5.0f;     bs.vals[HF_MD_POS] = 2.0f;
            bs.vals[HF_MD_RATE] = 0.1325f;  bs.vals[HF_MD_DEPTH] = 0.3325f;
            bs.vals[HF_MD_MIX] = 0.4975f;   bs.vals[HF_MD_WIDTH] = 0.0f;
            bs.vals[HF_MD_TYPE] = 8.0f;     // user swapped Phaser -> Script Phaser (no-feedback P90), 2026-07-31
            bs.vals[HF_DL_POS] = 6.0f;      bs.vals[HF_DL_MIX] = 0.22f;
            bs.vals[HF_RV_POS] = 7.0f;      bs.vals[HF_WH_POS] = 11.0f;
            bs.vals[HF_OC_POS] = 12.0f;     bs.vals[HF_NAIL_POS] = 13.0f;
            bs.vals[HF_AMP_PL_VOL2] = 0.0f; bs.vals[HF_EQ_POS] = 8.0f;
            bs.vals[HF_EQ_PRESET] = 1.0f;   bs.vals[HF_EQ_100] = 3.0f;
            bs.vals[HF_EQ_400] = -1.0f;     bs.vals[HF_EQ_800] = 2.0f;
            bs.vals[HF_EQ_1K6] = 1.0f;      bs.vals[HF_EQ_3K2] = 2.0f;
            bs.vals[HF_EQ_BYPASS] = 1.0f;   bs.vals[HF_CAB_SPKDRIVE] = 2.0f;
            Preset& ss = p->presets[17][2];
            ss.vals[HF_GT_POS] = 4.0f;      ss.vals[HF_GT_ENABLE] = 0.0f;
            ss.vals[HF_GT_BYPASS] = 1.0f;
            ss.vals[HF_CP_POS] = 5.0f;      ss.vals[HF_FZ_POS] = 6.0f;
            ss.vals[HF_DR_POS] = 7.0f;      ss.vals[HF_AMP_POS] = 1.0f;
            ss.vals[HF_AMP_GAIN] = 1.0f;    ss.vals[HF_AMP_BASS] = 0.33f;
            ss.vals[HF_AMP_MID] = 0.635f;   ss.vals[HF_AMP_TREBLE] = 0.63f;
            ss.vals[HF_AMP_PRESENCE] = 0.6475f; ss.vals[HF_AMP_MASTER] = 1.0f;
            ss.vals[HF_CAB_POS] = 2.0f;     ss.vals[HF_MD_POS] = 8.0f;
            ss.vals[HF_DL_POS] = 9.0f;      ss.vals[HF_RV_POS] = 10.0f;
            ss.vals[HF_WH_POS] = 11.0f;     ss.vals[HF_OC_POS] = 12.0f;
            ss.vals[HF_NAIL_POS] = 13.0f;   ss.vals[HF_AMP_PL_VOL2] = 1.0f;
            ss.vals[HF_EQ_POS] = 3.0f;      ss.vals[HF_EQ_ENABLE] = 1.0f;
            ss.vals[HF_EQ_PRESET] = 3.0f;   ss.vals[HF_EQ_100] = 3.0f;
            ss.vals[HF_EQ_200] = 1.0f;      ss.vals[HF_EQ_400] = -3.0f;
            ss.vals[HF_EQ_1K6] = 2.0f;      ss.vals[HF_EQ_3K2] = 4.0f;
            Preset& js = p->presets[17][3];
            js.vals[HF_CP_POS] = 7.0f;      js.vals[HF_FZ_POS] = 8.0f;
            js.vals[HF_DR_POS] = 2.0f;      js.vals[HF_DR_ENABLE] = 1.0f;
            js.vals[HF_DR_DRIVE] = 0.175f;  js.vals[HF_DR_LEVEL] = 0.9975f;
            js.vals[HF_AMP_POS] = 3.0f;     js.vals[HF_AMP_GAIN] = 0.77f;
            js.vals[HF_AMP_BASS] = 0.445f;  js.vals[HF_AMP_MID] = 0.68f;
            js.vals[HF_AMP_TREBLE] = 0.6225f; js.vals[HF_AMP_MASTER] = 0.825f;
            js.vals[HF_CAB_POS] = 4.0f;     js.vals[HF_MD_POS] = 10.0f;
            js.vals[HF_DL_POS] = 5.0f;      js.vals[HF_RV_POS] = 6.0f;
            js.vals[HF_WH_POS] = 11.0f;     js.vals[HF_OC_POS] = 12.0f;
            js.vals[HF_NAIL_POS] = 13.0f;
            js.vals[HF_CAB_MICPOS] = 0.120062f; js.vals[HF_CAB_MICDIST] = 0.052566f;
            js.vals[HF_EQ_POS] = 9.0f;
        }
        // Loudness parity vs the Bank-1 Crunch anchor (rev 95, clean-HOME
        // measured AFTER the user finals; the one deliberate deviation from
        // the verbatim store rows, per the user: "make the new presets have
        // volume parity with our other presets"). All four land -16.8 exact.
        p->presets[17][0].vals[HF_OUT_LEVEL] = -17.3f;
        p->presets[17][1].vals[HF_OUT_LEVEL] = -21.8f;   // +0.2 for variac v2 physics (rev 96)
        p->presets[17][2].vals[HF_OUT_LEVEL] = -23.0f;
        p->presets[17][3].vals[HF_OUT_LEVEL] = -20.6f;
        // USER FINAL round 2 (rev 97, 2026-07-31, store-diffed at exact
        // float32 -- DO NOT RETUNE): the user moved Brown Sound '84 onto the
        // REAL EP-3 system the same day it shipped:
        //  - Echo Primer (dr_model 8) replaces the Preamp 250 stand-in --
        //    level DIMED into the front end, barely any drive (.1375).
        //  - EP-3 Echo (dl_type 4) replaces the generic tape: 241 ms, fb
        //    .08, mix .02, age .02 -- a nearly-invisible fresh-tape slap.
        //  - Rig B = a SECOND variac'd Plexi (rb_amp 10, rb_pl_variac 1,
        //    Vol II cracked .1175, everything dimed, sag .50, +1.02 dB,
        //    blend .5075) into its own close-miked @factory Cab 2.
        //  - A side: gain .84, bass DIMED, mids .9975, sag .70, Vol II .12,
        //    chain EQ moved pre (pos 2, preset 5 curve), IT boost eased to
        //    +2.04, phaser mix down .3025.
        //  - Jungle Sleaze: hotter/brighter amp (gain .805, mid .8725,
        //    treble .7275, presence .66), mic on-cap at the grille, and its
        //    B-side cab prepped (@greenback Cab 2, 10.46k highcut).
        {
            Preset& bs = p->presets[17][1];
            bs.vals[HF_IT_BOOSTAMT] = 2.04f;
            bs.vals[HF_DR_POS] = 4.0f;      bs.vals[HF_DR_MODEL] = 8.0f;
            bs.vals[HF_DR_DRIVE] = 0.1375f; bs.vals[HF_DR_TONE] = 0.5f;
            bs.vals[HF_DR_LEVEL] = 1.0f;
            bs.vals[HF_AMP_POS] = 6.0f;     bs.vals[HF_AMP_GAIN] = 0.84f;
            bs.vals[HF_AMP_BASS] = 1.0f;    bs.vals[HF_AMP_MID] = 0.9975f;
            bs.vals[HF_AMP_SAG] = 0.6975f;  bs.vals[HF_CAB_POS] = 7.0f;
            bs.vals[HF_MD_POS] = 3.0f;      bs.vals[HF_MD_RATE] = 0.125f;
            bs.vals[HF_MD_DEPTH] = 0.35f;   bs.vals[HF_MD_MIX] = 0.3025f;
            bs.vals[HF_DL_POS] = 5.0f;      bs.vals[HF_DL_TYPE] = 4.0f;
            bs.vals[HF_DL_TIME] = 240.88f;  bs.vals[HF_DL_FEEDBACK] = 0.08085f;
            bs.vals[HF_DL_MIX] = 0.02f;     bs.vals[HF_DL_WIDTH] = 0.5f;
            bs.vals[HF_DL_AGE] = 0.02f;     bs.vals[HF_RV_POS] = 8.0f;
            bs.vals[HF_AMP_PL_VOL2] = 0.12f;
            bs.vals[HF_EQ_POS] = 2.0f;      bs.vals[HF_EQ_ENABLE] = 1.0f;
            bs.vals[HF_EQ_PRESET] = 5.0f;   bs.vals[HF_EQ_100] = -1.0f;
            bs.vals[HF_EQ_400] = 1.0f;      bs.vals[HF_EQ_800] = 3.0f;
            bs.vals[HF_EQ_1K6] = 4.0f;      bs.vals[HF_EQ_3K2] = 5.0f;
            bs.vals[HF_EQ_LEVEL] = 0.12f;   bs.vals[HF_EQ_BYPASS] = 0.0f;
            bs.vals[HF_RB_AMP] = 10.0f;     bs.vals[HF_RB_GAIN] = 0.7525f;
            bs.vals[HF_RB_BASS] = 0.985f;   bs.vals[HF_RB_MID] = 1.0f;
            bs.vals[HF_RB_TREBLE] = 1.0f;   bs.vals[HF_RB_PRESENCE] = 1.0f;
            bs.vals[HF_RB_SAG] = 0.5025f;   bs.vals[HF_RB_BLEND] = 0.5075f;
            bs.vals[HF_RB_LEVEL] = 1.02f;   bs.vals[HF_RB_PL_VOL2] = 0.1175f;
            bs.vals[HF_RB_PL_VARIAC] = 1.0f;
            bs.vals[HF_RB_CABMICPOS] = 0.323924f;
            bs.vals[HF_RB_CABMICDIST] = 0.133917f;
            bs.vals[HF_RB_CABROOMDENSE] = 1.0f; bs.vals[HF_RB_CABSPKDRIVE] = 2.0f;
            bs.vals[HF_RB_CAB2ON] = 1.0f;
            std::strncpy(bs.ir2Path, "@factory", kPathMax-1);
            bs.ir2Path[kPathMax-1] = '\0';
            Preset& js = p->presets[17][3];
            js.vals[HF_AMP_GAIN] = 0.805f;  js.vals[HF_AMP_MID] = 0.8725f;
            js.vals[HF_AMP_TREBLE] = 0.7275f; js.vals[HF_AMP_PRESENCE] = 0.66f;
            js.vals[HF_DL_MIX] = 0.40f;
            js.vals[HF_CAB_MICPOS] = 0.176666f; js.vals[HF_CAB_MICDIST] = 0.0f;
            js.vals[HF_RB_HIGHCUT] = 10460.0f;
            js.vals[HF_RB_CABMICPOS] = 0.055056f; js.vals[HF_RB_CABMICDIST] = 1.0f;
            js.vals[HF_RB_CABROOMDENSE] = 1.0f;   js.vals[HF_RB_CABSPKDRIVE] = 2.0f;
            js.vals[HF_RB_CAB2ON] = 1.0f;
            // Loudness parity after the round-2 bake (measured): the EP-3
            // rework sat 2.6 dB under the anchor, Jungle 0.2 over.
            bs.vals[HF_OUT_LEVEL] = -19.2f;
            js.vals[HF_OUT_LEVEL] = -20.8f;
            // USER round 3 (rev 98, 2026-07-31, store-diffed exact float32,
            // DO NOT RETUNE): Jungle Sleaze goes SIR #34 -- the user flipped
            // the new mod ON the hour it shipped (historically right: the
            // Appetite amp WAS an S.I.R. rental) and re-dialed around the
            // bite: mids DOWN .7475 (the mod adds them), gain UP .885,
            // treble .75, presence .55, master .855, Green Man boost eased
            // (.10 drive / .695 level), gate -48.4, echo .385/.505, mic
            // back on-cap, B-side cab mic zeroed.
            js.vals[HF_GT_THRESH] = -48.4f;
            js.vals[HF_DR_DRIVE] = 0.10f;   js.vals[HF_DR_LEVEL] = 0.695f;
            js.vals[HF_AMP_GAIN] = 0.885f;  js.vals[HF_AMP_MID] = 0.7475f;
            js.vals[HF_AMP_TREBLE] = 0.75f; js.vals[HF_AMP_PRESENCE] = 0.55f;
            js.vals[HF_AMP_MASTER] = 0.855f;
            js.vals[HF_DL_MIX] = 0.385f;    js.vals[HF_DL_WIDTH] = 0.505f;
            js.vals[HF_CAB_MICPOS] = 0.120062f; js.vals[HF_CAB_MICDIST] = 0.021277f;
            js.vals[HF_RB_CABMICPOS] = 0.0f;    js.vals[HF_RB_CABMICDIST] = 0.0f;
            js.vals[HF_AMP_SIR34] = 1.0f;
            js.vals[HF_OUT_LEVEL] = -20.3f;   // parity re-trim after the #34 build (was -20.8)
            std::strncpy(js.ir2Path, "@greenback", kPathMax-1);
            js.ir2Path[kPathMax-1] = '\0';
        }
    }

    // Speaker Drive per-amp defaults (rev 71, item #40, user request): applied AFTER
    // every preset is loaded (Bank 1 + extras), keyed on each preset's OWN amp choice
    // (kAmpMap indices) rather than hand-editing hundreds of raw literals. Only the
    // two categories the user specified are touched -- everything else stays Off,
    // matching how it was already seeded above.
    for (int b = 0; b < kBanks; ++b) {
        for (int s = 0; s < kSlots; ++s) {
            Preset& pr = p->presets[b][s];
            if (!pr.used) continue;
            const int ampModel = static_cast<int>(pr.vals[HF_AMP_MODEL] + 0.5f);
            if (ampModel == 0 || ampModel == 8)
                pr.vals[HF_CAB_SPKDRIVE] = 1.0f;   // Subtle: Fender/Clean Meanie, Vox/Chime Thirty
            else if (ampModel == 1 || ampModel == 2 || ampModel == 6 ||
                     ampModel == 12 || ampModel == 13)
                pr.vals[HF_CAB_SPKDRIVE] = 2.0f;   // Full: JCM800, EVH, Friedman, Recto, MT15
        }
    }

    // Fuzz Guitar Vol = FULL on every preset (rev 73, 2026-07-29 hotfix): the
    // factory tables are older-layout literals, so the new trailing FZ_GVOL
    // port ZERO-FILLS -- and unlike every previous appended port its default
    // is 1.0, not 0. Zero (clamped 0.05) = guitar volume rolled off = the
    // Tone-Bender-backed pedals (I Know It / Tone Bender) go inaudible --
    // user-reported on I Know It. Absolute assignment (not +=) = idempotent;
    // applied to every used preset since the port didn't exist before, so no
    // preset ever chose a value.
    for (int b = 0; b < kBanks; ++b)
        for (int sl = 0; sl < kSlots; ++sl)
            if (p->presets[b][sl].used) p->presets[b][sl].vals[HF_FZ_GVOL] = 1.0f;

    // JCM800 gain bump (rev 72, user request after the 2026-07-28 fuzzy-fix
    // re-voice): the stage-2 duty-collapse cap tamed that stage's drive
    // contribution, so every FACTORY preset on the JCM800 (amp model 1) gets
    // +0.10 gain to land back at its pre-fix drive feel. Scoped to
    // factory-owned slots ONLY (Bank 1 + kFactoryExtra), applied after their
    // tables are re-copied above, so the += is idempotent across future
    // reseeds -- a blanket all-presets loop like Speaker Drive's above would
    // COMPOUND the += on user-saved slots at every future rev bump (Speaker
    // Drive gets away with it because it assigns absolute values).
    {
        auto bump = [&](Preset& pr) {
            if (!pr.used) return;
            if (static_cast<int>(pr.vals[HF_AMP_MODEL] + 0.5f) == 1)
                pr.vals[HF_AMP_GAIN] = std::min(pr.vals[HF_AMP_GAIN] + 0.10f, 1.0f);
        };
        for (int s = 0; s < kSlots; ++s) bump(p->presets[0][s]);
        for (int i = 0; i < kFactoryExtraCount; ++i) {
            const HfFactoryPreset& fp = kFactoryExtra[i];
            if (fp.bank >= 0 && fp.bank < kBanks && fp.slot >= 0 && fp.slot < kSlots)
                bump(p->presets[fp.bank][fp.slot]);
        }
    }

    // Vox gate floor-compliance round 2 (rev 76, 2026-07-29): the Chime
    // Thirty whine = the gate HYSTERESIS-LATCHED open on the user's real
    // hands-off floor (close = thresh - hyst/2 sat below the post-comb
    // detector floor, so one transient opened it forever and the amp ran the
    // hum partials through +18 dB of bright voicing continuously). Measured:
    // constant -62 dBFS idle output on Regal Sustain (thresh -52). Raise every
    // Vox-model factory preset's threshold so the close point (-48) clears the
    // measured detector floor (~-54) with margin; notes/strums (-6..-25 raw)
    // still open at -40. Absolute clamp = idempotent.
    for (int b = 0; b < kBanks; ++b)
        for (int sl = 0; sl < kSlots; ++sl) {
            Preset& pr = p->presets[b][sl];
            if (!pr.used) continue;
            if (static_cast<int>(pr.vals[HF_AMP_MODEL] + 0.5f) == 8 &&
                pr.vals[HF_GT_THRESH] < -44.0f)
                pr.vals[HF_GT_THRESH] = -44.0f;
        }

    // Bank 1 Crunch = the USER'S on-device rework baked verbatim (rev 75,
    // 2026-07-29; store-diffed against a fresh factory seed on the Pi): the
    // +0.10 bump above still read too tame after the JCM800 re-voice, so they
    // rebuilt the preset — chain reordered to Drive->Amp->Cab->Reverb (comp
    // parked off; fuzz/EQ/mod/delay/wah/oct/nail shifted later), the Green Man
    // engaged as a clean boost (drive .07, level maxed — TS-into-Marshall
    // slam), amp gain .3275->.61 with bass/mid trimmed and master up.
    // Absolute assignments AFTER the JCM800 += bump so the effective values
    // equal their saved store exactly; out_level untouched (theirs matched).
    {
        Preset& pr = p->presets[0][1];
        pr.vals[HF_CP_POS]     = 6.0f;   pr.vals[HF_CP_ENABLE] = 0.0f;
        pr.vals[HF_CP_BYPASS]  = 1.0f;
        pr.vals[HF_FZ_POS]     = 7.0f;
        pr.vals[HF_DR_POS]     = 2.0f;   pr.vals[HF_DR_ENABLE] = 1.0f;
        pr.vals[HF_DR_DRIVE]   = 0.07f;  pr.vals[HF_DR_LEVEL]  = 0.9975f;
        pr.vals[HF_AMP_POS]    = 3.0f;
        pr.vals[HF_AMP_GAIN]   = 0.61f;  pr.vals[HF_AMP_BASS]  = 0.2525f;
        pr.vals[HF_AMP_MID]    = 0.475f; pr.vals[HF_AMP_TREBLE]= 0.67f;
        pr.vals[HF_AMP_MASTER] = 0.6825f;
        pr.vals[HF_CAB_POS]    = 4.0f;   pr.vals[HF_RV_POS]    = 5.0f;
        pr.vals[HF_EQ_POS]     = 8.0f;   pr.vals[HF_MD_POS]    = 9.0f;
        pr.vals[HF_DL_POS]     = 10.0f;  pr.vals[HF_WH_POS]    = 11.0f;
        pr.vals[HF_OC_POS]     = 12.0f;  pr.vals[HF_NAIL_POS]  = 13.0f;
    }
    // USER DIAL-INS BAKED VERBATIM (rev 84, 2026-07-30, store-diffed on-device
    // against a fresh factory seed -- DO NOT RETUNE). The user reworked the
    // rev-83 dual-rig voicings by ear on the device:
    //  - Skye (No Mod): A gain pulled to .8175 (NOTE: the fresh seed measured
    //    0.92 -- the rev-72 JCM800 +0.10 loop stacked on the rev-83 absolute
    //    0.82; this block runs AFTER that loop so the user value is final),
    //    master up .6625; the Orange B becomes a MAJOR voice (blend .57,
    //    +4.14 dB level, tight bass .37, bright, Full spkdrive + dense room).
    //  - Cardinal Rhythm: rb_pol back to NORMAL + blend .3425 / +3.12 dB level
    //    with mid-forward Orange; A-side re-dialed (bass .1925, mid .545,
    //    treble .3975, master .695, gain .15).
    //  - Cardinal Lead: A gain .45, Orange mids .595.
    {
        Preset& sk = p->presets[6][1];   // Skye (No Mod)
        sk.vals[HF_AMP_GAIN] = 0.8175f; sk.vals[HF_AMP_MID] = 0.545f;
        sk.vals[HF_AMP_MASTER] = 0.6625f;
        sk.vals[HF_RB_GAIN] = 0.7325f; sk.vals[HF_RB_BASS] = 0.37f;
        sk.vals[HF_RB_MID] = 0.495f;   sk.vals[HF_RB_TREBLE] = 0.6875f;
        sk.vals[HF_RB_PRESENCE] = 0.5075f;
        sk.vals[HF_RB_BLEND] = 0.57f;  sk.vals[HF_RB_LEVEL] = 4.14f;
        sk.vals[HF_RB_CABROOMDENSE] = 1.0f; sk.vals[HF_RB_CABSPKDRIVE] = 2.0f;
        Preset& cr = p->presets[3][2];   // Cardinal Rhythm
        cr.vals[HF_AMP_GAIN] = 0.15f;   cr.vals[HF_AMP_BASS] = 0.1925f;
        cr.vals[HF_AMP_MID] = 0.545f;   cr.vals[HF_AMP_TREBLE] = 0.3975f;
        cr.vals[HF_AMP_MASTER] = 0.695f;
        cr.vals[HF_RB_GAIN] = 0.68f;    cr.vals[HF_RB_BASS] = 0.44f;
        cr.vals[HF_RB_MID] = 0.70f;     cr.vals[HF_RB_TREBLE] = 0.6125f;
        cr.vals[HF_RB_BLEND] = 0.3425f; cr.vals[HF_RB_LEVEL] = 3.12f;
        cr.vals[HF_RB_POL] = 0.0f;
        Preset& cl = p->presets[3][3];   // Cardinal Lead
        cl.vals[HF_AMP_GAIN] = 0.45f;   cl.vals[HF_RB_MID] = 0.595f;
    }

    // NIN TOPOLOGY UPGRADES (rev 86, 2026-07-30, user: "really nail those NIN
    // presets"). Each closes a DOCUMENTED chain gap (nin-fuzz-eras research):
    //  - March Stabs (TDS): the studio chain was JMP-1 preamp -> Zoom 9030 ->
    //    cab sim. The Nail (Dahnward/Zoom mode) MOVES POST-AMP (amp 5 -> nail 6
    //    -> cab 7), drive eased for the hotter post-amp operating point.
    //  - World Went Away (Fragile): the wall deepens -- Sunn B layer heavier
    //    (blend .34, gain .60) + the Swollen-Pickle-mode Nail drives harder.
    //  - Broken Crush (Broken EP): the 9030's STACKED distortion patch -- a
    //    Grunge DS stage feeds Nail Broke (two chained algorithms, documented).
    //  - Con Molars (With Teeth): Nail's internal cab-sim was DOUBLE-cabbing on
    //    top of the real JCM800+Greenback re-amp; texture -> 0.05 (raw
    //    in-the-box clip), tone up -- the real amp does the documented re-amp.
    // dOut = measured chord-burst parity compensation per preset.
    {
        Preset& ms = p->presets[11][0];   // March Stabs
        ms.vals[HF_NAIL_POS] = 6.0f; ms.vals[HF_CAB_POS] = 7.0f;
        ms.vals[HF_NAIL_DRIVE] = 0.55f; ms.vals[HF_NAIL_LEVEL] = 0.5f;
        ms.vals[HF_OUT_LEVEL] += 1.9f;
        Preset& ww = p->presets[11][1];   // World Went Away
        ww.vals[HF_RB_BLEND] = 0.34f; ww.vals[HF_RB_GAIN] = 0.60f;
        ww.vals[HF_NAIL_DRIVE] = 0.78f;
        ww.vals[HF_OUT_LEVEL] += 0.2f;
        Preset& bc = p->presets[11][2];   // Broken Crush
        bc.vals[HF_DR_ENABLE] = 1.0f; bc.vals[HF_DR_MODEL] = 4.0f;   // Grunge DS
        bc.vals[HF_DR_DRIVE] = 0.45f; bc.vals[HF_DR_TONE] = 0.55f; bc.vals[HF_DR_LEVEL] = 0.5f;
        bc.vals[HF_OUT_LEVEL] += -1.3f;
        Preset& cm = p->presets[11][3];   // Con Molars
        cm.vals[HF_NAIL_TEXTURE] = 0.05f; cm.vals[HF_NAIL_TONE] = 0.60f;

        // Round 2 (rev 87, user ear-pass): March Stabs "weak -- more gain and
        // volume" (hotter amp + harder Zoom stage, +1.5 dB ON TOP of the
        // natural +1.2 the gain adds -- intentionally louder, not parity);
        // Broken Crush "noisey" (gate raised to the measured rig floor: -40
        // closes at -44, above the -45 hands-off peak; DS stage trimmed so it
        // feeds the bitcrusher less hiss -- level-neutral, -0.06 measured);
        // Con Molars "not much lowend, could be gainier" (drive .8, amp gain
        // .58, bass .62, cab lowcut 75). World Went Away = USER-APPROVED
        // ("perfect") -- DO NOT RETUNE.
        ms.vals[HF_AMP_GAIN] = 0.45f; ms.vals[HF_NAIL_DRIVE] = 0.80f;
        ms.vals[HF_NAIL_LEVEL] = 0.55f; ms.vals[HF_OUT_LEVEL] += 1.5f;
        bc.vals[HF_GT_THRESH] = -40.0f; bc.vals[HF_DR_DRIVE] = 0.38f;
        bc.vals[HF_DR_LEVEL] = 0.42f;
        cm.vals[HF_NAIL_DRIVE] = 0.80f; cm.vals[HF_AMP_GAIN] = 0.58f;
        cm.vals[HF_AMP_BASS] = 0.62f; cm.vals[HF_CAB_LOWCUT] = 75.0f;

        // Round 3 (rev 88, user): March Stabs "still needs more volume" -->
        // +2.5 dB more out_level (+4.0 total over the rev-86 parity point).
        // Broken Crush "still too much white noise" --> the hash tamed at the
        // SOURCE: crusher texture .5 -> .30 (less SR-decimation/bit hash),
        // cab-defeat tilt darker (tone .38), cab highcut 9000 -> 7500.
        // Level-neutral (0.00 measured); the Broke-era grit character stays.
        ms.vals[HF_OUT_LEVEL] += 2.5f;
        bc.vals[HF_NAIL_TEXTURE] = 0.30f; bc.vals[HF_NAIL_TONE] = 0.38f;
        bc.vals[HF_CAB_HIGHCUT] = 7500.0f;

        // Round 4 (rev 89, user: "tweak the eqs... move it around if it sounds
        // better or is more accurate; con molars is still missing something"):
        //  - March Stabs: the -6/-4/-6 low scoop was part of the "weak" -- eased
        //    to -3/-2/-2 with the stab bite kept (+4 @1.6k); nets +1.0 dB after
        //    a -2.4 comp (the user asked for volume twice; some of the EQ's
        //    loudness gain is kept deliberately).
        //  - Broken Crush: the old +3 @3.2k was RE-boosting the very hiss band
        //    the rev-88 fix tamed -- now 0; aggression moved into Wish-era mids
        //    (+1 @800 / +3 @1.6k). Fully re-leveled (-1.3).
        //  - Con Molars: THE MISSING PIECE = the mode's internal +5 dB @1.9k
        //    mid-bite, lost when its double cab-sim was zeroed (rev 86).
        //    Restored in-chain (+4 @1.6k, +2 @800) and the EQ MOVES PRE-AMP
        //    (nail 4 -> eq 5 -> amp 6 -> cab 7): it now shapes the digital clip
        //    INTO the JCM800, the documented Reaktor-then-reamp order -- which
        //    also adds the wanted grind in exactly that band. Nets +0.3 dB.
        ms.vals[HF_EQ_100] = -3; ms.vals[HF_EQ_200] = -2; ms.vals[HF_EQ_400] = -2;
        ms.vals[HF_EQ_800] = 1;  ms.vals[HF_EQ_1K6] = 4; ms.vals[HF_EQ_3K2] = 2;
        ms.vals[HF_OUT_LEVEL] += -2.4f;
        bc.vals[HF_EQ_100] = -3; bc.vals[HF_EQ_400] = -2; bc.vals[HF_EQ_800] = 1;
        bc.vals[HF_EQ_1K6] = 3;  bc.vals[HF_EQ_3K2] = 0;
        bc.vals[HF_OUT_LEVEL] += -1.3f;
        cm.vals[HF_EQ_POS] = 5.0f; cm.vals[HF_AMP_POS] = 6.0f; cm.vals[HF_CAB_POS] = 7.0f;
        cm.vals[HF_EQ_100] = -2; cm.vals[HF_EQ_200] = 0; cm.vals[HF_EQ_400] = 0;
        cm.vals[HF_EQ_800] = 2;  cm.vals[HF_EQ_1K6] = 4; cm.vals[HF_EQ_3K2] = 1;
        cm.vals[HF_OUT_LEVEL] += -0.5f;

        // USER FINAL (rev 90, 2026-07-30, store-diffed on-device -- DO NOT
        // RETUNE): the user's full creative reworks of all three, baked
        // verbatim as ABSOLUTE values (overrides every earlier round):
        //  - March Stabs: DIRECT/no-cab (TDS console sound), octave-up layer
        //    at pos 2, Nail back pre-amp, EQ pre-amp, inverted Clean Meanie
        //    B rig (+4 dB, blend .24), hot master, out -9.6.
        //  - Broken Crush: amp -> Diamond Plate, 76 ms tape slap, octave-up,
        //    Nail eased way down, Mid-Boost-based EQ at pos 8, Cali V B rig
        //    (inverted, cab2 on), and a MID-CHAIN Gate 2 at pos 7 (thresh
        //    -39.8) cleaning up after the crushers; cab -> @greenback.
        //  - Con Molars: starved Fuzz Zachary front end (bias .09, sustain
        //    .24), Nail drive .13 with texture back up, amp gain .28 -- the
        //    whole chain re-balanced around the FF spit.
        {
        ms.vals[HF_OUT_LEVEL] = -7.8f; ms.vals[HF_CP_POS] = 6.0f;
        ms.vals[HF_FZ_POS] = 7.0f; ms.vals[HF_DR_POS] = 8.0f;
        ms.vals[HF_AMP_POS] = 4.0f; ms.vals[HF_AMP_GAIN] = 0.4825f;
        ms.vals[HF_AMP_BASS] = 0.6625f; ms.vals[HF_AMP_MID] = 0.595f;
        ms.vals[HF_AMP_TREBLE] = 0.4975f; ms.vals[HF_AMP_PRESENCE] = 0.2f;
        ms.vals[HF_AMP_MASTER] = 0.765f; ms.vals[HF_CAB_POS] = 9.0f;
        ms.vals[HF_CAB_ENABLE] = 0.0f; ms.vals[HF_MD_POS] = 10.0f;
        ms.vals[HF_DL_POS] = 11.0f; ms.vals[HF_RV_POS] = 12.0f;
        ms.vals[HF_WH_POS] = 13.0f; ms.vals[HF_OC_POS] = 2.0f;
        ms.vals[HF_OC_ENABLE] = 1.0f; ms.vals[HF_OC_UP] = 0.425f;
        ms.vals[HF_OC_DOWN] = 0.1075f; ms.vals[HF_NAIL_POS] = 3.0f;
        ms.vals[HF_NAIL_DRIVE] = 0.66f; ms.vals[HF_NAIL_TONE] = 0.41f;
        ms.vals[HF_NAIL_TEXTURE] = 0.375f; ms.vals[HF_NAIL_LEVEL] = 0.51f;
        ms.vals[HF_EQ_POS] = 5.0f; ms.vals[HF_RB_ENABLE] = 1.0f;
        ms.vals[HF_RB_AMP] = 0.0f; ms.vals[HF_RB_GAIN] = 0.7325f;
        ms.vals[HF_RB_BASS] = 0.3675f; ms.vals[HF_RB_MID] = 0.5875f;
        ms.vals[HF_RB_TREBLE] = 0.625f; ms.vals[HF_RB_BLEND] = 0.2375f;
        ms.vals[HF_RB_LEVEL] = 4.02f; ms.vals[HF_RB_POL] = 1.0f;
        bc.vals[HF_OUT_LEVEL] = -16.8f; bc.vals[HF_CP_POS] = 10.0f;
        bc.vals[HF_FZ_POS] = 11.0f; bc.vals[HF_DR_POS] = 3.0f;
        bc.vals[HF_DR_DRIVE] = 0.1975f; bc.vals[HF_DR_TONE] = 0.7225f;
        bc.vals[HF_DR_LEVEL] = 0.4075f; bc.vals[HF_AMP_MODEL] = 12.0f;
        bc.vals[HF_AMP_GAIN] = 0.2875f; bc.vals[HF_AMP_BASS] = 0.265f;
        bc.vals[HF_AMP_MID] = 0.5975f; bc.vals[HF_AMP_TREBLE] = 0.63f;
        bc.vals[HF_AMP_MASTER] = 0.715f; bc.vals[HF_MD_POS] = 12.0f;
        bc.vals[HF_DL_POS] = 9.0f; bc.vals[HF_DL_ENABLE] = 1.0f;
        bc.vals[HF_DL_TYPE] = 1.0f; bc.vals[HF_DL_TIME] = 75.9625f;
        bc.vals[HF_DL_FEEDBACK] = 0.1764f; bc.vals[HF_DL_MIX] = 0.265f;
        bc.vals[HF_DL_WIDTH] = 0.1325f; bc.vals[HF_RV_POS] = 13.0f;
        bc.vals[HF_WH_POS] = 14.0f; bc.vals[HF_OC_POS] = 2.0f;
        bc.vals[HF_OC_ENABLE] = 1.0f; bc.vals[HF_OC_UP] = 0.4875f;
        bc.vals[HF_OC_DOWN] = 0.0225f; bc.vals[HF_AMP_FR_CHANNEL] = 2.0f;
        bc.vals[HF_NAIL_DRIVE] = 0.22f; bc.vals[HF_NAIL_TONE] = 0.6825f;
        bc.vals[HF_NAIL_TEXTURE] = 0.33f; bc.vals[HF_NAIL_LEVEL] = 0.38f;
        bc.vals[HF_EQ_POS] = 8.0f; bc.vals[HF_EQ_PRESET] = 4.0f;
        bc.vals[HF_EQ_100] = 4.0f; bc.vals[HF_EQ_200] = -2.0f;
        bc.vals[HF_EQ_400] = -5.0f; bc.vals[HF_EQ_800] = -2.0f;
        bc.vals[HF_EQ_1K6] = 2.0f; bc.vals[HF_EQ_3K2] = 4.0f;
        bc.vals[HF_EQ_LEVEL] = 0.66f; bc.vals[HF_DR2_POS] = 15.0f;
        bc.vals[HF_RB_ENABLE] = 1.0f; bc.vals[HF_RB_AMP] = 11.0f;
        bc.vals[HF_RB_GAIN] = 0.32f; bc.vals[HF_RB_BASS] = 0.3125f;
        bc.vals[HF_RB_BLEND] = 0.275f; bc.vals[HF_RB_LEVEL] = 1.2f;
        bc.vals[HF_RB_POL] = 1.0f; bc.vals[HF_RB_MV_MODE] = 7.0f;
        bc.vals[HF_RB_CAB2ON] = 1.0f; bc.vals[HF_GT2_POS] = 7.0f;
        bc.vals[HF_GT2_ENABLE] = 1.0f; bc.vals[HF_GT2_THRESH] = -39.8f;
        bc.vals[HF_GT2_ATTACK] = 0.1f; bc.vals[HF_GT2_RELEASE] = 328.4f;
        cm.vals[HF_OUT_LEVEL] = -16.8f; cm.vals[HF_CP_POS] = 7.0f;
        cm.vals[HF_FZ_POS] = 2.0f; cm.vals[HF_FZ_ENABLE] = 1.0f;
        cm.vals[HF_FZ_PEDAL] = 3.0f; cm.vals[HF_FZ_SUSTAIN] = 0.235f;
        cm.vals[HF_FZ_BIAS] = 0.0875f; cm.vals[HF_FZ_INPUTTRIM] = 0.2925f;
        cm.vals[HF_FZ_GETEMP] = 0.185f; cm.vals[HF_DR_POS] = 8.0f;
        cm.vals[HF_AMP_POS] = 5.0f; cm.vals[HF_AMP_GAIN] = 0.2775f;
        cm.vals[HF_AMP_BASS] = 0.515f; cm.vals[HF_CAB_POS] = 6.0f;
        cm.vals[HF_MD_POS] = 9.0f; cm.vals[HF_DL_POS] = 10.0f;
        cm.vals[HF_RV_POS] = 11.0f; cm.vals[HF_WH_POS] = 12.0f;
        cm.vals[HF_OC_POS] = 13.0f; cm.vals[HF_NAIL_POS] = 3.0f;
        cm.vals[HF_NAIL_DRIVE] = 0.13f; cm.vals[HF_NAIL_TONE] = 0.7225f;
        cm.vals[HF_NAIL_TEXTURE] = 0.5675f; cm.vals[HF_EQ_POS] = 4.0f;
            std::strncpy(bc.irPath, "@greenback", kPathMax-1); bc.irPath[kPathMax-1]='\0';
        }
    }

    // JCM800 stage-2 cap re-level (rev 85, 2026-07-30 evens session): the cap
    // 2.0 -> 1.4 fix (h2@223 landing on both knob-documented captures) costs
    // 0.4-0.9 dB on SOME JCM800 presets -- measured per preset at both caps
    // (chord-burst RMS); only >0.3 dB shifts compensated. The immune six
    // (Skye No Mod, WWA, Con Molars, March Stabs, PIJ, KOF) have dual-rig or
    // fuzz-dominated chains that mask the stage-2 contribution.
    {
        struct Lvl { int bank, slot; float d; };
        static const Lvl kJcmRelevel[] = {
            {0,1,+0.6f},   // Crunch
            {6,0,+0.6f},   // Skye Crusher
            {6,2,+0.7f},   // Skye Soar
            {8,0,+0.4f},   // Bridge Vibe
            {12,0,+0.8f},  // Quarter-Tone Lead
            {13,3,+0.9f},  // Grunge Drop
        };
        for (const Lvl& lv : kJcmRelevel) {
            Preset& pr = p->presets[lv.bank][lv.slot];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += lv.d;
        }
    }

    // ── padrive main-rig fix re-level (2026-07-31): applying the per-amp PA
    // drive to the main rig (was Rig-B-only) un-over-drives EVH/Rockerverb/Vox,
    // which drops their level. Measured on-device before/after; the delta is
    // baked back so loudness parity holds -- the LESS-saturated tone (EVH swell
    // back, less fizz) is the intended change. 0/3 Lead measured 0 (skipped).
    {
        struct Lvl { int bank, slot; float d; };
        static const Lvl kPadriveRelevel[] = {
            {0,2,+4.3f},  {1,0,+1.4f},  {1,3,+3.5f},  {2,0,+1.4f},  {2,1,+0.3f},
            {2,2,+0.3f},  {2,3,+0.3f},  {5,3,+0.3f},  {7,2,+2.0f},  {7,3,+1.8f},
            {9,1,+4.4f},  {10,0,+2.2f}, {10,1,+0.5f}, {10,2,+2.8f}, {10,3,+1.5f},
            {12,3,+1.9f}, {16,0,+3.5f}, {16,1,+3.2f},
        };
        for (const Lvl& lv : kPadriveRelevel) {
            Preset& pr = p->presets[lv.bank][lv.slot];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += lv.d;
        }
    }

    // ── USER bank-0 reworks (rev 103, 2026-07-31, store-diffed on-device --
    // DO NOT RETUNE): the user re-dialed the stock Clean/Crunch/Rhythm/Lead
    // AFTER the padrive main-rig fix (a strong accept signal -- Rhythm is an
    // EVH preset, rebuilt around the corrected, un-over-driven EVH):
    //  - Clean: more amp gain (.68) + a touch more drive level.
    //  - Crunch: SIR #34 mod ON + more bass -- the new JCM800 hot-rod.
    //  - Rhythm: full rebuild -- chain reordered (Drive->Amp->Cab->Reverb),
    //    comp parked+bypassed, gain .665 / master .5775 (much hotter power
    //    section into the corrected EVH), mids/treble eased, input phase
    //    normal. out_level kept at the padrive-releveled value (user didn't
    //    touch it).
    //  - Lead: input phase normal.
    {
        Preset& cl = p->presets[0][0];
        cl.vals[HF_DR_LEVEL] = 0.5925f; cl.vals[HF_AMP_GAIN] = 0.6825f;
        Preset& cr = p->presets[0][1];
        cr.vals[HF_AMP_BASS] = 0.405f;  cr.vals[HF_AMP_SIR34] = 1.0f;
        Preset& rh = p->presets[0][2];
        rh.vals[HF_IT_PHASE] = 0.0f;    rh.vals[HF_CP_POS] = 6.0f;
        rh.vals[HF_CP_ENABLE] = 0.0f;   rh.vals[HF_FZ_POS] = 7.0f;
        rh.vals[HF_DR_POS] = 2.0f;      rh.vals[HF_DR_DRIVE] = 0.04f;
        rh.vals[HF_AMP_POS] = 3.0f;     rh.vals[HF_AMP_GAIN] = 0.665f;
        rh.vals[HF_AMP_BASS] = 0.52f;   rh.vals[HF_AMP_MID] = 0.6225f;
        rh.vals[HF_AMP_TREBLE] = 0.65f; rh.vals[HF_AMP_MASTER] = 0.5775f;
        rh.vals[HF_CAB_POS] = 4.0f;     rh.vals[HF_MD_POS] = 9.0f;
        rh.vals[HF_DL_POS] = 10.0f;     rh.vals[HF_RV_POS] = 5.0f;
        rh.vals[HF_WH_POS] = 11.0f;     rh.vals[HF_OC_POS] = 12.0f;
        rh.vals[HF_CP_BYPASS] = 1.0f;   rh.vals[HF_NAIL_POS] = 13.0f;
        rh.vals[HF_EQ_POS] = 8.0f;
        Preset& ld = p->presets[0][3];
        ld.vals[HF_IT_PHASE] = 0.0f;
    }

        // Periphery + Gojira EVH presets REWORKED to specific songs on the new EVH
        // (rev 105, 2026-08-03, user request). All EVH Red + Green Man (TS) tight boost.
        // First pass -- user refines by ear on-device; out_level LEFT for re-measure.
        //   Flatliner       -> Periphery "Flatline"  (II-era: bright, tight, articulate)
        //   Prayer Djent    -> Periphery "Prayer Position" (Hail Stan: tighter/modern/hot)
        //   Castaway Groove -> Gojira "Silvera" (Magma: mid-forward, thick, percussive)
        // Both Periphery stay RHYTHM (kept their parallel clean blend); Gojira darker/thicker.
        {
            // (rev 106, 2026-08-03) hiss tame: the bright/hot rework amplified the Red
            // channel's HF input-noise into audible hiss (measured -32 dBFS 6-8k band,
            // evh_hiss). Pulled treble/presence/TS-tone down + cab highcut way in (7.8k->
            // 6.3k) -- kills the >6k hiss while keeping the 2-5k djent chug/bite.
            Preset& fl = p->presets[7][2];   // Flatliner -> "Flatline"
            fl.vals[HF_GT_THRESH] = -40.0f; fl.vals[HF_GT_RELEASE] = 150.0f;
            fl.vals[HF_DR_DRIVE] = 0.0f;   fl.vals[HF_DR_TONE] = 0.66f; fl.vals[HF_DR_LEVEL] = 0.72f;
            fl.vals[HF_AMP_GAIN] = 0.62f;  fl.vals[HF_AMP_BASS] = 0.28f; fl.vals[HF_AMP_MID] = 0.60f;
            fl.vals[HF_AMP_TREBLE] = 0.60f; fl.vals[HF_AMP_PRESENCE] = 0.50f; fl.vals[HF_AMP_MASTER] = 0.42f;
            fl.vals[HF_AMP_SAG] = 0.22f;   fl.vals[HF_CAB_LOWCUT] = 95.0f; fl.vals[HF_CAB_HIGHCUT] = 6300.0f;
            fl.vals[HF_RB_BLEND] = 0.18f;

            Preset& pd = p->presets[7][3];   // Prayer Djent -> "Prayer Position"
            pd.vals[HF_GT_THRESH] = -40.0f; pd.vals[HF_GT_RELEASE] = 140.0f;
            pd.vals[HF_DR_DRIVE] = 0.05f;  pd.vals[HF_DR_TONE] = 0.66f; pd.vals[HF_DR_LEVEL] = 0.74f;
            pd.vals[HF_AMP_GAIN] = 0.62f;  pd.vals[HF_AMP_BASS] = 0.26f; pd.vals[HF_AMP_MID] = 0.63f;
            pd.vals[HF_AMP_TREBLE] = 0.58f; pd.vals[HF_AMP_PRESENCE] = 0.48f; pd.vals[HF_AMP_MASTER] = 0.44f;
            pd.vals[HF_AMP_SAG] = 0.20f;   pd.vals[HF_CAB_LOWCUT] = 100.0f; pd.vals[HF_CAB_HIGHCUT] = 6300.0f;
            pd.vals[HF_RB_BLEND] = 0.22f;

            Preset& cg = p->presets[12][3];  // Castaway Groove -> Gojira "Silvera"
            cg.vals[HF_GT_THRESH] = -42.0f; cg.vals[HF_GT_RELEASE] = 150.0f;
            cg.vals[HF_DR_DRIVE] = 0.05f;  cg.vals[HF_DR_TONE] = 0.52f; cg.vals[HF_DR_LEVEL] = 0.95f;
            cg.vals[HF_AMP_GAIN] = 0.56f;  cg.vals[HF_AMP_BASS] = 0.42f; cg.vals[HF_AMP_MID] = 0.66f;
            cg.vals[HF_AMP_TREBLE] = 0.55f; cg.vals[HF_AMP_PRESENCE] = 0.48f; cg.vals[HF_AMP_MASTER] = 0.72f;
            cg.vals[HF_AMP_SAG] = 0.22f;   cg.vals[HF_AMP_RESONANCE] = 0.35f;
            cg.vals[HF_CAB_LOWCUT] = 90.0f; cg.vals[HF_CAB_HIGHCUT] = 7000.0f;
        }
        // Brown Sound '84 = USER's on-device dial-in on the OVERVOLT variac,
        // baked VERBATIM from the store (store-diffed 2026-08-03; amp pulled to a
        // real setting -- gain .655/bass .5/mid .65/treble .705/pres .63/master
        // .85/sag .75/VolII .30, IT boost OFF, EP-3 clean (drive 0), EQ off, more
        // tape echo). Placed LAST so it wins over the speaker-drive loop. DO NOT RETUNE.
        {
            static const float kBS84[HF_N_PORTS] = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -19.200001f,
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 1.0f, -48.0f, 0.1f, 140.0f, 268.700012f,
        6.0f, 9.0f, 0.0f, 0.0f, -20.0f, 1.0f, 5.0f, 5.0f,
        3.0f, 0.0f, 10.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f,
        0.65f, 0.5f, 0.5f, 0.4f, 3.0f, 1.0f, 8.0f, 0.0f,
        0.5025f, 0.5525f, 1.0f, 0.3f, 4.0f, 1.0f, 10.0f, 0.655f,
        0.5f, 0.6525f, 0.705f, 0.6275f, 0.8475f, 0.7475f, 0.0f, 0.0f,
        0.5f, 0.0f, 0.0f, 1.0f, 0.55f, 0.18f, 0.33f, 0.62f,
        0.42f, 0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 5.0f, 1.0f, 85.0f, 8000.0f, 1.0f, 2.0f, 1.0f,
        8.0f, 0.205f, 0.35f, 0.5025f, 0.5f, 6.0f, 1.0f, 4.0f,
        235.882507f, 0.0196f, 0.1775f, 0.5f, 0.003f, 0.001f, 10.0f, 1.0f,
        0.0f, 0.0f, 0.3f, 7.0f, 1.0f, 15.0f, 1.601f, 0.3f,
        0.0f, 0.022475f, 0.14f, 11.0f, 0.0f, 0.0f, 0.4f, 0.7f,
        0.5f, 0.6f, 0.8f, 12.0f, 0.0f, 0.0f, 0.5f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 13.0f,
        0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 0.0f,
        5.0f, 0.0f, 2.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.3f, 0.360628f, 0.090113f, 1.0f, 0.12f,
        0.35f, 7.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,
        8.0f, 0.0f, 5.0f, -1.0f, 0.0f, 1.0f, 3.0f, 4.0f,
        5.0f, 0.12f, 0.0f, 0.08f, 0.3f, 1.0f, 0.0f, 1.0f,
        0.5f, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 14.0f, 0.0f,
        0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 10.0f, 0.7525f, 0.985f, 1.0f, 1.0f, 1.0f, 0.7f,
        0.5025f, 0.0f, 0.0f, 0.0f, 80.0f, 16000.0f, 0.5075f, 1.02f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 7.0f, 0.0f, 0.0f, 2.0f,
        0.0f, 0.1175f, 0.0f, 1.0f, 1.0f, 0.55f, 0.18f, 0.33f,
        0.62f, 0.42f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.12f, 0.35f, 1.0f, 0.0f, 2.0f, 0.0f, 15.0f,
        0.0f, -60.0f, 2.0f, 120.0f, 250.0f, 8.0f, 0.0f, 16.0f,
        0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f, 3.0f, 0.0f,
        0.0f, 17.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f, 0.65f,
        0.5f, 0.5f, 0.4f, 1.0f, 0.0f, 18.0f, 0.0f, 2.0f,
        0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 19.0f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 20.0f, 0.0f, 0.0f, 250.0f, 0.4f, 0.15f, 0.5f,
        0.003f, 0.001f, 10.0f, 1.0f, 0.0f, 0.0f, 0.3f, 0.0f,
        5.0f, 0.0f, 21.0f, 0.0f, 10.0f, 1.5f, 0.3f, 0.0f,
        0.8f, 0.15f, 0.0f, 0.0f, 0.5f, 0.0f, 22.0f, 0.0f,
        0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f, 0.0f, 23.0f,
        0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 24.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 1.0f, 0.15f, 0.35f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f,
            };
            Preset& bs = p->presets[17][1];
            std::memcpy(bs.vals, kBS84, sizeof(bs.vals));
            std::strncpy(bs.irPath,  "@greenback", kPathMax-1); bs.irPath[kPathMax-1]='\0';
            std::strncpy(bs.ir2Path, "@greenback", kPathMax-1); bs.ir2Path[kPathMax-1]='\0';
            bs.ampNamPath[0]=bs.drNamPath[0]=bs.cabNamPath[0]=bs.amp2NamPath[0]=bs.dr2NamPath[0]='\0';
        }
        // Berlin Wall Pulse (b4/1) = user's Gilmour "Wall/Pulse" preset baked VERBATIM
        // from the store, with the DRIVE FIXED: New Dawn (Life Pedal) is not a Gilmour
        // pedal -> Dear Rodent Boy (ProCo RAT), his actual OD. Preserves the user's preset
        // (name + Hiwatt + ambience) across the reseed. (rev 106, 2026-08-03, user.)
        {
            static const float kBWP[HF_N_PORTS] = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -20.299999f,
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 4.0f, 1.0f, 1.0f, -60.0f, 2.0f, 120.0f, 250.0f,
        8.0f, 2.0f, 1.0f, 0.0f, -20.0f, 1.0f, 5.0f, 5.0f,
        3.0f, 1.0f, 3.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f,
        0.65f, 0.5f, 0.5f, 0.4f, 4.0f, 1.0f, 1.0f, 0.15f,
        0.6f, 0.55f, 1.0f, 0.3f, 5.0f, 1.0f, 7.0f, 0.2f,
        0.5f, 0.45f, 0.6f, 0.6f, 0.7f, 0.25f, 0.0f, 0.0f,
        0.5f, 0.0f, 0.0f, 1.0f, 0.55f, 0.18f, 0.33f, 0.62f,
        0.42f, 0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 6.0f, 1.0f, 85.0f, 11000.0f, 1.0f, 7.0f, 1.0f,
        3.0f, 0.12f, 0.4f, 0.4f, 0.4f, 8.0f, 1.0f, 3.0f,
        380.0f, 0.5f, 0.4f, 0.4f, 0.003f, 0.001f, 10.0f, 1.0f,
        0.1f, 0.05f, 0.2f, 9.0f, 1.0f, 10.0f, 1.4f, 0.3f,
        0.0f, 0.8f, 0.1f, 10.0f, 0.0f, 0.0f, 0.4f, 0.7f,
        0.5f, 0.6f, 0.8f, 11.0f, 0.0f, 0.0f, 0.5f, 1.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 12.0f,
        0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 0.0f,
        5.0f, 0.0f, 2.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.15f, 1.0f, 0.12f,
        0.35f, 7.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,
        6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.1f, 0.25f, 1.0f, 0.0f, 1.0f,
        0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 14.0f, 0.0f,
        0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.7f,
        0.3f, 0.0f, 0.0f, 0.0f, 80.0f, 16000.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 7.0f, 0.0f, 0.0f, 2.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.55f, 0.18f, 0.33f,
        0.62f, 0.42f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.12f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 15.0f,
        0.0f, -60.0f, 2.0f, 120.0f, 250.0f, 8.0f, 0.0f, 16.0f,
        0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f, 3.0f, 0.0f,
        0.0f, 17.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f, 0.65f,
        0.5f, 0.5f, 0.4f, 1.0f, 0.0f, 18.0f, 0.0f, 2.0f,
        0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 19.0f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 20.0f, 0.0f, 0.0f, 250.0f, 0.4f, 0.15f, 0.5f,
        0.003f, 0.001f, 10.0f, 1.0f, 0.0f, 0.0f, 0.3f, 0.0f,
        5.0f, 0.0f, 21.0f, 0.0f, 10.0f, 1.5f, 0.3f, 0.0f,
        0.8f, 0.15f, 0.0f, 0.0f, 0.5f, 0.0f, 22.0f, 0.0f,
        0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f, 0.0f, 23.0f,
        0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 24.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.35f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f,
            };
            Preset& bwp = p->presets[4][1];
            std::memcpy(bwp.vals, kBWP, sizeof(bwp.vals));
            bwp.used = true;
            std::snprintf(bwp.name, sizeof(bwp.name), "Berlin Wall Pulse");
            bwp.vals[HF_DR_MODEL] = 2.0f;   // New Dawn (Life Pedal, 1) -> Dear Rodent Boy / RAT (2)
            bwp.vals[HF_DR_DRIVE] = 0.35f; bwp.vals[HF_DR_TONE] = 0.45f; bwp.vals[HF_DR_LEVEL] = 0.60f;
            std::strncpy(bwp.irPath, "@hiwatt", kPathMax-1); bwp.irPath[kPathMax-1]='\0';
            bwp.ampNamPath[0]=bwp.drNamPath[0]=bwp.cabNamPath[0]=bwp.amp2NamPath[0]=bwp.ir2Path[0]=bwp.dr2NamPath[0]='\0';
        }
        // Solar Monolith (b6/3, was "Wizard's Doom") -> SUNN O))) drone doom (rev 107,
        // 2026-08-03, user). New Dawn (the EQD Life Pedal -- literally built with Sunn O)))
        // for this sound) into the cranked Doom Daddy (Sunn Model T, series-linked) = a
        // massive dark saturated monolith. Fuzz OUT / drive IN (pos 4, already pre-amp),
        // huge sub, dark top, deep sag, Full speaker drive, Hex Ambient bloom wash, gate
        // opened so it drones. out_level LEFT for on-device re-measure.
        {
            // Solar Monolith = USER's on-device dial-in baked VERBATIM (store-diffed rev
            // 108, 2026-08-03): less drive (.14) but more octave (.43), brighter (tone .62/
            // highcut 8030), PARALLEL link, Sunn Vol II pushed .82, gate -43. DO NOT RETUNE.
            static const float kSM[HF_N_PORTS] = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -22.700001f,
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 4.0f, 1.0f, 1.0f, -43.400002f, 3.0f, 200.0f, 400.0f,
        8.0f, 2.0f, 0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f,
        3.0f, 0.0f, 3.0f, 0.0f, 0.0f, 4.0f, 0.62f, 0.38f,
        0.4f, 0.5f, 0.5f, 0.4f, 4.0f, 1.0f, 1.0f, 0.1375f,
        0.625f, 0.6725f, 1.0f, 0.425f, 5.0f, 1.0f, 3.0f, 0.68f,
        0.775f, 0.45f, 0.32f, 0.32f, 0.78f, 0.62f, 0.0f, 0.0f,
        0.8175f, 1.0f, 0.0f, 1.0f, 0.55f, 0.18f, 0.33f, 0.62f,
        0.42f, 0.5f, 0.0f, 1.0f, 0.825f, 0.3975f, 0.33f, 1.0f,
        1.0f, 6.0f, 1.0f, 45.0f, 8030.0f, 1.0f, 7.0f, 0.0f,
        0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 8.0f, 0.0f, 0.0f,
        250.0f, 0.4f, 0.15f, 0.5f, 0.003f, 0.001f, 10.0f, 1.0f,
        0.0f, 0.0f, 0.3f, 9.0f, 1.0f, 20.0f, 4.0f, 0.4f,
        0.0f, 0.8f, 0.32f, 10.0f, 0.0f, 0.0f, 0.4f, 0.7f,
        0.5f, 0.6f, 0.8f, 11.0f, 0.0f, 0.0f, 0.5f, 1.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 12.0f,
        0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 0.0f,
        5.0f, 0.0f, 2.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.374779f, 0.133917f, 1.0f, 0.12f,
        0.35f, 7.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,
        6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 1.0f, 2.0f, 1.0f,
        0.85f, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 14.0f, 0.0f,
        0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 10.0f, 0.42f, 0.605f, 0.5f, 0.5f, 0.5f, 0.7f,
        0.3f, 0.0f, 0.0f, 0.0f, 80.0f, 8390.0f, 0.2475f, 0.0f,
        1.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 7.0f, 0.0f, 0.0f, 2.0f,
        0.0f, 0.495f, 0.0f, 1.0f, 1.0f, 0.55f, 0.18f, 0.33f,
        0.62f, 0.42f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.152691f,
        1.0f, 0.12f, 0.35f, 1.0f, 0.0f, 2.0f, 1.0f, 15.0f,
        0.0f, -60.0f, 2.0f, 120.0f, 250.0f, 8.0f, 0.0f, 16.0f,
        0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f, 3.0f, 0.0f,
        0.0f, 17.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f, 0.65f,
        0.5f, 0.5f, 0.4f, 1.0f, 0.0f, 18.0f, 0.0f, 2.0f,
        0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 19.0f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 20.0f, 0.0f, 0.0f, 250.0f, 0.4f, 0.15f, 0.5f,
        0.003f, 0.001f, 10.0f, 1.0f, 0.0f, 0.0f, 0.3f, 0.0f,
        5.0f, 0.0f, 21.0f, 0.0f, 10.0f, 1.5f, 0.3f, 0.0f,
        0.8f, 0.15f, 0.0f, 0.0f, 0.5f, 0.0f, 22.0f, 0.0f,
        0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f, 0.0f, 23.0f,
        0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 24.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.35f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f,
            };
            Preset& sm = p->presets[6][3];
            std::memcpy(sm.vals, kSM, sizeof(sm.vals));
            std::snprintf(sm.name, sizeof(sm.name), "Solar Monolith");
            std::strncpy(sm.irPath,  "@doom", kPathMax-1); sm.irPath[kPathMax-1]='\0';
            std::strncpy(sm.ir2Path, "@doom", kPathMax-1); sm.ir2Path[kPathMax-1]='\0';
            sm.ampNamPath[0]=sm.drNamPath[0]=sm.cabNamPath[0]=sm.amp2NamPath[0]=sm.dr2NamPath[0]='\0';

            // Regal Sustain (b1/0, Brian May / Vox "Chime Thirty") -- little tuning after the
            // Vox re-voice: tame the top (treble/presence/TS-tone), push the violin mids, a
            // touch more air. Treble booster (IT +6) + cranked AC30 + singing sag kept.
            Preset& rsus = p->presets[1][0];
            rsus.vals[HF_AMP_TREBLE] = 0.72f; rsus.vals[HF_AMP_MID] = 0.68f; rsus.vals[HF_AMP_PRESENCE] = 0.58f;
            rsus.vals[HF_DR_TONE] = 0.70f;    rsus.vals[HF_RV_MIX] = 0.16f;

            // Regal Solo (b10/3, Brian May lead) -- same top tame + violin mids; the 800 ms
            // Brighton-Rock delay upgraded to the EP-3 tape echo (warmer), a bit more space.
            Preset& rsol = p->presets[10][3];
            rsol.vals[HF_AMP_TREBLE] = 0.72f; rsol.vals[HF_AMP_MID] = 0.68f; rsol.vals[HF_AMP_PRESENCE] = 0.56f;
            rsol.vals[HF_DR_TONE] = 0.70f;    rsol.vals[HF_DL_TYPE] = 4.0f; rsol.vals[HF_DL_AGE] = 0.20f;
            rsol.vals[HF_DL_MIX] = 0.22f;     rsol.vals[HF_RV_MIX] = 0.22f;
        }

    // ── Green Man (TS-808) gain-floor fix re-level (rev 109, 2026-08-21): the
    // real x11.85 feedback floor at drive 0 finally makes the drive-0/level-up
    // boost trick work (user request) — 22 presets run the Green Man, mostly at
    // low drive, and now push their amps correctly. The HOTTER/more-saturated
    // tone is the intended change; measured preset-RMS deltas ≥0.5 dB are baked
    // back into out_level so loudness parity holds (preset_diag pre/post,
    // /tmp/preset_rms_{pre,post}_ts.txt). Sub-0.5 dB shifts left alone.
    {
        struct Lvl { int bank, slot; float d; };
        static const Lvl kTsFloorRelevel[] = {
            {0,0,-1.5f},   // Clean          (+1.5 measured)
            {0,1,+0.5f},   // Crunch         (-0.5)
            {0,2,+1.5f},   // Rhythm         (-1.5)
            {0,3,+1.9f},   // Lead           (-1.9)
            {6,1,+0.9f},   // Skye (No Mod)  (-0.9)
            {7,2,+3.2f},   // Flatliner      (-3.2: drive 0.00 — the poster child)
            {7,3,+1.6f},   // Prayer Djent   (-1.6)
            {12,3,+1.4f},  // Castaway Groove (-1.4)
            {14,3,+1.7f},  // Duality Crush  (-1.7: drive 0.00)
        };
        for (const Lvl& lv : kTsFloorRelevel) {
            Preset& pr = p->presets[lv.bank][lv.slot];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += lv.d;
        }
    }

    // ── Periphery rig-B low cut (rev 110, 2026-08-21): the djent parallel
    // clean blends' low-mids pump the inverted-polarity cancellation depth
    // (the mono "woosh"; isolated + measured by build-tools/woosh2.cpp —
    // 150 Hz recovers about half the rig-B flutter while keeping the layer's
    // attack clarity). User-adjustable in the Amp 2 Blend tab.
    p->presets[7][2].vals[HF_RB_LOCUT] = 150.0f;   // Flatliner
    p->presets[7][3].vals[HF_RB_LOCUT] = 150.0f;   // Prayer Djent
    // Measured parity restore for the removed low-mid energy (preset_diag).
    p->presets[7][2].vals[HF_OUT_LEVEL] += 1.2f;
    p->presets[7][3].vals[HF_OUT_LEVEL] += 1.6f;
    // Rev 112 (2026-08-21, user still heard the swoosh; woosh2 sweep): the
    // cancellation pump SCALES WITH BLEND — 0.10 measures at the rig-B-off
    // flutter floor on both presets while pol/locut-400 barely move it. The
    // layer becomes a subtle attack sheen; the user can zero it entirely if
    // it no longer earns its place on a mono rig.
    p->presets[7][2].vals[HF_RB_BLEND] = 0.10f;    // was 0.18
    p->presets[7][3].vals[HF_RB_BLEND] = 0.10f;    // was 0.22
    // Measured parity restore (less inverted cancellation = louder).
    p->presets[7][2].vals[HF_OUT_LEVEL] += -1.4f;
    p->presets[7][3].vals[HF_OUT_LEVEL] += -2.1f;
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
    p->trimLoad.prepare(rate);
    p->evhFit[0].prepare(rate);
    p->evhFit[1].prepare(rate);
    p->rectoFit[0].prepare(rate);
    p->rectoFit[1].prepare(rate);
    // (FRFR voice EQ coeffs are set in run() from the fv_* knobs on first
    // enable + on any knob move — see the fvOn block before the Output stage.)
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
    p->drive2.prepare(rate, kMaxBlock, 1);
    p->pa2.prepare(rate, kMaxBlock, 1);
    p->cab2.prepare(rate, kMaxBlock, 1);
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
    // X2 second instances (v38)
    p->gate2.prepare(rate, kMaxBlock, 1);
    p->comp2.prepare(rate, kMaxBlock, 1);
    p->fuzz2Muff   = std::make_unique<OversamplingWrapper>(std::make_unique<EHXBigMuff>());
    p->fuzz2Bender = std::make_unique<OversamplingWrapper>(std::make_unique<ToneBenderMkII>());
    p->fuzz2Octavia= std::make_unique<OversamplingWrapper>(std::make_unique<Octavia>());
    p->fuzz2Factory= std::make_unique<OversamplingWrapper>(std::make_unique<ZVexFuzzFactory>());
    p->nail2       = std::make_unique<OversamplingWrapper>(std::make_unique<NailDistortion>());
    if (!p->fuzz2Muff || !p->fuzz2Bender || !p->fuzz2Octavia || !p->fuzz2Factory || !p->nail2) { delete p; return nullptr; }
    p->fuzz2Muff->prepare(rate, kMaxBlock, 1);
    p->fuzz2Bender->prepare(rate, kMaxBlock, 1);
    p->fuzz2Octavia->prepare(rate, kMaxBlock, 1);
    p->fuzz2Factory->prepare(rate, kMaxBlock, 1);
    p->fuzz2Muff->setParameter("era", 2.0f);
    p->nail2->prepare(rate, kMaxBlock, 1);
    p->nail2->setParameter("mode", 2.0f);
    p->modfx2.prepare(rate, kMaxBlock, 2);
    p->modfx2.setType(ModulationFactory::fromIndex(0));
    p->delay2.prepare(rate, kMaxBlock, 2);
    p->delay2.setType(DelayFactory::fromIndex(0));
    p->reverb2.prepare(rate, kMaxBlock, 2);
    p->wah2.prepare(rate, kMaxBlock, 2);
    p->octave2.prepare(rate, kMaxBlock, 2);
    p->eq2.prepare(rate);
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
// mod-ui MATERIALIZES atom:Path values when a pedalboard is saved: a built-in
// "@sentinel" cab becomes "<pedalboard>/effect-N/@sentinel" on restore, plus a
// BROKEN self-referential symlink on disk. Found 2026-08-21: every preset with
// a non-@factory synthetic cab had silently fallen back to the factory V30
// since the pedalboard was last saved (the '@' prefix check missed the
// absolute form, then the symlink failed to load). Recover the sentinel from
// the BASENAME so any path shape mod-ui hands back resolves correctly.
static const char* cabSentinel(const char* path) {
    const char* base = std::strrchr(path, '/');
    base = base ? base + 1 : path;
    return base[0] == '@' ? base : nullptr;
}
static LV2_Worker_Status hf_work(LV2_Handle h, LV2_Worker_Respond_Function respond,
                                 LV2_Worker_Respond_Handle handle, uint32_t, const void* data) {
    auto* p = static_cast<HexForge*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == W_AMP_FREE) { delete msg->amp; return LV2_WORKER_SUCCESS; }
    if (msg->type == W_NAM_FREE) { delete msg->nam; return LV2_WORKER_SUCCESS; }
    if (msg->type == W_CAB_IR) {
        std::vector<float> L, R;
        const char* sent = cabSentinel(msg->path);
        if (msg->namSlot == 1) {   // Rig B cab: built-in sentinel OR a user .wav (v39)
            if (sent)
                p->cab2.setIR(CabModels::generate(sent, p->rate));
            else if (msg->path[0] && loadIRFile(msg->path, p->rate, L, R))
                p->cab2.setIR(L, R.empty()?nullptr:&R);
            else p->cab2.setIR(CabModels::generate("@factory", p->rate));
            return LV2_WORKER_SUCCESS;
        }
        if (sent)                                                         // built-in synthetic cab
            p->cab.setIR(CabModels::generate(sent, p->rate));
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
    na->setEco(msg->eco);   // Engine Quality: must precede setAmpModel (wrapper built there)
    na->setAmpModel(kAmpMap[clampi(static_cast<float>(msg->modelIdx), 0, kMt15Idx)]);
    WorkMsg reply; reply.type = W_AMP_LOAD; reply.amp = na; reply.modelIdx = msg->modelIdx; reply.namSlot = msg->namSlot;   // slot 1 = Rig B (forgetting this routed B builds into the MAIN amp)
    respond(handle, sizeof(reply), &reply);
    return LV2_WORKER_SUCCESS;
}
static LV2_Worker_Status hf_work_response(LV2_Handle h, uint32_t, const void* data) {
    auto* p = static_cast<HexForge*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == W_NAM_LOAD) {
        // Defer the swap to the mute-ramp's zero point (a mid-waveform model swap
        // is a guaranteed pop). If a previous pending model was never applied,
        // free it instead of leaking.
        const int sl = msg->namSlot;
        if (p->pendNam[sl]) {
            WorkMsg fm; fm.type = W_NAM_FREE; fm.nam = p->pendNam[sl];
            p->schedule->schedule_work(p->schedule->handle, sizeof(fm), &fm);
        }
        p->pendNam[sl] = msg->nam;
        // Re-arm during fade-IN too (state 2): a fast load's response lands while the recall
        // that requested it is still fading back in — with the ==0 check only, the pending
        // model was never applied until the NEXT switch (measured: every preset ran the
        // PREVIOUS preset's model; race is latent on-device as well). State 1 needs nothing
        // (zero point is coming anyway).
        if (p->swFadeState == 0 || p->swFadeState == 2) p->swFadeState = 1;
        return LV2_WORKER_SUCCESS;
    }
    if (msg->type != W_AMP_LOAD) return LV2_WORKER_SUCCESS;
    if (msg->namSlot == 1) {   // Rig B amp reply
        if (p->pendAmp2) {
            WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = p->pendAmp2;
            p->schedule->schedule_work(p->schedule->handle, sizeof(fm), &fm);
        }
        p->pendAmp2 = msg->amp;
        p->pendAmp2Model = msg->modelIdx;
        if (p->swFadeState == 0 || p->swFadeState == 2) p->swFadeState = 1;
        return LV2_WORKER_SUCCESS;
    }
    if (p->pendAmp) {
        WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = p->pendAmp;
        p->schedule->schedule_work(p->schedule->handle, sizeof(fm), &fm);
    }
    p->pendAmp = msg->amp;
    p->pendAmpModel = msg->modelIdx;
    if (p->swFadeState == 0 || p->swFadeState == 2) p->swFadeState = 1;   // re-arm during fade-in (see NAM note above)
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

// ── Seamless switching: tail resets + the zero-point apply ───────────────────
static void hfResetBlockTails(HexForge* p, int b) {
    switch (b) {   // long-memory blocks only; the mute ramp covers the ms-scale rest
        case B_DELAY:  p->delay.reset();  break;
        case B_REVERB: p->reverb.reset(); break;
        case B_MODFX:  p->modfx.reset();  break;
        case B_CAB:    p->cab.reset();    break;
        default: break;
    }
}
static void hfResetTails(HexForge* p) {
    p->delay.reset(); p->reverb.reset(); p->modfx.reset(); p->cab.reset(); p->eq.reset();
    std::fill(p->dblBuf.begin(), p->dblBuf.end(), 0.0f);
    std::fill(p->dblApA.begin(), p->dblApA.end(), 0.0f);
    std::fill(p->dblApB.begin(), p->dblApB.end(), 0.0f);
}
// Runs while the output is at ZERO: apply every deferred discontinuity at once.
static void hfApplySwitch(HexForge* p) {
    // blocks (re-)engaging: clear their stale tails so nothing old replays
    for (int i = 0; i < B_COUNT; ++i) {
        const bool now = (*p->ports[kEnablePort[i]] > 0.5f) && (*p->ports[kBypassPort[i]] <= 0.5f);
        if (p->swHold && now && !p->swEnabledHeld[i]) hfResetBlockTails(p, i);
    }
    p->swHold = false;
    if (p->swPendRecall) {
        p->swPendRecall = false;
        psRecall(p, p->swPendBank, p->swPendSlot);
        hfResetTails(p);
    }
    if (p->pendAmp) {
        AmpBlockExtended* old = p->amp;
        p->amp = p->pendAmp; p->pendAmp = nullptr;
        p->lastAmpModel = p->pendAmpModel;
        WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = old;
        p->schedule->schedule_work(p->schedule->handle, sizeof(fm), &fm);
    }
    if (p->pendAmp2) {
        AmpBlockExtended* old = p->amp2;
        p->amp2 = p->pendAmp2; p->pendAmp2 = nullptr;
        p->lastAmp2Model = p->pendAmp2Model;
        p->amp2Requested = false;
        if (old) { WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = old;
                   p->schedule->schedule_work(p->schedule->handle, sizeof(fm), &fm); }
    }
    for (int sl = 0; sl < 5; ++sl) if (p->pendNam[sl]) {
        NamModel** slot = (sl == 0) ? &p->ampNam : (sl == 1) ? &p->drNam : (sl == 2) ? &p->cabNam
                        : (sl == 3) ? &p->amp2Nam : &p->dr2Nam;
        NamModel* old = *slot;
        *slot = p->pendNam[sl]; p->pendNam[sl] = nullptr;
        WorkMsg fm; fm.type = W_NAM_FREE; fm.nam = old;
        p->schedule->schedule_work(p->schedule->handle, sizeof(fm), &fm);
    }
    // v40: fuzz/nail Eco -- rebuild the oversampling wrappers at the new factor
    // HERE (output is at zero; the same allocate-on-switch class of behavior the
    // OverdriveBlock eco crossfade already ships). States reset; params reapplied
    // by the per-block sections next run.
    auto ecoNow = [&](int port){ return *p->ports[port] > 0.5f; };
    auto rebuildFuzz = [&](std::unique_ptr<OversamplingWrapper>& w, std::unique_ptr<AmpModelBase> m, bool eco){
        w = std::make_unique<OversamplingWrapper>(std::move(m), eco ? 2 : 4);
        if (w) w->prepare(p->rate, kMaxBlock, 1);
    };
    if (ecoNow(HF_FZ_ECO) != p->lastFzEco) {
        p->lastFzEco = ecoNow(HF_FZ_ECO);
        rebuildFuzz(p->fuzzMuff,    std::make_unique<EHXBigMuff>(),      p->lastFzEco);
        rebuildFuzz(p->fuzzBender,  std::make_unique<ToneBenderMkII>(),  p->lastFzEco);
        rebuildFuzz(p->fuzzOctavia, std::make_unique<Octavia>(),         p->lastFzEco);
        rebuildFuzz(p->fuzzFactory, std::make_unique<ZVexFuzzFactory>(), p->lastFzEco);
    }
    if (ecoNow(HF_FZ2_ECO) != p->lastFz2Eco) {
        p->lastFz2Eco = ecoNow(HF_FZ2_ECO);
        rebuildFuzz(p->fuzz2Muff,    std::make_unique<EHXBigMuff>(),      p->lastFz2Eco);
        rebuildFuzz(p->fuzz2Bender,  std::make_unique<ToneBenderMkII>(),  p->lastFz2Eco);
        rebuildFuzz(p->fuzz2Octavia, std::make_unique<Octavia>(),         p->lastFz2Eco);
        rebuildFuzz(p->fuzz2Factory, std::make_unique<ZVexFuzzFactory>(), p->lastFz2Eco);
    }
    if (ecoNow(HF_NAIL_ECO) != p->lastNailEco) {
        p->lastNailEco = ecoNow(HF_NAIL_ECO);
        rebuildFuzz(p->nail,  std::make_unique<NailDistortion>(), p->lastNailEco);
        if (p->nail) p->nail->setParameter("mode", (float)p->lastNailMode);
    }
    if (ecoNow(HF_NAIL2_ECO) != p->lastNail2Eco) {
        p->lastNail2Eco = ecoNow(HF_NAIL2_ECO);
        rebuildFuzz(p->nail2, std::make_unique<NailDistortion>(), p->lastNail2Eco);
        if (p->nail2) p->nail2->setParameter("mode", (float)p->lastNail2Mode);
    }
    p->swAcceptNext = true;   // adopt the new topology silently next block
}

static void hf_run(LV2_Handle h, uint32_t n) {
    DenormalGuard denormalGuard;
    auto* p = static_cast<HexForge*>(h);
    const URIs& u = p->uris;
    // ── Output Voice: FRFR (2026-08-21, global layer — auto-cal pattern, never
    // preset-captured). OFF = Headphones/Studio = bit-identical. ON = the
    // FRFR-10 room voice: de-close-mic EQ post-mono-sum (below), plus the
    // doubler and airFeel are auto-muted (mono speaker; the real room supplies
    // the reflections a close-mic IR lacks).
    const bool fvOn = p->ports[HF_OUT_VOICE] && *p->ports[HF_OUT_VOICE] > 0.5f;

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
                else if (which == u.amp2_nam){ dst = p->amp2NamPath; msg.type = W_NAM_LOAD; msg.namSlot = 3; }
                else if (which == u.dr2_nam) { dst = p->dr2NamPath; msg.type = W_NAM_LOAD; msg.namSlot = 4; }
                else if (which == u.ir2_file){
                    // Cab 2 user IR: "@builtin" (or empty) = defer to the rb_cab
                    // dropdown -- clear the override and let run() regenerate the
                    // sentinel; a real path loads on the Cab 2 slot.
                    const char* e2 = (std::strcmp(path, "@builtin") == 0) ? "" : path;
                    std::strncpy(p->ir2Path, e2, kPathMax-1); p->ir2Path[kPathMax-1]='\0';
                    if (std::strcmp(e2, "@nocab") == 0) { p->lastCab2Model = -2; continue; }   // bypassed below; nothing to load
                    if (e2[0]) {
                        WorkMsg m2; m2.type = W_CAB_IR; m2.namSlot = 1;
                        std::strncpy(m2.path, e2, kPathMax-1); m2.path[kPathMax-1]='\0';
                        p->schedule->schedule_work(p->schedule->handle, sizeof(m2), &m2);
                    } else p->lastCab2Model = -1;   // re-sentinel from rb_cab next block
                    continue;
                }
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
                writeFileToNotify(p, u.amp2_nam, p->amp2NamPath);
                writeFileToNotify(p, u.ir2_file, p->ir2Path);
                writeFileToNotify(p, u.dr2_nam, p->dr2NamPath);
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
        psRecallRequest(p, p->curBank, p->curSlot);                      // re-applies sound + refreshes the UI list (seamless)
    // Direct jump from the UI list: recall when ps_goto changes to a valid index.
    {
        const float gf = p->hostPorts[HF_PS_GOTO] ? *p->hostPorts[HF_PS_GOTO] : -1.0f;
        const int g = static_cast<int>(std::lround(gf));
        if (g >= 0 && g < kBanks * kSlots && g != p->lastGoto) { p->lastGoto = g; psRecallRequest(p, g / kSlots, g % kSlots); }
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

    // ── Auto-calibration wizard ── measures the RAW input (same tap as the tuner,
    // pre-Input-Trim: the domain every gate threshold was floor-complianced in).
    // cal_cmd is UI-pulsed: 1-3 start that phase, 9 aborts. Results/recommendation
    // ride the #cal notify string; Apply writes the cal_*_offs ports (below).
    {
        const float cv = p->ports[HF_CAL_CMD] ? *p->ports[HF_CAL_CMD] : 0.0f;
        if (cv != p->calCmdPrev) {
            p->calCmdPrev = cv;
            const int cmd = (int)(cv + 0.5f);
            if (cmd >= 1 && cmd <= 3) {
                p->calMeas.begin(p->rate);
                p->calState = cmd;
                p->calSamplesTotal = p->calSamplesLeft =
                    (uint32_t)(p->rate * (cmd == 3 ? 6.0 : 5.0));
            } else if (cmd == 9) {
                p->calState = 0;
                p->calSamplesLeft = 0;
            }
        }
        if (p->calState >= 1 && p->calState <= 3 && inL && inR) {
            const uint32_t len = n < p->calSamplesLeft ? n : p->calSamplesLeft;
            for (uint32_t i = 0; i < len; ++i)
                p->calMeas.feed(0.5f * (inL[i] + inR[i]));
            p->calSamplesLeft -= len;
            if (p->calSamplesLeft == 0) {
                const int ph = p->calState - 1;
                p->calPh[ph] = p->calMeas.finish();
                p->calPhDone[ph] = true;
                if (p->calPhDone[0] && p->calPhDone[1] && p->calPhDone[2]) {
                    p->calRec = calRecommend(p->calPh[0], p->calPh[1], p->calPh[2]);
                    p->calState = p->calRec.error ? 5 : 4;
                } else {
                    p->calState = 0;
                }
                p->calNotifyTicks = 70;   // ~5 s of #cal re-pushes at the 14 Hz throttle
            }
        }
        if (p->ports[HF_CAL_STATE])    *p->ports[HF_CAL_STATE] = (float)p->calState;
        if (p->ports[HF_CAL_PROGRESS]) *p->ports[HF_CAL_PROGRESS] =
            (p->calSamplesTotal && p->calState >= 1 && p->calState <= 3)
                ? 1.0f - (float)p->calSamplesLeft / (float)p->calSamplesTotal : 0.0f;
        // Slew the applied offset layer toward the ports (~10 dB/s, click-free Apply).
        const float trimTgt  = p->ports[HF_CAL_TRIM_OFFS]  ? *p->ports[HF_CAL_TRIM_OFFS]  : 0.0f;
        const float floorTgt = p->ports[HF_CAL_FLOOR_OFFS] ? *p->ports[HF_CAL_FLOOR_OFFS] : 0.0f;
        if (!p->calOffsInit) { p->calOffsInit = true; p->calTrimOffsSm = trimTgt; p->calFloorOffsSm = floorTgt; }
        const float step = 10.0f * (float)n / (float)p->rate;
        auto slew = [step](float cur, float tgt) {
            const float d = tgt - cur;
            return (std::fabs(d) <= step) ? tgt : cur + (d > 0.0f ? step : -step);
        };
        p->calTrimOffsSm  = slew(p->calTrimOffsSm,  trimTgt);
        p->calFloorOffsSm = slew(p->calFloorOffsSm, floorTgt);
    }
    // Mute while measuring the floors (phases 1-2: idle noise through a cranked
    // preset is the very thing being measured); phase 3 passes audio — the player
    // must hear the rig to genuinely play their hardest.
    const bool calMute = (p->calState == 1 || p->calState == 2);

    // ── Global bypass: unity passthrough (or silence while tuning-muted) ──
    if (*p->ports[HF_BYPASS] > 0.5f) {
        if (tunerMute || calMute) { std::memset(outL, 0, sizeof(float)*n); std::memset(outR, 0, sizeof(float)*n); }
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
    p->trimLoad.set(*p->ports[HF_IT_LOAD]);
    const float itGain    = std::pow(10.0f, (*p->ports[HF_IT_GAIN] + p->calTrimOffsSm)/20.0f)
                            * ((*p->ports[HF_IT_PHASE] > 0.5f) ? -1.0f : 1.0f);
    const bool  itHum     = *p->ports[HF_IT_HUM] > 0.5f;
    const bool  itHB      = *p->ports[HF_IT_HUMBK] > 0.5f;
    const int   itHBModel = (int)(*p->ports[HF_IT_HBMODEL] + 0.5f);
    if (itHB) p->trimVoice.prepare(p->rate, itHBModel, *p->ports[HF_IT_HBAMT]);   // recompute on model/amount change
    const bool  itBoost   = *p->ports[HF_IT_BOOST] > 0.5f;
    if (itBoost) p->trimBoost.prepare(p->rate, *p->ports[HF_IT_BOOSTAMT]);
    // Gate
    p->gate.setBypass(false);
    p->gate.setParameter("threshold",
        std::clamp(*p->ports[HF_GT_THRESH] + p->calFloorOffsSm, -80.0f, 0.0f));
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
    p->fuzzBender->setParameter("gvol",      *p->ports[HF_FZ_GVOL]);
    p->drive.setParameter("eco",       *p->ports[HF_DR_ECO]);
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
    // Drive B — same pedal family, no NAM (one neural slot per instance); a NAM
    // selection arriving anyway (e.g. hand-edited pedalboard) falls back to model 0.
    p->drive2.setBypass(false);
    const int drive2Model = clampi(*p->ports[HF_DR2_MODEL], 0, kDrMax);
    int drive2Algo = (drive2Model == kDrNamIdx) ? 0 : drive2Model;   // NAM (v40): algo core parked on Green Man
    if (drive2Algo != p->lastDrive2Model) { p->lastDrive2Model = drive2Algo; p->drive2.setType(kDriveMap[drive2Algo]); }
    p->drive2.setParameter("eco",    *p->ports[HF_DR2_ECO]);
    p->drive2.setParameter("drive",  *p->ports[HF_DR2_DRIVE]);
    p->drive2.setParameter("tone",   *p->ports[HF_DR2_TONE]);
    p->drive2.setParameter("level",  *p->ports[HF_DR2_LEVEL]);
    p->drive2.setParameter("mix",    *p->ports[HF_DR2_MIX]);
    p->drive2.setParameter("octave", *p->ports[HF_DR2_OCTAVE]);
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
    const bool ecoQ = *p->ports[HF_QUALITY] > 0.5f;
    if (ampIsAlgo && (ampModel != p->lastAmpModel || ecoQ != p->lastEco)) {   // rebuild for algo models (model or Engine Quality change)
        WorkMsg msg; msg.type=W_AMP_LOAD; msg.modelIdx=ampModel; msg.eco=ecoQ;
        if (p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg) == LV2_WORKER_SUCCESS)
            { p->lastAmpModel = ampModel; p->lastEco = ecoQ; }
    }
    // ── Rig B (dual amp/cab, 2026-07-30): schedule builds + set params ──────
    const bool rbOn = *p->ports[HF_RB_ENABLE] > 0.5f;
    if (!rbOn && p->amp2 && !p->amp2Requested && !p->pendAmp2) {
        // Rig B disabled: release the second amp (it is only processed while
        // enabled, so a direct handoff to the free worker is safe) -- a player
        // who parked Rig B should get the RAM back.
        WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = p->amp2;
        if (p->schedule->schedule_work(p->schedule->handle, sizeof(fm), &fm) == LV2_WORKER_SUCCESS) {
            p->amp2 = nullptr; p->lastAmp2Model = -1;
        }
    }
    if (rbOn) {
        int rbModel = clampi(*p->ports[HF_RB_AMP], 0, kMt15Idx);
        const bool rbNamSel = (rbModel == kAmpNamIdx);   // Neural on the B side (v39)
        const bool rbEco = *p->ports[HF_RB_ECO] > 0.5f;
        // amp2Requested guards the build-in-flight window: without it, !p->amp2
        // stays true until the crossfade swap lands and this scheduled a fresh
        // ~MB amp build EVERY BLOCK (user-reported RAM/CPU runaway, 2026-07-30).
        if (!rbNamSel && (rbModel != p->lastAmp2Model || rbEco != p->lastAmp2Eco || !p->amp2) && !p->amp2Requested) {
            WorkMsg msg; msg.type=W_AMP_LOAD; msg.modelIdx=rbModel; msg.eco=rbEco; msg.namSlot=1;
            if (p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg) == LV2_WORKER_SUCCESS)
                { p->lastAmp2Model = rbModel; p->lastAmp2Eco = rbEco; p->amp2Requested = true; }
        }
        const int rbCab = clampi(*p->ports[HF_RB_CAB], 0, 6);
        const bool ir2NoCab = (std::strcmp(p->ir2Path, "@nocab") == 0);   // unified picker's No Cab entry
        const bool ir2User = (p->ir2Path[0] != '\0') && !ir2NoCab;   // IR/sentinel override active (v39)
        if (ir2User && p->lastCab2Model != -2) {
            // recall/patch installed a user IR; mark the sentinel cache invalid so a
            // later clear re-generates. -2 = "user IR active".
            WorkMsg m2; m2.type = W_CAB_IR; m2.namSlot = 1;
            std::strncpy(m2.path, p->ir2Path, kPathMax-1); m2.path[kPathMax-1]='\0';
            if (p->schedule->schedule_work(p->schedule->handle, sizeof(m2), &m2) == LV2_WORKER_SUCCESS)
                p->lastCab2Model = -2;
        }
        if (!ir2User && rbCab != p->lastCab2Model && rbCab < 6) {
            static const char* kRbCabIr[6] = { "@factory", "@vox2x12", "@american-ob", "@greenback", "@hiwatt", "@doom" };
            WorkMsg msg; msg.type=W_CAB_IR; msg.namSlot=1;
            std::snprintf(msg.path, sizeof(msg.path), "%s", kRbCabIr[rbCab]);
            if (p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg) == LV2_WORKER_SUCCESS)
                p->lastCab2Model = rbCab;
        }
        if (!ir2User && rbCab >= 6) p->lastCab2Model = rbCab;   // No Cab (Direct): bypass below
        if (p->amp2) {
            AmpBlockExtended* a2 = p->amp2;
            const int rbM = p->lastAmp2Model;
            a2->setBypass(false);
            // Full A-side parity (2026-07-30 redesign): every model family gets
            // its complete control set on the B side too.
            if (rbM == kSunnIdx) {
                a2->setParameter("vol1",         *p->ports[HF_RB_GAIN]);
                a2->setParameter("vol2",         *p->ports[HF_RB_SUNN_VOL2]);
                a2->setParameter("channel_link", *p->ports[HF_RB_SUNN_LINK]);
                a2->setParameter("bass1",        *p->ports[HF_RB_BASS]);
                a2->setParameter("mid1",         *p->ports[HF_RB_MID]);
                a2->setParameter("treble1",      *p->ports[HF_RB_TREBLE]);
                a2->setParameter("bass2",        *p->ports[HF_RB_SUNN_BASS2]);
                a2->setParameter("mid2",         *p->ports[HF_RB_SUNN_MID2]);
                a2->setParameter("treble2",      *p->ports[HF_RB_SUNN_TREBLE2]);
                a2->setParameter("bright1",      *p->ports[HF_RB_SUNN_BRIGHT1]);
                a2->setParameter("bright2",      *p->ports[HF_RB_SUNN_BRIGHT2]);
            } else {
                a2->setParameter("gain",   *p->ports[HF_RB_GAIN]);
                a2->setParameter("bass",   *p->ports[HF_RB_BASS]);
                a2->setParameter("mid",    *p->ports[HF_RB_MID]);
                a2->setParameter("treble", *p->ports[HF_RB_TREBLE]);
            }
            a2->setParameter("presence",  *p->ports[HF_RB_PRESENCE]);
            a2->setParameter("master",    *p->ports[HF_RB_MASTER]);
            a2->setParameter("sag",       *p->ports[HF_RB_SAG]);
            a2->setParameter("channel",   *p->ports[HF_RB_CHANNEL]);
            a2->setParameter("resonance", *p->ports[HF_RB_RESONANCE]);
            if (rbM == kFriedmanIdx) {
                a2->setParameter("channel", *p->ports[HF_RB_FR_CHANNEL]);
                a2->setParameter("fat",     *p->ports[HF_RB_FR_FAT]);
                a2->setParameter("c45",     *p->ports[HF_RB_FR_C45]);
                a2->setParameter("sat",     *p->ports[HF_RB_FR_SAT]);
            }
            if (rbM == 10) {
                a2->setParameter("vol2",   *p->ports[HF_RB_PL_VOL2]);
                a2->setParameter("variac", *p->ports[HF_RB_PL_VARIAC]);   // brown sound (v41)
            }
            if (rbM == 1) a2->setParameter("sir34", *p->ports[HF_RB_SIR34]);   // SIR #34 (v43)
            if (rbM == kMesaIdx) {
                a2->setParameter("mode", *p->ports[HF_RB_MV_MODE]);
                a2->setParameter("geq0", *p->ports[HF_RB_MV_GEQ0]);
                a2->setParameter("geq1", *p->ports[HF_RB_MV_GEQ1]);
                a2->setParameter("geq2", *p->ports[HF_RB_MV_GEQ2]);
                a2->setParameter("geq3", *p->ports[HF_RB_MV_GEQ3]);
                a2->setParameter("geq4", *p->ports[HF_RB_MV_GEQ4]);
                a2->setParameter("eqpreset", *p->ports[HF_RB_MV_EQPRESET]);
            }
            if (rbM == kRectoIdx) {
                a2->setParameter("mode",   *p->ports[HF_RB_RC_MODE]);
                a2->setParameter("variac", *p->ports[HF_RB_RC_VARIAC]);
                a2->setParameter("rect",   *p->ports[HF_RB_RC_RECT]);
            }
            if (rbM == kMt15Idx) {
                a2->setParameter("mode",   *p->ports[HF_RB_MT_MODE]);
                a2->setParameter("bright", *p->ports[HF_RB_MT_BRIGHT]);
            }
            const int rbAlgo = (rbM < 0) ? 1 : ((rbM == 5) ? 1 : rbM);
            const float rbRcMode = *p->ports[HF_RB_RC_MODE];
            const bool rbRectoModern = (rbM == kRectoIdx) &&
                ((rbRcMode > 3.5f && rbRcMode < 4.5f) || rbRcMode > 6.5f);
            if (*p->ports[HF_RB_PAMP_AUTO] > 0.5f) {
                const auto d2 = PowerAmpProcessor::getDefaultsForModel(kCanonical[rbAlgo]);
                p->pa2.setParameter("master",   d2.master);
                p->pa2.setParameter("presence", d2.presence);
                p->pa2.setParameter("depth",    d2.depth);
                p->pa2.setParameter("nfb",      rbRectoModern ? 0.05f : d2.nfb);
                p->pa2.setParameter("sag",      d2.sag);
                { const int t2 = p->amp2->getRecommendedTubeType();
                  if (t2 >= 0) p->pa2.setTubeType(static_cast<TubeType>(t2)); }
            } else {
                p->pa2.setParameter("presence", *p->ports[HF_RB_PAMP_PRESENCE]);
                p->pa2.setParameter("depth",    *p->ports[HF_RB_PAMP_DEPTH]);
                p->pa2.setParameter("sag",      *p->ports[HF_RB_PAMP_SAG]);
                p->pa2.setParameter("master",   *p->ports[HF_RB_PAMP_MASTER]);
                p->pa2.setParameter("nfb",      *p->ports[HF_RB_PAMP_NFB]);
                p->pa2.setTubeType(static_cast<TubeType>(clampi(*p->ports[HF_RB_PAMP_TUBE], 0, 4)));   // 4 = 6V6 (2026-08-21)
            }
            p->pa2.setParameter("resonance", *p->ports[HF_RB_PAMP_RESONANCE]);
            p->pa2.setParameter("airFeel",   fvOn ? 0.0f : *p->ports[HF_RB_PAMP_AIRFEEL]);   // FRFR voice: air off
            p->pa2.setParameter("coupling",  *p->ports[HF_RB_PAMP_COUPL]);
            {   // per-amp voicing rows stay authoritative for the deep internals
                const auto d2 = PowerAmpProcessor::getDefaultsForModel(kCanonical[rbAlgo]);
                p->pa2.setParameter("bloomvca", d2.bloomVca);
                p->pa2.setParameter("duty",     d2.duty);
                p->pa2.setParameter("evengen",  d2.evenDepth);
                p->pa2.setParameter("padrive",  d2.paDrive);
                p->pa2.setParameter("pamakeup", d2.paMakeup);
                p->pa2.setParameter("ripplesag",d2.rippleSagCoupling);
                p->pa2.setParameter("ltptail",  d2.ltpTail);
                p->pa2.setParameter("fluxOT",   d2.fluxOT ? 1.0f : 0.0f);
            }
            p->pa2.setBypass((*p->ports[HF_RB_PAMP_BYPASS] > 0.5f) || rbM == kSunnIdx);
            // Full cab parity
            p->cab2.setParameter("lowCutHz",  *p->ports[HF_RB_LOWCUT]);
            p->cab2.setParameter("highCutHz", *p->ports[HF_RB_HIGHCUT]);
            p->cab2.setParameter("mix",       *p->ports[HF_RB_CABMIX]);
            p->cab2.setParameter("micpos",    *p->ports[HF_RB_CABMICPOS]);
            p->cab2.setParameter("micdist",   *p->ports[HF_RB_CABMICDIST]);
            p->cab2.setParameter("roomon",    *p->ports[HF_RB_CABROOMON]);
            p->cab2.setParameter("roommix",   *p->ports[HF_RB_CABROOMMIX]);
            p->cab2.setParameter("roomamt",   *p->ports[HF_RB_CABROOMAMT]);
            p->cab2.setParameter("roomdense", *p->ports[HF_RB_CABROOMDENSE]);
            p->cab2.setParameter("voice",     *p->ports[HF_RB_CABVOICE]);
            p->cab2.setParameter("monoroom",  (p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f) ? 1.0f : 0.0f);
            { const int spk = clampi(*p->ports[HF_RB_CABSPKDRIVE], 0, 2);
              p->cab2.setParameter("spkdrive",    spk > 0 ? 1.0f : 0.0f);
              p->cab2.setParameter("spkdriveamt", spk >= 2 ? 0.75f : (spk == 1 ? 0.35f : 0.0f)); }
            // No Cab (Direct): the player may run rig A into a real power amp +
            // physical cab -- rig B then skips speaker sim too.
            p->cab2.setBypass((std::strcmp(p->ir2Path, "@nocab") == 0)
                              || (p->ir2Path[0] == '\0' && clampi(*p->ports[HF_RB_CAB], 0, 6) >= 6));
        }
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
    if (ampModel == 10) {
        amp->setParameter("vol2",   *p->ports[HF_AMP_PL_VOL2]);
        amp->setParameter("variac", *p->ports[HF_AMP_PL_VARIAC]);   // brown sound (v41)
    }
    if (ampModel == 1) amp->setParameter("sir34", *p->ports[HF_AMP_SIR34]);   // SIR #34 (v43)
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
        p->pa.setParameter("airFeel",   fvOn ? 0.0f : *p->ports[HF_AMP_PAMP_AIRFEEL]);   // FRFR voice: real room supplies the air
        desiredTube = kAmpTube[ampAlgo];
    } else {
        p->pa.setParameter("presence",  *p->ports[HF_AMP_PAMP_PRESENCE]);
        p->pa.setParameter("depth",     *p->ports[HF_AMP_PAMP_DEPTH]);
        p->pa.setParameter("sag",       *p->ports[HF_AMP_PAMP_SAG]);
        p->pa.setParameter("master",    *p->ports[HF_AMP_PAMP_MASTER]);
        p->pa.setParameter("nfb",       *p->ports[HF_AMP_PAMP_NFB]);
        p->pa.setParameter("resonance", *p->ports[HF_AMP_PAMP_RESONANCE]);
        p->pa.setParameter("airFeel",   fvOn ? 0.0f : *p->ports[HF_AMP_PAMP_AIRFEEL]);   // FRFR voice: real room supplies the air
        desiredTube = clampi(*p->ports[HF_AMP_PAMP_TUBE], 0, 4);   // 4 = 6V6 (2026-08-21)
    }
    // Post-saturation sag-VCA depth is a per-amp voicing value with no user port.
    p->pa.setParameter("bloomvca", PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).bloomVca);
    p->pa.setParameter("duty",     PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).duty);
    p->pa.setParameter("evengen",  PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).evenDepth);
    p->pa.setParameter("ripplesag",PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).rippleSagCoupling);
    p->pa.setParameter("ltptail",  PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).ltpTail);
    p->pa.setParameter("fluxOT",   PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).fluxOT ? 1.0f : 0.0f);
    // Per-amp PA drive/makeup (2026-07-31 fix): these were applied only to Rig B's
    // pa2 -- the main rig ran them at the 1.0 default, so EVH/Rockerverb/Vox (the
    // three models with non-neutral values) were 2-3x over-driven vs their
    // capture-tuned voicing. 1.0/1.0 for every other model = bit-identical.
    p->pa.setParameter("padrive",  PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).paDrive);
    p->pa.setParameter("pamakeup", PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).paMakeup);
    // HG round 2 per-amp bakes (2026-08-20): knee/tilt from AmpDefaults (the
    // tilt setters early-out on unchanged values, so per-run pushes are cheap).
    p->pa.setParameter("pakneecurve",  PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).kneeCurve);
    p->pa.setParameter("shapertilt",   PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).shaperTiltDb);
    p->pa.setParameter("shapertiltlo", PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).shaperTiltLoDb);
    // (The 2026-08-20 restored Fender PA-compression lab lived here for mv184
    // only. VERDICT: the user finally swept it by ear and chose STOCK
    // (depth 0.15 / release 13 ms) — nothing baked; the PA-compression
    // frontier is now closed WITH an ears verdict, not just by abandonment.
    // See pa-compression-fender memory.)
    // EVH capture-fit voicing (baked): applied post-PA in the amp block below.
    const bool evhFitOn = kAmpMap[ampAlgo] == AmpModel::EVH5150III;
    // (The 2026-08-20 dbg_hgfit LAB lived here; retired at mv181 after the user
    // approved both candidates at blend 0.72 — values baked into AmpDefaults
    // rows 1/8 with the Plexi/MarkV row-10 split.)
    // Recto CH3-Modern capture-fit voicing (BAKED 2026-08-20 at the user's
    // blend 1.0; the nonlinear half — satDrive 1.84, tightHP 200 — is baked in
    // the model's mode-7 ModeCfg row): applied post-PA in the amp block below,
    // Diamond Plate + mode 7 only. See lv2/common/RectoCaptureFit.h.
    const bool rectoFitOn = (ampModel == kRectoIdx) && rcMode > 6.5f;
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
    // monoRoom (2026-08-21): with the output mono-summed, the cab room's two
    // decorrelated banks fold into a doubled static comb — flanger-like woosh
    // on broadband playing (Periphery presets, one-speaker rig). One bank when
    // mono; bit-identical when the sum is off.
    const bool monoSum = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
    p->cab.setParameter("monoroom", monoSum ? 1.0f : 0.0f);
    p->pa.setParameter("coupling",   *p->ports[HF_AMP_PAMP_COUPL]);   // speaker-impedance coupling (v24)
    p->reverb.setParameter("density", *p->ports[HF_RV_DENSITY]);      // classic / dense tank (v25)
    p->reverb.setParameter("type",    *p->ports[HF_RV_TYPE]);          // plate / spring / ambient (v26/v27)
    p->reverb.setParameter("bloom",   *p->ports[HF_RV_BLOOM]);         // Hex Ambient bloom (v27)
    p->cab.setParameter("roomdense",  *p->ports[HF_CAB_ROOMDENSE]);    // classic / dense room (v26)
    {   // Speaker Drive (item #40, v28): 0 Off/1 Subtle/2 Full -> CabinetBlock's two
        // internal params. Subtle/Full (0.35/0.75) chosen conservatively for a brand-
        // new, not-yet-user-tuned character feature.
        const int spk = static_cast<int>(*p->ports[HF_CAB_SPKDRIVE] + 0.5f);
        p->cab.setParameter("spkdrive",    spk > 0 ? 1.0f : 0.0f);
        p->cab.setParameter("spkdriveamt", spk >= 2 ? 0.75f : (spk == 1 ? 0.35f : 0.0f));
    }
    {   // EQ block: preset base curve + slider offsets + level (rebuilds only on change)
        const float eqDb[GraphicEQ::kBands] = {
            *p->ports[HF_EQ_100], *p->ports[HF_EQ_200], *p->ports[HF_EQ_400],
            *p->ports[HF_EQ_800], *p->ports[HF_EQ_1K6], *p->ports[HF_EQ_3K2],
        };
        p->eq.update((int)*p->ports[HF_EQ_PRESET], eqDb, *p->ports[HF_EQ_LEVEL]);
    }
    // Modfx
    p->modfx.setBypass(false);
    const int modfxType = clampi(*p->ports[HF_MD_TYPE], 0, 8);   // was stuck at 6: Seasick Vibe (7) silently ran as Nevermind Chorus
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
    p->modfx.setParameter("shape", *p->ports[HF_MD_SHAPE]);          // tremolo waveform (ignored by other types)
    // Mono mode: force EVERY width-based stereo effect to CENTERED (width 0) so a summed-
    // mono rig keeps full-level content. Otherwise the delay's pan (Seraph) loses ~6 dB to
    // pan law, the Digital delay's L/R time offset combs, and the chorus/Uni-Vibe LFO-phase
    // spread partially cancels — all read as the effect "cutting out" in mono. Reverb has no
    // width knob (decorrelated tails just narrow, never null) so the output sum covers it.
    const bool monoOut = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
    p->modfx.setParameter("stereoWidth", monoOut ? 0.0f : *p->ports[HF_MD_WIDTH]);
    // Delay
    p->delay.setBypass(false);
    const int delayType = clampi(*p->ports[HF_DL_TYPE], 0, 4);
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
    p->delay.setParameter("age",      *p->ports[HF_DL_AGE]);        // EP-3 (v42)
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

    // ── X2 second instances (v38): params applied only while the block is enabled
    // (cheap idle; values re-read on the first enabled run, so nothing goes stale).
    int fz2Pedal = 0;
    if (*p->ports[HF_GT2_ENABLE] > 0.5f) {
        p->gate2.setBypass(false);
        p->gate2.setParameter("threshold",
            std::clamp(*p->ports[HF_GT2_THRESH] + p->calFloorOffsSm, -80.0f, 0.0f));
        p->gate2.setParameter("attack",     *p->ports[HF_GT2_ATTACK]);
        p->gate2.setParameter("hold",       *p->ports[HF_GT2_HOLD]);
        p->gate2.setParameter("release",    *p->ports[HF_GT2_RELEASE]);
        p->gate2.setParameter("hysteresis", *p->ports[HF_GT2_HYST]);
    }
    if (*p->ports[HF_CP2_ENABLE] > 0.5f) {
        p->comp2.setBypass(false);
        p->comp2.setParameter("type",      *p->ports[HF_CP2_TYPE]);
        p->comp2.setParameter("threshold", *p->ports[HF_CP2_THRESH]);
        p->comp2.setParameter("ratio",     *p->ports[HF_CP2_RATIO]);
        p->comp2.setParameter("attack",    *p->ports[HF_CP2_ATTACK]);
        p->comp2.setParameter("release",   *p->ports[HF_CP2_RELEASE]);
        p->comp2.setParameter("knee",      *p->ports[HF_CP2_KNEE]);
        p->comp2.setParameter("makeup",    *p->ports[HF_CP2_MAKEUP]);
    }
    if (*p->ports[HF_FZ2_ENABLE] > 0.5f) {
        fz2Pedal = clampi(*p->ports[HF_FZ2_PEDAL], 0, 3);
        p->fuzz2Muff->setBypass(false); p->fuzz2Bender->setBypass(false);
        p->fuzz2Octavia->setBypass(false); p->fuzz2Factory->setBypass(false);
        p->fuzz2Muff->setParameter("era",   *p->ports[HF_FZ2_MODE]);
        p->fuzz2Muff->setParameter("drive", *p->ports[HF_FZ2_SUSTAIN]);
        p->fuzz2Muff->setParameter("tone",  *p->ports[HF_FZ2_TONE]);
        p->fuzz2Muff->setParameter("level", *p->ports[HF_FZ2_VOLUME]);
        p->fuzz2Bender->setParameter("attack",    *p->ports[HF_FZ2_SUSTAIN]);
        p->fuzz2Bender->setParameter("level",     *p->ports[HF_FZ2_VOLUME]);
        p->fuzz2Bender->setParameter("bias",      *p->ports[HF_FZ2_BIAS]);
        p->fuzz2Bender->setParameter("inputtrim", *p->ports[HF_FZ2_INPUTTRIM]);
        p->fuzz2Bender->setParameter("getemp",    *p->ports[HF_FZ2_GETEMP]);
        p->fuzz2Bender->setParameter("gvol",      *p->ports[HF_FZ2_GVOL]);
        p->fuzz2Octavia->setParameter("drive", *p->ports[HF_FZ2_SUSTAIN]);
        p->fuzz2Octavia->setParameter("tone",  *p->ports[HF_FZ2_TONE]);
        p->fuzz2Octavia->setParameter("level", *p->ports[HF_FZ2_VOLUME]);
        p->fuzz2Factory->setParameter("sustain",   *p->ports[HF_FZ2_SUSTAIN]);
        p->fuzz2Factory->setParameter("bias",      *p->ports[HF_FZ2_BIAS]);
        p->fuzz2Factory->setParameter("inputtrim", *p->ports[HF_FZ2_INPUTTRIM]);
        p->fuzz2Factory->setParameter("getemp",    *p->ports[HF_FZ2_GETEMP]);
        p->fuzz2Factory->setParameter("level",     *p->ports[HF_FZ2_VOLUME]);
    }
    if (*p->ports[HF_NAIL2_ENABLE] > 0.5f) {
        p->nail2->setBypass(false);
        const int nail2Mode = clampi(*p->ports[HF_NAIL2_MODE], 0, NailDistortion::kNumModes - 1);
        if (nail2Mode != p->lastNail2Mode) { p->lastNail2Mode = nail2Mode; p->nail2->setParameter("mode", (float)nail2Mode); }
        p->nail2->setParameter("drive",   *p->ports[HF_NAIL2_DRIVE]);
        p->nail2->setParameter("tone",    *p->ports[HF_NAIL2_TONE]);
        p->nail2->setParameter("texture", *p->ports[HF_NAIL2_TEXTURE]);
        p->nail2->setParameter("level",   *p->ports[HF_NAIL2_LEVEL]);
    }
    if (*p->ports[HF_MD2_ENABLE] > 0.5f) {
        p->modfx2.setBypass(false);
        const int modfx2Type = clampi(*p->ports[HF_MD2_TYPE], 0, 8);
        if (modfx2Type != p->lastModfx2Type) { p->lastModfx2Type = modfx2Type; p->modfx2.setType(ModulationFactory::fromIndex(modfx2Type)); }
        if (*p->ports[HF_MD2_SYNC] > 0.5f) {
            const int mv2 = clampi(*p->ports[HF_MD2_DIV], 0, 7);
            const float ps2 = (60.0f / p->hostBpm) * kDivFactor[mv2];
            p->modfx2.setSyncHz(ps2 > 0.0f ? (1.0f / ps2) : 0.0f);
        } else p->modfx2.setSyncHz(0.0f);
        p->modfx2.setParameter("rate",        *p->ports[HF_MD2_RATE]);
        p->modfx2.setParameter("depth",       *p->ports[HF_MD2_DEPTH]);
        p->modfx2.setParameter("mix",         *p->ports[HF_MD2_MIX]);
        p->modfx2.setParameter("centerDelay", *p->ports[HF_MD2_OFFSET]);
        p->modfx2.setParameter("shape",       *p->ports[HF_MD2_SHAPE]);
        const bool monoOut2 = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
        p->modfx2.setParameter("stereoWidth", monoOut2 ? 0.0f : *p->ports[HF_MD2_WIDTH]);
    }
    if (*p->ports[HF_DL2_ENABLE] > 0.5f) {
        p->delay2.setBypass(false);
        const int delay2Type = clampi(*p->ports[HF_DL2_TYPE], 0, 4);
        if (delay2Type != p->lastDelay2Type) { p->lastDelay2Type = delay2Type; p->delay2.setType(DelayFactory::fromIndex(delay2Type)); }
        float dl2Ms = *p->ports[HF_DL2_TIME];
        if (*p->ports[HF_DL2_SYNC] > 0.5f) {
            const int dv2 = clampi(*p->ports[HF_DL2_DIV], 0, 7);
            dl2Ms = (60000.0f / p->hostBpm) * kDivFactor[dv2];
            if (dl2Ms < 1.0f) dl2Ms = 1.0f; else if (dl2Ms > 2000.0f) dl2Ms = 2000.0f;
        }
        const bool monoOut2 = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
        p->delay2.setParameter("timeMs",       dl2Ms);
        p->delay2.setParameter("feedback",     *p->ports[HF_DL2_FEEDBACK]);
        p->delay2.setParameter("mix",          *p->ports[HF_DL2_MIX]);
        p->delay2.setParameter("stereoWidth",  monoOut2 ? 0.0f : *p->ports[HF_DL2_WIDTH]);
        p->delay2.setParameter("wowDepth",     *p->ports[HF_DL2_WOW]);
        p->delay2.setParameter("flutterDepth", *p->ports[HF_DL2_FLUTTER]);
        p->delay2.setParameter("headMask", static_cast<float>(kEchorecProgram[clampi(*p->ports[HF_DL2_HEADS],0,11)]));
        p->delay2.setParameter("pattern",  *p->ports[HF_DL2_PATTERN]);
        p->delay2.setParameter("ducking",  *p->ports[HF_DL2_DUCKING]);
        p->delay2.setParameter("modDepth", *p->ports[HF_DL2_MODDEPTH]);
        p->delay2.setParameter("modRate",  *p->ports[HF_DL2_MODRATE]);
        p->delay2.setParameter("age",      *p->ports[HF_DL2_AGE]);   // EP-3 (v42)
    }
    if (*p->ports[HF_RV2_ENABLE] > 0.5f) {
        p->reverb2.setBypass(false);
        p->reverb2.setParameter("density",    *p->ports[HF_RV2_DENSITY]);
        p->reverb2.setParameter("type",       *p->ports[HF_RV2_TYPE]);
        p->reverb2.setParameter("bloom",      *p->ports[HF_RV2_BLOOM]);
        p->reverb2.setParameter("preDelayMs", *p->ports[HF_RV2_PREDELAY]);
        p->reverb2.setParameter("decayTime",  *p->ports[HF_RV2_DECAY]);
        p->reverb2.setParameter("damping",    *p->ports[HF_RV2_DAMPING]);
        p->reverb2.setParameter("modDepth",   *p->ports[HF_RV2_MODDEPTH]);
        p->reverb2.setParameter("modRate",    *p->ports[HF_RV2_MODRATE]);
        p->reverb2.setParameter("mix",        *p->ports[HF_RV2_MIX]);
    }
    if (*p->ports[HF_WH2_ENABLE] > 0.5f) {
        p->wah2.setBypass(false);
        p->wah2.setParameter("type",  *p->ports[HF_WH2_TYPE]);
        p->wah2.setParameter("freq",  *p->ports[HF_WH2_FREQ]);
        p->wah2.setParameter("depth", *p->ports[HF_WH2_DEPTH]);
        p->wah2.setParameter("sens",  *p->ports[HF_WH2_SENS]);
        p->wah2.setParameter("q",     *p->ports[HF_WH2_Q]);
        p->wah2.setParameter("mix",   *p->ports[HF_WH2_MIX]);
    }
    if (*p->ports[HF_OC2_ENABLE] > 0.5f) {
        p->octave2.setBypass(false);
        p->octave2.setParameter("up",       *p->ports[HF_OC2_UP]);
        p->octave2.setParameter("down",     *p->ports[HF_OC2_DOWN]);
        p->octave2.setParameter("dry",      *p->ports[HF_OC2_DRY]);
        p->octave2.setParameter("micro",    *p->ports[HF_OC2_MICRO]);
        p->octave2.setParameter("interval", *p->ports[HF_OC2_INTERVAL]);
    }
    if (*p->ports[HF_EQ2_ENABLE] > 0.5f) {
        const float eq2Db[GraphicEQ::kBands] = {
            *p->ports[HF_EQ2_100], *p->ports[HF_EQ2_200], *p->ports[HF_EQ2_400],
            *p->ports[HF_EQ2_800], *p->ports[HF_EQ2_1K6], *p->ports[HF_EQ2_3K2],
        };
        p->eq2.update((int)*p->ports[HF_EQ2_PRESET], eq2Db, *p->ports[HF_EQ2_LEVEL]);
    }

    // ── Resolve chain order (Input Trim locked first; rest sorted by pos) ──
    int order[B_COUNT];
    for (int i=0;i<B_COUNT;++i) order[i] = i;
    int posv[B_COUNT];
    for (int i=0;i<B_COUNT;++i) posv[i] = clampi(*p->ports[kPosPort[i]], 1, 24);  // 24 movable blocks since the X2 clones (v38) — clamp must track B_COUNT
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
    // ── Seamless switching: detect discontinuous events, freeze topology while
    // the mute ramp falls, adopt the new state at the zero point (hfApplySwitch).
    if (!p->swPrevInit || p->swAcceptNext) {
        p->swPrevInit = true; p->swAcceptNext = false;
        for (int i = 0; i < B_COUNT; ++i) p->swEnabledPrev[i] = enabled[i];
        for (int w = 0; w < kSwWatchN; ++w) p->swWatchPrev[w] = *p->ports[kSwWatch[w]];
    } else {
        bool topo = false;
        for (int i = 0; i < B_COUNT; ++i) if (enabled[i] != p->swEnabledPrev[i]) topo = true;
        bool step = false;
        for (int w = 0; w < kSwWatchN; ++w)
            if (*p->ports[kSwWatch[w]] != p->swWatchPrev[w]) { step = true; p->swWatchPrev[w] = *p->ports[kSwWatch[w]]; }
        if (topo && !p->swHold) {
            p->swHold = true;
            for (int i = 0; i < B_COUNT; ++i) p->swEnabledHeld[i] = p->swEnabledPrev[i];
            if (p->swFadeState == 0) p->swFadeState = 1;
        }
        if (step && p->swFadeState == 0) p->swFadeState = 1;
    }
    if (p->swHold) for (int i = 0; i < B_COUNT; ++i) enabled[i] = p->swEnabledHeld[i];
    else           for (int i = 0; i < B_COUNT; ++i) p->swEnabledPrev[i] = enabled[i];

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
                float x = p->trimLoad.process(L[i]);   // pickup loading: physically first
                if (itHum) x = p->trimHum.process(x);
                if (itHB)    x = p->trimVoice.process(x);   // single-coil -> humbucker voicing
                if (itBoost) x = p->trimBoost.process(x);   // output boost + beef
                x *= itGain;
                L[i] = x; R[i] = x;
            }
        }

        struct CpuClk { static double now() {
            timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec); } };
        const double chunkT0 = CpuClk::now();
        // Rig B: run amp2 -> pa2 -> cab2 on the tapped buffer and mix into L/R.
        // Called after the main CAB (or after the AMP when no cab is active).
        auto rigBMix = [&](void) {
            const bool rbNamRun = (clampi(*p->ports[HF_RB_AMP], 0, kMt15Idx) == kAmpNamIdx)
                                  && p->amp2Nam && p->amp2Nam->isLoaded();
            if (!p->rigBHeld || (!rbNamRun && !p->amp2)) return;
            p->rigBHeld = false;
            const bool cab2In = *p->ports[HF_RB_CAB2ON] > 0.5f;
            const double rbT0 = CpuClk::now();
            float* io1[1] = { p->rigBBuf };
            if (rbNamRun) {
                // Neural Amp 2 (v39): mono capture + Gain/Level trims, no power amp
                // (the capture carries its own) -- mirrors the A-side NAM path.
                const float ig = std::pow(10.0f, *p->ports[HF_RB_NAM_GAIN] / 20.0f);
                const float og = std::pow(10.0f, *p->ports[HF_RB_NAM_VOL]  / 20.0f);
                for (int i=0;i<len;++i) p->rigBBuf[i] *= ig;
                p->amp2Nam->processBuffer(p->rigBBuf, p->monoOut, len);
                for (int i=0;i<len;++i) p->rigBBuf[i] = p->monoOut[i] * og;
            } else {
            p->amp2->process(io1, io1, len, 1);
            p->pa2.process(io1, io1, len, 1);
            p->amp2->setExternalSag(p->pa2.getSagEnvNorm());
            const int rbAlgo = (p->lastAmp2Model < 0) ? 1 : ((p->lastAmp2Model == 5) ? 1 : p->lastAmp2Model);
            const float mk2 = kAmpMakeup[rbAlgo];
            if (mk2 != 1.0f) for (int i=0;i<len;++i) p->rigBBuf[i] *= mk2;
            }
            // Cab 2 only when it has been brought into the chain; otherwise rig B
            // joins BEFORE Cab 1 and shares it (incl. sharing its bypass -- the
            // real-poweramp rig works naturally).
            if (cab2In) {
                const double c2T0 = CpuClk::now();
                p->cab2.process(io1, io1, len, 1);
                p->cpuCab2 += CpuClk::now() - c2T0;
            }
            // rb_locut (v44): HP the layer BEFORE it blends — the dual-rig clean
            // blends partially cancel rig A, and the clean layer's dynamics pump
            // the cancellation depth in the low-mids (the Periphery woosh,
            // isolated by build-tools/woosh2.cpp). 0 = off = bit-identical.
            const float lc = p->ports[HF_RB_LOCUT] ? *p->ports[HF_RB_LOCUT] : 0.0f;
            if (lc > 10.0f) {
                if (lc != p->rbLoCutLast) { p->rbLoCutLast = lc;
                    p->rbLoCut.setCoeffs(Filters::highpass(lc, 0.707, p->rate)); }
                for (int i=0;i<len;++i) p->rigBBuf[i] = p->rbLoCut.process(p->rigBBuf[i]);
            }
            const float blend = *p->ports[HF_RB_BLEND];
            float g = std::pow(10.0f, *p->ports[HF_RB_LEVEL] / 20.0f) * blend;
            if (*p->ports[HF_RB_POL] > 0.5f) g = -g;
            const float ga = 1.0f - blend;
            for (int i=0;i<len;++i) {
                L[i] = ga * L[i] + g * p->rigBBuf[i];
                R[i] = ga * R[i] + g * p->rigBBuf[i];
            }
            p->cpuRigB += CpuClk::now() - rbT0;
        };
        for (int oi=0; oi<B_COUNT; ++oi) {
            const int id = order[oi];
            if (!enabled[id]) continue;
            const double blkT0 = CpuClk::now();
            switch (id) {
                case B_DRIVE2:
                    if (drive2Model == kDrNamIdx && p->dr2Nam && p->dr2Nam->isLoaded()) {
                        // Neural Drive 2 (v40): mirror of the Drive 1 NAM path.
                        const float ig2=std::pow(10.0f,*p->ports[HF_DR2_NAM_GAIN]/20.0f);
                        const float og2=std::pow(10.0f,*p->ports[HF_DR2_NAM_VOL] /20.0f);
                        for (int i=0;i<len;++i) p->mono[i]=ig2*(stereo?0.5f*(L[i]+R[i]):L[i]);
                        p->dr2Nam->processBuffer(p->mono, p->monoOut, len);
                        const float mix2=*p->ports[HF_DR2_MIX], wet2=og2*mix2, dry2=1.0f-mix2;
                        for (int i=0;i<len;++i){ float d=stereo?0.5f*(L[i]+R[i]):L[i]; float o=dry2*d+wet2*p->monoOut[i]; L[i]=o; R[i]=o; }
                    } else runMono(p->drive2, L, R, len, p->mono, stereo);
                    break;
                case B_GATE: {
                    // Keyed gate (2026-07-29 idle-whine fix): detector on the RAW
                    // pre-InputTrim input -- the IT pickup voicing + boost lift the
                    // in-chain floor up to ~+20 dB, which grazed the open threshold
                    // and hysteresis-latched the gate open on the rig's hum floor
                    // (the thresholds were floor-complianced against RAW levels).
                    if (!stereo) for (int i=0;i<len;++i) p->mono[i] = L[i];
                    else         for (int i=0;i<len;++i) p->mono[i] = 0.5f*(L[i]+R[i]);
                    float* io[1] = { p->mono };
                    p->gate.processKeyed(io, inL + off, io, len, 1);
                    for (int i=0;i<len;++i) { L[i] = p->mono[i]; R[i] = p->mono[i]; }
                    break;
                }
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
                    // Rig B tap: the parallel rig feeds from the main amp's input.
                    if (*p->ports[HF_RB_ENABLE] > 0.5f
                        && (p->amp2 || (p->amp2Nam && p->amp2Nam->isLoaded()))) {
                        if (!stereo) for (int i=0;i<len;++i) p->rigBBuf[i] = L[i];
                        else         for (int i=0;i<len;++i) p->rigBBuf[i] = 0.5f*(L[i]+R[i]);
                        p->rigBHeld = true;
                    }
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
                    } else if (!stereo) {
                        // Mono chain (2026-07-23 perf): everything upstream is identical L/R,
                        // so run the 4x-oversampled cascade + PA ONCE and mirror — the amp is
                        // the heaviest internal DSP and this halves it. Bit-identical output.
                        float* io1[1] = { L };
                        amp->process(io1, io1, len, 1);
                        p->pa.process(io1, io1, len, 1);
                        amp->setExternalSag(p->pa.getSagEnvNorm()); // item #22, 2026-07-28
                        if (evhFitOn) for (int i=0;i<len;++i) L[i]=p->evhFit[0].process(L[i]);
                        if (rectoFitOn) for (int i=0;i<len;++i) L[i]=p->rectoFit[0].process(L[i]);
                        if (ampMakeup != 1.0f) for (int i=0;i<len;++i) L[i]*=ampMakeup;
                        for (int i=0;i<len;++i) R[i]=L[i];
                    } else {
                        float* io[2] = { L, R };
                        amp->process(io, io, len, 2);
                        p->pa.process(io, io, len, 2);
                        amp->setExternalSag(p->pa.getSagEnvNorm()); // item #22, 2026-07-28
                        if (evhFitOn) for (int i=0;i<len;++i) {
                            L[i]=p->evhFit[0].process(L[i]);
                            R[i]=p->evhFit[1].process(R[i]);
                        }
                        if (rectoFitOn) for (int i=0;i<len;++i) {
                            L[i]=p->rectoFit[0].process(L[i]);
                            R[i]=p->rectoFit[1].process(R[i]);
                        }
                        if (ampMakeup != 1.0f) for (int i=0;i<len;++i){ L[i]*=ampMakeup; R[i]*=ampMakeup; }
                    }
                    // NOTE: the amp's output is mono-identical when its input was — do NOT
                    // force the stereo flag; downstream blocks keep their mono fast paths.
                    break;
                }
                case B_CAB:
                    if (p->cabNam && p->cabNam->isLoaded()) {
                        // Neural cab/rig overrides the IR convolver; NAM Gain (input) + NAM Level (output), dB; Mix = dry/wet.
                        const bool wasMono = !stereo;
                        if (wasMono) for (int i=0;i<len;++i) R[i]=L[i];
                        const float ig=std::pow(10.0f,*p->ports[HF_CAB_NAM_GAIN]/20.0f);
                        const float og=std::pow(10.0f,*p->ports[HF_CAB_NAM_VOL] /20.0f);
                        for (int i=0;i<len;++i) p->mono[i]=ig*0.5f*(L[i]+R[i]);
                        p->cabNam->processBuffer(p->mono, p->monoOut, len);
                        const float mix=*p->ports[HF_CAB_MIX], dry=1.0f-mix;
                        for (int i=0;i<len;++i){ float w=p->monoOut[i]*og*mix; L[i]=dry*L[i]+w; R[i]=dry*R[i]+w; }
                        if (!wasMono) stereo = true;   // mono in -> mono-identical out
                    } else if (!stereo) {
                        // Mono chain (2026-07-23 perf): ONE convolution + EQ, fanned out with
                        // per-channel room — bit-identical to two identical channels.
                        p->cab.processMonoToStereo(L, R, len);
                        // Only the room decorrelates; the output is truly stereo iff it ran.
                        if (p->cab.getParameter("roomon") > 0.5f
                            && p->cab.getParameter("roommix") > 0.001f
                            && p->cab.getParameter("voice") < 0.5f) stereo = true;
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
                // X2 second instances (v38)
                case B_GATE2: {   // keyed on the RAW pre-InputTrim input, like Gate 1
                    if (!stereo) for (int i=0;i<len;++i) p->mono[i] = L[i];
                    else         for (int i=0;i<len;++i) p->mono[i] = 0.5f*(L[i]+R[i]);
                    float* io2[1] = { p->mono };
                    p->gate2.processKeyed(io2, inL + off, io2, len, 1);
                    for (int i=0;i<len;++i) { L[i] = p->mono[i]; R[i] = p->mono[i]; }
                    break;
                }
                case B_COMP2:   runMono(p->comp2, L, R, len, p->mono, stereo); break;
                case B_FUZZ2:   runMono(fz2Pedal==0 ? *p->fuzz2Muff : (fz2Pedal==1 ? *p->fuzz2Bender : (fz2Pedal==2 ? *p->fuzz2Octavia : *p->fuzz2Factory)), L, R, len, p->mono, stereo); break;
                case B_NAIL2:   runMono(*p->nail2, L, R, len, p->mono, stereo); break;
                case B_MODFX2:  runStereo(p->modfx2,  L, R, len, stereo); break;
                case B_DELAY2:  runStereo(p->delay2,  L, R, len, stereo); break;
                case B_REVERB2: runStereo(p->reverb2, L, R, len, stereo); break;
                case B_WAH2:    runMono(p->wah2, L, R, len, p->mono, stereo); break;
                case B_OCTAVE2: runMono(p->octave2, L, R, len, p->mono, stereo); break;
                case B_EQ2:     p->eq2.processCh(L, len, 0);
                                if (stereo) p->eq2.processCh(R, len, 1); break;
            }
            p->cpuAcc[id] += CpuClk::now() - blkT0;
            if (id == B_CAB && *p->ports[HF_RB_CAB2ON] > 0.5f) rigBMix();       // Cab 2 present: join after Cab 1
            if (id == B_AMP && !(*p->ports[HF_RB_CAB2ON] > 0.5f)) rigBMix();     // no Cab 2: join before Cab 1 (shared)
        }
        rigBMix();   // no active cab in the chain: join after the last block
        p->cpuAcc[B_COUNT] += CpuClk::now() - chunkT0;
        p->cpuSamps += len;
        if (p->cpuSamps >= 24000) {   // publish ~2x/sec: % of the audio-time budget
            const double budget = double(p->cpuSamps) / p->rate;
            // Explicit map: the contiguous cpu_gt..cpu_eq region covers the original
            // 13 blocks; later blocks (Drive B...) have their meters appended after
            // cpu_total, so HF_CPU_GT + id would land on the wrong port.
            static const int kCpuPort[B_COUNT] = {
                HF_CPU_GT, HF_CPU_CP, HF_CPU_FZ, HF_CPU_DR, HF_CPU_AMP, HF_CPU_CAB,
                HF_CPU_MD, HF_CPU_DL, HF_CPU_RV, HF_CPU_WH, HF_CPU_OC, HF_CPU_NAIL,
                HF_CPU_EQ, HF_CPU_DR2,
                HF_CPU_GT2, HF_CPU_CP2, HF_CPU_FZ2, HF_CPU_NAIL2, HF_CPU_MD2,
                HF_CPU_DL2, HF_CPU_RV2, HF_CPU_WH2, HF_CPU_OC2, HF_CPU_EQ2,
            };
            for (int b = 0; b < B_COUNT; ++b) {
                if (p->hostPorts[kCpuPort[b]])
                    *p->hostPorts[kCpuPort[b]] = float(100.0 * p->cpuAcc[b] / budget);
                p->cpuAcc[b] = 0.0;
            }
            if (p->hostPorts[HF_CPU_TOTAL])
                *p->hostPorts[HF_CPU_TOTAL] = float(100.0 * p->cpuAcc[B_COUNT] / budget);
            p->cpuAcc[B_COUNT] = 0.0;
            if (p->hostPorts[HF_CPU_RIGB])
                *p->hostPorts[HF_CPU_RIGB] = float(100.0 * p->cpuRigB / budget);
            p->cpuRigB = 0.0;
            if (p->hostPorts[HF_CPU_CAB2])
                *p->hostPorts[HF_CPU_CAB2] = float(100.0 * p->cpuCab2 / budget);
            p->cpuCab2 = 0.0;
            p->cpuSamps = 0;
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
    if (p->ports[HF_OUT_DOUBLER] && *p->ports[HF_OUT_DOUBLER] > 0.5f && !p->dblBuf.empty()
        && !fvOn) {   // FRFR voice: doubler auto-muted (one mono speaker would comb the two takes)
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

    // ── Output Voice: FRFR de-close-mic EQ (2026-08-21) — post-mono-sum, pre-
    // Output. The IRs are close-mic recordings; through a flat speaker in a
    // real room that perspective reads boxy/hyped. Four levers (LAB knobs for
    // the FRFR-10 ears session, bake + retire after): steep low cut (10" + the
    // floor-coupling boost), 160 Hz proximity tame, ~4 kHz close-mic presence
    // dip, 8 kHz fizz tilt (a horn tweeter reproduces IR fizz too faithfully).
    if (fvOn) {
        const float cut  = p->ports[HF_FV_LOCUT] ? *p->ports[HF_FV_LOCUT] : 100.75f;
        const float prox = p->ports[HF_FV_PROX]  ? *p->ports[HF_FV_PROX]  : 1.815f;
        const float pres = p->ports[HF_FV_PRES]  ? *p->ports[HF_FV_PRES]  : 3.225f;
        const float fizz = p->ports[HF_FV_FIZZ]  ? *p->ports[HF_FV_FIZZ]  : 2.70f;
        if (cut != p->fvLastCut || prox != p->fvLastProx || pres != p->fvLastPres || fizz != p->fvLastFizz) {
            p->fvLastCut = cut; p->fvLastProx = prox; p->fvLastPres = pres; p->fvLastFizz = fizz;
            for (int c = 0; c < 2; ++c) {
                p->fvHP[c].setCoeffs(Filters::highpass(cut, 0.707, p->rate));
                p->fvProx[c].setCoeffs(Filters::peaking(160.0, -prox, 1.0, p->rate));
                p->fvPres[c].setCoeffs(Filters::peaking(4000.0, -pres, 1.2, p->rate));
                p->fvFizz[c].setCoeffs(Filters::highshelf(8000.0, -fizz, p->rate));
            }
        }
        for (uint32_t i = 0; i < n; ++i) {
            outL[i] = p->fvFizz[0].process(p->fvPres[0].process(p->fvProx[0].process(p->fvHP[0].process(outL[i]))));
            outR[i] = p->fvFizz[1].process(p->fvPres[1].process(p->fvProx[1].process(p->fvHP[1].process(outR[i]))));
        }
    }

    // ── Master output level (the "Output" stage — last in the chain) ──
    // The knob is in dB (0 dB = unity, up to +12 dB boost); convert to a linear
    // gain and apply it smoothed. Auto-Limit only adds the clip-safe limiter on
    // top — below the ceiling both modes sound identical.
    const float outGain = dbToGain(*p->ports[HF_OUT_LEVEL]);
    const bool  outLimit = *p->ports[HF_OUT_AUTO] > 0.5f;
    p->autoOut.process(outL, outR, n, outGain, outLimit);

    // ── Seamless-switch mute ramp: ~2.5 ms down, apply everything at zero, ~10 ms up ──
    if (p->swFadeState != 0) {
        const float stepOut = 1.0f / (0.0025f * (float)p->rate);
        const float stepIn  = 1.0f / (0.0100f * (float)p->rate);
        for (uint32_t i = 0; i < n; ++i) {
            if (p->swFadeState == 1) {
                p->swFadeGain -= stepOut;
                if (p->swFadeGain < 0.0f) p->swFadeGain = 0.0f;
            } else {
                p->swFadeGain += stepIn;
                if (p->swFadeGain >= 1.0f) { p->swFadeGain = 1.0f; p->swFadeState = 0; }
            }
            outL[i] *= p->swFadeGain; outR[i] *= p->swFadeGain;
        }
        if (p->swFadeState == 1 && p->swFadeGain <= 0.0f) {
            hfApplySwitch(p);
            p->swFadeState = 2;
        }
    }

    // Silence the output while tuning (mute engaged) or measuring the calibration
    // floors — both engines still read the dry input upstream of the chain.
    if (tunerMute || calMute) { std::memset(outL, 0, sizeof(float)*n); std::memset(outR, 0, sizeof(float)*n); }

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
        // Calibration wizard: state|progress|floorDb|humFrac|touchDb|play10msDb|
        // recTrim|recFloor|err — pushed while running + ~5 s after completion so a
        // mid-run page reload resyncs (output ports alone don't reach the modgui).
        const bool calRunning = (p->calState >= 1 && p->calState <= 3);
        if (calRunning || p->calNotifyTicks > 0) {
            if (!calRunning) --p->calNotifyTicks;
            const float prog = (p->calSamplesTotal && p->calState >= 1 && p->calState <= 3)
                ? 1.0f - (float)p->calSamplesLeft / (float)p->calSamplesTotal : 0.0f;
            char cbuf[112];
            std::snprintf(cbuf, sizeof(cbuf), "%d|%.2f|%.1f|%.2f|%.1f|%.1f|%.1f|%.1f|%d",
                          p->calState, prog,
                          p->calPh[0].peakDb, p->calPh[0].humFrac,
                          p->calPh[1].peakDb, p->calPh[2].rms10msMaxDb,
                          p->calRec.trimOffsDb, p->calRec.floorOffsDb, p->calRec.error);
            forgeStringSet(p, p->uris.cal, cbuf);
        }
    }

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

static void hf_cleanup(LV2_Handle h) {
    auto* p = static_cast<HexForge*>(h);
    delete p->amp;
    delete p->ampNam; delete p->drNam; delete p->cabNam; delete p->amp2Nam; delete p->dr2Nam;
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
    saveOne(p->uris.amp2_nam, p->amp2NamPath);
    saveOne(p->uris.ir2_file, p->ir2Path);
    saveOne(p->uris.dr2_nam, p->dr2NamPath);

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
    putU32(44);                 // version (44: + rb_locut; 43: + SIR #34 pair; 42: + EP-3 Age pair; 41: + Plexi Variac; 40: + fuzz/nail Eco + Drive 2 NAM; 39: + Amp 2 NAM trims/path + Cab 2 IR path; 38: + X2 clone families; 37: + Cab 2 presence; 36: + Rig B full parity; 35: + Rig B dual amp/cab; 34: + Drive B block; 33: + Drive Eco; 32: + Engine Quality; 31: + CPU meter outputs; 30: + fuzz guitar vol; 29: + tremolo shape; 28: + speaker drive; 27: + ambient bloom; 26: + reverb type / room density; 25: + reverb density; 24: + pickup load / coupling; 19: + NAM gain/level trims; 18: + Mod Center Delay; 17: + Cali V EQ preset; 16: + Cali V graphic EQ; 15: + Cali V Mesa mode; 14: + Octave shimmer; 13: + tempo-sync; 12: + Nail; 11: + factory rev; 10: + Output Mono Sum; 9: + per-block bypass; 8: + Wah/Octave; 7: + Seraph; 6: + Boost; 5: + HB Model; 4: + HB voicing; 3: dB; 2: linear)
    putU32(kBanks); putU32(kSlots); putU32(HF_N_PORTS); putU32(kFactoryRev);
    for (int b=0;b<kBanks;++b) for (int s=0;s<kSlots;++s) {
        const Preset& pr = p->presets[b][s];
        putU32(pr.used ? 1u : 0u);
        putBytes(pr.name, sizeof(pr.name));
        putBytes(pr.vals, sizeof(pr.vals));
        putPath(pr.irPath); putPath(pr.ampNamPath); putPath(pr.drNamPath); putPath(pr.cabNamPath);
        putPath(pr.amp2NamPath); putPath(pr.ir2Path);   // v39
        putPath(pr.dr2NamPath);                         // v40
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
    restoreNam(p->uris.amp2_nam, &p->amp2Nam, p->amp2NamPath);
    restoreNam(p->uris.dr2_nam, &p->dr2Nam, p->dr2NamPath);
    {   // Cab 2 user IR: restore the override path; run() schedules the load
        size_t size=0; uint32_t type=0, vflags=0;
        const void* v = retrieve(handle, p->uris.ir2_file, &size, &type, &vflags);
        if (v && type == p->uris.atom_Path) {
            char* path = const_cast<char*>(static_cast<const char*>(v));
            if (mapPath) path = mapPath->absolute_path(mapPath->handle, path);
            std::strncpy(p->ir2Path, path, kPathMax-1); p->ir2Path[kPathMax-1]='\0';
            p->lastCab2Model = -1;   // (re)load on the first enabled block
            if (mapPath && path != v) free(path);
        }
    }

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
        if (ver < 2 || ver > 44) return LV2_STATE_SUCCESS;    // unknown layout — start fresh
        const bool migrateOutDb = (ver == 2);     // v2 stored out_level as 0..1 linear
        const bool needMigrate  = (ver < 44);     // ...v44 rb_locut
        getU32(np);
        if (ver == 36 && np >= 318) ver = 37;     // deployed v36 stamps already carry rb_cab2on (318-param layout)                                 // param-port count at save time
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
            char ir[kPathMax],an[kPathMax],dn[kPathMax],cn[kPathMax],a2[kPathMax],i2[kPathMax],d2n[kPathMax];
            getPath(ir); getPath(an); getPath(dn); getPath(cn);
            a2[0] = i2[0] = d2n[0] = '\0';
            if (ver >= 39) { getPath(a2); getPath(i2); }   // v39: Amp 2 NAM + Cab 2 IR
            if (ver >= 40) getPath(d2n);                   // v40: Drive 2 NAM
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
            std::strncpy(pr.amp2NamPath,a2,kPathMax-1); pr.amp2NamPath[kPathMax-1]='\0';
            std::strncpy(pr.ir2Path,i2,kPathMax-1);     pr.ir2Path[kPathMax-1]='\0';
            std::strncpy(pr.dr2NamPath,d2n,kPathMax-1); pr.dr2NamPath[kPathMax-1]='\0';
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
