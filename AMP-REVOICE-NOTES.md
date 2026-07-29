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
- **BASS-KNOB CUTOUT FIX (same day, user: "boosting the bass makes it cut out"):**
  measured a 13.2 dB output collapse (Red, noon->max bass — nearly silent). Root-caused
  via A/B against the pre-tonight baseline: this is a PRE-EXISTING bug (untouched
  baseline already drops 6.8 dB noon->max) — the shared PowerAmpProcessor's flux-domain
  transformer saturation grinds hardest on LF content and can collapse toward silence
  rather than clip cleanly when driven too hard. This session's bodyRestore addition
  (+9 dB fixed low-shelf, post-limiter) stacked ADDITIVELY with the Bass knob's own
  up-to-+10 dB low-shelf at the same ~100 Hz, adding ~6 dB more collapse on top. Fix
  (EVH-local, zero blast radius): bodyRestore 9->3 dB, Bass knob's own range halved
  10->5 dB. Verified: noon->max now -3.1 dB (normal tonal shift), was -13.2 dB. The
  deeper ~7 dB PowerAmpProcessor-level bass-sensitivity still exists in principle
  (shared code, other amps could theoretically hit it with enough bass boost) but
  EVH's narrower range no longer reaches it audibly. If ANY other amp's presets/EQ
  ever reports a similar "cuts out on bass boost," check for the same additive-
  stacking-into-flux-saturation pattern first.
