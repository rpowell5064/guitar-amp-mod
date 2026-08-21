# DI-clip re-measurement of every NAM-checked model (2026-08-19)

Every model historically tuned/verified against a NAM capture was re-measured
using **the player's own DI clips** instead of pink noise. Rationale: amps are
driven nonlinearities — the response you measure depends on the excitation, and
sustained pink noise (a) parks the amp at one operating point, (b) creates
intermodulation density no guitar produces, and (c) sits outside the NAM's own
training distribution, so part of any pink-noise delta is the NAM's
extrapolation error, not a real voicing difference.

## The probe

`di_ref/` (also on the Pi at `~/di_ref/`): four takes recorded 2026-08-19 on
the reference rig (raw pre-plugin input, Music-mode codec filters, guitar
straight in): 1 clean fingerpicking (peak −24.5 dBFS), 2 hard strums (−5.2 —
matches the −6.6 historical figure), 3 palm mutes (−4.9), 4 sustained lead
(−11.1). `di_all.wav` = all four with 0.5 s gaps; nam_compare rescales to
−18 dBFS RMS (`--inlevel`).

## New metrics in nam_compare (`--in <clip>` now reports both)

- **waveESR** — time-domain error-to-signal, level-matched (LS gain),
  alignment-searched ±256. **Relative use only**: it punishes phase and
  clip-shape differences ears barely register and grows with nonlinearity
  (dimed models read 50-90 % while sounding close). Never an absolute
  pass/fail. Worst-1s-window skips silence (< −60 dBFS ref RMS).
- **specESR** — spectrogram-magnitude ESR (2048/50 % Hann, LS magnitude gain).
  Phase-blind = the perceptual one. **Calibration from this run's
  distribution: ~6 % is the achievable floor for this architecture (TS-808,
  the known-done reference point), 10-20 % = typical shipped quality,
  >25 % = genuine outlier.** The in-code hint thresholds (<2 / 8 / 15 %) are
  conservative — read against the distribution, not the labels.

## Results (specESR ascending; knobs [doc] = quoted from AMP-REVOICE-NOTES, [est] = estimated — treat [est] rows as unverified)

| run | specESR | waveESR | worstFR | makeup | knobs |
|---|---|---|---|---|---|
| ts808-od5t2 | **6.3 %** | 14.5 % | 6.2 dB | −1.6 dB | doc |
| rockerverb-clean | 10.2 % | 29.0 % | 8.4 dB | −4.0 dB | doc |
| fender-clean | 13.1 % | 21.6 % | 8.1 dB | **+4.8 dB** | doc |
| sd1-d1 | 13.1 % | 21.4 % | 13.7 dB | **+11.3 dB** | doc |
| rat-d5f3 | 13.4 % | 27.0 % | 3.2 dB | +3.0 dB | doc |
| sd1-d7 | 15.1 % | 26.5 % | 10.1 dB | +1.5 dB | doc |
| peavey-sat3 | 16.8 % | 52.1 % | 4.4 dB | **+8.0 dB** | doc |
| hiwatt-all12 | 16.8 % | 33.1 % | 10.2 dB | +3.1 dB | doc |
| plexi-jumped | 17.6 % | 56.7 % | 3.5 dB | −1.0 dB | est |
| muff-civilwar-e3 | 17.9 % | 47.9 % | 6.7 dB | −2.6 dB | est knobs |
| ds1-g5 | 18.1 % | 43.6 % | 4.0 dB | +2.0 dB | doc |
| rockerverb-dirty | 18.4 % | 48.9 % | 2.9 dB | −0.6 dB | doc |
| friedman-be-g6 | 19.4 % | 47.8 % | 3.1 dB | −4.6 dB | doc |
| muff-blackrus-e4 | 19.8 % | 53.1 % | 6.1 dB | −3.7 dB | est |
| vox-b5t5cut5m8 | 20.0 % | 44.2 % | 9.7 dB | −0.3 dB | doc |
| markv-iic-hgbal | 20.0 % | 54.1 % | 6.2 dB | −1.4 dB | est gain |
| muff-blackrus-e3 | 21.0 % | 53.8 % | 7.0 dB | −6.5 dB | est |
| muff-civilwar-e4 | 21.4 % | 60.6 % | 9.5 dB | +0.2 dB | est |
| mt15-sweetspot | 21.8 % | 62.5 % | 4.8 dB | +1.1 dB | doc |
| jcm800-high | 23.3 % | 84.4 % | 8.5 dB | +0.4 dB | doc |
| evh-blue-sixes | 27.1 % | 73.6 % | 8.5 dB | −0.4 dB | doc-ish |
| evh-red-sixes | **30.2 %** | 70.2 % | 10.5 dB | −2.7 dB | doc-ish |
| muff-bluebeard-e1 | 31.2 % | 69.1 % | 9.8 dB | +1.7 dB | est |
| fender-hot | 35.6 % | 55.6 % | 15.0 dB | +5.8 dB | est gain |
| muff-cherub-e2/e5 | 45.4/40.5 % | — | 13-16 dB | — | est |
| recto-ch3-modern | **47.0 %** | 90.3 % | 17.0 dB | −0.0 dB | **est — rerun with real knobs before believing** |

