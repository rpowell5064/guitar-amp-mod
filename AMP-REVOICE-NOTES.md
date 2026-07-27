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

### 7. EVH 5150 III (`EVH5150Model.cpp/.h`) — pass #1, 2026-07-27  [superseded by #7b]
- What: first fix (softened cascade, gentle 1-pole HPFs, first post-limiter EQ pass).
  User verdict: "sounds better" but still dark/muddy/boxy/synthetic, esp. Red.

### 7b. EVH 5150 III re-voice #2 — 2026-07-27 (later same day)  [current, needs A/B]
- User supplied genuine SPEAKER-LESS captures ("EVH 5150 III Head Only Pack" — All
  Sixes / Balanced). These proved the presence hump is NOT speaker/mic coloration —
  the real 5150 head itself has a broad +6-8 dB hump (1.2-8k) and a +4-5 dB low punch
  (~100 Hz). Pass #1's highshelf-based EQ was the wrong SHAPE (climbs forever, never
  comes back down) — replaced with peaking/shelf filters matching the real curve.
- What changed: bodyRestore stays a 100 Hz low shelf (+9 dB — a 100 Hz PEAKING version
  was tried and found to corrupt the 1 kHz THD reading via some filter/PA interaction,
  reverted). presencePk is now a 3 kHz peaking filter. NEW topShelf (6 kHz shelf)
  extends the top independently. Gains are LARGE and CHANNEL-DEPENDENT (Red 16/9,
  Blue 11/4) — see "why so large" below.
- Why so large: the shared PowerAmpProcessor's own tube saturation eats a big chunk of
  any post-preamp EQ boost (confirmed with --nopa: same filter measures near-perfect
  with the PA bypassed, badly short with it engaged) — NOT a bug, the numbers are
  deliberately oversized to compensate for real downstream compression.
- Listen for: Red should be noticeably more open/present at the top (3-8k), especially
  compared to pass #1. Blue improves more modestly — it has much less headroom before
  a fixed EQ boost over/under-saturates it (this is the known weak point of this pass).
- KNOWN GAPS (be specific if these show up): (1) FR still runs 3-7 dB under the real
  amp in several bands — pushing further risked destabilizing the PA saturation during
  tuning, stopped at a safe point; (2) a PRE-EXISTING gain-taper mismatch at low knob
  settings (~0.5, "Balanced") causes loudness/THD overshoot — confirmed present in the
  pass-#1 baseline too, NOT something this pass introduced, separate future work.
- Revert to pass #1 if this is worse: `git revert` this commit only (topShelf member +
  the presencePk/bodyRestore/channel-scaling changes); pass #1's EQ is still the commit
  right before it.

### 8. Preset re-level + lead gain (2026-07-27)  [test the volumes + the two leads]
- Imperial Lead gain 0.56->0.66, Cardinal Lead 0.24->0.34 (you asked for "a little more"
  — both still short of a fuzz-wall; tell me up/down). Beardo BE amp itself unchanged.
- FULL preset re-measure: found the whole suite had drifted ~2-3 dB QUIET (cleans -4 dB)
  from all the accumulated fidelity work never being re-leveled. Re-leveled every non-
  manual preset back to the -12.5/-13.0 dirty/clean parity targets. This is tone-neutral
  (out_level = post-gain only) but EVERY preset's volume moved — most got LOUDER by 2-3 dB
  to hit target. If the overall set now feels too hot or uneven vs your Bank 1, that's the
  thing to flag. Your hand-locked presets (Surf Splash, Quiet Drive, I Saw a Deer, Knights
  of Fuzz) were left exactly as you dialed them.

### 9. Niche DSP batch (2026-07-27) — #7
- TAPE rework: authentic voicing (tapeVoice>0) fixed — head bump lowshelf->peak (no
  boom), gentler flutter. Default still 0 (no preset shift); good + ready to enable.
- CHORUS BBD clock-warp: SHIPPED default-on (Lush-2 presets) — subtle ±0.4% clock
  jitter = organic CE-2 wander. Level-neutral + deterministic (re-level stays valid).
  Listen for: chorus feels a touch more organic/analog. If you hear pitch instability,
  it's this — easy to disable.