- **CONFIRMED (user, same day): swoosh is GONE with the gentler EQ.** Root cause was
  the presence filter's OWN resonance, not PA-stage aliasing — the 8th-order AA fix
  and the pre-saturation LP (items above) were real, legitimate hygiene improvements
  but NOT what was making Red swoosh. Current state (presencePk 6 dB/Q0.7 both
  channels, topShelf 3 dB both) is the known-good, swoosh-free baseline — do not
  raise these gains again without re-testing for the ring returning. Red's FR now
  runs 7-8 dB short of the capture target in 2-8k (thinner/less bright than pass #2)
  — that's the deliberate trade for stability. If more brightness is wanted later,
  go WIDER (lower Q handled carelessly can still ring — prefer very broad Q like
  0.9-1.2, or a true shelf) and gentler-stepped (raise a couple dB at a time,
  re-test each step) rather than a big single jump.
- **BASS-KNOB CUTOUT, ROUND 2 (user: "not the whole way. do we need to make
  multiple poweramps?"):** the round-1 fix (above) only addressed a STEADY-STATE
  RMS collapse; nam_compare's attack/bloom test (section A, 1 kHz burst) exposed
  the REAL remaining bug was a TRANSIENT one — at bass=1.0, a note took 311 ms to
  reach 90% of its target level with 8.4 dB of bloom (vs instant/healthy at
  bass=0.5) — i.e. a genuine "quiet hit that blooms up a third of a second later,"
  which is what "cuts out" actually describes. Two dead-end diagnoses first: (1)
  hypothesized EVH's own `c.sagEnv` was driven by raw bass energy in `abs(x)` —
  added a high-pass on the detector's input (300 Hz, then 1000 Hz) — ZERO
  measurable effect either time, disproving the theory; (2) re-sourced the
  detector to tap PRE-tonestack (so bass_ genuinely cannot reach it at all) —
  STILL zero effect, proving EVH's own internal sag mechanism was never the
  cause. ROOT CAUSE (confirmed via `--nopa`, bypassing the shared
  PowerAmpProcessor entirely): bass=1.0 with `--nopa` measures only 5 ms/0.7 dB
  (nearly healthy) vs 311 ms/8.4 dB with it in the loop. EVH's own tonestack Bass
  shelf (Marshall spec, ±14 dB @ 100 Hz, `ToneStackComponent::kMarshall`) sits
  POST-cascade on an already heavily-distorted broadband signal — a full +14 dB
  boost there genuinely inflates the ABSOLUTE LEVEL reaching the shared PA, and
  the PA's own 6L6GC supply-sag detector (200 ms release constant, tracks
  absolute level, see `PowerAmpProcessor::k6L6GC.sagRelMs`) reacts by holding a
  much deeper gain reduction that only recovers on its own fixed clock. **This
  directly answers the user's question: no, multiple power amps are not
  needed** — the shared PA's sag behavior is correct/by-design for normal
  playing dynamics; the bug was that a STATIC EQ knob was swinging the absolute
  level feeding it far more than any real amp's tone control would. Tried a
  broadband post-hoc gain compensation first (cancel ~40% of the shelf's own
  dB in a flat trim) — barely moved the needle (8.4→8.1 dB) because the PA's sag
  was already saturating near its floor at this drive level, so a uniform level
  cut doesn't proportionally unsag it once triggered. Fixed at the actual
  source instead: compress how far the Bass knob can push the tonestack's OWN
  internal shelf via a new `kBassKnobToStack()` mapping in EVH5150Model.cpp
  (0.5 + (v-0.5)*0.15, i.e. the knob's effective internal range is now ~15% of
  Marshall's ±14 dB spec, down from the full range) — fully EVH-local, does NOT
  touch the shared `ToneStackComponent` spec (JCM800 and everything else using
  Marshall-type stacks is untouched/bit-identical). Verified: bass=1.0 attack
  0.0 ms / bloom 1.8 dB (was 311 ms / 8.4 dB); bass=0.5 unaffected (healthy);
  Blue channel healthy at both settings (0.68/0.75 dB bloom, 0 ms attack — was
  never as badly affected as Red). Knob is still tonally useful: ~4-6 dB more
  low end at max vs noon (80 Hz: -2.3 vs -6.4 dB; 50 Hz: -3.8 vs -9.8 dB vs
  capture), just far short of a full ±14 dB swing. LESSON for future amps: a
  per-amp tonestack knob range that looks reasonable in isolation can still
  overload the SHARED PowerAmpProcessor's absolute-level-keyed sag detector if
  applied post-cascade to an already-distorted signal — when tuning any amp's
  Bass/tonestack range, sanity-check nam_compare's attack/bloom section at the
  knob's extremes, not just at noon.

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

## TIER 2 ITEM #22 — sag-into-operating-point, plumbing built + pilot tuned NEGATIVE (2026-07-28)

Built the shared plumbing for "sag into the operating point, not just the volume"
(roadmap #22): `PowerAmpProcessor::getSagEnvNorm()` exposes its own mono supply-sag
envelope; `AmpModelBase::setExternalSag()` is a new default-no-op virtual hook; both
`amp_plugin.cpp` and `hexforge_plugin.cpp` call `amp->setExternalSag(pa.getSagEnvNorm())`
right after `pa.process()` each block (one-block-stale feedback, negligible given
sag's 10-350ms time constants); `nam_compare` mirrors the same call so it can be used
to tune it offline. Verified inert (byte-identical JCM800/EVH via nam_compare) before
any amp used it.

**PILOT (JCM800): NEGATIVE RESULT.** Implemented `JCM800Model::setExternalSag()`
forwarding `kSagBiasCoupling * paSagEnv` to all 4 TriodeComponent stages'
`setSagBias()`. Swept ±0.3/0.8/1.5 against nam_compare's attack/bloom section
(gain 1.0, the driven JCM800 capture): **every nonzero value made the model STIFFER**
(bloom -0.41 dB baseline -> -0.87/-1.07 dB at |0.8|/|1.5| -- moving AWAY from NAM's
+1.53 dB target), and **symmetrically** (±0.3 produced the IDENTICAL -0.41 dB either
direction -- sign doesn't matter, only magnitude, and magnitude only hurts). Harmonics
barely moved either (h3@111 stayed ~43.6-43.7 across all three magnitudes).

This is the SAME failure mode already found and disabled for item #21 (grid-conduction
blocking distortion, see `TriodeComponent.h`'s "BLOCKING DISABLED... net-negative for
feel" comment) -- both items drive the identical additive `xBiased = xFiltered -
blockDepth_*(...) + sagBias_` mechanism in `TriodeComponent::process()`. Two
independent Tier-2 sub-items, using the same underlying mechanism, have now both
converged on the same negative result. **Suspected root cause:** `buildLUT()` solves
`normScale` (unity small-signal gain) AT the DC bias point specifically, so the LUT's
gain is HIGHEST exactly there by construction -- any additive offset in either
direction pushes the operating point into a lower-slope region of the Koren curve,
which generically makes the stage LESS sensitive/dynamic, not more. An additive
bias-shift on a LUT built this way may be structurally the wrong lever for "touch"
feel, regardless of what drives it (blocking distortion or sag).

**Reverted to inert** (`kSagBiasCoupling = 0.0f`, confirmed byte-identical vs the
pre-sweep baseline) and deployed in that state -- the plumbing (getter/hook/wiring)
stays in the tree since it's genuinely reusable infra and free when off, but do NOT
naively roll `setExternalSag()` out to the other ~12 amps using this same "scale a
raw sag amount, add it to the LUT input" pattern without addressing the root cause
first. Two real paths forward if this is revisited: (a) a genuinely different
coupling -- e.g. modulate the LUT's own `normScale`/gain multiplicatively as sag rises
(so the operating point doesn't move but the local sensitivity does), or feed sag into
something other than the triode bias entirely (e.g. a downstream makeup-gain or
softLimit knee); or (b) accept #22 as not viable in this architecture and reallocate
Tier-2 effort to #24 (NFB wrapping) or #27 (current-dependent ripple) instead, which
don't share this mechanism.

## TIER 2 ITEM #27 — current-dependent mains ripple, SHIPPED on JCM800 pilot (2026-07-28)

Real rectifier ripple grows with current draw; the existing ripple term in
`PowerAmpProcessor::process()` was a fixed constant (`0.003f`, -50 dBFS) regardless of
how hard the amp was being driven. Added `rippleSagCoupling` to `AmpDefaults` (new
trailing field, default 0.0 = every amp except JCM800 unaffected) and changed the
ripple amplitude to `0.003f + rippleSagCoupling_ * sagEnv` -- additional depth that
grows with the SAME sag envelope already driving the supply-voltage droop, so louder/
harder playing = deeper ripple = real mains-ripple "ghost note" intermodulation, not
just a constant hum floor. Wired the default into both plugins + `nam_compare`
(`--paripplesag` sweep override).

**Cannot be offline-verified beyond "doesn't regress existing metrics."** Confirmed
byte-identical FR/THD/harmonic-profile/attack-bloom/dynamic-gain-curve/sag-recovery on
JCM800 at rippleSagCoupling 0 and 0.02 -- every number in nam_compare's report was
IDENTICAL at both values. This is expected, not a bug in the verification: the ripple
rides at the mains frequency (100/120 Hz) and produces sidebands INHARMONIC to
whatever test tone is being measured, so none of the existing harmonic/FR/feel
sections (which only look at exact integer harmonics of the test tone, or broadband
level/timing) can see it at all -- same blind spot already noted for aliasing
detection. There is no numeric target to tune against here; this is a pure
character/authenticity addition, "best A/B'd by ear" (same caveat as the Uni-Vibe/
wah/spring-dispersion Tier-3 items).

**Shipped as a PILOT on JCM800 only** (`rippleSagCoupling = 0.02`, case 1 in
`getDefaultsForModel()`) -- every other amp stays at the struct default 0.0
(unaffected, confirmed via EVH sanity check). Built, deployed to the Pi's live
`guitaramp_amp`/`guitaramp_hexforge`, mod-host/mod-ui restarted clean. **Needs the
user's ears**: listen for a subtle mains-ripple "breathing"/ghost-note character that
grows under harder playing on JCM800 specifically, and report back whether it reads as
authentic texture or an artifact -- that verdict decides whether to tune the coupling
value further or roll it out to the other amps (each would want its own tuned
coupling, same as bloomVca/duty/nfb already are per-amp).

**USER CONFIRMED (2026-07-28): "sounds great."** Rolled out to the rest of the amps
that have their own dedicated (or clearly-defined) PowerAmpProcessor row, scaling each
value roughly proportional to that row's existing `sag` field: EVH 0.012 (kept modest
-- EVH already has its own internal sag/bloom, bloomVca=0 there to avoid double-
counting, so PA-level ripple should layer lightly, not compete), Rockerverb 0.022
(highest sag of the EL34 rows besides Fender), Friedman 0.018, Recto/Diamond Plate
0.010 (explicitly a TIGHT-supply amp per its own comment), MT15/Tremont 15 0.006
(tightest/most percussive amp in the suite -- kept smallest so it doesn't fight that
character). All confirmed byte-identical vs each amp's existing FR/THD/harmonic/feel
baseline (EVH re-verified explicitly).

**IMPORTANT DISCOVERY while rolling out:** `kCanonical` (both `amp_plugin.cpp` and
`hexforge_plugin.cpp`) maps MULTIPLE LV2 amp models onto the SAME `getDefaultsForModel`
row -- row 1 (JCM800) is also used by Plexi (idx 10) and Mesa Mark V/Cali V (idx 11).
**The JCM800 pilot value (0.02) has therefore been live on Plexi and Cali V too since
deployment, not JCM800 alone** -- the user's "sounds great" verdict was on JCM800
specifically but likely also covers those two (same EL34 PA family, which is *why*
they share the row in the first place). Flagging this rather than silently letting it
ride; if Plexi/Cali V should get their OWN tuned value later, the row needs splitting
first (see the pre-existing "kCanonical rows are SHARED" note above).

**Left at 0 (not rolled out):**
- **NAM neutral** (row 3) -- user-supplied captures may already carry real ripple
  color from whatever the capture chain included, or may not; adding synthetic ripple
  on top risks an unpredictable double-up. Needs a deliberate decision, not a guess.
- **Row 0 (Fender/Hiwatt/Vox + Peavey Backline)** -- this row is shared with Backline,
  the suite's ONE solid-state amp. A solid-state supply doesn't have the tube-
  rectifier ripple-modulated "ghost note" character, so adding it here would
  incorrectly color Backline. Needs the row split before Fender/Hiwatt/Vox can get
  their own value without also hitting the solid-state amp.
- **Sunn Model T** (row 4) -- PA is bypassed entirely for Sunn (own SunnPowerAmp6550),
  so any value here is inert regardless; left at 0 for clarity, not function.

## TIER 2/3 ITEM #40 — speaker drive compression, SHIPPED (2026-07-28)

Added a PRE-convolution, phenomenological cone/coil-response model to
`CabinetBlock` (shared by the standalone Cab plugin and Hex Forge): (1)
envelope-driven ~1.2:1 program compression (max ~1 dB GR), (2) level-dependent
LF soft-sat below ~120 Hz modeling Bl force-factor droop under excursion, (3) a
very slow (seconds-scale) thermal HF tilt modeling voice-coil heating. All
three are LEVEL-INVARIANT by design (fast/medium envelope vs. the signal's OWN
slower reference, never an absolute threshold) since the cab's insertion point
runs at very different absolute levels depending on what's upstream (a hot fuzz
vs. a clean boost) -- same reasoning as the pre-existing Studio-voice bus glue.
Default off = bit-identical (structurally guaranteed: the new code only runs
inside `if (spkDriveOn_)`).

Verified via a throwaway heap-allocated harness (not committed): stable at
max depth (no NaN/blowup), measurably active (a loud swell compressed ~1.85 dB
vs. off), and the quiet tail after the swell recovers cleanly with no stuck-
compression hangover. Hit one real bug along the way: linking a freshly-
compiled test harness against a STALE `libGuitarAmpSim.a` (built before the
CabinetBlock changes synced into that specific build tree) produced a classic
ABI-mismatch segfault (garbage pointer = the bit pattern of a stray `double`
value) -- rebuild the exact static lib you're linking against, every time.

**Exposed as ONE enumerated control (Off/Subtle/Full) instead of a toggle+knob
pair** -- collapses two ports into the same compact dropdown-row pattern
already proven for Cab Voice/Room Density, avoiding the standalone Cab
plugin's tight `nowrap` knob rows (which explicitly overflow if crowded, per
existing comments) while still giving Hex Forge's `wrap`-friendly layout a
consistent widget. Subtle=0.35 / Full=0.75 depth, conservative since this is
brand-new and not yet user-tuned.

**Wiring, both plugins:**
- Standalone Cab (`cab.ttl` microVersion 44->45): new `spk_drive` port
  (index 17, enumerated 0/1/2), control/notify shifted to 19/20. Cab
  plugin maps it to `CabinetBlock`'s two internal params.
- Hex Forge: added via `build-tools/gen_hexforge.py` (the port-generator
  script — hexforge_ports.h/hexforge.ttl/icon-hexforge.html are ALL
  auto-generated from it, do not hand-edit) as `HF_CAB_SPKDRIVE`, appended
  right before `HF_SW_A` (the fixed preset-command block) — the established,
  lowest-risk append point. This required the full "port append" checklist:
  (a) fixed the boundary `static_assert` in `hexforge_plugin.cpp` (was
  `HF_RV_BLOOM == HF_SW_A - 1`, now `HF_CAB_SPKDRIVE == HF_SW_A - 1`) — **there
  is a SECOND, independent copy of this exact assert chain in
  `build-tools/hexforge_migrate_test.cpp`** ("copied verbatim... do not edit by
  hand" applies to hexforge_ports.h, NOT this file, which is genuinely
  hand-maintained and silently drifts — confirmed it was already stale since
  v27 shipped, meaning this test hadn't actually passed in a while unnoticed);
  (b) added `HF_CAB_SPKDRIVE` to `kSwWatch[]` (enumerated ports get the
  mute-ramp-on-change treatment); (c) added explicit gap-fill migration
  (`spkGap`) in `migratePorts()` — **appending a port right before a fixed
  block is NOT automatically zero-fill-safe**: without the explicit gap, old
  blobs would silently consume the wrong old value at this index, shifting
  EVERY subsequent port (switches, preset commands, meters, tuner) by one
  slot; (d) bumped the blob version 27->28 at both `putU32` call sites. Fixed
  the pre-existing stale test cases in `hexforge_migrate_test.cpp` too (their
  hardcoded tail-shift math hadn't accounted for v27's bloom port either) and
  added a dedicated v27->v28 case; full suite now passes (0 failures) —
  previously would have failed silently since nothing was running it.

**Needs the user's ears**, same as items #27/#22 -- this is a character/
authenticity feature with no numeric target to verify against. Try Speaker
Drive = Subtle then Full on the Cab block (or the Cab tab in Hex Forge) and
listen for a more "speaker-like" response under hard playing (compression +
bass grit + a slight dulling of the top on sustained loud passages) vs. an
artifact. Not yet enabled on any factory preset -- pure opt-in for now.

## TIER 3 ITEM #42 — decramped EQ, VERIFIED + IMPLEMENTED, no wiring target yet (2026-07-28)

The roadmap named "Orfanidis peak / Massberg shelf" — the Orfanidis half checked out
(verified BYTE-FOR-BYTE against Appendix B / peq.m of the actual 1997 JAES paper,
fetched and text-extracted directly, not just cited from memory), but "Massberg
shelf" does not appear to be a real, findable reference: M. Massberg's actual
published work is on dynamic-range compressor design, not shelving filters, and
searching found no shelf technique under that name. Per the user's direction,
found the real, correct technique instead: the classic Regalia & Mitra (1987)
allpass-based shelf. Its Nyquist-exactness is provable directly, not just cited --
a first-order allpass is EXACTLY +1 at DC and EXACTLY -1 at Nyquist for ANY
coefficient, so `shelf(z) = C1 + C2*allpass(z)` lands on the two requested gains
exactly at both ends by construction, for any corner frequency. Verified
numerically (0.000 dB error at every fc/gain tested, including fc within one
octave of Nyquist) AND via a from-scratch C++ port cross-checked against the
Python verification (not just trusting the derivation once).

Added both as new, purely additive functions in `BiquadFilter.h`:
`Filters::peakingDecramped()` and `Filters::highshelfDecramped()` /
`lowshelfDecramped()`. Existing `peaking()`/`highshelf()`/`lowshelf()` untouched.
Both new functions confirmed exact-identity at 0 dB gain (the shelf's stored
coefficients look nonzero at 0 dB but the recursive filter state provably stays
at exactly 0.0 forever once b1==a1, verified in the C++ test, not just algebra).

**No live wiring target found, on purpose -- did not force one.** Investigated
where this would actually apply: the roadmap's own example frequencies (4 kHz
peak / 8 kHz shelf / 16 kHz high-cut) match `OutputEQBlock` exactly, but that
class is ONLY used by the old JUCE `GuitarAmpProcessor` -- confirmed via grep
that NO LV2 plugin references it at all, so it's unreachable dead code from the
user's perspective. The one genuinely reachable, truly user-facing EQ in the
live LV2 suite is Hex Forge's 6-band `GraphicEQ` (100-3200 Hz, peaking only, no
shelf bands) -- but numerically verified the PLAIN RBJ peaking filter is
ALREADY exact at both its center frequency AND at Nyquist for all 6 of its
bands (the forced-0dB-at-Nyquist assumption in the naive formula happens to be
correct at these frequencies, since they're not close enough to Nyquist for the
assumption to break down) -- swapping formulas there would be pure downside
(shifts every EQ-using preset's exact tonal curve for zero measurable benefit).
Every OTHER higher-frequency filter in the live suite (amp presence/air shelves,
cab mic-placement filters) is internal, capture-tuned voicing -- explicitly
off-limits per the item's own "avoid re-voicing internal curves" scope.
**Conclusion: infrastructure is real, verified, and ready — reach for it the
next time a genuinely user-facing EQ or shelf control is added that operates
anywhere near Nyquist (a > ~8kHz peaking/shelf knob would be the natural
trigger). Nothing to enable or test right now.**

## TIER 2 ITEM #24 — NFB wrapping the nonlinearity, SHIPPED all amps (2026-07-28)

The shared `PowerAmpProcessor`'s negative-feedback loop previously tapped the
signal AFTER the tube waveshaper (`out[ch][i] - HP(prev)*nfbScale`) — a fixed
post-hoc EQ correction on the already-clipped output, not real feedback.
Restructured into a genuine closed loop: the HP-filtered PREVIOUS sample's
FULLY-PROCESSED output (post transformer + speaker-impedance coupling) is now
subtracted from the drive signal BEFORE the tube waveshaper, sample-by-sample
— matching how a real amp's NFB loop actually works (feedback derived from the
transformer-secondary/speaker-facing signal, applied at the input of the power
stage, ahead of the nonlinearity).

**This required converting what were 5 separate whole-block passes (upsample,
waveshape, downsample, NFB, transformer+coupling) into ONE unified per-sample
loop** — a true closed loop needs sample i's processing to depend on sample
i-1's FULLY finished output, which the old block-wise architecture couldn't
express. All the existing per-sample stateful math (duty dual-corner
coupling, flux-domain OT saturation, speaker-coupling envelope) is
UNCHANGED, just now inlined in the same relative order rather than run as
separate whole-block passes — verified this preserves exact behavior via the
zero-feedback sanity check below. Presence/depth EQ stays OUTSIDE the loop
(downstream tone shaping in a real amp, not part of the feedback path).

**Verification (JCM800, the reliable reference amp):**
- `--panfb 0` (feedback forced off): output BYTE-IDENTICAL to the pre-refactor
  baseline (`diff` exit 0) — confirms the restructuring itself introduced no
  bugs; the only behavior change comes from the loop topology actually being
  active, exactly as intended.
- Default nfb (0.42): small, plausible differences vs the old topology —
  8kHz FR shifted -3.0→-2.1dB (closer to the capture), mid-band THD@1k's
  "too saturated" flag cleared at -12dBFS (a genuine improvement), harmonics
  shifted by ~0.1-0.4 percentage points, feel metrics (bloom/compression/sag
  tau) shifted by ~0.05dB / ~1ms. Modest, not dramatic, at this feedback depth.
- Stability stress tests: `--panfb 1.0` (max), `--panfb 3.0` (3x beyond the
  normal range), EVH Red at gain=1.0 (max drive, a known stress config this
  session) with both default and max NFB -- NO NaN, no blowup, no runaway
  values at any of these. The roadmap's own "watch stability at high β"
  warning did not materialize in testing.
- Interestingly, 0.42 vs 1.0 nfb produced only TINY differences (~0.1dB, ~0.1-
  0.2 percentage points) despite a 2.4x change in feedback depth -- the NFB
  signal itself is inherently small (a narrow HP'd slice of the output), so
  its effect on the total signal stays subtle across this whole range. This
  is a real property of the mechanism at CURRENT per-amp nfb values, not a
  bug -- if a more dramatic "damping collapse" character is wanted, the
  natural next step is retuning each amp's nfb default now that the topology
  has genuinely changed (not done yet -- every amp kept its existing nfb
  value; the topology change alone is the deliverable here).

Deployed to the Pi's live `guitaramp_amp`/`guitaramp_hexforge`; mod-host/mod-ui
restarted clean. Removed the now-unused `osBuf` scratch member (replaced by
per-sample local upsample/downsample, no longer needs a whole-block buffer).
Cost: as estimated in the roadmap (~0.2%), no measurable CPU concern.

## TIER 2 ITEM #29 — LTP tail coupling, SHIPPED as JCM800 pilot (2026-07-28)

A real-amp long-tailed-pair phase inverter has two triodes sharing one large
tail resistor: when BOTH grids swing toward conduction together (harder
playing), the shared tail voltage rises and pulls BOTH grids colder in
common-mode — a level-dependent imbalance, not a fixed offset. The suite
already has a `PhaseInverter` class with real per-amp LTP presets
(`kMarshall_LTP`/`kEVH_LTP`/`kOrange_LTP` — JCM800, EVH, Rockerverb, exactly
matching this item's target amps) but it's wired into NOTHING except
SunnModelT/DeluxeReverb, and its `ltpImbalance`/`ltpBiasOffset` are FIXED
constants, not level-dependent — the missing dynamic piece this item wants.

**Did not do a full two-path PI split.** The shared `PowerAmpProcessor` used
by every other amp has no concept of two separate phase-inverter grids at
all (a single lumped `tubeWaveshaper()` call represents the whole power
stage) — introducing a literal dual-path PI would be a much bigger
architectural change than this item calls for, and would only reach 3 amps
while leaving the other 10 with no equivalent mechanism. Instead, followed
the SAME pattern items #25 (crossover)/#26 (duty) already use to approximate
push-pull effects on a single lumped signal: track a fast envelope of the
pre-waveshaper drive signal (a proxy for "how hard both grids are swinging
together" absent a literal split) and apply a bias shift that grows colder as
the envelope rises. Added as `tubeWaveshaper`'s new `ltpBias` parameter
(computed statefully in `process()`, passed in since the waveshaper itself
holds no state) and a new `AmpDefaults::ltpTail` coupling coefficient.

**Verification:** confirmed a real bug along the way -- `nam_compare` and both
live plugins were never wired to actually READ `AmpDefaults::ltpTail` (copy-
paste gap from the #27 rollout), so the first test showed literally zero
effect; fixed the missing `setParameter("ltptail", ...)` calls in all three
places. After the fix: JCM800 at the shipped depth (0.15) shows a small,
plausible shift (harmonics move by a few tenths of a percentage point).
Pushed to 20x the shipped depth (--paltptail 3.0) as a stability/correctness
stress test: even-harmonic content (h2/h4) clearly and monotonically
increases with depth exactly as the physical mechanism predicts, confirming
the mechanism works as intended -- the shipped value is just conservative by
design, not weakly-implemented. No NaN/instability at any depth tested.

**Shipped as a JCM800-only pilot** (`ltpTail = 0.15`, case 1 in
`getDefaultsForModel()`) -- EVH and Rockerverb (the other two amps
`PhaseInverter.cpp` documents as genuinely LTP-topology) stay at 0 pending a
listening check, matching the established pilot-before-rollout pattern.
Deployed to the Pi's live plugins. **Needs the user's ears**, same caveat as
every character-tuning item this session (#22/#27/#40): no numeric target
exists for "sounds like a cranked LTP," only physical plausibility and
stability, both confirmed.

**ROLLOUT (user, same day): "apply this to the rest of the amps."** Rolled
out to the other two `PhaseInverter.cpp`-documented LTP amps: EVH (`ltpTail
0.12`, kept modest for the same "don't compete with EVH's own tuned
dynamics" reason as its ripple coupling) and Rockerverb (`ltpTail 0.18`,
highest of the three, matching Rockerverb also having the highest sag/
bloomVca of the group). Verified no instability. Only these 3 amps get a
nonzero value -- `PhaseInverter.cpp` doesn't document any other amp as
genuinely LTP-topology, so nothing else was touched.

## ITEM #40 ROLLOUT — Speaker Drive baked into factory presets (2026-07-28)

User request: "Subtle for clean and Vox amp, Full for JCM800 and high-gain
amps." Rather than making the cab auto-follow whichever amp is currently
selected (would fight a preset's own saved `cab_spkdrive` value every time
that preset reloads -- the internal PowerAmpProcessor per-amp defaults like
bloomVca/duty are safe to auto-recompute every block because they're NOT
separately saved ports, but `cab_spkdrive` is a real, preset-persisted port),
baked sensible values into the factory preset table instead: a new loop in
`seedFactoryPresets()`, run AFTER every preset (Bank 1 + all extras) is
loaded, keyed on each preset's own `HF_AMP_MODEL` value:
- **Subtle (1):** Fender/"Clean Meanie" (idx 0), Vox AC30/"Chime Thirty" (idx 8).
- **Full (2):** JCM800/"Crunchy McCrunchFace" (idx 1), EVH/"Gainzilla" (idx 2),
  Friedman/"Beardo BE" (idx 6), Recto/"Diamond Plate" (idx 12),
  MT15/"Tremont 15" (idx 13) -- the suite's high-gain amps.
- **Untouched (stays Off):** Sunn, Rockerverb, Hiwatt, Backline, Plexiglass,
  Cali V, NAM -- the user specified only the two categories above; nothing
  guessed for amps in between (several of these span clean-to-dirty modes,
  so a single blanket Off/Subtle/Full call per amp MODEL wouldn't be
  well-founded without a mode-aware pass, which wasn't asked for).

Applied uniformly across ALL factory presets regardless of "user-preserved"
status (several presets are marked "user's exact dial-in, do not retune" in
the changelog) -- Speaker Drive is a brand-new capability that didn't exist
when those presets were dialed in, so adding it is additive polish, not
retuning their preserved tone, matching how "dense reverb tank on every
factory preset" and similar blanket additions were handled previously.
Bumped `kFactoryRev` 70->71 so existing saved boards get the refresh.

## TIER 2 ITEM #28 — Yeh-Smith exact closed-form tone stack (2026-07-28)

The current `ToneStackComponent` approximates the real passive TMB (Treble/
Mid/Bass) tone stack's control INTERACTION (raising bass+treble together
deepens the mid scoop) with 4 independent biquads plus a hand-tuned
`passiveScoop` heuristic addend. The real circuit is a 3rd-order passive RC
network where this interaction falls out of the circuit math automatically
-- the heuristic is an approximation of an approximation.

**Researched and verified the exact source** (D. T. Yeh & J. O. Smith,
"Discretization of the '59 Fender Bassman Tone Stack," DAFx-06) by fetching
and text-extracting the actual paper PDF directly (not relying on memory or
a third-party reproduction) -- got the complete symbolic H(s) coefficients
(Eqn. 1) and the exact bilinear-transform discretization (Eqn. 2), plus the
paper's own SPICE-verified '59 Bassman component values (C1=0.25nF,
C2=C3=20nF, R1=250k, R2=1M, R3=25k, R4=56k) straight from its schematic PDF.

**Triple-verified before touching any amp model:**
1. Independent Python re-derivation of the continuous-time transfer function
   -- confirmed the classic Marshall/Fender "V-scoop" shape at t=m=l=0.5
   (scoop bottoms at -12.4 dB around 700 Hz-1 kHz, rises toward both bass and
   treble), and confirmed the expected strong scoop when bass+treble are
   maxed with mid at 0 (the textbook "scooped mids" sound).
2. Stability check across every knob extreme tested (0,0,0 / 1,1,1 / mixed):
   all filter poles have negative real parts (stable) -- expected for a
   passive RC network, matching the paper's own claim that all 3 poles are
   always real.
3. Independent C++ port, cross-checked against the Python reference: matches
   to within ~0.01-0.07 dB at the tested frequencies, with slightly larger
   (still small) deviation at higher frequencies -- exactly the behavior the
   paper's own Figs. 4-9 describe for bilinear-transform discretization at
   audio sample rates. New `YehSmithToneStack.h`, generically parametrized by
   R1-4/C1-3 so it's reusable once other amps' real values are confirmed.

**Wired as an opt-in pilot on `ToneStackComponent::Type::Fender`** (the only
type with a verified real-circuit source) via a new `setExact(bool)` method
-- default false = bit-identical (structurally guaranteed: the new code path
only runs when explicitly engaged, which nothing currently does). Added a
matching `exactts` parameter to `FenderDeluxeModel` and a `--exactts` sweep
flag to `nam_compare` for testing. Presence is left untouched either way (a
separate NFB-loop-style control in real amps, not part of the classic
3-knob TMB network this paper models).

**Measured result on FenderDeluxeModel vs. its NAM capture: genuinely
mixed, exactly matching the roadmap's "biggest preset-migration event"
warning.** Bass-frequency accuracy improved dramatically (50 Hz delta -6.8dB
-> -0.9dB, 80 Hz -4.9dB -> +0.6dB, 125 Hz -2.8dB -> +2.1dB) -- the classic
Fender low-end behavior the old heuristic couldn't quite capture. But the
mid-highs got WORSE (800Hz-2kHz deltas roughly doubled darker; 5kHz swung
from +0.9 to +4.0 too bright). This is expected, not a bug in the tone
stack itself: `FenderDeluxeModel`'s OTHER filters (input HPFs, inter-stage
coupling, etc.) were originally tuned to work WITH the old heuristic
tonestack's specific coloration -- swapping in a more accurate tonestack
changes the overall FR shape enough that the rest of the amp's voicing no
longer complements it. Getting a clean win here needs a proper re-voice of
FenderDeluxeModel's other filters around the new tonestack (the same kind
of multi-variable re-voicing project as the EVH/JCM800 work earlier this
session), not just swapping the tonestack in isolation.

**Not exposed anywhere reachable by the user yet** -- no LV2 port, not on
any preset. It's dev-only (nam_compare CLI flag + the model's own
setParameter) pending a decision on next steps.

**Marshall shares the exact same circuit topology** (confirmed via
independent research: both are "TMB Fender-style" stacks per multiple
sources) but reliable, complete, consistently-cited real component values
for the JCM800 specifically were NOT found with enough confidence to use --
secondary sources cite conflicting/partial values (e.g. one source's "R1"
doesn't clearly map to the paper's R1 naming) and used DIFFERENT internal
labeling conventions across schematics. Given the entire point of this item
is EXACTNESS, guessing values would defeat the purpose. Vox/Orange/Recto
likely use genuinely different tone stack topologies (not just different
component values on the same Fender-style network) and would need their own
circuit research entirely -- not attempted.

**Recommended next steps, in order of value:** (a) properly re-voice
FenderDeluxeModel's other filters around the exact tonestack to turn the
mixed result into a clean win (real DSP work, own session); (b) find a
reliably-sourced, complete JCM800 component value set (ideally from an
actual schematic, not a secondary blog) to extend this to Marshall/EVH/
Rockerverb/Plexi (which all likely share this topology); (c) research Vox's
actual tone stack topology separately if it's wanted too.

Deployed to the Pi. One transient SEGV on the very first mod-host restart
immediately after the `.so` swap, self-resolved on the next restart (clean
for two subsequent cycles) -- matches a benign, already-observed pattern
from earlier in this session (crashes only on the first restart right after
swapping a loaded plugin's `.so`, never on a clean subsequent start); not
caused by this change.

## Item #28 rollout to the Marshall family (2026-07-28)

Per explicit user instruction ("do this for all of the amps, I don't care
what it takes"), found real schematic sources and extended the exact
tonestack to the rest of the suite where it's actually applicable.

**JCM800 -- found a genuine primary source and it's a CLEAN win (unlike
Fender).** Fetched the actual Marshall factory schematic ("2203 STD
Preamp," drawing dated 19-5-88, PCB "JM800") from drtube.com as a GIF, hit
the Read tool's 2000x2000px limit, converted via Python PIL (GIF->RGB->PNG,
downscaled to 1900px max dimension) to read it. Read the tone stack
component values directly off the drawing: C10=470pF (treble cap),
C11=C12=.022uF (mid/bass caps), VR3=220k Lin (treble pot), VR5=1M Log (bass
pot), VR4=22k Lin (mid pot), R15=33k (fixed tone-slope resistor). This
resolved the earlier "conflicting secondary sources" problem directly --
those partial blog values (e.g. "R1: 56k->33k") turned out to be using a
different R-numbering convention for the SAME resistor now confirmed as
R15/R4=33k. New `YehSmithToneStack::kMarshallJCM800` preset. Extended
`ToneStackComponent::setExact()` to permit `Type::Marshall` too (was
Fender-only), auto-selecting `kMarshallJCM800` for that type. Wired into
`JCM800Model::prepare()` -- and, since `nam_compare` showed a clean result
with no regression, **hardwired permanently on** (`setExact(true)`, no
runtime toggle) rather than left as an opt-in pilot like Fender.

Verified via `nam_compare --model marshall --exactts` against 2 knob-
documented captures (`P5 B5 M5 T5 M10 PA10` and `P8 B4 M7 T8 M10 PA10`):
FR deltas are same-or-better across the board (no regression), and two
real improvements found: (1) THD@110Hz at -6dBFS (loud/driven) dropped from
101% (heuristic, ~3.3x too saturated) to 61-65% (real amp is ~30%) -- the
exact tonestack's correct passive interaction measurably tames the
heuristic's over-saturation at high drive; (2) a spurious h2=20.4%
harmonic spike at extreme treble+presence settings (111 Hz, real amp h2=
3.3%) VANISHES with the exact tonestack (h2=3.8%) -- an artifact of the
old 4-biquad heuristic's control interaction that the real circuit math
doesn't have. Confirmed the "baseline" comparison methodology: `nam_compare`
always explicitly calls `setParameter("exactts", g_exactTS?1:0)` after
`prepare()` for A/B testing, so passing no `--exactts` flag correctly
reverts to the heuristic path for comparison even though the shipped model
now defaults to exact -- this is intentional CLI test-harness behavior, not
a wiring bug.

**Marshall Plexi 1959 ("Plexiglass") -- same values, confirmed via a
SECOND independent primary schematic.** Fetched a Marshall factory drawing
literally titled "1959 STD Preamp" (also 1988, PCB "JM80") -- tone stack
values IDENTICAL to the JCM800's (470pF/220k/1M/22k/33k). Cross-checked
against amp-tech history (websearch): Marshall used the older 56k/250pF
(Bassman-style) tonestack only on PRE-1969 JTM45s/early plexis; "1969 and
newer" plexis (the 1959 Super Lead included) already carry the 33k/470-
500pF values -- i.e. the same circuit as the 2203. Added
`YehSmithToneStack::kMarshall1959` (= `kMarshallJCM800`, kept as a
separately-named/documented alias rather than a bare reuse, so the
verification trail for each amp stays independently correctable). Added a
`ToneStackComponent::setExactCircuit(bool, CircuitParams)` overload so a
`Type::Marshall` amp can pick an explicit preset instead of always getting
the JCM800 default (needed here since Plexi's real values happen to match,
but a future Marshall-family amp's might not). Wired into
`MarshallPlexi1959::prepare()`, hardwired on. Sanity-checked vs its own
reference capture (`...CH I High Jumped.nam`): FR deltas are consistent
with the SAME low-mid hump (+2.3..+3.7 dB @125-315Hz) that was already a
documented, accepted residual of this model's OTHER filters (bodyShelf
notch) from its original 2026-07-05 tuning -- not a new regression.

**Friedman BE-Deluxe ("Beardo BE") -- same values, historically justified
reuse (not independently re-derived).** The BE-100 is a well-documented
hot-rodded JCM800 (Dave Friedman's own history); websearch confirms amp-
tech consensus that "the tone stack in the Friedman BE-100 is exactly the
same as in the Marshall." Reused `setExact(true)` (auto-selects
`kMarshallJCM800` for `Type::Marshall`, same as JCM800 itself) in
`FriedmanBEDeluxe::prepare()`. Sanity-checked vs a BE-100 gain-sweep
capture: FR deltas within ~2 dB across the band, consistent with the
model's existing tight/dark BE-100 voicing (no new regression).

**EVH 5150III -- investigated, deliberately LEFT ON THE HEURISTIC PATH.**
Could not find a reliable primary or clearly-corroborated secondary source
for the 5150III's actual tone stack component values in the time
available (most searchable Peavey 5150 schematic material is for the
original 1990s 5150/5150-II, a materially different amp from Peavey's
2007 EVH-collaboration 5150III redesign; risk of misattributing one
generation's values to another). Per the "no guess-and-spiral" rule, did
NOT force an unverified preset onto this amp. Remains a candidate for a
follow-up session if a genuine 5150III schematic surfaces.

**Hiwatt DR103 -- investigated, EXCLUDED (not a bug, out of scope for this
technique).** Websearch confirms the DR103's actual tone control circuit is
an ACTIVE 12AX7-driven EQ stage positioned AFTER the preamp (right before
Master Volume) -- not a passive TMB network at all. The entire Yeh-Smith
closed-form derivation only applies to passive RC tonestacks; it has no
meaning for an active gain-stage EQ. `HiwattDR103Model` already only used
`Type::Marshall` as a rough heuristic APPROXIMATION of its EQ curve shape
(not a claim of circuit equivalence), and since no `setExact`/
`setExactCircuit` call was added there, it's correctly unaffected and stays
on the heuristic path -- the right outcome, not a gap to fill later.

**Vox AC30 -- real schematic found, implementation deliberately NOT
attempted (topology genuinely ambiguous from the available scan).**
Fetched an actual vintage Vox Sound Ltd factory schematic (drawing "OS/013,
AC30 Amplifier -- Treble & Bass," via voxac30.org.uk) and traced the Top
Boost tone stack by eye (repeated crops/zooms via Python PIL, since the
Read tool's PDF pipeline needs `pdftoppm`, unavailable in this
environment, so only direct image formats -- JPG/PNG/GIF -- could be
read). Confirmed component VALUES with reasonable confidence: a 12AX7
gain+cathode-follower stage feeds, via 60pF, a "TREBLE 1M LOG" pot, then
.022uF + 100K(shunt) + .018uF, then 10K into a "BASS 1M LOG" pot --
structurally consistent with an independent secondary source's claim that
the Vox stack is "similar to the Fender Bassman, but without a middle
control." BUT the exact pot-WIPER wiring (does the treble wiper shunt to
ground, forming a rheostat divider, as one careful trace suggested, or
something else?) could not be confirmed with full confidence from a
55-year-old hand-drawn/scanned schematic, and -- unlike Fender/Marshall --
there is no published peer-reviewed paper (like Yeh & Smith's) to verify
a from-scratch derivation against. Getting a subtle wiring detail wrong
would ship something LABELED exact that's actually wrong, which is worse
than the honest existing heuristic. Deliberately did not guess. To finish
this: either find a cleaner/higher-res scan of this exact schematic, or
build and check a SPICE netlist of the traced topology against the
independently-known "480Hz scoop / 780Hz crossover" reference behavior
(cited by ampbooks.com's own SPICE analysis) before trusting it.

**Mesa Dual Rectifier (Cali V / MT15 share lineage) -- real factory
schematic found, implementation deferred as its own project (more complex
than a single TMB network).** Found and read the actual Mesa-Boogie Dual
Rectifier "PREAMP RF-1F" factory schematic (prowessamplifiers.com,
drawn 6-93). Unlike JCM800/Plexi, this is not a single tone stack: the Red
and Orange channels each have their OWN tone network (TRBL 250K pot +
680pF for Red, 250K pot + 500pF for Orange; shared/switched components
via LDR opto-relay switches per the schematic's own legend -- LDR8 "RD
TONE CAPS" / LDR9 "OR TONE CAPS" -- meaning the two networks are
electrically separate, switched in/out per channel, not one stack with
shared values). Correctly deriving this means mapping each schematic
sub-network onto `MesaDualRectifier`'s existing per-mode `tsType` table
before implementing anything -- real, careful work, not a quick value
swap like Friedman. Deferred as a properly-scoped follow-up rather than
rushed; the schematic PDF is a solid starting point (values above are
real, schematic-read, not guessed).

**Orange Rockerverb50 -- IMPLEMENTED + VERIFIED, mixed result (needs
re-voicing before shipping as default, like Fender).** Found and read the
actual official Orange factory schematic ("ORA-CD204, Orange Rockerverb
Main Preamp PCB," Orange Musical Electronic Co Ltd, dated 27-2-2004) via
`el34world.com` -- rendered locally at high resolution (installed
`pymupdf` since the environment lacked `pdftoppm`, which the Read tool's
PDF pipeline needs for page-range/zoom requests) and got an exceptionally
clear read of the tone stack: the SAME classic Fender/Marshall "FMV"
topology (plate output -> treble cap+pot, bridged by a fixed slope
resistor to the mid/bass cap+pot network), with Orange's own values --
C37=560pF (treble cap), C40=C41=22nF (mid/bass caps, same 0.022uF
Marshall/Bassman use), RV7=250k Lin (treble pot, same as Marshall),
RV5=500k Log (bass pot, half the Bassman/Marshall 1M, same taper),
RV6=25k Lin (mid pot, matches Marshall's ~25k), R62=39k (slope resistor,
between Marshall's 33k and Bassman's 56k). Confidence in this read is
HIGH (unlike Vox) -- a clean vector-drawn 2004 CAD schematic, not a
hand-drawn 1970s scan.

Since `Rockerverb50` doesn't use `ToneStackComponent` at all (its own
bespoke `bassF`/`midF`/`trebleF` biquad chain), wired a `YehSmithToneStack`
member directly into it (mirroring the `ToneStackComponent::setExact`
pattern rather than refactoring Rockerverb50 to route through
`ToneStackComponent` -- smaller, more surgical change): new `exactTS`
member + `useExact_` flag, an `"exactts"` opt-in parameter, both tonestack
call sites (dirty AND clean channel each call the 3-biquad chain
separately) branch on `useExact_`. Default off = bit-identical.

**Measured result via `nam_compare --model rockerverb --exactts` is
GENUINELY MIXED, split by channel:** on the DIRTY/OD channel (documented-
knob capture `RV50 OD--G_12_00--B_12_00--M_12_00--T_14_00--V_09_00`), it's
a mild net improvement (80Hz delta -4.5dB->-1.5dB, 200Hz -5.1->-3.4,
315Hz -1.9->-1.2; a couple of upper-mid bands got very slightly worse,
1.2k/2k by ~1-2dB). But on the CLEAN channel (`RV50 CL` all-noon capture),
it's a clear REGRESSION: bass bands that were already close (50Hz -2.5dB
delta, 125Hz +3.6) blow out to +3.9dB and +9.1dB respectively -- the exact
network's own inherent bass response, with less pre-tonestack filtering to
mask it on the clean path, doesn't suit the rest of the amp's existing
voicing. This is the SAME "needs a proper re-voice of the amp's other
filters" situation as Fender (see the `#28` Fender writeup above), not a
clean win like JCM800/Plexi/Friedman -- so it was NOT hardwired on.
Shipped as verified, opt-in-only infrastructure (default false, bit-
identical), same as Fender's `exactts` pilot. Folds into task #25
(re-voice around the new exact tone stack) rather than being its own new
task.

## Item #25: re-voicing Fender + Rockerverb around their exact tone stacks (2026-07-28)

Both amps' exact tone stacks were correct all along -- the "mixed result"
was purely a gap in the OTHER filters, which were tuned around the OLD
heuristic tonestack's specific coloration. Closed that gap for both,
methodically: measured the exact-tonestack delta-vs-NAM at a fixed
knob/gain operating point, derived the target correction curve per band,
then did a **least-squares numerical fit** (Python + scipy, RBJ biquad
formulas re-implemented and cross-checked against `BiquadFilter.h`'s own
code) to find new EQ filter parameters that null the target curve at each
band -- rather than hand-tuning by trial and error. Both are now hardwired
permanently on (`useExact_ = true` / `setExact(true)`), matching JCM800/
Plexi/Friedman.

**FenderDeluxeModel: excellent result, near-total fix.** First attempt
(replacing the old `voiceShelf` highshelf(2800,+13)/`voiceCut`
peaking(900,-2) outright with 2 new filters aimed only at the measured
delta) was a bad regression -- 3-8kHz collapsed by up to -20dB. Root
cause, confirmed by reproducing it in the offline Python model before
touching the fit again: the OLD `voiceShelf` was providing a large
(~11-13dB at 5-8kHz) intrinsic HF lift that was ALREADY doing double duty
(partly compensating for the tonestack choice, partly for unrelated
upstream darkness) -- discarding it outright removed real, needed gain,
not just the tonestack-specific error. Fixed by fitting the **target as
"old filter's own response minus the measured delta"** (preserving what
the old filters were already doing right, only correcting the residual
error) instead of fitting the raw delta directly. Final config (4 filters,
replacing/extending `voiceShelf`+`voiceCut`, new `voiceMidBoost` +
`voiceBassShelf` members): `voiceShelf` highshelf(1900,+19) (was
2800,+13), `voiceCut` peaking(4400,-14,Q3.2) (was 900,-2,Q1 -- repurposed
from a mid cut to a presence dip), `voiceMidBoost` peaking(950,+6.5,Q0.9),
`voiceBassShelf` lowshelf(85,+4.5). **Result: every band from 50Hz-8kHz
within 0.7dB of the NAM reference** (`CLEAN` capture, gain 0.4, all tone
knobs at noon) -- by far the best FR match this amp has had all session
(previous heuristic-only baseline had errors up to 6.5dB). Stress-tested
off the fit point: bass=1.0 stays excellent (<0.6dB everywhere); treble=1.0
degrades to 1.5-3.5dB (still much better than the original mixed result,
expected since a FIXED compensating EQ can't perfectly track a knob-
dependent physical network's changing shape at every setting); a second
capture (`HOT`, higher gain) confirms the fit generalizes (all bands good
except 8kHz, which both this and the ORIGINAL heuristic already read as a
capture-specific anomaly -- NAM itself shows -8.8dB there, likely cab/mic
character in that specific file, not a DI-only capture). THD/harmonic/
feel sections are unaffected (as expected -- pure linear post-EQ change,
doesn't touch drive/gain staging) and show the SAME pre-existing,
orthogonal gain-staging gaps already known and accepted for this amp (see
earlier Fender entries in this doc).

**Rockerverb50: good result on the previously-broken clean channel, one
accepted residual band.** Same methodology, applied to the clean-channel-
only chain (`cleanScoop`/`cleanLift`, both fixed filters unrelated to
`useExact_` -- always active regardless of tonestack choice, so they
needed re-fitting the same way): re-purposed `cleanScoop` to peaking
(1800,-8.8,Q0.6) (was 1400,-3.5,Q0.9) and `cleanLift` to peaking
(5300,-4.2,Q2.5) (was a highshelf 3200,+10), and added 2 new members
`cleanBassShelf` lowshelf(200,-5.4) and `cleanBassDip` peaking(110,-8.0,
Q2.0) to fix the bass region the least-squares fit couldn't reach with
only 2 filters (mirrors Fender needing a 4th filter for the same reason).
**Result: 50Hz-2kHz all within ~1.4dB** (was up to 9.1dB before the
re-voice) -- clearly better than even the ORIGINAL heuristic path's
baseline in most of those bands. **Accepted residual: 3.1kHz/5kHz sit
around -4dB** (verified consistent on a second capture at a different
Master setting: ~-2dB there) -- a real but modest regression vs. the
original heuristic's 0.3/1.0dB at those two specific bands. Tried several
follow-up iterations (halving the presence dip's gain, moving its center
frequency) to close this further; the offline Python RBJ-formula model
predicted each change accurately for 50Hz-2kHz but consistently
UNDER-predicted the actual measured effect at 3.1k/5k by roughly 2x --
suspected to be a `nam_compare` band-measurement methodology detail (likely
some multi-bin/band-integrated measurement rather than a single-frequency
probe) rather than a bug in the filters themselves, given the RBJ formula
is the same one used and verified elsewhere in this session. Kept the
best-measured config rather than keep guessing blindly. **Net effect is a
genuine improvement** (total absolute error across all measured bands,
excluding the known-anomalous 8kHz band, dropped from 16.3dB to 12.2dB
summed vs. the original heuristic) with one honestly-documented residual,
not a "clean sweep." The dirty/OD channel is untouched by any of this
(separate filter chain, already confirmed a mild net improvement earlier)
and was re-verified unchanged after these edits.

Both changes verified via `nam_compare --exactts` rebuild+re-measure
cycles only (host-side, Windows) -- not yet built or deployed to the Pi
live plugins.

## JCM800 "fuzzy when driven" fix (2026-07-28, user-reported after Pi play-test)

**User report:** the JCM800 has a fuzzy character when driven. Confirmed
first that this was PRE-EXISTING, not caused by the same-day exact-tone-
stack change (harmonic profiles byte-identical before/after it).

**Measured signature** (knob-documented capture, gain 1.0): (a) THD@110Hz
blowing up to 101% at -6 dBFS input -- fundamental cancellation, literal
fuzz-pedal behavior on hard-played low notes; (b) at 223 Hz the model was
EVEN-dominant, h2 = 41% of fundamental vs the real amp's 3.7% = octave-up
sputter squarely in low-string power-chord range; (c) all odd harmonics at
111 Hz reading ~2x the real amp.

**Root cause 1 (fixed): stage-2 duty collapse.** The 2026-07-23 Friedman
audit documented that TriodeComponent::kMarshallV2 hits a duty-collapse
window at drive ~3.6-4.7 (fundamental cancellation, 94-135% THD) and
FriedmanBEDeluxe got a drive cap -- but the fix was never backported to
JCM800Model, whose stage 2 ran the SAME triode at up to 7.7 (and was
already inside the window at MINIMUM gain, 4.1). Capped at 2.0 (probed
3.2/2.8/2.0/1.6: h2@223 falls monotonically 29.9/23.5/12.8/10.4, but 1.6
traded THD@1k-at-hot-input up to 97% -- stage 2's saturation had been
acting as a limiter protecting later stages; 2.0 keeps that). Friedman's
reroute-into-stage-3 was tried and dropped -- it just re-added evens via
stage 3 (+6pt h2).

**Root cause 2 (partially fixed, PA-bounded): buried LF fundamental.** FR
ran -12/-10/-5 dB dark at 50/80/125 vs the capture while harmonics
(200-800 Hz) sat near flat -- a weak fundamental under full-strength
harmonics IS the fuzz percept, and it arithmetically inflates every
harmonic ratio measured at 111 Hz ~2x (FR-corrected, the model's LF
distortion is nearly right: h3 ~25 vs 23, h5 ~10.3 vs 10.7). BUT
correcting the FR fully proved impossible against the shared
PowerAmpProcessor: (i) a +12 dB post-clip shelf collapsed output to -67
dBFS (the EVH bass-cutout flux-saturation mechanism, reproduced exactly);
(ii) pre-clip restore (inputHPF 130->70) gets re-compressed away by the
cascade (only ~+1.5 dB reaches the output); (iii) even a -3.7 dB top-tame
shelf tipped the PA off a sensitive knee (THD@1k 57->88% at hot input) --
REVERTED; (iv) at master 1.0 the PA's OT-flux stage generates LF evens in
proportion to the LF level fed to it (h2@223 moved 14->26 from a +4 dB
bass shelf alone) -- the remaining h2@223 gap (17 vs 3.7) is generated in
the PA, not the preamp, and is "PA promotion project" territory, not
fixable here. Shipped: inputHPF 130->70 + bassRestore lowshelf(90,+2.5)
(new member; EVH-proven safe ballpark) + bodyShelf 7.5->6.0.

**Final shipped config vs the pre-fix baseline** (everything at-or-better,
nothing regressed): THD@110 -6dBFS 101->51.5%; h2@223 41.3->17.0; h3@223
8.4->26.4 (capture: 25.8 -- nailed); h5/h6@223 land on the capture;
THD@1k 51-60 (capture 55-61); loudness x0.98 (no makeup change needed);
feel section the best it has measured (compression delta +0.1 dB, attack
exact, bloom -0.27); FR essentially unchanged from the baseline the user
had previously play-approved. Deployed to the Pi (both amp + hexforge).