Skipped (captures no longer on the Pi): Tone Bender "Page HG", Fuzz Factory
(`nam_refs/ff`). Full logs from the run: Pi `/tmp/namrerun/*.log`.

## Findings (measurement only — nothing re-voiced; per the DSP-change workflow rule)

1. **EVH is the worst documented amp on real playing** (27-30 % both
   channels) — corroborates the already-known deferred EVH work (needs a
   knob-labeled supervised capture session; on the roadmap).
2. **The cherub muff capture fits NO era** (40-45 % against its two nearest
   candidates vs ~18-21 % for the other three caps) — either it was never a
   tuning reference or its voicing isn't represented. Era mapping otherwise:
   civilwar → era 3, blackrussian → era 4 (marginal), bluebeard → era 1 (weak,
   31 % — worth a second look with documented knobs).
3. **Level laws off at extremes:** SD-1 at drive 0.1 is **11.3 dB quiet** vs
   its capture (low-drive level law), Peavey Sat3 **8.0 dB quiet**, Fender
   clean/hot **+4.8/+5.8** (the known real-Fender PA-compression gap, see
   pa-compression notes — offline fitting already tried and reverted; needs
   ears-first work).
4. **Clip-FR ≠ pink-noise-FR:** several amps that measured "within ~1 dB" on
   pink noise show 8-10 dB worst-band deltas on real playing (Hiwatt 10.2,
   Vox 9.7, JCM800 8.5). Part probe-dependence of nonlinear FR, part knob
   uncertainty — re-measure any model with documented knobs BEFORE touching
   voicing, and only chase deltas that Ryan's ears confirm.
5. The [est]-knob rows (recto 47 %!, fender-hot, plexi, markv, muff knobs) are
   NOT evidence of model problems until rerun with the real capture knobs.

## Round 2 (same day): acting on the findings ("do them all")

### Knob-fit reruns of the [est] rows (sweeps in Pi /tmp/namfit)
- **Plexi** 16.1-17.5 % and **Mark V** 19.7-20.2 % flat across all knob grids →
  knob-insensitive, healthy band, CLEARED (matches their pink-noise-era verdicts).
- **Fender HOT** improves monotonically toward gain 0.9 (30.4 %) — capture is
  near-dimed; the residual is the PA-compression gap (see protocol below).