- CAB speaker comp (#40): NO change — the cab ALREADY has a level-invariant "glue"
  compressor (CabinetBlock.cpp:159) that delivers the speaker-compression feel; a
  separate thermal stage would be marginal + must be default-off. Effectively covered.
- PITCH WSOLA (#43): DEFERRED — current pitch is standard 2-grain Hann COLA granular
  (working, preset-used). WSOLA reduces warble but a blind rewrite risks clicks/
  artifacts worse than shipped. Needs an A/B session with your ears; not shipped blind.

### 10. Shared PowerAmpProcessor anti-aliasing — 2026-07-27  [affects EVERY amp]
- User report on EVH ("gain swooshes the higher the gain stages get", "almost every
  company gets this right") pointed at classic under-aliased digital distortion.
- Found: OversamplingWrapper (wraps every preamp at 4x) was explicitly raised 4th->8th
  order previously because "4th-order left a measurable alias floor on high-gain amps"
  — but the SHARED PowerAmpProcessor's own 2x-oversampling AA filter (runs the tube
  waveshaper's hard saturation for every amp) was STILL the old 4th-order design
  (confirmed: identical Q values to the ones the wrapper's own comment names as
  superseded). Raised to the same 8th-order prewarped-bilinear Butterworth.
- Verified SAFE (not regressive): JCM800 measures byte-for-byte identical FR/THD/
  harmonics before/after (true A/B). But nam_compare's harmonic tool can only measure
  energy at EXACT integer harmonics — it structurally CANNOT detect aliasing (which is
  inharmonic) — so this is verified safe, NOT verified to cure the swoosh. Needs ears.
- SECOND CANDIDATE FOUND, NOT YET FIXED: the output-transformer saturation stage
  (xfmrHP/xfmrLP + the tanh/flux-domain saturation) runs AFTER downAA — i.e. entirely
  OUTSIDE the oversampled domain, with ZERO anti-aliasing protection at all, in every
  amp. Likely a bigger contributor than the one fixed here. Moving it inside the
  oversampled window is a bigger, riskier restructuring of shared code (affects every
  amp's transformer-saturation timing) — deliberately NOT attempted blind; do this
  properly in a supervised session if the swoosh persists after this fix.
- Listen for (all amps, esp. EVH/high-gain): does turning the gain knob up still
  produce an audible sweeping/whooshing artifact? If yes, the xfmr-stage fix above is
  next. If it's gone/much better, this WAS the fix and #2 can wait.
- UPDATE (same day, user: "blue sounds better, red still swooshes"): tried the proper
  fix for #2 TWICE — giving the xfmr saturation dedicated 2x oversampling. BOTH attempts
  measurably changed JCM800's distortion (h3 43%->61-65%), even in the "surgical"
  version where NFB was left completely untouched at native rate. Root cause: the flux
  integrate->saturate->differentiate design is a self-inverting pair calibrated for the
  NATIVE rate's implicit "1 sample = 1 unit delay"; the differentiator's 1/T gain
  doesn't cancel cleanly at 2x rate no matter how the integrator pole is rate-adjusted
  (tried both a properly-rescaled pole and the original native pole — both shifted
  tone, rescaled pole was CLOSER but not exact). Making this truly rate-invariant is a
  real DSP redesign (properly deriving the differentiator's rate-compensation), not a
  quick fix. BOTH attempts reverted.
- SHIPPED INSTEAD (lower risk): a single fixed, native-rate low-pass (0.72x Nyquist)
  right before the tanh/flux nonlinearity — touches NOTHING recursive. Verified safe
  (JCM800 unchanged within noise) and measurably active (EVH Red THD@1k 151-162% vs
  145-166% before). This is a PARTIAL mitigation — trims the most alias-prone content
  feeding the nonlinearity, but does NOT give it full oversampling protection. May not
  fully resolve the swoosh. A complete fix needs someone to properly work out the
  flux-differentiator's rate-compensation math — real DSP design work for a dedicated
  session, not something to retry blind.
- PASS #3 (same day, user: "still swooshes, but I can tame it with EQ — maybe the EQ
  is messed up too"): this redirected suspicion onto the pass-#2 presence EQ itself —
  a large, fairly resonant peaking filter (+16 dB/Q0.4 on Red at 3 kHz) sitting in the
  harmonically-dense high-gain signal could plausibly RING on its own, independent of
  any PA-stage aliasing. Cut both channels to unified, gentler, wider values
  (presencePk 16/11→6 dB both, Q 0.4→0.7; topShelf 9/4→3 dB both) — a direct test of
  the "my own filter is ringing" hypothesis, at the cost of FR accuracy vs the capture
  (Red is back to running 7-8 dB short in 2-8k). Listen for: is the swoosh gone/much
  better now? If YES → the EQ was (at least a big part of) the cause; re-add presence
  later more carefully (lower gain, even wider Q, maybe a shelf not a peak). If NO →
  the PA-aliasing theory stands and the pre-saturation LP above is the real lever;
  the presence EQ can go back up since it wasn't the culprit after all.

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

### UPDATE (2026-07-27): harness SUCCEEDED, real-chain integration PARTIAL
tools/evens_harness.cpp built + run. Two mechanisms tested on a clean railed square:
- M1 grid-restore (fast/slow DC envelope): INERT (h2 max 4.4) — settles to memoryless.
- M2 dual-corner (sign-split HP, different corner per half): WORKS. Best on a clean
  square: depth 0.8 / fcA 600 / fcB 3 / sign-selector 4 kHz → 111Hz h2 19.8 h3 23.6
  h4 11.2 (targets 17/22/13). Evens comparable to odds — the mechanism is correct.
  This is now the PA duty mechanism (PowerAmpProcessor dcLpA/dcLpB/dcSgn, corners in
  prepare); duty_ = blend depth, 0 = off = bit-identical.
BUT wired into the real PA and measured via nam_compare it does NOT hit targets:
at duty 0.4-0.6 h2@111 barely moves (2-1%, even DROPS vs the 5.5 baseline); only at
duty 0.9 do evens appear (h2 15.5@223) and then FREQUENCY-LOPSIDED (223>>111) with the
ODDS inflated too (h3 42, h5 27) and a real FR shift (125Hz -1.7, 800Hz +1.8). NFB is
NOT the cause (panfb 0.05 vs 0.4 identical). ROOT CAUSE: the harness fed a PURE
full-rail square; the real preamp output reaching the PA is BAND-LIMITED (rounded
edges from inter-stage LPs ~8-9kHz) + level-dependent, so the dual-corner has weak
flat-tops to key off. Kept default-OFF for every amp (verified bit-identical).
NEXT: either (a) apply the mechanism to a sharper/re-squared node, (b) make depth
level+edge-adaptive, or (c) accept that DI-capture even-harmonic matching may need a
fundamentally different power-stage topology. The harness (--paduty/--panfb sweep
flags + tools/evens_harness) is the desk-loop for all of it.
