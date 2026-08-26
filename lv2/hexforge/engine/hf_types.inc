// ─────────────────────────────────────────────────────────────────────────────
// Hex Forge engine — hf_types.inc (Stage A textual split, M0)
// Engine types & tables: constants, Preset, model maps, DSP helper structs,
// WorkMsg/URIs, TunerDetector, struct HexForge, WAV/IR file readers.
// NOT a standalone TU: #included by hexforge_plugin.cpp at the exact spot
// this text was cut from. Do not add includes or include guards here.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kAmpNamIdx = 5;   // amp model slot = Neural (NAM)
static constexpr int kDrNamIdx  = 3;   // drive model slot = Neural (NAM)
static constexpr int kDrDs1Idx  = 4;   // drive model slot = Grunge DS (DS-1)
static constexpr int kDrKlonIdx = 5;   // drive model slot = Gilded Horse (Klon)
static constexpr int kDrSd1Idx  = 6;   // drive model slot = Super Nova (Boss SD-1)
static constexpr int kDrMax     = 9;   // highest dr_model index (9 = Tube Chauffeur / Butler Tube Driver)

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
static constexpr uint32_t kFactoryRev = 123;  // 123: NIN bank compensated for the post-bake model shifts (March direct-chain PA restore master .445; WWA Sunn wall darkened; Broken Crush Recto-fit gain .16; Con Molars master .50; measured parity). (sort-stability fix needs no reseed) 122: Solar Monolith re-dial on the re-voiced Sunn baked VERBATIM. 121: Solar Monolith = user dial-in baked VERBATIM (store-diffed; rig B off, brighter/hotter Sunn). 120: bank-1 Lead push/volume corrected (restore factor was too strong at drive .06). 119: + drift trims. 118: Numb Sustain Pulse-rig rebuild (Tube Chauffeur added, dl2 dropped, washes eased) + Desert Robot SD-1 push eased. 117: + measured parity trims for the dropped curves. 116: blanket Lead Cut EQ dropped from 11 leads + Periphery EQs off + Periphery gain .56 (user). 115: + measured parity trims. 114: Green Man push restore (dr_level scaled to the pre-fix amp push on the 9 affected presets; rev-109 out_level deltas dropped). 113: + parity restore for the blend drop. 112: Periphery rb_blend -> 0.10 (pump scales with blend; 0.10 = measured flutter floor). 111: + measured parity restore (+1.2/+1.6 out_level). 110: Periphery rb_locut 150 Hz (kills the dual-rig low-mid cancellation pump; measured via woosh2). 109: Green Man gain-floor fix re-level (real x11.85 TS feedback floor at drive 0 = the boost trick works; 22 presets carry the TS, 9 measured >=0.5 dB re-leveled to parity -- the hotter pushed-amp tone is the intended change). 108: Solar Monolith = user's on-device dial-in baked VERBATIM (less drive/more octave, brighter, parallel link, Vol II up). 107: Wizard's Doom -> "Solar Monolith" Sunn O))) drone (New Dawn/Life Pedal + cranked Doom Daddy/Sunn, dark monolith, Hex Ambient wash); Regal Sustain/Solo (Brian May/Vox) tuned for the Vox re-voice (top tamed, violin mids, Solo delay -> EP-3). 106: Periphery hiss tame (Flatliner/Prayer Djent -- treble/presence/TS + cab highcut 7.8k->6.3k, kill the amplified Red HF hiss); Berlin Wall Pulse (b4/1) drive New Dawn->Dear Rodent Boy (RAT, Gilmour), baked verbatim to survive the reseed. 105: Brown Sound '84 = user's overvolt-variac dial-in baked VERBATIM from store (gain .655/EQ off/EP-3 clean); Periphery/Gojira EVH presets reworked to songs (Flatliner=Flatline, Prayer Djent=Prayer Position, Castaway Groove=Silvera) on the new EVH. (104 was a briefly-live rev, superseded.) 103: USER bank-0 reworks baked (Clean gain up; Crunch SIR #34 ON; Rhythm full rebuild around the corrected EVH; Lead phase normal) -- store-diffed, DO NOT RETUNE. 102: padrive/pamakeup now applied on the MAIN rig (+ standalone amp), was Rig-B-only -- EVH/Rockerverb/Vox were 2-3x over-driven on the main rig; 18 affected presets re-leveled to loudness parity (measured before/after). 101: Brown Sound '84 mod Phaser -> Script Phaser (user swap, md type 8). 100: REVERTED the Fender PA compression re-voice (rev 99) -- user: bitcrush-like distortion on Fender, click on Chime Thirty when pushed, nothing felt right. Bumped past 99 to force the re-seed that restores the original Fender out_levels (the store was already reseeded to 99). Everything back to the rev-98 state. 98: Jungle Sleaze = USER's SIR #34 build (mod ON, re-dialed around the bite). DO NOT RETUNE. 97: USER round 2 baked verbatim (Brown Sound '84 -> REAL EP-3 chain: Echo Primer + EP-3 Echo slap + second variac'd Plexi B rig; Jungle Sleaze hotter/brighter + Cab 2 prep). DO NOT RETUNE. 96: Plexi Variac v2 physics audit (s=V/120 exact-transform knees, gm~s^(1/3), swing~s, sag~s^-1.5 + 15ms fast RC) -- Brown Sound '84 re-trimmed -22.5 (net -0.2 dB shift). 95: bank 18 loudness parity (user request) -- out_levels re-trimmed to the -16.8 Crunch anchor after the rev-94 user finals (-17.3/-22.0/-23.0/-20.6). 94: bank 18 B/C/D = USER's on-device finals baked verbatim (Brown Sound re-staged: IT boost +7.05 into eased EP-3, gain .73 / treble+master dimed, Vol II out; Acca Dacca -> 'Sister's Singer', everything on 10, gate off; Jungle intro hotter w/ dimed Green Man). DO NOT RETUNE. 93: Brown Sound '84 round 3 -- EP-3 Echoplex-preamp stand-in (Preamp 250, drive .25 / level .78 hot into V1) fixes 'plexi short on gain for 1984'; out re-trimmed -22.7 (clean-HOME measured to the -16.8 anchor). 92: Frayed Justice = USER's on-device rework baked verbatim (amp-first chain, treble .17/presence up, Mid-Boost EQ post-cab, spkdrive Full, Cali V mode-7 B rig + Greenback Cab 2, DO NOT RETUNE); Jungle Sleaze -> the WTTJ DELAY INTRO (gain .45, 470ms digital, fb .55, mix .42); Brown Sound '84 pass 2 (gain/VolII/sag up, treble<->presence inverted, mids .68, phaser 100%-wet .45, echo eased). 91: NEW BANK 18 (idx 17) CLASSIC ROCK/METAL (user): Frayed Justice (AJFA Cali V deep-V, dry) / Brown Sound '84 (Plexi VARIAC + pre-amp phaser + tape echo) / Acca Dacca (jumpered non-master Plexi) / Jungle Sleaze (Appetite modded-1959 ~= JCM800). 90: NIN presets = the USER'S on-device final reworks baked verbatim (March direct/no-cab + octave + inverted Fender rig; Broken -> Recto + tape slap + Cali V rig + MID-CHAIN Gate 2; Con Molars starved Fuzz Zachary front end). DO NOT RETUNE. 89: NIN EQ retune round 4 (user) -- March scoop eased (+1 net), Broken hiss-band boost removed + Wish mids, Con Molars regains the lost 1.9k mid-bite with the EQ MOVED PRE-AMP (Reaktor-then-reamp order). 88: NIN round 3 (user) -- March Stabs +2.5 dB more, Broken Crush white-noise tamed at the crusher (texture .30, tone .38, highcut 7500). 87: NIN ear-pass round 2 (user): March Stabs hotter+louder (+~2.7 dB total), Broken Crush de-noised (gate -40 at the rig floor + DS stage trim), Con Molars low end + gain; WWA locked user-approved. 86: NIN topology upgrades (user) -- March Stabs Nail moves POST-amp (JMP-1 -> Zoom -> cab, the documented TDS order), Broken Crush gains the 9030 stacked-distortion stage (Grunge DS -> Nail Broke), Con Molars drops the double cab-sim (real amp does the re-amp), WWA wall deepened; all parity-measured. 85: JCM800 stage-2 cap 2.0->1.4 (h2@223 15->10, whole profile overlays both knob-documented captures; the 2026-07-28 fix shipped 2.0 while its comment said 1.6) + measured per-preset re-level on the 6 affected JCM800 presets. 84: Skye (No Mod) + Cardinal Rhythm/Lead = the USER'S on-device dual-rig dial-ins baked verbatim (store-diffed; Cardinal Rhythm pol back to NORMAL, Skye B = major voice at blend .57; placed AFTER the rev-72 JCM800 loop which had been stacking +0.10 on the rev-83 absolute gain). 83: Skye (No Mod) heavier (A gain .82, Orange B .65 crunchy) + Cardinal B layers crunched (.62/.60) and +1 dB over parity (user: too clean / a little quiet). 82: Cardinal Rhythm/Lead dual rigs (user: "those are ghost too") -- Orange layer, BOTH inverted (Cali V + Friedman-lead phase), parity measured. 81: Skye (No Mod) riff gain 0.62->0.72 (user) + Streets Chime parity trim (+1.45->+2.0; the baked dl2 shifted the joint delta). 80: dual-rig round 2 (user: Ghost + Periphery + Skye (No Mod)) -- Imperial pair (Orange under Friedman; rhythm coherent, lead INVERTED -- the pedal chain flips net phase), djent parallel clean blends, Streets Chime = Edge dual amp + FIRST dl2 presets (Streets quarter-note second delay, Numb Sustain rack delay); all phase+parity measured (rig_meas2, wiggle-safe writes). 79: Gilmour dual-rig pol/parity fix (the rev-78 first-pass measured a wrong B amp via the zero-write port trap). 78: DUAL RIGS (2026-07-30, user): 7 documented multi-amp presets gain a measured Amp 2/Cab 2 layer (Muse x2 clean-Vox-under-fuzz, Gilmour x2 Twin-under-Hiwatt INVERTED polarity, QOTSA Orange layer inverted, NIN Sunn wall, Mastodon Orange) with on-device phase + RMS-parity verification baked into out_level. 77: factory rows now MIGRATED at seed time (2026-07-30): the 224-port v30-era kFactoryExtra literals were memcpy'd raw into the grown layout, so the old command/meter tail landed on every param appended since (quality/dr2/rig-B = garbage; rig B silent everywhere). kFactoryTableVer=30 + migratePorts at seed; reseed flushes the corrupted slots. 76: Vox preset gates raised to -44 (2026-07-29 whine fix: hysteresis close point sat below the real hands-off floor -> latch-open -> constant amplified hum partials; -44 puts close at -48, above the ~-54 post-comb detector floor). 75: Bank 1 Crunch = the user's on-device rework baked verbatim (2026-07-29): the rev-72 +0.10 JCM800 bump still read too tame after the re-voice, so they rebuilt it -- chain Drive->Amp->Cab->Reverb (comp parked), Green Man clean-boost (drive .07, level max), amp gain .61 / bass .2525 / mid .475 / master .6825; store-diffed on-device against a fresh factory seed. 74: Moondust Glam overhauled to Mick Ronson's documented Ziggy rig (2026-07-29, user request): germanium Tone Bender (hot, tamed volume) -> cocked Cry Baby (kept) -> CRANKED jumpered Plexiglass (was a clean-ish Hiwatt platform -- the Major 'Pig' is Marshall lineage) -> Greenbacks; out_level re-measured on device. 73: fz_gvol=1.0 re-seed on every factory preset (2026-07-29 hotfix -- older-layout factory tables zero-filled the new port; 0 = guitar vol rolled off = I Know It / Tone Bender inaudible, user-reported). 72: +0.10 gain on every factory JCM800 preset (2026-07-28, user request after the fuzzy-fix re-voice -- the stage-2 duty-collapse cap tamed its drive contribution; factory-scoped, idempotent). 71: Speaker Drive per-amp defaults (2026-07-28, item #40, user request) -- every factory preset now gets Speaker Drive seeded from its own amp choice: Subtle for Fender/Clean Meanie + Vox/Chime Thirty, Full for JCM800 + the suite's high-gain amps (EVH/Gainzilla, Friedman/Beardo BE, Recto/Diamond Plate, MT15/Tremont 15); every other amp (Sunn, Rockerverb, Hiwatt, Backline, Plexiglass, Cali V, NAM) left at Off -- the user specified only these two categories, nothing guessed for the rest. 70: Cydonia Sunrise -> "Knights of Fuzz" = the USER'S full-arc KOC build baked verbatim (slap INTO near-max Fuzz Zachary + SD-1 -> low-gain JCM800, eased trem, Ambient wash, Lead Cut EQ; out locked -18.6; parked comp stripped per the no-residue rule) (2026-07-25). 69: Cavalier Charge REPLACED by "Cydonia Sunrise" (user 2026-07-25) — the KOC INTRO/VERSE retro sci-fi surf: bright scooped Chime Thirty at the edge, fast deep tremolo, 140 ms slap, small plate. 68: I Saw a Deer — leftover bypassed Gilded Horse cleaned to defaults (user: "there shouldn't be a klon"; block was disabled = zero audio change). 67: Bank 17 parody names (Homesick Saucer / Hand in Cloud) + Cloudforge -> "I Saw a Deer" = the USER'S on-device rework baked verbatim (octave-up->Nail->maxed phaser->hot 76 ms tape slap->max-bloom ambient->Clean Sparkle EQ; out locked -20.94) (2026-07-25). 66: HEX AMBIENT (2026-07-25) — new reverb type 2 (Cloudburst/SLO-family blooming tail, rv_bloom port, blob v27) + Bank 17 AMBIENT: Sweet Dispersion (Temper Trap) / Homesick Alien (Radiohead) / Hand in Mine (EITS) / Cloudforge (showcase, bloom max). 65: Cardinal Lead joins the Lead Cut set (user request — EQ appends end-of-chain, their preserved amp/drive dial-in untouched; re-measured + re-leveled). 64: Lead Cut on every lead preset (2026-07-24, user): end-chain EQ block with the stock Lead Cut curve (-1/0/+1/+3/+4/+5) baked into the sliders + eq_preset=5 retained selection, on Regal Sustain/Sermon Solo/Imperial Lead/Numb Sustain/Gravity Lead/Hazy Solo/Skye Soar/Regal Solo/Quarter-Tone Lead/Spectrum Lead (Cardinal Lead user-preserved + Bank-1 Lead excluded); all 10 re-measured + parity re-leveled. 63: EQ-block polish pass (2026-07-24): Sweet Soy Stabs de-boomed/de-muffled (bass .45/treble .66/pres .58/highcut 9200 + end-chain EQ), end-chain EQ curves on all 5 nu metal presets + Flatliner/Prayer Djent (documented per-preset intents, boosts capped +2 dB), One Step Deeper -1.5 dB perceptual trim; all 7 re-measured + parity re-leveled. 62: NU METAL — first Diamond Plate presets (2026-07-24, drop-D voiced): 14/D Duality Crush (Slipknot Iowa, CH3 Modern+silicon+TS clamp) + Bank 16 Blind Squall (Korn, CH2 Vintage+tube sag)/Quiet Drive (Deftones shimmer-clean)/One Step Deeper (LP tight CH3 Modern)/Sweet Soy Stabs (SOAD mid-forward staccato); seed-clear list shrunk (14/D + bank 15 now seeded). 61: Surf Splash retuned to the user's surf recipe — mid SCOOP (.55->.35, the honk), bass/treble/presence into the Fender-surf bands, reverb mix .62->.5, predelay 4->15 ms (clean staccato attack ahead of the drip) (2026-07-24). 60: Surf Splash rebuilt FROM SCRATCH (user dropped the edge-of-breakup idea: pure clean high-headroom Showman + fast comp glue + cranked spring tank) + March Stabs/Con Molars -2.5 dB perceptual trim (EQ presence push reads louder at RMS parity) (2026-07-24). 59: Surf Splash REDO (edge-of-breakup: Green Man push + Clean Meanie gain up, @american-ob, spring tank kept wet) + the 4 NIN presets gain the user's end-of-chain 6-band EQ curves (eq block pos 13; 6.4k intent folded into cab highcut) (2026-07-24). 58: Dense cab room on every factory preset + Spring reverb where the rig calls for it (Surf Splash / Apache Echo — the genuinely surf/early-60s presets); residual loudness shifts absorbed via ROOMDENSE_MEAS_DELTA. 57: Dense-tank parity re-level (24 presets measured; Surf Splash -4.0 dB the extreme; bank-1 inline outs adjusted). 56: Dense reverb tank on every factory preset (user A/B'd: 'it sounds great'). 55: peak-capped presets re-leveled too (Come As Water / Innerspeaker were pinned by the peak cap). 54: polish loudness parity — 15 coupling-lifted presets re-leveled from on-device measurement (+0.3..+1.0 dB absorbed into out_level). 53: fidelity polish — per-rig Speaker Coupling + Pickup Load across all factory presets (user-preserved slots untouched). 52: Retro Poland = the USER'S on-device dial-in baked verbatim (Preamp 250 drive, reordered chain, bypassed tape slap) — user-preserved, do not retune. 51: renamed to Retro Poland + a touch more queasy (depth .75, mix .68). 50: Maromaro Retro — Klon (Gilded Horse) push + audible seasick (rate ~0.5 Hz, depth .7, mix .6). 49: Maromaro Retro retuned to the user's tone blueprint (slow 0.25 Hz seasick + tape drift + blueprint amp dial-in). 48: Maromaro Retro moved to the new Seasick Vibe mod type (deep sweep + dry/wet crossfade = true pitch heave). 47: + Maromaro Retro (bank 15/C, 2026-07-23): compressed edge-of-break-up clean + deep seasick Small Clone, for the Retro Poland covers. 46: re-seed to flush transient store churn (2026-07-23): a partially-migrated Bank 1 Clean (~5 dB quiet) was found live in a user store; fresh seeds measure correct (-13.9 dBFS DI at out -24 = parity), so the seed VALUES are unchanged - the bump just guarantees every store gets a clean Bank 1. 44: Bank 1 room (2026-07-14, user: "make sure my first four presets have it") — the stock Clean/Crunch/Rhythm/Lead seeds now get roomon 1 / mix 0.12 / size 0.35 explicitly after their v3 migration (appended ports migrate in as 0). 43: ROOM ON BY DEFAULT (2026-07-14, user) — every preset now carries a subtle small-to-medium room (roomon 1, mix 0.12, size 0.35 via the seeded defaults; old user blobs zero-fill = off, their sounds untouched); full loudness re-measure baked. Cab panel rearranged: CABINET over ROOM left, mic pad right. 42: CAB ROOM AMBIENCE (2026-07-14) — cab_roomon/roommix/roomamt: a small Schroeder room (4 damped combs + allpass) after the cab convolution, toggle + mix + size, default OFF (bit-identical; presets reseeded off). 41: MIC PLACEMENTS IN PRESETS + NEVERMIND WALL REBUILD (2026-07-14) — (a) researched per-rig mic positions baked into 35 presets ('60s = backed-off warm placements: Hendrix .30/.25, Apache .20/.50, Surf .15/.40; '70s = off-cap + air: Trower/May/Gilmour ~.25-.30/.15-.20; room-heavy doom/psych: Wizard's Doom .40/.35, Sleep .35/.30, Innerspeaker .30/.35; Vig-era Nirvana .20-.25/.05-.10; modern tight metal (Mesa/EVH/Periphery/Mastodon/Gojira) + NIN direct rigs stay 0/0 = close cap edge; user-preserved presets untouched); full loudness re-measure follows. (b) Nevermind Wall REBUILT to the documented SLTS chain (user: old rework "is trash"): Mustang -> DS-1 at Kurt's settings (tone 10 o'clock, dist 4, LEVEL MAXED) -> pushed Fender Bassman (Clean Meanie gain .6 master .8), NO chorus on the wall, @american-ob, SM57 slightly off-cap. 40: CAB MIC PLACEMENT (2026-07-14) — new cab_micpos (cap edge -> cone edge: HF slide 12k->3.3k + bite recede + body) and cab_micdist (close -> ~30 cm: proximity bass falls away + air loss) on the Cabinet block; post-convolution morphs, 0/0 = bit-identical baseline (append-only, presets reseeded with 0/0 = no tone change). 39: ENRICHED FACTORY CABS (2026-07-14, user: "this is where we could really make this sound real") — all 6 built-in cabs (incl. @factory V30) gain deterministic measured-IR anatomy (CabModels::enrich: seeded cone-breakup ripple, multi-cone/baffle-edge sub-ms taps, closed-back box return / open-back dipole cancel, ringing box air mode) with an iterated DAMPED macro-correction so each cab keeps its tuned voicing (verified ≤0.9 dB drift on 4 cabs / ≤1.7 on greenback+doom, L2 loudness exact); full 54-preset loudness re-measure baked. 38: PLEXI VOL II (2026-07-14, user: "the original had Volume I and Volume II") — new amp_pl_vol2 port (append-only, default 0 = old voicing bit-identical, no blob migration): a parallel Normal-channel V1 half jumpered under the capture-anchored bright path, like the real 1959. Hendrix presets jumpered (Mauve Haze VolI .5/VolII .5/master .7; Hazy Solo — the user's "starved Octavia" — VolI .62/VolII .55/master .75/sag .45 = the DIMED stack the Octavia historically rang over; Little Feather VolII .35; Bottle Jangle VolII .3). Nevermind Wall untouched (user-preserved, vol2 0). 37: INPUT-TRIM PHASE default fixed 1->0 (2026-07-14) — default 1 meant INVERTED polarity on every instance + all factory presets (mismatched the standalone Input Trim's phase_invert default 0 and every offline NAM calibration); all presets reseeded with normal polarity; the toggle stays for DI-blend use. 36: PERIPHERY DJENT RETUNE (2026-07-14, user + research: Misha's Precision-Drive philosophy = cut bass INTO the amp, definition from mids/treble, saturation-not-mush) — Flatliner bass 0.5->0.25 mid 0.62 treble 0.62 pres 0.55 gain 0.58 TS-tone 0.7 lowcut 100; Prayer Djent bass 0.55->0.22 mid 0.65 treble 0.6 gain 0.58 TS-tone 0.72 lowcut 105. 35: user ear-pass 2 (2026-07-14) — Skye Crusher + Skye (No Mod) bass 0.78->0.48 ("way too much bass" at the new gain; Skye Soar left at 0.75 — not named, and the lead's octave/delay masks differently). 34: user ear-pass (2026-07-14) — Mastodon bank MORE GAIN (Skye Crusher/No Mod amp gain 0.45->0.62, Skye Soar 0.55->0.7, TS level 0.74->0.8) + Castaway Groove LESS BASS (0.55->0.35, low-cut 84->92). 33: CALI V DOCTRINE PASS (2026-07-14) — the user's confirmed Mark-series recipe ("the amp works like an overdrive; the tone comes from the EQ"): Cardinal Rhythm = the user's exact device dial-in BAKED (gain 0.15, Bass 0, Treble 0.38, DOD 250 level-boost in front, brightness at GEQ 6.6k — USER-PRESERVED, do not retune); the recipe translated to the other Cali V presets (Marionette Master gain 0.8->0.5 Bass 0.05 Treble 0.40 geq4 0.78; Spectrum Rhythm gain 0.76->0.35 Bass 0.08 Treble 0.40 geq4 0.72; Spectrum Lead gain 0.78->0.42 Bass 0.10 Treble 0.45 geq4 0.66) + loudness re-measured. 32: OVERNIGHT AUTHENTICITY PASS (2026-07-14, user-authorized full-permission run) — all 54 presets audited against researched real rigs by a 15-agent web-research fan-out, 28 adjusted after adjudication: historically-correct cabs across 15 presets (Ghost=V30 PPC412 not Greenbacks; Hendrix='67 G12M Greenbacks not V30s; Mastodon=Mills V30s; Nirvana verse=Bassman open-back + Come As You Are=@vox2x12; Mayer/Cure/Chic=American open-backs; Trower/Police=Greenbacks; Numb Sustain=@hiwatt), drive-pedal identities fixed (Impera rhythm=Sugar Drive/Klon, Impera lead=DOD 250, Run Like Hell=Colorsound Power Boost full-series), Cardinal Rhythm moved to the DOCUMENTED Skeleta rhythm amp (Hetfield's Mesa IIC+ = Cali V mode 6, voiced per Cali V rules), Holy Smoke rebuilt to Pike's documented no-fuzz chain (Soldano-style OD into cranked saggy Orange), Comfortably Numb delay fixed to the MXR Digital + added the missing RA-200 rotary blend, Innerspeaker fuzz=germanium Fuzz Face family (I Know It), Glide Wall reverse-verb predelay 0, Streets Chime 359 ms (measured 125.5 BPM), Bottle Jangle=Marshall 1959 platform + 330 ms Echoplex at 50/50, March Stabs master 0.55 (JMP-1 direct, no power amp), Regal Solo 800 ms Echoplex canon, Cavalier Charge 330 ms + the Manson-wired Phase 90, Trower's documented presence-0/treble-low Marshall settings, Candlelit comp=optical. Full loudness re-measure follows. 31: FULL preset overhaul (2026-07-14) — (a) LOUDNESS PARITY: all 54 presets re-measured on-device (hexforge_meas) against the NEW DSP (FF v3 re-model, Cali V retune, DNR additions) and out_level rewritten to the -12.5 dirty / -13 clean targets; the 9 never-measured presets (Gojira/Mesa/Muse banks, Regal Solo, Grunge Drop) leveled for the first time. (b) GATE FLOOR-COMPLIANCE vs the measured rig floor: Bridge Vibe/Streets Chime raised off the -60 default, Regal Solo -54->-50, Con Molars -54->-48 (fuzz/doom deep gates kept deliberately). (c) MUSE presets retuned to Bellamy's DOCUMENTED FF recipe (Drive max/Comp low/Stab max, probe-verified squeal-safe at the preset Gate; back Stab off to ~0.55-0.85 for the splatty scream). 30: FF cut-out fix — Muse chain gates OPENED (-50/-48 -> -58, hold 200/release 400; the raw-guitar-keyed gate in FRONT of the fuzz chopped sustain while the fuzz still roared — same as the old Octavia choke; the FF's own Gate knob is the noise-killer) + Spectrum Lead gate -52 -> -48 (could never close against the user's measured -45 dBFS hands-off idle floor) (2026-07-14). 29: renamed preset "With Teeth" -> "Con Molars" (bank 11/D) + fuzz pedal "Fizz Factory" -> "Fuzz Zachary" (label only, value 3 unchanged) (2026-07-13). 28: Cali V HISS RETUNE (Marionette Master/Spectrum Rhythm/Spectrum Lead) — Bass down (tight; real MarkV wants Bass 1-3 on high gain), pre-gain Treble down + brightness moved to the POST-gain GEQ 6.6 kHz (Treble is early in the circuit = adds saturation/hiss), gain nudged up to offset the model's ~3 dB high-gain trim; out_level nudged +1 for the bass loss (verify by ear) (2026-07-13). 27: PACKED presets — added Knights of Cydonia ("Cavalier Charge") + compacted so no populated bank before the last has blank slots (banks 0-13 full; MUSE = last bank, index 14, A/B only; index 15 emptied). Regal Sustain->1/A, Regal Solo->11/D, Grunge Drop->14/D, Muse->bank 14 (2026-07-13). 26: Plug-In Junior Stab 0.5->0.2 (below the osc onset — was squealing too much) (2026-07-13). 25: Plug-In Junior retune — Stab 0.72->0.5 (below the self-osc onset; PIB riff shouldn't squeal), Drive 0.92, Comp 0.5, Gate 0.42 (2026-07-13). 24: Fizz Factory — hotter drive + gentler gate (fix "cut out") + note-gated SELF-OSCILLATION above Stab 0.55 (the unruly FF scream, dies on silence); calibrated to the real Stab sweep (THD/level rise); Plug-In Junior Drive 0.85/Gate 0.35/Stab 0.72 (2026-07-12). 23: Fizz Factory Stab = SUPPLY-RAIL model (ZVEX "operating voltage" — germanium FF); UP=tight/full, DOWN=starved squishy sag/sputter (correct real-pedal direction); Plug-In Junior Stab 0->0.9 (tight) (2026-07-12). 22: Fizz Factory Stab reworked (note-gated feedback + low 480 Hz growl + late onset — no more standalone whine); Plug-In Junior Stab 0.15->0 (2026-07-12). 21: NEW Bank 16 (index 15) MUSE — "Plug-In Junior" (Plug In Baby, Fizz Factory riff), slot A; B/C/D reserved; seed-clear spares 15/0 (2026-07-12). 20: trimmed the batch to Regal Sustain/Solo + Grunge Drop (removed the other 11 + Rotary Dream/Surfing Lead); added the INPUT-TRIM clean boost (it_boost ~6 dB) to both Regal presets; clears the vacated banks 15-17 + tail slots; 52 presets (2026-07-12). 19: +14 new presets (Brown Sound/Back in Black/Appetite/Texas Flood/Iron Man Doom/Surfing Lead/Microtonal Mirage/Cinematic Swell/Nashville Twang/Warm Archtop/Funk Machine/Djent Modern/Grunge Drop/Rotary Dream); moved both Queen presets together into new Bank 15 (Royalty), backfilled Bank 10/D=Rotary Dream + Bank 13/D=Surfing Lead so no blank slots; 65 presets total (2026-07-12). 18: removed "There There" + Gojira "Quicksilver Lead"; added Brian May LEAD "Regal Solo" (treble-boosted AC30) + made "Regal Sustain" hotter; repacked the tail so NO blank slots (Gojira -> Bank 12 C/D, Mesa Mark+Regal Solo -> Bank 13); clears the vacated old Bank 15 slots (2026-07-12). 17: Quicksilver Lead octave-up moved POST-cab (pos 2->7) + boosted (up 0.45->0.65) — in front of the high-gain EVH it was masked by the amp's own 2nd harmonic ("octave didn't work") (2026-07-12). 16: parody-renamed the new presets (Winterborn/Quicksilver Lead/Castaway Groove/Marionette Master/Spectrum Rhythm+Lead); Quicksilver = former Silvera turned into an octave-up LEAD (was too close to the rhythm); Marionette Master DE-BASSED (2026-07-11). 15: Gojira Silvera/Stranded amp JCM800->EVH 5150 III (Gainzilla, red ch) per user — their actual high-gain amp (2026-07-11). 14: NEW banks — Bank 14 GOJIRA (Born in Winter clean-tap / Silvera / Stranded, JCM800+TS) + Bank 15 MESA MARK (Master of Puppets = Cali V IIC+ smiley-scoop; Colors Rhythm/Lead = Cali V Mark IV) (2026-07-11). Muse (Plug In Baby / Knights of Cydonia) deferred — needs a ZVex Fuzz Factory model. 13: REVOICED Ghost/Periphery/Mastodon for the current gear (input-clip removed + TS808/Friedman re-voiced) — normalized the TS clean-boost level (hot 0.9-1.0 down / attenuating 0.6 up, all ~0.72-0.78) + eased treble/presence/high-cut for the now-untamed brighter front-end (2026-07-11). NOTE: out_level still on the pre-revoice measurements — re-measure once tone is locked. 12: Imperial Lead tamed (was too fuzzy: dropped Sat + gain 0.72->0.56 + Klon 0.35->0.16); Hazy Solo un-starved (opened the gate in front of the Octavia + more sustain/amp gain) (2026-07-11). 11: FULL LOUDNESS PARITY re-level (2026-07-09) — amp makeup leveled (all amps equal-feel at noon) + every factory preset out_level re-measured & set to the Bank-1 target (-12.5 dirty / -13 clean); dropped the -5.9 clean pin + the +8 dB pushed-loud leads. 10: real Plexi loudness for Nevermind Wall/Mauve Haze/Hazy Solo (they were mis-loading as Backline; re-measured) + Nevermind Wall reworked to Plexiglass. 2: Gravity Lead out_level -27.5 -> -23.0. 3: Ghost Imperial/Cardinal Lead phaser -> subtle Lush-2 chorus. 4: + Angine de Poitrine bank (Bank 13, microtonal shimmer). 5: Bank 13 B/C -> Radiohead (Anyone Can Play Guitar / There There); kept Quarter-Tone Lead. 6: Cardinal Lead drive -> Preamp 250 (DOD 250). 7: Cardinal Lead re-staged (DOD drive 0.48->0.25, amp gain 0.78->0.62) — was too hot/mushy. 8: Cardinal Lead out_level re-measured on-device (-17.3->-19.2; DOD250 denser than the old RAT)
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
static const OverdriveType kDriveMap[10] = {
    OverdriveType::TubeScreamer808, OverdriveType::LifePedal, OverdriveType::ProcoRAT,
    OverdriveType::ProcoRAT /* [3]=NAM placeholder, never used */, OverdriveType::DS1,
    OverdriveType::Klon, OverdriveType::SuperOverdriveSD1, OverdriveType::DOD250,
    OverdriveType::EchoplexPreamp,   // 8 = Echo Primer (EP-3 JFET, v42) OverdriveType::TubeDriver,
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

// ── Engine ⇄ host interfaces (Stage B1) ───────────────────────────────────────
// The engine talks to its host ONLY through these two interfaces (plus the
// float port arrays): the LV2 wrapper backs them with the worker extension and
// the atom notify forge; the JUCE desktop build backs them with a worker
// thread + the webview bridge. Keeps every engine file free of LV2 API calls.
enum HfProp {   // host-visible properties (the wrapper maps these to URIDs)
    HFP_PS_NAME, HFP_METERS, HFP_TUNER, HFP_CAL,
    HFP_IR_FILE, HFP_AMP_NAM, HFP_DR_NAM, HFP_CAB_NAM,
    HFP_AMP2_NAM, HFP_IR2_FILE, HFP_DR2_NAM,
};
struct HfWorkerIface {  // async off-RT work (NAM parse, amp rebuild, IR load)
    virtual bool schedule(const void* msg, uint32_t size) = 0;
    virtual ~HfWorkerIface() = default;
};
struct HfHostIface {    // engine → UI notifications
    virtual void stringSet(int prop, const char* s) = 0;    // string property push
    virtual void fileSet(int prop, const char* path) = 0;   // file-path property push
    virtual void emitIndex() = 0;                           // preset list/index refresh
    virtual void emitApply() = 0;                           // knob resync pulse
    virtual void statusDump() = 0;                          // debug status snapshot
    virtual ~HfHostIface() = default;
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
    // ── EVH capture-fit voicing — LAB-BLENDABLE again (2026-08-22, user
    // request: re-tune session on the 6L6/FRFR rig). Same 5 filters as the
    // 2026-08-19 bake (EvhCaptureFit.h), dB scaled by the dbg_evhfit port;
    // default 0.8875 = the baked blend = today's sound. Makeup is anchored to
    // the bake (0.6918 at 0.8875), log-linear in blend.
    BiquadFilter evhLab[2][5];
    float evhLabLast = -1.0f, evhLabGain = 1.0f;
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
    // engine ⇄ host bridges (Stage B1): the engine schedules work and pushes
    // notifications ONLY through these (wrapper-owned, installed at instantiate).
    HfWorkerIface*       worker = nullptr;
    HfHostIface*         host   = nullptr;
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

