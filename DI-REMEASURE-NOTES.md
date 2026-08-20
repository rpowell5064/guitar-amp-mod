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

## Reproduce

Pi: `bash ~/nam_rerun.sh` (repo copy: `build-tools/nam_rerun.sh`) — rebuilds
nothing; runs `build/tools/nam_compare` (build with `-DGUITARAMP_BUILD_TOOLS=ON`,
target `nam_compare`, -j1) over `~/di_ref/di_all.wav` × the capture inventory
(`~/dl_caps`, `~/guitar-amp-mod/nam_refs`). Record new takes: jack_rec the raw
input (see `~/segment_di.py`).
