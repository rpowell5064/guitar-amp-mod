# Amp Re-Voice Notes — 2026-07-26 overnight sweep

Per-amp record of what changed, what to listen for, and how to revert each change
independently. All tuning was measured on the Pi with `nam_compare` against the DI
captures from Downloads (staged at Pi `~/dl_caps/amps2/`). Presets were NOT
re-measured — if a changed amp's presets feel off in level or tone, that's expected
fallout to flag, not a mystery.

Revert any single amp: `git log --oneline -- <file>` → `git revert <hash>` (each amp
is its own commit), rebuild `guitaramp_amp` + `guitaramp_hexforge`, deploy.

---

## CHANGED — play these and judge

### 1. JCM800 (`JCM800Model.cpp/.h`) — commit 71565ea  [USER-APPROVED ✓]
- What: softened cascade drives, bright-cap pre-emphasis into the cascade, input HPF
  60→130 Hz, opened top (inter23LP 11k, air 19k), body restore 260 Hz.
- Why: knob-matched capture showed near-square fizz (h4–h9 20-30%) + LF over-clip.
- Listen for: smoother/more open crunch. Presets: any JCM800 preset may be brighter.

### 2. Rockerverb dirty channel (`Rockerverb50.cpp/.h`) — commit 1aeb53f
- What: inter-stage couplings 0.50/0.45/0.42 → 0.70/0.65/0.60 (the cascade now
  actually saturates at high gain), bright pre-emph 800 Hz/+8 dB into the cascade,
  small asymmetry biases into stages 2/3/4, dimed bass shelf 11→9.5 dB.
- Why: knob-exact dimed capture (Gain10 B10 M10 T10): model was 25% THD vs the
  amp's 43-60% — audibly under-saturated; FR too bright in lows.
- Listen for: MORE saturation/compression on the dirty channel at med-high gain —
  this is the biggest audible change of the sweep. If Orange presets now feel
  over-gained, the couplings are the lever (they raise drive ~2.9x through the
  chain). Clean channel untouched.

### 3. Backline Plus / Peavey (`PeaveyBackstageModel.cpp`) — commit 0090ede
- What: clipper drive ×3.2 + output makeup ×1.9.
- Why: labeled capture (Pre10 Sat3 Pos7): the model ran ~10.5 dB too quiet into its
  clipper — clipped too late, half the grind; harmonic profile now overlays.
- Listen for: Backline is LOUDER and grindier at the same knobs. The Desert Robot
  preset (QOTSA) WILL be louder/dirtier — likely needs its level trimmed. This is
  the most preset-affecting change.

### 4. Hiwatt (`HiwattDR103Model.cpp`) — commit f9a8148
- What: FR only — lows restore (lowshelf 90 Hz +5.5, was a -1.5 CUT) + 180 Hz -2.6
  dip. No drive/distortion change.
- Why: knob-labeled ALL-12/Master-10 capture is +5 dB fatter below 100 Hz.
- Listen for: fatter low end (Who-style thump). If too boomy in a preset, the
  90 Hz shelf is the lever.

### 5. Fender + Deluxe Reverb cleans (`FenderDeluxeModel.cpp`,
   `DeluxeReverbAmpAB763.cpp`) — commit d33cb4a
- What: cathode keystone 0.38 → 0.08 (near-off) on the clean stages.
- Why: the '65 DR capture is STIFF (~0.5 dB bloom); uniform keystone made it spongy.
- Listen for: slightly firmer sustain on cleans. Subtle.

### 6. Power amp (`PowerAmpProcessor.cpp/.h` + plugin call sites) — infrastructure only
- What: added a "duty" push-pull-asymmetry param (even harmonics), DEFAULT 0 =
  bit-identical for every amp. No amp uses it yet (see finding below).
- Listen for: nothing — it's inert plumbing.

## TESTED AND VERIFIED ALREADY-GOOD — unchanged, no action needed
- Plexi (CH I High: THD 54/51 vs 48/50, FR ~2 dB)
- Mark V / Cali V (IIC+ HG bal: FR ±0.4 dB, harmonics overlay)
- Recto / Diamond Plate (CH3 Modern: FR dead-on incl. the +20 dB @50 resonance)
- Friedman (BE gain-sweep = its own tuning ref)
- MT15 (Metal_Sweet_Spot g0.7 = its tuning ref, FR ±0.1 dB)

## ATTEMPTED AND REVERTED — shipped voicing kept (needs supervised structural work)
- EVH (2 attempts): both capture packs agree the LF distorts 2x and mids run half —
  but 2-pole tightening sprayed high-order junk (Butterworth overshoot into the
  clipper; Q=0.5 helped but Blue/Red want CONFLICTING filter sets) and the fixed
  mid scoop overshot Red to 163% THD@1k. Needs per-channel filters + post-limiter
  EQ restructure. Shipped EVH's known defects are milder than any attempt.
- Vox AC30: the input tighten FIXED the LF THD (matched the capture across the
  whole sweep) but the low-end restore has nowhere to go — the Vox chain has no
  post-limiter EQ slot, so restoring lows pre-limiter re-clips them (THD comes
  back). Needs the EVH-style post-limiter EQ restructure. Reverted.
- Sunn: no DI capture exists in Downloads (Shift Line pack is full-rig).

## THE EVENS/MID-THD PROJECT — mechanism search results (2026-07-27 ~3 AM)
The "promote the PA" project was executed as far as blind measurement allows.
FIVE candidate mechanisms implemented + measured against the knob-exact dimed
Rockerverb capture (target h2 17 / h4 13 / h6 11; THD@1k 60 vs our 31):

1. PA input drive x4 (padrive): INERT — the PA waveshaper is ALREADY railed
   (tanh arg ~±2.3 at stock); more drive doesn't change a railed waveform.
2. Static tanh-arg offset (duty v1, up to 1.76): INERT.
3. NFB reduction to 0.02: INERT (THD@1k 31→34 — NFB isn't the ceiling either).
4. Offset + low NFB combos: INERT.
5. Stateful positive-flat-top droop (duty v2, 8 ms): level shifts but h2 stays
   fixed — the droop settles within the half-cycle and degenerates into amplitude
   asymmetry.

WHY (the actual physics, worth internalizing): the high-gain preamps output a
near-SQUARE. A two-level signal has fixed zero-crossings, so NO memoryless
post-nonlinearity can shift its duty cycle or add even harmonics — offsets just
rescale the two levels (DC-blocked away). And a settled envelope droop is the
same thing. Real push-pull stages get their evens from CONTINUOUS asymmetric
coupling dynamics (different +/− RC time constants → frequency-dependent
flat-top TILT) and from PI grid-current — mechanisms with real filter state per
half. Designing that correctly needs an OFFLINE harmonic harness (square in →
candidate mechanism → FFT) before ever touching the live PA again.

WHAT SHIPPED (all default-neutral, verified bit-identical at defaults):
- PowerAmpProcessor: padrive/pamakeup/duty params + per-amp AmpDefaults fields
  + the stateful droop implementation (duty 0 = off for every amp).
- nam_compare: --padrive/--pamakeup/--paduty/--panfb CLI overrides (sweep any
  PA config against any capture with NO rebuild — the tool the next session
  needs).
NEXT SESSION: build tools/evens_harness (offline square+mechanism+FFT), find the
coupling-tilt mechanism that yields h2 15-20% on a square, THEN wire it into the
PA duty param and tune per amp vs captures. Do NOT retry memoryless approaches.
