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

## PA project, phase 1: OT-flux shear + the collapse mechanism root-caused (2026-07-28/29)

**Shipped: `fluxShear_` (default 0.12) in PowerAmpProcessor's flux-domain OT
saturator** -- a linear term blended into the tanh (`sat = shear*flux +
(1-shear)*tanh(...)`) modeling the B-H curve's residual deep-saturation
slope (air-core inductance + winding resistance). Real iron never goes
truly flat; without the term, a pinned tanh makes consecutive saturated
samples cancel in the integrate->saturate->differentiate construction's
differentiate step (d(flux)/dt = 0 = silence). Small-signal response is
mathematically unchanged (slope exactly 1). Verified transparent at normal
levels via nam_compare A/B on the knob-documented JCM800 capture (loudness
identical, THD within 0.3pt, all harmonics within 0.4pt) and on Fender at
line-hot input (identical). Tunable via setParameter("fluxshear") + a new
`--pafluxshear` nam_compare flag.

**New tool: `tools/pa_flux_probe.cpp`** (build-tools target) -- feeds the
isolated PowerAmpProcessor (a) a single LF sine at an amplitude sweep and
(b) a two-tone test (big 50 Hz "pinner" + 500 Hz "music" at -12 dBFS,
Goertzel-measured survival), per shear value.

**The honest, surprising finding: the -67 dBFS in-chain collapse is NOT
the flux stage.** The isolated PA never collapses on sines (20/30/50 Hz up
to +18 dBFS in -- every column rises monotonically to a plateau, even at
shear 0): zero-crossings unpin the flux integrator too often. The two-tone
test then showed the REAL crush mechanism: the 500 Hz tone loses 32 dB as
the 50 Hz pinner rises -- IDENTICALLY at every shear value -- meaning the
cross-modulation lives in the TUBE WAVESHAPER, not the flux stage: any
memoryless saturator pinned by a huge LF component has ~zero local slope
for whatever rides on it. That is qualitatively REAL push-pull behavior
(a real power stage overdriven by massive LF also chokes the music);
the earlier "+12 dB post-clip LF shelf -> -67 dBFS" was GIGO at a level
no real amp's power stage would be fed either.

**Reframing for the roadmap:** the "PA LF wall" constraining the JCM800/
EVH low-end restores is partly PHYSICAL, not purely a model defect. The
real amps' healthy low end exists because their preamps pass more LF
fundamental to begin with (and their cascades tolerate it); our models
carve LF out pre-clip to protect over-hot cascades, then can't reinstate
it post-clip. The productive lever for the dark-lows gap is therefore
UPSTREAM -- making each cascade tolerate LF like a real 12AX7 chain (the
JCM800 stage-2 duty-collapse cap was exactly this kind of fix) -- not
force-feeding the PA. The flux shear stays as a correct-physics
micro-improvement (protects the edge case of very-long VLF pins) at zero
measured cost. Deployed to the Pi (amp + hexforge).

## Item #26: Vox AC30 exact Top Boost tone stack -- SHIPPED (2026-07-29)

The deferred Vox tone stack, finished via the SPICE-verification path the
earlier deferral prescribed, plus a structural fix the amp had needed since
the 2026-07-26 sweep.

**Topology resolved via two independent primary schematics.** The blocker
had been pot-wiring ambiguity in the vintage OS/013 scan. Fetched the much
cleaner JMI OS/010 Top Boost module detail (voxac30.org.uk, hand-annotated
with component designators: C2=50pF -- the earlier "60pF" was a misread --
VR7=1M log treble, C3=C4=22nF -- the ".018" was also a scan artifact --
R3=100K, R4=10K, VR8=1M log bass) AND its documented direct ancestor, the
Gibson GA-77 Vanguard circuit ("simply a lift" per the Vox historians).
The GA-77's unambiguous vertical layout resolved the wiring question: the
input feeds the low network DIRECTLY through R3 (100K) into the midpoint
of the two 22n caps; R4 (10K) goes to ground; output = treble wiper; bass
pot is a rheostat bridging across the series cap pair. Genuinely different
topology from TMB -- no mid control, no Bassman-style slope-into-C2.

**Verified before implementation, all offline:** (1) numeric MNA solve of
the traced topology reproduces ampbooks' independently published SPICE
signatures -- both-dimed scoop at 459 Hz (ampbooks: 480), trace crossover
at 747 Hz (ampbooks: 780), within vintage tolerance; the sweep also
established the bass-rheostat direction (bass up = more resistance). (2)
closed-form H(s) solved symbolically (sympy MNA), machine-generated CSE
C code (no hand transcription), cross-checked vs the numeric solve to
0.00002 dB over a 5x5 knob grid. (3) new `VoxToneStack.h` (3rd-order
bilinear/DF2T, the verified YehSmith pattern; Rs=614/RL=544K from
ampbooks baked in) verified by a new `tools/vox_ts_check.cpp` harness:
discrete response within 0.35 dB of the analog reference at every grid
point. That harness also later served as the decisive stale-binary
detector.

**Wiring:** `ToneStackComponent::setExact()` now covers Type::Vox with the
new engine. The Mid knob SURVIVES as a clean post-EQ peaking (1 kHz,
+/-10 dB, exactly flat at noon) -- the real Top Boost has no mid control,
but 9 factory presets set mid off-noon (audited the factory tables:
Innerspeaker Swirl 0.70, Regal Solo 0.62, etc.), so making the knob inert
would have silently changed them. Hardwired permanently on in
VoxAC30Model.

