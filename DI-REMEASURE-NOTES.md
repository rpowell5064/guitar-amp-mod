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

## Reproduce

Pi: `bash ~/nam_rerun.sh` (repo copy: `build-tools/nam_rerun.sh`) — rebuilds
nothing; runs `build/tools/nam_compare` (build with `-DGUITARAMP_BUILD_TOOLS=ON`,
target `nam_compare`, -j1) over `~/di_ref/di_all.wav` × the capture inventory
(`~/dl_caps`, `~/guitar-amp-mod/nam_refs`). Record new takes: jack_rec the raw
input (see `~/segment_di.py`).