- **Recto vs CH3 Modern: genuine outlier.** Gain-insensitive (46-49 %) AND
  mode-insensitive (best mode 6 = 42.0 %, worst mode 0 = 73 %). No knob rescues
  it → needs its own investigation session (suspects: capture chain content,
  or the model's CH3 voicing itself). Do not re-voice from this data alone.
- **Muff era map, settled by full cross-check:** civilwar → era 3 (17.4 %),
  blackrussian → era 4 (19.2 %), **bluebeard → era 0** (21.1 % — NOT era 1;
  the Ram's-Head-clone assumption was wrong, it fits Triangle voicing),
  **cherub → NO era** (40-52 % against all six) — an unrepresented voicing
  (likely op-amp/IC-era Muff); a 7th era would be a new feature, not a retune.

### SD-1 rework — SHIPPED (pending user A/B)
Root cause was structural, found via the clip THD ladders: the old per-half
`tanh(g·x)/tanh(g)` normalisation (a) divided away the op-amp's linear gain
below clipping = the −11 dB low-drive loudness gap, and (b) put a slope kink at
the zero crossing = constant ~8 % spurious h2 at ALL low levels (capture: 0.4 %
growing with level). Rework (`SuperOverdriveSD1`): correct feedback-diode clamp
(full gain below threshold both halves; asymmetry in the CLAMP rails, 2-diode
positive = kPosRail× the 1-diode negative) + output coupling-cap DC block.
Constants fit on the user-DI harness (12+9-point grid): kGainMin 6.5,
kPosRail 1.4, kOutScale 0.32. Results: d1 specESR 13.1→**2.6 %**, d7
15.1→**6.8 %**, d1/d7 level spread 9.8→1.8 dB, spurious h2 gone. Known
residual: tanh knee softer than the real diode (driven upper harmonics run
low) — revisit only with ears. Deployed to guitaramp_drive + hexforge.
**Affected preset: Desert Robot (SD-1 drive 0.65) — needs the user's A/B;
expect fuller low-drive, no low-level fizz, ~+2 dB at high drive.**

### EVH exploration — knobs CANNOT close it (voicing session required)
The clip gap is linear (dark tilt >800 Hz worsening to −10.5 dB @8k, missing
+12.6 dB 125 Hz hump, +4 dB excess sub-50 Hz; harmonics/THD match). Presence ×
resonance grid at corrected All-Sixes knobs: presence 0.8 narrows worstFR
8.9→5.6 dB but RAISES specESR 29.9→33.2 % (the added HF mismatches in fine
structure); resonance is a no-op on the 125 Hz hump. Conclusion: needs voicing
filters (post-stage low-mid hump + HF tilt), which history says must be done
with ears in the loop (the 2026-07 post-EQ brightening was reverted for fizz).
This is the supervised-session item; the target curve is now quantified (the
FR table in /tmp/namrerun/evh-red-sixes.log).

### Fender PA-compression live A/B protocol (prepared, needs the user)
The gap is real (clean +4.8 dB makeup, HOT residual 30 % at gain 0.9; the
known NAM −16 dB vs model −6 dB PA level-compression), and the 2026-07-31
offline fit was REVERTED for artifacts (bitcrush on Fender, click on Vox) —
ears-first is mandatory. Plan (next session, ~1-2 h):
1. Add two hidden ZERO-MIGRATION tail ports (the cal_offs/cpu_cab2 pattern —
   no blob bump): `dbg_pacomp` (compression strength, default 0 = bit-identical)
   + `dbg_paknee`, plumbed to the PowerAmpProcessor **Fender-gated only** (the
   Vox click came from the shared PA — gating prevents it by construction).
2. User plays Fender clean + HOT presets and sweeps dbg_pacomp live in MOD
   advanced settings until the touch compression feels right or artifacts
   appear (watchlist from rev-99: bitcrush on fast transients, click on push).
3. Bake the ears-approved value into AmpDefaults (Fender rows), re-level the
   affected presets, delete the debug ports.

## Round 4 (same day): EVH fit BAKED at the user's ear-chosen blend

User swept the live `dbg_evhfit` knob (LAB strip, mv178) and saved at
**0.8875**. Baked permanently (mv179 / amp micro 53) as
`lv2/common/EvhCaptureFit.h`: the 5 post-PA biquads at that blend plus a
−3.2 dB makeup so the change is loudness-neutral — every EVH preset keeps its
measured parity, no kFactoryRev bump, both plugins. The tuning-lab dbg_* ports
and LAB strip retired the same day (tail removal = no blob impact).

## Round 5 (2026-08-20): HG round 2 verified post-bake

User approved both LAB candidates at blend 0.72; baked (mv181/amp54) with the
Plexi/MarkV row-10 split. Fleet re-run, all gates green:

| amp | round 1 | round 5 | verdict |
|---|---|---|---|
| JCM800 | 23.26 % | **21.77 %** | baked (padrive 1.72 + knee 0.36, neutral +0.2 dB) |
| Tremont 15 | 21.80 % | **20.43 %** | baked (tilt 2.88/6.84, neutral +1.0 dB) |
| Plexi | 17.55 % | 17.55 % EXACT | row-10 freeze verified |
| Mark V | 19.99 % | 19.99 % EXACT | row-10 freeze verified |
| EVH r/b, Fender, Recto, Rockerverb, SD-1, TS808 | — | all EXACT | fleet bit-identical |

HG round 2 conclusions: knee+drive works on the JCM800 class; tilt works on the
tight-NFB MT15 class; tilt is NOT a universal THD@1k fix (falsified on
EVH/Rockerverb — matching the THD number with the wrong distortion color makes
the spectral match worse); the Recto is a capture-content question, not a model
question, until a trusted CH3 capture exists; dynamic duty measured a no-op
(code inert at 0). Remaining known gaps: EVH nonlinear fine structure
(supervised capture session), Recto re-capture, cherub 7th-era Muff (feature).

## Round 3 (same day): the high-gain techniques pass ("do them all", commit 898ed8f)

- **Recto capture forensics: NO cab baked in** (8 kHz only −4.9 dB below
  500 Hz — a cab would be −25+). The outlier decomposes into (a) +10–17 dB of
  LF the capture GENERATES on guitar input below 125 Hz (rectifier pumping /
  ghost-note IM the model doesn't make) and (b) a preamp harmonic chasm
  (THD@1k 180 % vs 46 %, insensitive to every PA lever). Preamp-structural —
  own session.
- **Dynamics survey (feel-B, all 7 high-gain amps): NO compression gap.** The
  staircase probe rails for capture and model alike (deltas ≤ 1.9 dB, model
  slightly over if anything). The Fender was the special case. "Extend the
  compression bake to high-gain" is dead on data.
- **Optimizer sweeps:** PA nonlinear levers cannot close EVH (padrive toward
  THD parity WORSENS specESR — wrong distortion color; pascreen/pabias
  no-ops) or Recto. One candidate: **JCM800 padrive 2.0** (23.3→21.6 %) —
  parked for ears (user-approved amp).
- **EVH re-diagnosis: the dark tilt is mostly LINEAR** (THD@1k only ~15 % low).
  Shipped `dbg_evhfit` (0..1, default 0 = bit-identical): 5 harness-fit
  biquads post-PA, EVH only — lowshelf 55 −5 dB · peak 130 +6.5 · peak 1.2k
  +2.5 · highshelf 2.4k +4.5 · highshelf 7.5k +5.5, all × blend. Offline:
  worstFR red 8.9→2.5 dB, specESR 29.9→27.3 (best 0.5–0.8), blue improves
  too. Residual ~27 % = nonlinear fine structure = the supervised session.
  `nam_compare --evhfit` mirrors the exact filters.
- **PA lab now UNIVERSAL** (mv177): dbg_pacomp −1..1 (−1 = stock) /
  dbg_parel 0..300 ms (0 = stock) apply to whichever amp is selected.
  **RETIRED at mv179 unused** — and rightly so: the PA-compression frontier
  was already CLOSED by the user's own 2026-08-04 abandonment (third
  attempt; "output-VCA compression eats pick attack"). The lab was an
  accidental fourth brush (memory-hook summary misled; full file re-read
  late). Do not reopen; see pa-compression-fender memory.
- **DynamicBias preamp rollout REJECTED before coding**: items #21/#22 piloted
  bias-offset into the triode LUTs twice, net-negative both times (LUT gain
  peaks AT bias; any offset deadens). Surviving variant (envelope-modulated PA
  duty) deferred — no measured need after the clean feel survey.

## Recto re-capture: trusted replacement SOURCED (2026-08-20, not yet downloaded)

The ch3_modern.nam outlier (47 % specESR, gain/mode-insensitive, +10-17 dB
self-generated LF <125 Hz, THD@1k 180 % vs 46 %) needs replacing before any
Recto voicing work. Candidate found on TONE3000 that meets every trust bar:

**northern_fox "Mesa Dual Rectifier 3ch 6L6 - Modern Red/Orange/Green"**
<https://www.tone3000.com/tones/mesa-dual-rectifier-3ch-6l6-modern-redorangegreen-4717>
- Mid-2000s 3-channel Solo 6L6 = the EXACT variant the Diamond Plate models.
- Full head -> Suhr Reactive Load -> interface. NO cab, NO boost (the OD808 /
  Dirty Trees boosted files are separate -- exclude, per the old TS-9 rule).
- Documented knobs: EQ all noon, master 2.5-2.7, **SS/Bold** = our default
  rc_rect=Silicon / rc_variac=Bold. Calibrated 12.2 dBu. ESR 0.005-0.015.
- **Gain ladders G1-G10 per channel** (Red=CH3 Modern->mode 7, Orange=CH2
  Modern->mode 4, Green=CH1 Clean->mode 0, G1-G8) -- 28 files; lets us fit the
  gain-knob taper properly instead of single-point [est] knobs.
- License T3K: free to use, **do NOT redistribute the .nam files** (keep in
  Pi ~/dl_caps or local nam_refs; never in a release tarball or public repo).

Cross-check source if wanted: honkkis "Dual Rectifier 6L6 Modern Red 96kHz HD"
<https://www.tone3000.com/tones/mesa-dual-rectifier-6l6-modern-red-96khz-hd-5470>.
Rejected: deathblossomaudio Multiwatt packs (SD-1 + Mesa 4x12 baked in);
"2025" pack is preamp-only w/ undocumented knobs (possible preamp-chasm
diagnostic later, not a reference).

Plan when downloaded (needs TONE3000 login -- user): Red G1-G10 + Orange +
Green A2-Full files -> Pi ~/dl_caps/recto3ch/; nam_compare --model recto,
mode 7, EQ noon, gain grid vs the G-ladder (calibrate knob<->G mapping from
the clean end); verdict FIRST (if model fits this set ~20 % like its peers,
old capture confirmed contaminated -> retire it; only if a real gap remains
does a Recto voicing session open).

## Round 6 (2026-08-20): trusted Recto ladder — the model IS the outlier (verdict reversed)

Ran the full northern_fox 3ch 6L6 set (28 caps, ESR 0.0007-0.017, 12.2 dBu,
SS/Bold, EQ noon, gain ladders) on the Pi: 308 runs, 11-point model-gain grid
per capture, measuring binary rebuilt from the bake-aligned nam_compare.cpp.
Results CSV + per-run logs: Pi /tmp/recto3ch/. Captures: ~/dl_caps/recto3ch/
(T3K license — NEVER redistribute; MANIFEST.txt maps slugs to original names).

| channel | mapped mode | best-fit specESR | shape |
|---|---|---|---|
| Green CH1 | 0 (G1-G5) / 1 Pushed (G6-G9) | **19-27 %** | healthy peer band |
| Orange CH2 Modern | 4 | 30-42 % | worsens with gain, rails at model gain 1.0 from G6 |
| Red CH3 Modern | 7 | 31-48 % | same; G5+ sits at 43-48 % ≈ the old capture's 47 % |

**The round-2/3 "capture-content question" conclusion is FALSIFIED.** The old
ch3_modern.nam is corroborated, not contaminated:
- The "+10-17 dB LF the capture generates" is REAL AMP BEHAVIOR — trusted red
  G5 shows the same 80/125 Hz mountain (+11.6/+17.5 dB vs model +2.9/+7.2).
  Ghost-note IM / rectifier pumping is a genuine Recto trait the model lacks.
- The preamp harmonic chasm is CONFIRMED: THD@1k 134-136 % flat across levels
  vs model ~65 % on a 0.005-ESR capture. (Caveat to the old rule "THD>100 %
  flat = capture artifact" — that rule was learned on unverified caps; a
  low-ESR capture faithfully reproducing it means it's the amp.)
- Also measured: 110 Hz model OVER-saturated at low drive (24 % vs 14 %);
  FR model dark 3-8 kHz by 2-4 dB and bright 800-1.2k by ~2-3 dB; specESR
  insensitive to model gain 0.4-1.0 = wrong distortion COLOR, not amount.

**Next: a CH2/CH3-Modern voicing session is now justified** (was blocked on
capture trust). The HG-round-2 fit levers are already deployed default-neutral
(fit_satdrive/fit_cascdrive/fit_backdrive/fit_tighthp/fit_ghostim + nam_compare
--recto* flags) — optimizer-sweep them against the trusted red ladder, targets
in priority order: 1k harmonic chasm, 80-125 Hz LF generation (fit_ghostim),
low-drive 110 Hz over-saturation, 3-8 kHz darkness. Per the DSP-change
workflow: research the real CH3 Modern circuit (cold-clipper / bias-shift
structure) before fitting, and nothing bakes without ears.

## Round 7 (2026-08-20): Recto voicing session — fit done, LAB deployed (mv182)

Phase 1 (nonlinear levers, red_g05 @ gain 0.7, Pi /tmp/rectofit/sweep.csv):
near-exhausted. Best {rectosat 1.15, rectothp 200} = 41.3 % vs 43.5 % stock;
casc/back drives best at STOCK (raising them worsens — consistent with the
110 Hz low-drive over-saturation); **fit_ghostim is a specESR no-op at every
depth** (42.49→42.5x) — the ripple-IM mechanism as coded doesn't buy fit.

Phase 2 (`--rectofit` correction-EQ blend, 5 biquads off the winner's clip-FR
delta: peak 100 +8.0 Q1.0 · peak 210 −2.8 Q1.4 · peak 1k −2.4 Q0.8 · hishelf
2.6k +2.0 · hishelf 6.5k +2.2, ×blend): **monotonic, big** — red_g03 41.1→30.3,
g05 43.5→30.9, g08 49.0→35.8 at blend 1.0. EQ adds +0.7 dB RMS at b=1
(neutralizer ×(1−0.077b) in the LAB). Confirms the EVH lesson again: the
"harmonic chasm" was mostly LINEAR voicing.

**LAB deployed (mv182, TEMPORARY dbg_rectofit tail port, zero blob impact):**
blend 0 = bit-identical stock; blend b = fit_satdrive 1+0.15b + fit_tighthp
280→200 + the 5 biquads post-PA + neutralizer. **CH3 Modern (mode 7) ONLY** —
the measured mode; inert on every other amp/mode. Bake path when the user
picks a blend: RectoCaptureFit.h (EvhCaptureFit pattern) + fold sat/thp into
ModeCfg row 7, retire the port (tail removal, migrate test untouched).

Residual at blend 1.0 (~30 %): nonlinear fine structure (same class as EVH's
27 %) + the 80-125 Hz generated-LF mechanism the EQ only approximates
linearly. CH2 Modern (orange, 30-42 %) shares the signature but is UNFIT —
candidate follow-up after the CH3 verdict.

## Round 8 (2026-08-20): Fender PA lab — EARS VERDICT: STOCK, frontier closed

The user requested the mv176 bloom-VCA lab back (restored mv184, the one
retired unused at mv179), swept dbg_pacomp/dbg_parel on the live rig, and
chose **0.15 / 13 ms — exactly stock**. Nothing baked; lab retired at mv185.
The PA-compression frontier is now closed by DIRECT LISTENING, not just the
2026-08-04 abandonment: even the mildest variant (existing bloom VCA, no new
compressor) didn't beat stock on ears despite the offline specESR favoring
depth. Final word on the Fender +13.7 dB compression gap: the model keeps it.

## Round 9 (2026-08-21, overnight): tube-correctness audit — DEPLOYED

User directive: every amp on its real power tube; implement missing types.
Audit found THREE defects: the **Fender Deluxe had run a 6L6GC power section
its entire life** (real AB763 = 6V6GT + GZ34), the **EVH 5150III ran EL34**
(real = 6L6), and **nam_compare measured the Mark V with EL34 while the rig
plays 6L6** (rig was right). Also: Sunn set to KT88≈6550 (inert, PA
force-bypassed), Backline documented as deliberately-nominal (solid-state).
All other assignments verified correct against the real hardware.

Implemented `Tube_6V6` (TubeType 4) in PowerAmpProcessor: driveScale 3.2 /
biasShift .03 / sagDepth .50 (GZ34) / xfmrHP 45 Hz (small Deluxe OT), plus
the manual Power Tube dropdown entry + ALL FOUR clamp ceilings (both rigs,
both plugins — the stale-clamp bug class). Factory recommendedTubeType table
aligned (rig B inherits; no freeze exists post-rev-99-revert).

**Fleet verify (27 captures, /tmp/namrerun_pre_tube vs /tmp/namrerun): the
correct tubes IMPROVE every affected amp, nothing else moved:**
fender-clean 13.12→11.04, fender-hot 35.57→31.86, evh-blue 27.11→26.56,
evh-red 30.17→29.78, markv 19.99→19.57 (harness-only); all 22 other rows
bit-identical. No compensating retune needed.

Loudness: Fender rose +0.54 dB at the hf_amplevel noon anchor →
kAmpMakeup/kModelMakeup[0] 5.20→4.89 restores −13.0 dBFS clean parity EXACT
(re-measured −12.96). EVH +0.1 dB = left alone. Gates on the final build:
hf_selftest OK, preset floors all ≤ −54.3, whine_repro silent on all 8
presets (worst −69.7), 14 URIs enumerate, mv186/amp56 live.

**Morning A/B flags:** (1) Fender Deluxe feel — correct 6V6 = earlier
breakup, more sag "breathe", tighter lows; measured closer to the captures
but the user's ears rule. (2) EVH — subtle warm shift. (3) Dark Side Air +
Streets Chime — their rig-B Fender layer inherited the 6V6 (net ~+0.2-0.7 dB
after makeup); if a blend reads wrong, the layer trim is the lever.

## Output Voice: FRFR layer (2026-08-21, mv187→188 BAKED)

User plays a Fender FRFR-10 in the room; everything was voiced on headphones
(= the close-mic IR perspective). New GLOBAL layer (auto-cal pattern, tail
ports, presets untouched, off = bit-identical, verified −12.96 parity):
`out_voice` (PERMANENT toggle) + 4 TEMPORARY dbg_fv_* LAB knobs. ON =
de-close-mic EQ post-mono-sum pre-Output (low cut 40-140 Hz def 85 · prox dip
160 Hz 0-6 dB def 2.5 · presence dip 4 kHz def 2.5 · fizz tilt 8 kHz def 2.5)
+ doubler AUTO-MUTED (mono speaker combs the two takes) + airFeel AUTO-OFF
both rigs (the real room supplies the reflections). Ears session on the
FRFR-10 next: tune the 4 knobs in the room, then bake constants + retire the
knobs + give the switch a permanent UI home.

**BAKED same day (mv188):** user tuned in the room and saved — locut
**100.75 Hz** / prox **1.815 dB** / pres **3.225 dB** / fizz **2.70 dB**
(close to the estimates; presence dip deepest, prox lighter than guessed).
Constants fixed at instantiate; dbg_fv_* retired (tail removal); out_voice
now permanent in the bottom-left "OUT" corner strip (solid cyan, not lab
styling). Off-parity re-verified −12.96. The user's saved pedalboard keeps
out_voice=1; dropped dbg symbols are discarded harmlessly on load.

**mv189 (same day): the four levers promoted to PERMANENT user knobs**
(fv_locut/fv_prox/fv_pres/fv_fizz — user request: adjustable per speaker/
user). Defaults = the in-room reference bake above; the knobs appear in the
OUT strip only while the voice is ON (script stamps .hf-ov-on). Still tail
ports: global, never preset-captured, saved with the pedalboard.

## Round 10 (2026-08-21): TS-808 gain floor + the cab sentinel-mangling discovery

**Green Man gain-floor fix (user: "the pedal should boost even with drive at
zero"):** the real 808's feedback network (51k + drive·500k over 4.7k) holds a
×11.85 (+21.5 dB) mid-band floor at drive 0 — the old law (1+34·d) collapsed
to unity there, so the classic drive-0/level-up boost did NOTHING. New law
`11.85 + 24.6·d²` crosses the old value EXACTLY at d=0.5 (ts808-od5t2 capture
verifies bit-true, 6.26 %). 22 factory presets carry the Green Man, mostly at
low drive (Flatliner + Duality Crush at 0.00 — they'd been getting nothing);
9 presets' measured RMS deltas ≥0.5 dB re-leveled into out_level (rev 109).

**Cab sentinel-mangling bug (found via the re-level verification, 2-day-old
LIVE regression):** mod-ui materializes atom:Path pedalboard saves into
`<pedalboard>/effect-N/@sentinel` + a broken self-symlink — the worker's
`path[0]=='@'` check missed the absolute form, so EVERY non-@factory
synthetic-cab preset silently played the factory V30 since the Aug 19
pedalboard save. Fixed via basename sentinel recovery (`cabSentinel()`).
Post-fix the affected presets return to their rev-108 parity-measured
loudness on their CORRECT cabs. Process lessons baked into memory: snapshot
the .dat + store-diff (fresh-seed fake-HOME) BEFORE any kFactoryRev reseed;
frev-108 store preserved at Pi ~/hexforge-presets-frev108-SAVED.dat.

## Reproduce

Pi: `bash ~/nam_rerun.sh` (repo copy: `build-tools/nam_rerun.sh`) — rebuilds
nothing; runs `build/tools/nam_compare` (build with `-DGUITARAMP_BUILD_TOOLS=ON`,
target `nam_compare`, -j1) over `~/di_ref/di_all.wav` × the capture inventory
(`~/dl_caps`, `~/guitar-amp-mod/nam_refs`). Record new takes: jack_rec the raw
input (see `~/segment_di.py`).