**The structural fix: voicing EQ moved POST-limiter.** The initial re-fit
(Fender item #25 methodology) mysteriously would not express: 12 dB of
internal level change produced 0.0 dB of measured output change -- the
model's terminal softLimit was railing as a hard AGC at performance
master settings, crushing all pre-limiter EQ back toward flat (this is
the 2026-07-26 finding "Vox resists the pattern / no post-limiter EQ
slot" made quantitative). Diagnosis chain: sag ruled out (--sag 0
identical), stale-binary ruled out (vox_ts_check failed by exactly the
-12 dB, proving the constant was live), master-knob test proved the
limiter (FR opened up at master 0.15). Fix = the EVH bodyRestore
precedent: brightShelf/bodyShelf/chimePk moved AFTER softLimit where they
are linear. Re-fit in two iterations (the shared PA downstream still
absorbs ~22% of post-model EQ -- measured, matching the documented EVH
"PA absorbs post-preamp EQ" effect; iteration 2 scaled by the observed
0.78 expression ratio). Final: brightShelf highshelf(1460,+9.5),
bodyShelf lowshelf(297,+15.3), chimePk peaking(10270,+14.5,Q1.2).

**Results vs the knob-documented capture** (B5 T5 Cut5 M8, the only
documented-knob Vox capture on disk): total |FR error| across all 12
bands 33.3 dB (heuristic baseline) -> 12.5 dB, every band within 2.2 dB
(was up to -6.5); loudness unchanged (-16.3 vs -16.1 dBFS = no makeup
change needed); knob-extreme sweep stable (output within 0.4 dB across
bass/treble extremes -- passive networks self-balance -- and no
EVH-style sag/attack pathology). THD character (LF slightly hot, 1 kHz
too clean) is the PRE-EXISTING drive-staging gap documented in the
2026-07-26 sweep, untouched by and out of scope for the tonestack work
-- but note the post-limiter EQ slot now EXISTS, which was the missing
prerequisite for the previously-reverted Vox LF-THD fix (input 2-pole
@160 Hz + post-limiter restore) if that gets revisited.

Deployed to the Pi (amp + hexforge). Committed locally; push deferred to
after 17:00 per the weekday rule.

## Item #30: Mesa Recto + Cali V exact tone stacks (2026-07-29)

**Mesa Dual Rectifier -- exact TMB on all six dirty modes, hardwired.**
Zoomed the factory schematic (RF-1F, 6x render) and verified node-by-node
that each channel carries a COMPLETE standard TMB network (series treble
cap -> 250K treble pot with wiper output, 47K slope from the input into
twin .02uF caps, 1M bass rheostat, 25K mid to ground -- the Marshall/
Bassman layout exactly, so the verified Yeh-Smith formulas apply as-is).
The channels differ only in treble cap: ORANGE (vintage) C5=500pF, RED
(modern) C4=680pF, selected by LDR opto-switches. New
`YehSmithToneStack::kRectoOrange`/`kRectoRed`; channel-faithful per-mode
wiring in MesaDualRectifier (CH2 modes 2-4 = Orange, CH3 modes 5-7 =
Red); clean modes 0-1 (Fender-type heuristic approximation of the CH1
stack, which is NOT on this 2-channel schematic) stay heuristic -- no
guessing. `setExactCircuit` gating extended to Type::Recto, and
`ToneStackComponent::prepare()` now clears `useExact_` (a mode switch
from a Recto mode to a clean mode could otherwise leave a stale exact
path with the wrong circuit loaded). A/B vs the Solo Head captures
(CH3 Modern = the model's own tuning reference): NOON RESPONSE ESSENTIALLY
IDENTICAL to the heuristic (within 0.3 dB per band -- the heuristic was
already capture-tuned at noon), loudness identical (-18.0 dBFS both), CH2
Vintage slightly better (total |err| 11.9 -> 10.5). The exact stack's real
win is correct-by-construction knob interaction OFF-noon, where no
captures exist to tune a heuristic against. Hardwired live default ON;
`exactts` A/B toggle (nam_compare convention: flag absent = heuristic
baseline).

**Mesa Mark IIC+ stack for the Cali V Ch3 lead modes -- a genuine clean
win, hardwired.** Found and read the circulating hand-drawn Mark IIC+
factory schematic (schematicheaven.net): the Mark-series stack is the
Fender Blackface network sitting right after V1A and BEFORE the lead
cascade (the `tsPre` position MesaMarkV already models!) with Mesa's
values: C1=750pF treble cap (3x Blackface; lead mode parallels a second
750p via LDR1, not modeled), C2=.1uF, C3=.047uF, treble 250K, bass 250K,
mid 10K, slope 100K. New `kMarkIIC` preset, wired to modes 6/7/8 (IIC+/
Mark IV/Extreme -- the modes explicitly modeling this circuit); Ch1/Ch2
modes stay heuristic (no schematic for the Mark V's own clean/crunch
stacks). Measured vs the knob-documented IIC+ HG-bal capture (the amp's
own tuning reference): **total |FR err| 30.4 -> 18.8 dB** -- the
125/200 Hz low-mid over-brightness fixed (+4.8 -> +2.0, +3.2 -> -0.7)
and the famous IIC+ presence region much closer (2k -4.6 -> -2.8, 3.1k
-5.9 -> -3.9, 5k -3.1 -> -1.2, 8k -6.0 -> -4.0); only 315 Hz slightly
worse (-0.1 -> -2.0). Loudness identical (-18.0 dBFS both) = no preset
re-level needed. Hardwired live default ON with the same `exactts` A/B
toggle.

**PRS MT15 -- excluded, documented.** PRS does not publish schematics for
its modern amps (2018 design); its Marshall/Fender-type modes stay on the
heuristic path per the no-guess rule. If a service schematic ever
surfaces, the wiring pattern above applies directly.

Deployed to the Pi (amp + hexforge). This closes the tone-stack project:
every amp in the suite now runs a schematic-verified exact tone stack
except Hiwatt (active EQ -- technique doesn't apply), Sunn (own bespoke
stack), Backline (solid-state), NAM (runtime capture), and MT15/Mark-V
non-lead modes (no published schematics).

## Roadmap stragglers #41 + #34 landed; #45 scoped out (2026-07-29)

**#41 (studio inter-mic time offset) -- landed with honest scope.** micDist
now adds real mic time-of-flight (0-0.9 ms = 0-30 cm) in CabinetBlock on
both the studio and plain mic paths; micDist 0 = bit-identical. The
roadmap's OTHER half (a fixed 0.2-0.6 ms delay on the studio ribbon blend)
was implemented, measured, and REJECTED: 0.4 ms against the 35% coherent
blend digs ~10 dB comb notches with the first null at ~1.25 kHz, straight
through the ear-approved studio voicing -- cab_voice_check failed all four
spectral signatures, and the math says any single audible delay at that
blend ratio notches somewhere in the passband. The coherent blend IS the
studio voice's design; ribbon stays time-aligned (decision documented in
rebuildEQ, ring infrastructure retained for future use). cab_voice_check
passes 0 failures with the final config.

FOLLOW-UP AUDIT (same night): the shipped micDist delay DOES comb against
the undelayed ribbon when a user dials micDist>0 while in Studio voice --
the same physics that killed the fixed ribbon delay. Verified this shifts
NO factory preset (parsed the factory tables: ZERO presets use Studio
voice at all, so no baked micdist value meets the blend). Kept BY DESIGN
with that distinction made explicit: a fixed delay re-carves the curated
default voicing for everyone; the micDist comb is off-at-zero,
progressive, user-dialed, and reversible -- it is the real sound of
pulling one mic of a pair back, i.e. the multi-mic realism the roadmap
item actually wanted. cab_voice_check guards the micDist=0 baseline.

**#34 (tremolo shape) -- the missing Hex Forge port added, blob v29.** The
DSP (bias lagged-sine / opto hard-chop LDR / harmonic brownface LP-HP
phase-split) and the standalone modfx port both landed 7/26; Hex Forge
could not reach it. Added `md_shape` via the full 11-step port-append
checklist (memory: hexforge-port-append-checklist) -- generator + three
regenerated outputs, split boundary static_asserts (v28/v29) in BOTH
copies, kSwWatch entry, explicit gap-fill in BOTH migratePorts copies,
blob version bumped at both sites, DSP wiring next to the centerDelay
push. hexforge_migrate_test extended (new v28->v29 case + all hardcoded
tail-shift arithmetic updated across the four existing cases): PASSED
(0 failures). Live-verified on the Pi (lv2info shows md_shape, all
services clean).

**#45 (fuzz source impedance) -- deliberately NOT attempted, scoped for
its own session.** It needs: (a) a NEW port (guitar-volume / source-
impedance control -- another full checklist append), (b) a change INSIDE
ToneBenderMkII's Newton-Raphson germanium solve with bit-identical-at-
Rs=0 verification against its capture-tuned voicing, (c) the per-pedal
input-Z table (TS ~500k / Muff ~40k / RAT ~1M / TB+FF 5-10k) with
preset-compat decisions around the existing it_load knob. Multi-hour,
capture-reverification work -- not a straggler.

## Roadmap #45, first half SHIPPED: Tone Bender guitar-volume cleanup (2026-07-29)

**The physics.** A guitar's volume pot presents a Thevenin source resistance
Rs = th*(1-th)*Rpot to whatever it feeds (zero at full volume, max at
mid-travel). Against a germanium fuzz's tiny nonlinear input impedance this
is what creates the famous "roll the guitar back and it cleans up" —
attenuation, brightening, and LINEARIZATION together. A digital rig's hi-Z
interface erases the interaction entirely; the pedal model has to recreate
it, and it needs to be TOLD the pot position (a port) because the real knob
is invisible upstream.

**The elegant bit:** in ToneBenderMkII's Ebers-Moll Newton-Raphson solve, a
base-series Rs enters EXACTLY as extra emitter degeneration scaled by
1/beta — a one-term change to Q1's re argument (re + th(1-th)*kRsNorm),
plus the th divider on the input. th = 1.0 -> both terms vanish =
bit-identical to the capture-tuned voicing (verified: the gvol=1.0 sweep
reproduces the documented HG capture-match numbers exactly, h2 15.6/h3 26.0).

**Calibration (nam_compare --gvol sweeps vs the Page HG captures):** first
try kRsNorm=1.2 over-drove the near-cutoff Q1 into rectifier-like
asymmetry (h2 42% at gvol 0.3). At 0.5: gvol 0.75 = smoother edge (h7/h9
fizz drops 8->2.3/6.6->3.6), gvol 0.5 = the classic "clean with hair"
(h2 25/h3 22, high orders collapsed), gvol 0.3 = characterful germanium
sputter (h2-dominant — the famous dying-fuzz roll-off extreme). Level
holds while rolling down (-1 dB at half, -2.8 dB at 0.3) = the real
"cleans up without losing volume" behavior.

**Ports:** standalone fuzz "gvol" (index 11, default 1.0, microVersion 32,
modgui knob in the TB-conditional group, rdflib-verified) + Hex Forge
`fz_gvol` via the 11-step checklist (blob v29->v30, gap-fill default 1.0 —
the checklist's first NON-ZERO gap default, worth noting; kSwWatch skipped
correctly since it's a continuous knob). hexforge_migrate_test extended
with a v29->v30 case + arithmetic updates: PASSED (0 failures). Live on
the Pi, both TTLs verified.

**Second half (per-pedal input-Z table on PickupLoadSim) still deferred:**
scaling the Input Trim's load sweep by the first pedal's real input Z
(TS ~500k / Muff ~40k / RAT ~1M / TB+FF 5-10k) would shift every factory
preset that carries a nonzero it_load value (the 2026-07-23 fidelity
polish seeded many) — needs a preset-compat decision + re-measure, its own
session.

## EVH 5150 III improvement pass (2026-07-29 overnight, user: "weakest after the amp overhauls")

**ROOT CAUSE FOUND: the model's hot output was over-saturating the shared
PowerAmpProcessor so deeply that the PA crushed ~80% of any post-limiter
EQ and flattened the dynamics** -- measured directly: an EQ step adding
+4.5 dB at 2 kHz expressed only +0.9 dB at the output (~20% expression vs
the ~75-78% measured on Vox/Fender). The 5150's own power amp in the
capture is far more linear at the capture's operating point; ours was
being fed a rail-to-rail square at full drive. This single mechanism
explains the "weakest/boxed/compressed" percept better than any FR
number alone.

**Shipped: EVH AmpDefaults paDrive 0.30 / paMakeup 1.25** (the per-amp
loudness-neutral pair; EVH has its own kCanonical row, so no other amp is
touched). Every metric moved toward the head-only Red capture
SIMULTANEOUSLY: 2 kHz FR -7.7 -> -4.4, 3.1k -6.6 -> -3.4, 5k -5.7 -> -2.0,
8k -5.8 -> -2.9, THD@1k 58 -> 72% (real: 93 -- the Red is nearly square),
THD@110 30 -> 24% (real: 16), bloom -2.37 -> -1.71 dB. Loudness at exact
baseline parity (-11.7 vs -11.9 dBFS at noon = no preset re-level).
Sag/NFB/ripple character all remain active (only the waveshaper drive
dropped). PA drive (padrive x2/x4) and duty were measured first and ruled
out as levers: drive is railed-inert upward, duty adds h7/h9 fizz.

**Also shipped: presence EQ step 1** -- presencePk peaking(3000,+6,Q0.7)
-> (2300,+7.5,Q0.6), topShelf highshelf(6000,+3) -> (3500,+6). The
least-squares fit wants ~(2310,+12,Q0.56)+(3500,+10) after absorption;
per the swoosh lesson ("step gains up gradually, wide Q") only ~60% is
shipped tonight. If tomorrow's ears confirm no swoosh, the remainder can
follow. BLUE channel with the combined changes now lands ON the capture
through the mids (800 Hz-5 kHz all within 0.7 dB).

**Guards verified:** bass-knob extremes show NO sag pathology (bloom
-1.44..-1.71 across bass 0/0.5/1.0, level stable +/-0.1 dB -- the lower
PA drive actually reduces the sag-detector trigger risk from the old
bass-cutout saga); all EQ Q <= 0.7; loudness parity exact.

**Still open (documented, not chased tonight):** the remaining ~40% of
the presence fit (pending swoosh ear-check); THD@1k 72 vs the real 93
(the Red's >90% THD implies a partially-suppressed fundamental --
duty/bias territory, structural); LF FR darkness (-12 dB @ 50 Hz, the
same buried-fundamental family as JCM800 but bounded by the same PA LF
physics); the lost ~300 ms Red attack swell (PRE-EXISTING -- verified
identical at baseline; a documented casualty of the bass-cutout range
compression, not tonight's change).

## Moondust Glam overhauled to the documented Ronson rig (2026-07-29 overnight)

User: "the moondust glam preset needs an overhaul." Research-confirmed Mick
Ronson's Ziggy-era rig (GuitarPlayer/Gibson/Equipboard agree): stripped '68
Les Paul Custom -> Sola Sound TONE BENDER + CRY BABY wah (famously parked
as a fixed filter -- the Moonage Daydream honk) -> 200 W Marshall MAJOR
"The Pig" (KT88) -> angled Marshall 4x12.

The old preset already had the right BONES -- I Know It (the germanium
Tone Bender engine: literally Ronson's pedal) into a parked wah (type 1 @
freq .72) into @greenback -- but the amp was a HIWATT at gain .81: a
clean hi-fi platform that left the fuzz doing all the work = thin and
glam-less. The Ronson sound is fuzz+wah INTO A CRANKED MARSHALL.

Changes: amp Hiwatt -> Plexiglass (Marshall 1959, the Major's direct
lineage cousin; KT88-vs-EL34 is secondary to the cranked-Marshall
voicing), JUMPERED per the real 1959 (Vol I .62 / Vol II .5), bass .35 /
mid .60 / treble .70 / presence .60 / master .80 (power-amp crunch);
Tone Bender hotter (sustain .59 -> .68, Ronson ran it hot) with volume
tamed .38 -> .32 per the fuzz->cranked-amp staging rule; wah + Greenbacks
+ small plate kept. out_level re-measured ON DEVICE with a freshly
rebuilt hexforge_meas (the stale-binary gotcha bit again -- an old
binary gave -16.28, the correct rebuild -13.99): -22.4 -> -18.5 for the
-12.5 dirty-parity target. kFactoryRev 73 -> 74. Deployed.

## EVH bass-range/LF restoration + Vox LF-THD attempt (2026-07-29 morning)

**EVH (SHIPPED): bodyRestore back to 9 dB + bass-knob range 0.15 -> 0.40.**
Last night's paDrive 0.30 changed the safety calculus enough to re-sweep
the old bass-cutout constraints: bodyRestore at 9 dB (cut to 3 during the
cutout saga) now measures completely safe at noon (bloom -1.61, level
parity) and buys +4 dB at 50 Hz (-12.3 -> -8.2). The knob range re-swept
at extremes: 0.40 healthy at max bass (+1.6 dB bloom = musical sag
flavor, -1 dB level, +7.8 dB real 50 Hz authority), 0.55 starts the
nonlinear growth (+2.8 dB bloom), 1.0 still fully explodes (+19.7 dB --
the PA sag detector tracks the RAW input, so paDrive does NOT shield it).
~2.7x the knob authority of the original fix.

**The ~300 ms Red attack swell did NOT return** from LF energy alone
(bodyRestore 9 at noon: attack still reads 0 ms) -- it predates last
night and died somewhere in the July re-voices (HPF raises + the old
bodyRestore cut). Restoring it needs June-era model archaeology (git
diff of the LF path vs the 2026-06-14 state that measured 311 ms) --
scoped as its own item, not chased blind.

**Vox LF-THD fix: attempted, measured, REVERTED -- the premise was
half-wrong.** The 2-pole @160 input HPF DID nail the LF THD shape again
(10.9/21/31.8/37.2 vs capture 7/17/24/26 -- rising with level, real
cleanup dynamics) and even cleaned the top end. But restoring the lows
afterward re-inflated the THD to exactly the original values: the
post-limiter slot only fixed the MODEL-side re-clip; the downstream
shared PA is a SECOND re-clipper that grinds whatever LF the restore
adds (restore sweep: none = great THD/dark FR; full = original THD/good
FR; every point between = a strict tradeoff). The original Vox config
was already at the achievable optimum for this architecture. REAL
prerequisite identified: give Vox its own canonical PA row (currently
SHARED row 0 with Fender/Hiwatt/Backline) and reduce its paDrive like
EVH's -- which then cascades into re-fitting all of last night's Vox
post-limiter EQ. A proper future session, cleanly scoped.

## 2026-07-29 -- EVH attack swell FOUND AND RESTORED (flux OT was the killer)

The archaeology paid off in one mechanism. Bisect established the swell
was alive as late as 6f46783 (July 25) and that the killer was in shared
DSP, not EVH5150Model.cpp (June model files + current shared code still
measured dead). Direct default-toggling at HEAD then isolated it in two
probes:

- xoverDepth_ 0.12 -> 0: attack still -298 ms (innocent)
- fluxOT_ true -> false: **attack +4 ms, bloom +0.01 dB** -- the real
  Red's ~300 ms swell back, essentially exact vs the capture

Mechanism: the Phase-2 flux-domain OT saturation integrates the signal
(25 Hz leaky integrator) and saturates on FLUX, which makes it
LF-selective by design. The 5150's swell IS slow LF envelope dynamics --
the flux stage grinds exactly that band flat. Bonus: flux-off also fixed
the two other documented EVH gaps in one shot:

- THD@110 now 15.8/19.1 vs capture 15.3/16.3 (was 23-25 "LF 2x
  over-distorted" -- that verdict's root cause was flux, not the model)
- LF response +4 dB closer (50 Hz -8.5 vs -12.3 before)
- Mids/THD@1k/level essentially unchanged; Blue channel also improved
  (attack +0 ms, LF THD down)

Fix shipped as **per-amp AmpDefaults.fluxOT** (default true -- every
other amp bit-identical, so none of the last 48 h of Vox/Fender/JCM800
fits move). EVH row = false. Wired via the existing "fluxOT" param in
nam_compare + amp plugin + hexforge. JCM800 control run confirmed
unchanged.

Lesson for the Vox future session: fluxOT is now a per-amp lever --
worth probing flux-off for Vox alongside the paDrive cut, since its
LF-THD tradeoff had the same "second re-clipper" signature.

## PA EVENS PHASE 2 (2026-07-29) -- h2 MECHANISM FOUND; h4/h6 still open

Desk-loop session on the dimed Rockerverb capture (targets 111 Hz: h2 17
h4 13.2 h6 11.5 / 223 Hz: h2 19.4 h4 20.3 h6 20.8). Chronology matters
-- half of it was spent on a WRONG PATH:

**The nam_compare exactts trap (now fixed).** nam_compare defaulted
"exactts" OFF while the shipped plugins run the exact tone stacks
(Rockerverb/Recto/MarkV honor the param; other amps hardwired). Every
un-flagged Rockerverb run measured the heuristic stack -- on that path
the LTP mechanism showed a perfect-looking 111 Hz result plus a total
223 Hz collapse (h2 0.6, ANTIPHASE cancellation) that sent the session
into a frequency-fragility rabbit hole. On the SHIPPED path the
cancellation does not exist. nam_compare now defaults exactts ON
(--noexactts to opt out). Rule: any Rockerverb/Recto/MarkV measurement
made without --exactts before 2026-07-29 tested a non-shipped sound.

**Findings on the shipped path:**
- tools/pa_node_dump (new): the preamp NODE signal already carries evens
  (h2 10.1 @111 / 5.4 @223, rail dwell 68%/40%) -- stage asymmetries
  work. The PA was destroying them: the railed waveshaper re-squares
  (two-level = evens erased), confirmed by padrive/pamakeup sweeps.
- evens_harness2 (new): band-limiting does NOT kill the dual-corner on
  synthetic squares -- the phase-1 "band-limited input" postmortem was
  wrong. Static flux bias (M3) inert (integrator swing >> bias).
- Static curve asymmetry (screenComp/biasShift scaling, new pascreen/
  pabias params) inert at every drive: the shaper is either railed
  (theorem) or the asymmetry washes through downstream re-normalisers.
- NFB exonerated (0.0 identical). Sag VCA + bloom VCA exonerated (new
  --pasag/--pabloom overrides). Flux shear inert for evens.
- **WINNER: LTP tail-bias envelope ripple at reduced PA drive.**
  --padrive 0.4 --pamakeup 1.2 --paltptail 3.0 on the exact path:
  h2 20.9 @111 (target 17) / 17.7 @223 (target 19.4) -- ON TARGET at
  both frequencies (vs baseline 1.3/3.0). The 2 ms/8 ms envelope
  discharges within the fundamental's half-cycle, giving a phase-robust
  intra-cycle bias wobble = real grid-blocking/duty-shift physics.
  FR at this config: mids/HF within ~1.7 dB (bright), LF +2.6..+4.7
  bright (needs a modest trim), level -15.4 (makeup ~1.5x for parity).
- Still open: h4/h6 land at only ~40%/20% of target (6.5/2.4 vs
  13.2/11.5 @111) -- the LTP bias is first-order asymmetry (h2-rich);
  higher-order evens need a mechanism with curvature, and every current
  lever is exhausted (higher drive re-squares and kills h2 itself:
  padrive 1.0 + tail 3.0 -> h2 7.1). THD@110 32.5 vs capture 42.7,
  THD@1k ~35 vs 60 (the known structural gap) both remain.

**Deliverables (all default-neutral, EVH regression-checked +4 ms):**
sweepable ltpatt/ltprel/pascreen/pabias params in PowerAmpProcessor;
nam_compare --pasag/--pabloom/--pafluxot/--paltpatt/--paltprel/
--pascreen/--pabias flags; tools/pa_node_dump + tools/evens_harness2.

**NEXT (enablement, own session):** bake padrive 0.4/pamakeup ~1.7/
ltpTail 3.0 into the Rockerverb AmpDefaults row, re-fit the LF trim +
loudness, re-level its presets, play-test. Then evaluate the same
recipe for the other evens-poor amps.

## ROCKERVERB EVENS ENABLEMENT (2026-07-29) -- DEPLOYED, awaiting ears

The phase-2 winner is baked into the Rockerverb AmpDefaults row:
paDrive 1.0 -> 0.4, paMakeup 1.0 -> 1.75, ltpTail 0.18 -> 3.0.

Final measurements at defaults (dimed DI capture, shipped exact-TS path):
- h2 19.9 @111 / 17.5 @223 vs targets 17 / 19.4 (baseline was 1.3/3.0)
- dirty loudness parity: -12.5 vs -12.2 pre-change; attack +0.0 ms
- clean channel: kCleanOutGain 4.5 -> 10.4 (the paDrive cut runs the
  PA's linear region ~6.5 dB quieter; dirty rides paMakeup at the rail,
  clean doesn't) -- parity -17.5 vs -17.2
- gain 0.6 sanity: level/attack consistent

Attempted + REJECTED: an 80 Hz -3.5 dB model-side trim for the +4.7 dB
LF hump. Findings: (a) the hump is PRE-EXISTING on the shipped path
(identical under old + new PA rows -- NOT a regression from this
change), (b) the trim's biquad phase rotation collapsed the 223 Hz
evens 17.5 -> ~7 while only expressing -1.4 dB of FR through the PA.
Doctrine call (pedal-amp-already-tuned-findings): single-capture LF
deltas may be capture-side; the evens are the point of this task.

FLAG FOR EARS: bloom now +3.5 dB vs the capture's (was +0.49) -- the
un-railed shaper passes supply-wobble the old rail used to clamp.
Counter-levers measured and all made it WORSE (sag down -> +4.0, bloom
VCA up -> +5.3), so it ships as-is as a feel flavor: MORE blooming,
compressed-then-swelling sustain on the dirty channel. If the user
dislikes it, the mechanism hunt continues; if they like it, it's free
EL34 character. Also still open: h4/h6 under target, THD@110 32.7 vs
42.7, THD@1k structural gap.

## VOX LF-THD ROUND 2 (2026-07-29) -- FIXED AND DEPLOYED

Round 1 (yesterday) was reverted because restoring lows re-inflated the
LF THD -- the shared PA was a second re-clipper. With the per-amp levers
from the EVH/Rockerverb work, round 2 landed in one session:

**Vox split onto its own PA row (case 9; kCanonical[8]=9 in hexforge +
amp plugin + nam_compare vox spec):** same voicing fields as the shared
clean row 0, but paDrive 0.3 / fluxOT OFF / paMakeup 1.11 (measured
loudness parity -15.9 vs -16.1 old) / sag 0.74 -> 0.50. Physically
right too: a real AC30 has no NFB and a cathode-biased class-A EL84
output.

Measured vs the labeled AC30 capture (B5 T5 Cut5 M8, gain .6):
- THD@110: 20.1 / 29.7 straddling the capture's 24.2 / 26.1
  (was 45.4 / 51.0 -- the original "2x over-distorted" complaint DEAD)
- LF: most of the missing lows returned FROM THE ROW CHANGE ALONE
  (80 Hz -4.6 -> -2.0 before any EQ touch -- the flux grind had been
  eating the model's existing bodyShelf). Remainder closed with a NEW
  wide lowBody peak (245 Hz, +2.1, Q .55) -- NOT a bodyShelf bump,
  which overshot 50 Hz by +4.7 dB (the un-compressed PA now expresses
  EQ at ~2-3x, and 50 Hz was already on target).
- Final FR: 50-200 Hz within 1.3 dB, mids/top within 0.8, two known
  residuals left per the capture-chain doctrine: 315 Hz -1.9 (squeezed
  against the 500 Hz normalisation anchor) and 8 kHz +2.1 (PRE-EXISTING
  brightness, unchanged by this work).
- Feel: bloom initially grew +2.0 -> +5.5 (same un-railed-shaper effect
  as Rockerverb) but unlike Rockerverb the SAG LEVER WORKS on this amp:
  row sag 0.50 restores the pre-change bloom (+1.85 vs +2.03), THD
  unchanged. Attack -4 ms unchanged.
- Fender control run (still row 0): attack +0.0, sane -- untouched.

Listen for: Chime Thirty presets (Bank 9 fun bank, Regal Sustain/Solo,
Cydonia Sunrise, Come As Water) -- fuller lows, cleaner low-string
notes at the same chime.

### Chime Thirty idle whistle (same day, user-reported) -- FIXED + re-fit

User: "high pitched noise when not playing" right after the row-9
deploy. tools/vox_idle_probe (new) diagnosed it in two runs:

- NOT self-oscillation: burst-then-silence decays to -300 dBFS. (Though
  the probe DID show the NFB loop can scream at 5.6 kHz at paDrive
  1.0 + flux-off -- margin note for future flux-off amps.)
- The -50 dBFS input floor's output spectrum was CLUSTERED at
  10.2-10.4 kHz: the chimePk (+14.5 dB, Q1.2 @ 10.27k) was
  concentrating the noise floor into a whistle band. The old rail/flux
  compression had been masking it.

Root cause ran deeper: the chime peak's 10 kHz energy was acting as a
hidden PA-SAG EXCITER -- the PA sag env tracks its raw input, so the
boosted HF held vSupply down and compressed the ENTIRE morning fit.
Cutting the chime un-compressed everything (THD@1k 34 -> 10, lows
+4 dB), so the fit was redone honestly:

- chimePk 14.5/Q1.2 -> 9.0/Q1.0 (whistle gone: floor peak -47 -> -55,
  broadband; silence -314 dBFS)
- bodyShelf 15.3 -> 13.0, lowBody re-centred 245+2.1 -> 315+1.5/Q0.8
- row 9: paDrive 0.45 / paMakeup 1.07 (THD@110 21.3/31.4 astride the
  capture's 24.2/26.1; parity -16.1 exact; bloom +1.21; attack -4 ms)
- Final FR: 80/125 within 0.2 dB, mids/top within 1.3; accepted
  residuals 50 Hz +2.1 (likely capture-side rolloff) + 315 -1.7
  (normalisation-anchor squeeze).

LESSON (goes with the "EQ expresses 2-3x" one): a big pre-PA HF peak
isn't just tone -- it FEEDS THE SAG DETECTOR. Any fit made with one in
place is calibrated around its compression; remove/resize it and the
whole amp must be re-measured.

### Chime Thirty whine round 2 (2026-07-29 evening) -- keyed gate SHIPPED, awaiting ears

User: whine persisted after the chimePk cut. Findings from live-device
recording + offline analysis (long session, two harness traps burned --
documented for the next archaeologist):

WHAT THE WHINE IS: the rig's 60 Hz hum floor's odd upper partials
(540-1380 Hz + 3.2-3.5 kHz lines, 120 Hz comb spacing), distortion-
regenerated and EQ-emphasized by the amp chain. It rides in whenever the
NOISE GATE passes the floor.

ROOT CAUSE (structural): in Hex Forge the gate sits AFTER the Input
Trim's pickup voicing + boost (up to ~+20 dB of pre-gate gain), but the
gate thresholds were floor-complianced against the RAW rig floor. The
boosted floor grazes the open threshold and the 8 dB hysteresis LATCHES
the gate open at idle (close = thresh-4 sits BELOW the boosted floor,
so it never re-closes).

FIX SHIPPED: NoiseGateBlock::processKeyed -- the DETECTOR now keys on
the RAW pre-InputTrim input (like a real gate pedal's key input) while
the gain still gates the in-chain audio. Thresholds now mean what the
compliance pass measured, independent of in-chain gain staging. Direct
NoiseGateBlock tests on the real recorded floor: closes at every
threshold raw, closes at -52 with +6 boost; latches open at -60+boost
(exactly the Come As Water/Apache Echo settings -- explains preset-to-
preset variability).

HARNESS TRAPS (do not re-burn): (1) the LV2 test stub's ps_goto recall
gets OVERWRITTEN by the plugin's deferred initial Bank1/A recall --
half the session's "Regal Sustain" measurements were actually the stock
CLEAN preset (which also matched the live recording: the device lands
on Clean after every mod-host restart, so the "live whine" capture was
probably Clean too, NOT the preset the user complained about).
(2) The stub emits a spurious loud 5749 Hz tone under certain run
patterns (amp-bypass toggles / long warmups) that does NOT exist under
real mod-host -- it burned hours as a phantom suspect. Next session:
test at the mod-host level (param_set via socket), not the LV2 stub.

STILL OPEN if ears say the whine persists: per-preset gate thresholds
for the -60-threshold clean presets (Come As Water -60, Apache -60,
Homesick Saucer -58, Sweet Dispersion -56 all latch-prone even
raw-keyed if the floor is at the documented -45 peak), and/or an idle
darkener for the bright clean amps.

### Chime Thirty whine round 3 (same evening) -- THE VOX HAD NO DNR

User: whine persists, "none of the other amps do this" -- the decisive
clue. Live measurement first: while the user heard the whine on Regal
Sustain, the plugin's DIGITAL output recorded as bit-perfect silence
(-300 dBFS) at idle -- so pure idle is NOT the path. The audible window
is the OPEN-GATE window (playing + note decays + hold/release), where
the amp legitimately amplifies whatever the floor feeds it.

Why only the Vox: VoxAC30Model NEVER GOT the 2026-07-14 DnrRolloff
decay-darkener extension (EVH/JCM800/Rockerverb/Friedman all did), and
today's PA row split removed the compression that used to squash its
amplified-floor decay content. At Regal Sustain's gain 0.9 through
brightShelf +9.5 + chimePk +9, the rig's hum partials (fresh floor
measured: lines at 2.1-4.1 kHz) ride every note tail as a bright whine.

FIX: DnrRolloff wired into VoxAC30Model (JCM800 pattern -- track() on
raw input, process() at terminal output), engaged at gain > 0.45 so the
chimey clean presets (gain .2-.35) are untouched. DnrRolloff::prepare
grew a cornerHz parameter (default 6000 = unchanged for existing amps):
the stock 6 kHz LP was designed for amplified HISS, but the Vox's
exposed noise is 2-4 kHz HUM PARTIALS -- Vox uses 1200 Hz. Verified
transparent on all capture metrics (FR/THD/feel/level bit-consistent:
the darkener only acts below -44 dBFS raw input).

Also shipped this round (all still valid): raw-keyed gate detector
(latent latch bug), chimePk 14.5->9 re-fit. The pedalboard-snapshot
lesson: mod-ui restores saved port values (gt_thresh -45 found in
Hex_Forge.pedalboard) over presets on load -- remember it when a
preset "ignores" its stored values after reboot.

### Chime Thirty whine -- ROOT CAUSE FOUND AND KILLED (round 4, final)

User's decisive report: "extremely high pitched -- all I have to do is
land on a preset with the dirty 30 and it goes off even without
anything plugged in." That is a SELF-OSCILLATION, and with the repro
harness recall finally fixed (PS_BANK_UP pulses; ps_goto in the LV2
stub was being overwritten by the deferred Bank1/A initial recall),
pure silence + landing on Regal Sustain reproduced it exactly:
**5749 Hz pure tone at -21.2 dBFS**.

It was the PA NFB LOOP: with flux OT off (this morning's row split) and
paDrive 0.45 (this evening's chime re-fit -- the oscillation margin was
probed at 0.3 but never re-probed after the raise), the one-sample-
delay NFB loop at nfb 0.82 oscillates in the full plugin context. The
0.82 was inherited from the shared Fender row when Vox split off. Fix:
**row 9 nfb 0.82 -> 0.05** -- also the physically correct topology (a
real AC30 has NO global negative feedback; it's a defining trait of the
amp). Verified: repro now -422 dBFS on silence; capture metrics barely
moved (parity -16.1 exact, THD 20.8/31.2 same straddle, bloom +0.88,
FR shifts <= 1 dB).

Post-mortem of the false trails (all four earlier "fixes" were real
latent bugs, but none were THE whine): chimePk noise concentration
(real, re-fit kept), raw-keyed gate (real latch bug, kept), Vox DNR
(real gap vs other amps, kept), gate thresholds -44 (kept). The
5749 Hz tone WAS seen hours earlier and dismissed as a harness
artifact because it "wasn't in the live recording" -- but that
recording was the stock Clean preset (broken recall), not a Vox
preset. LESSON: when a tone reproduces under silence with every block
bypassed, believe the tone, not the assumption about which preset is
active -- VERIFY the active preset's identity (eff amp_model) before
dismissing anything.

RULE FOR FLUX-OFF AMPS (now 2 incidents): after ANY paDrive change on
a flux-off row, re-run the silence-input oscillation probe IN THE FULL
PLUGIN CONTEXT (whine_repro pattern), not just the isolated PA probe --
the loop margin depends on the whole in-loop filter chain.

## JCM800 STAGE-2 CAP GAP CLOSED (2026-07-30 evens session)

The 2026-07-28 fuzzy-fix note documented "capped at 1.6... h2@223 41->10"
but the SHIPPED code capped at 2.0 -- leaving h2@223 at ~15-17% (real amp
3.7-4.6%). Cap ladder re-measured against BOTH knob-documented captures
(P5B5M5T5 + P8B4M7T8, gain 1.0, exact TS): at 1.4 the whole 223 Hz
profile overlays the capture (h3/h4/h5/h6/h7/h9 within ~1pt; h2 15->10,
residual 2.5x), -0.2..-0.4 dB level (makeup-absorbable), THD unchanged.
Coupling-corner sweep (in/i12/i23 70/150/140 down to 20/20/15) checked
first and REJECTED: lower corners worsen h2 at BOTH frequencies (111:
2->13.6) while fixing LF FR -- the corners sit at the family optimum and
the LF darkness (-13 dB @50) remains an accepted capture-chain residual.
Preset re-level: measured per JCM800 preset (12 of them) at both caps;
only shifts >0.3 dB re-leveled (see kFactoryRev 85).

TWO STALE-BINARY TRAPS in one session: the Pi's nam_compare measured
h2@223 = 41.8 (a pre-July-29 build); after rebuild it reads 17.3 =
matching local. ALWAYS rebuild the measuring binary before believing it.

## EVH 223 Hz EVENS: SAME FAMILY SIGNATURE (2026-07-30, indicative only)

JFE Red-channel capture (knob-UNLABELED -- deltas indicative, not
verdicts): model h2@223 18.0 vs NAM 4.1, h3 8.1 vs 17.8 (odd collapsed),
h7/h9 ~2x over. Same shape as the JCM800 defect. BUT the EVH stages run
at drives 7-9 (fully railed -- past the kMarshallV2-style duty-collapse
window, a different regime), and the roadmap already defers EVH to a
supervised session with knob-labeled captures. DO NOT blind-restructure
the flagship: acquire/label captures first, then run the same cap-ladder
methodology per stage.

## ITEM #24 FOLLOW-UP CLOSED (2026-07-30): per-amp nfb re-tune has no headroom

The 2026-07-28 NFB-wrap shipping note left one thread open: "retuning each
amp's nfb default now that the topology has genuinely changed." Measured
tonight on the reference JCM800 (dimed knob-matched capture, exact TS):
nfb swept 0.1 / 0.42 / 0.7 / 1.0 -- 8k FR moves 0.1 dB, THD@1k 0.2 pt,
223 Hz harmonics identical to the decimal. The HP'd feedback slice is
inherently tiny at every legal depth; there is nothing to tune. #24 is
DONE in full.

CONSEQUENCE FOR THE ROADMAP: the three remaining structural gaps --
THD@1k ~half of captures, the -16 vs -6 dB level-compression gap, and
h4/h6 evens at 40/20% of target -- now ALL point at the same single
project: the PA as a first-class distortion contributor (gain-staging
re-architecture so the power stage carries a real share of the total
distortion, + full preset re-level). Supervised, one big session, the
last structural amp item standing.
