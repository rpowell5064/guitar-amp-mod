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

## THE BIG STRUCTURAL FINDING (next real project)
Every model is odd-harmonic-dominant where every capture is even-rich (h2/h4/h6),
and THD@1k runs ~half the captures' everywhere. Root cause measured: the shared
PowerAmpProcessor contributes almost no distortion at current gain-staging (the
preamps dominate), so it can't supply the push-pull evens or the power-stage mid
saturation real cranked amps have. Confirmed by experiment: PA duty 0.45 on
Rockerverb = no measurable change (diluted). The fix is a supervised project:
drive the PA as a first-class distortion contributor per amp + re-level with the
hfmeas preset workflow.
