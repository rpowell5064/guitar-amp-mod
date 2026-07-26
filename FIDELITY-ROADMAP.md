# Hex Chain Fidelity Roadmap — 2026-07-26

## PROGRESS LOG
- **2026-07-26 Batch A — DONE, offline-verified (MSVC harness in scratchpad):**
  - #17 Small Clone + Rotary Hermite tap reads (ACTIVE; pure quality, parity with
    the shipped CE-2/flanger pass). SmallClone min-delay clamp 1→2 samp for the +2 tap.
  - #12 Gate detector hum-reject: detector-only 60/120/180/240 Hz notch comb in
    NoiseGateBlock (ACTIVE, "humReject" param default on). Verified 86 dB hum
    rejection in the detector; real notes pass untouched. Added Filters::notch().
  - #13 Comp sidechain HPF: VCA + FET 1176 detector-only HP ("scHP" Hz, default 0=off,
    ADDITIVE). Verified −13 dB (VCA)/−8.6 dB (FET) less over-compression on a 40 Hz
    tone, 0.01 dB on mids (proves off = bit-identical).
  - #16 VCA program-dependent (dual-TC) release ("progRel", default off, ADDITIVE).
  - Execution model established (see below). NOT yet built into the LV2 plugins /
    presets — #13/#16 land as capabilities; enabling them is a Phase-2 preset pass.

- **2026-07-26 Batch B — DONE, offline-verified:**
  - #15 DC/subsonic hygiene (ACTIVE): user IRs DC-corrected at load (mean subtracted
    in IrResample::conditionIr) + ~5 Hz 1-pole master DC blocker in hexforge AutoOutput.
    Verified IR mean 0.021→0.00004.
  - #14 Limiter anti-aliasing: new shared lv2/common/AdaaSoftClip.h (first-order ADAA
    soft-knee clip) wired into AutoOutput as a default-OFF "soft" mode. Verified
    transparent below knee, bounded ≤ceiling, and ~10 dB less near-Nyquist alias energy
    vs the hard gain-snap. OFF until a Phase-2 hfmeas loudness re-measure enables it.

- **2026-07-26 Batch C — DONE, offline-verified (drive/fuzz topology, all additive default-off):**
  - #1 TS-808 clean-path topology ("cleanPath" bool). Verified 0.000% THD at drive=0
    (linear clean path) vs the original's 2.6%; +1.2 dB more 100 Hz at drive=1.
  - #2 RAT frequency-dependent gain legs: −14 dB low-shelf (fc 250 Hz) on the clipper
    drive ("gainLegs" bool). Verified −11.8 dB at 80 Hz, 0.0 dB at 1.5 kHz.
  - #3 Octavia germanium rectifier threshold: `max(|fz|−Vf,0)·norm` ("geThresh" bool).
    Verified the quiet-note octave collapses to 24% of the ideal rectifier's (Ge ON
    quiet/loud ratio 1.87 vs ideal 7.65) — level-dependent octave, sputter on decay.
  - #4 DS-1 dynamic bias shift via new shared include/DynamicBias.h ("biasShift" depth,
    default 0). Reworked to a fast−slow (transient) model so it's TRANSIENT bark that
    SETTLES to identity (no permanent asymmetry). Verified attack diff 0.008 → steady
    0.0002. Muff/Nail bias-shift deferred (pairs with reading EHXBigMuff + Nail clipStage
    refactor in the fuzz/amp work).
  - #5 DOD250 741 slew — DEFERRED: at our normalized signal scale 0.5 V/µs is masked
    by the hard shunt clip (pre-clip slew re-squares; clip output too small to slew).
    Doing it right needs an explicit large-swing op-amp node + capture validation, not
    a Phase-1 flag. Backed out cleanly (DOD250 unchanged). RAT's LM308 slew already works.

- **2026-07-26 Batch D — DONE, offline-verified (AMP KEYSTONE, all additive default-off):**
  TriodeComponent now takes a per-sample bias offset on the LUT input (the shared change
  that upgrades ALL twelve amps at once, ~free when off). Verified default == depths-0
  is BIT-IDENTICAL (0.0 diff).
  - #21 grid conduction / blocking: fast-charge (~0.3ms) slow-leak (Rg·Cc ~22ms) cap on
    grid-conduction overshoot past a bias-derived knee → colder bias after transients.
    "setBlockingDepth". Verified transient cold-bias 5.7 that recovers to 0.6.
  - #23 dynamic cathode bias: slow env (tau=Rk·Ck) of the drive shifts bias → compression/
    bloom. "setCathodeDepth". Verified time-dependent (early 1.00 → late 0.88 settled).
  - #22 sag hook: "setSagBias" external input for the power-amp sag envelope to drive
    (the driving side is the next power-amp sub-batch: #22 wiring, #24 NFB-wrap, #25
    class-AB crossover, #26 flux-domain OT). Depths are Phase-2 per-amp nam_compare tuning.
  Lesson banked: at hard-clipped drive the cathode bias shift moves the WAVEFORM (even
  harmonics/duty) more than RMS — measure harmonics, not just level, when tuning.

- **2026-07-26 Batch E — power-amp (PARTIAL), offline-verified (additive default-off):**
  - #25 class-AB crossover ("xover" depth): soft dead-zone `y -= xover·dz·tanh(y/dz)`
    in tubeWaveshaper (odd, f(0)=0, no DC). Reduced small-signal gain + zero-crossing
    kink = "dirty when quiet". Verified crossover effect 3× bigger at moderate vs loud
    level (0.125 vs 0.042). NOTE: first tried a Gaussian dip — it made sub-dead-zone
    signals LOUDER; the tanh dead-zone is the correct odd model.
  - #26 flux-domain OT saturation ("fluxOT" bool): step-5 OT now integrate→tanh(flux)→
    differentiate — a SELF-INVERTING leaky integrator/differentiator, so it's exactly
    unity+uncoloured until the core flux clips, and LF (which accumulates far more flux)
    grinds before HF. Verified 82 Hz THD +76% / 2 kHz unchanged; transparent below sat.
    fluxPole ~25 Hz, fluxDrive 0.015 (Phase-2 tunable).
  - DEFERRED to a dedicated power-amp session: #22 sag-into-ceiling (entangled with the
    already-tuned pre-sag + bloomVca) and #24 NFB-wraps-nonlinearity (structural reorder
    of steps 2-5 with loop-stability risk). The #22 triode hook (setSagBias) already
    exists from Batch D; driving it belongs with the NFB restructure.

- **2026-07-26 Batch F — DONE, offline-verified (Tier-3, self-contained, no preset re-voicing):**
  - #31 spring-reverb dispersion ("drip", default 0 = bit-identical): the old
    "dispersion" allpasses were Schroeder DIFFUSERS (no frequency-dependent group
    delay → could never chirp). Added a real in-loop cascade of 100 first-order
    allpasses H(z)=(a+z⁻¹)/(1+a·z⁻¹) per spring (a = drip·0.72, bypassed at 0) +
    a ~5.5 kHz transition LP → lows travel slower than highs → the descending
    "boing", re-chirping every recirculation. Verified: drip 0→0.85 pushes the
    LF-minus-HF arrival from 9.8 ms to 26.3 ms (descending chirp), loop stays
    stable/bounded, drip=0 still reverberates. Serves BOTH the user spring type
    (PlateReverbBlock, "drip" param wired) AND the Deluxe amp's tank. Full port/
    modgui exposure + a tasteful default = Phase-2. [Parker & Bilbao DAFx'09.]

- **2026-07-26 Batch G — DONE, offline-verified (Tier-3, self-contained):**
  - #35 wah authentic transfer ("voicing", default 0 = classic boost, bit-identical):
    the old wah was `x + mix·bp·2.2` (resonant boost on FULL dry → lows never thin).
    Authentic path passes the signal THROUGH the resonant band (built from the SVF:
    peak·bandpass + treble-bleed·highpass + small dry floor, peak gain falling toward
    the toe for the inductor's finite Q). Verified: toe-down a 150 Hz note thins −16.5 dB
    vs classic, the peak vowel is preserved, and low/peak scoop is 4× deeper (0.077 vs
    0.316). Pot-taper refinement deferred (would shift the classic fc map). [ElectroSmash GCB-95.]

- **2026-07-26 Batch H — DONE, offline-verified (Tier-3, self-contained):**
  - #32 Uni-Vibe truth pass ("authentic", default 0 = classic, bit-identical): morphs
    the 4 all-pass stage centres from our polite even spread {82,196,440,1020} toward
    the REAL lopsided Shin-Ei caps {~10,140,450,4500} (log-freq morph of C_[k]) AND
    swaps the linear photocell for the log-law CdS sweep R=Rmax·(Rmin/Rmax)^L (geometric
    R → exponential freq sweep that dwells dark, flicks bright = the throb). Verified:
    reshapes the static HF response 4.6 dB, dynamic output 98.5% different + stable +
    still throbs. Tonal "chew/throb" rests on the real component values (cited geofex);
    best A/B'd by ear in Phase-2 — a 4-stage LFO phaser's timbre doesn't reduce to one
    offline scalar. NOTE learned: allpass phaser notches are NOT at the stage centres
    (accumulated phase), and the 83:1 photocell R sweep makes every stage cover the
    whole band — so "HF reach" can't discriminate voicings; the static response can.
    LFO rate-skew deferred (the asymmetric lamp rise/fall already skews the sweep).

- **2026-07-26 Batch I — DONE, offline-verified (tape cluster, "tapeVoice" default 1.0=ON):**
  Record/playback chain now colours EVERY repeat incl. the first (was: first repeat raw,
  tape LP/sat feedback-only) + playback head bump (+3.5 dB ~105 Hz low shelf, slides with
  tapeAge) + random-walk wow (~0.5 Hz) / capstan periodic (rate ∝ 1/timeMs) / scrape
  flutter (~11 Hz) replacing the pure sines. tapeVoice blends old↔authentic (default ON so
  tape sounds like tape; set 0 to A/B). Verified first echo −2.2 dB HF + 3.0 dB LF head bump,
  stable under deep wow/flutter. (Overnight-deploy note: defaulted ON to be testable.)

## EXECUTION MODEL (two phases)
- **Phase 1 (code):** land every item's DSP. Strictly-non-regressive improvements go
  ACTIVE now (Hermite, gate hum-reject, DC hygiene, limiter anti-alias-on-clip…).
  Anything that alters a capture/preset-tuned voicing lands behind an ADDITIVE param
  defaulting to bit-identical (Plexi-vol2 pattern) — code progresses, zero regressions.
- **Phase 2 (voicing):** dedicated tuning passes that ENABLE + fit the additive params.
  Pedal/amp ones on the Pi with nam_compare refs; preset-level ones (comp, mod defaults)
  via the hexforge preset engine + A/B. This is where constants get set — no offline guessing.

---

Full-suite audit: what the real hardware does that our DSP doesn't, ranked by
audibility-per-CPU. Produced by five parallel research passes (code read first,
then DAFx/VA literature + open-source references). Target: Pi 5, 48 kHz,
64-frame JACK, ~46% worst-case bench → real headroom available.

House rules that apply to every item below:
- Additive-parameter pattern (Plexi vol2 style): new behavior behind a depth
  param defaulting to bit-identical, so no silent re-voicing of the 54 presets.
- Every DSP change goes through the offline harnesses (nam_compare, hfmeas,
  amp_alias, seraph/ambient verifiers) before deploy.
- Items that change pedal/amp output level must re-check fuzz→amp staging presets.

---

## TIER 1 — Near-free, high-audibility fixes (S effort, ~0 CPU each)

### Drives & fuzzes
1. **TS-808: stop clipping the clean path.** Real circuit: output = input +
   diode-limited feedback voltage — lows pass unclipped at unity even at max
   drive. Ours tanh-clips the whole sum (`TubeScreamer808.cpp:66-72`), pinning
   output at ±0.55. Fix: `wet = x + kClip*tanh((gain-1)*hp/kClip)`. One line +
   nam_compare re-tune. Bonus: drive-tracked pre-clip LP corner (real 51 pF
   feedback cap: ~60 kHz → 5.6 kHz as drive rises). [ElectroSmash TS analysis;
   Yeh & Smith DAFx-07]
2. **RAT: frequency-dependent gain legs.** Real Zg = 47Ω/2.2µF + 560Ω/4.7µF →
   corners ~60 Hz / 1.5 kHz; bass gets far less gain than mids (tight palm
   mutes). Ours applies flat gain (`ProcoRAT.cpp:56`). Fix: 2-corner shelving
   pre-emphasis into the existing NR solve. [ElectroSmash RAT; cushychicken LTSpice]
3. **Octavia: thresholded rectifier + transformer bandpass.** Ideal `fabs()`
   today → octave never collapses on decay. Real Ge diodes have ~0.2-0.3 V
   threshold → level-dependent octave (strong attack, sputter decay). Fix:
   `max(|x|-Vf,0)` in signal domain (NOT env-normalized) + 2nd-order BP.
4. **Dynamic bias shift for Muff/DS-1/Nail** ("attack bark"): 1-pole envelope of
   rectified post-clip signal (5-30 ms tau) subtracted from clip bias — the
   ToneBenderMkII `starveEnv` pattern, copied per stage. Static tones unchanged.
5. **DOD250: LM741 slew limiting** (0.5 V/µs) — copy the 4-line limiter from
   `ProcoRAT.cpp:83-90`. Amplitude-dependent treble = touch-sensitive fizz.
   (Skip TS's JRC4558 — 1.7 V/µs rarely limits.)

### Delays & reverbs
6. **Tape delay: first repeat is digitally clean.** Tape LP/tanh/EQ live only in
   the feedback path (`TapeDelay.cpp:83-105`); repeat #1 is full-bandwidth.
   Restructure so the record/playback chain is in the through path (ChowTape
   ordering: record→loss→playback). Same ops, different topology. Re-check
   tape preset brightness after.
7. **Seraph: same first-repeat bypass** (`SeraphDelay.cpp:91-104`) — apply
   tone+clip once to the read tap, reuse for wet and feedback (halves filter work).
8. **Tape wow/flutter: pure sines → real spectra.** Replace 0.8 Hz + 6 Hz sines
   with (i) RandomWalk wow ~0.5 Hz, (ii) periodic capstan term with rate ∝
   1/timeMs, (iii) gated 8-15 Hz scrape-flutter RandomWalk. `DelayBase::RandomWalk`
   already exists (Echorec uses it; TapeDelay doesn't). [ChowTape; Valhalla notes]
9. **Head bump**: +3-4 dB low shelf ~100 Hz in the tape & Echorec repeat path
   (real playback-head LF resonance; current 120 Hz feedback HP actively thins).
10. **Tape/Echorec self-oscillation**: raise feedback cap 0.98 → ~1.15 with the
    loop tanh made unconditional above 0.9 (currently skipped at saturation=0,
    `TapeDelay.cpp:92`). Bounded runaway + pitch-bend on time change = dub tricks.
    Verify bounded limit-cycle level offline.
11. **Echorec swirl**: rotation-locked sine (+2nd harmonic) in `currentSpeedMod_`
    at 1/rev (≈1/timeMs) + correlated ±0.5 dB AM on head outputs, scaled by a
    drum-wear macro. The Binson signature the RandomWalk can't make. [Effectrode; Boonar]

### Cab / EQ / IO
12. **Gate detector hum filter** (BIGGEST rig-specific win): `NoiseGateBlock`
    detects on raw abs(x) including the 60 Hz hum that is 89% of the measured
    −45 dBFS floor. Run the detector (only) through ~120 Hz HPF or a HumNotchComb
    copy → thresholds can drop ~8-10 dB → longer note tails, same rejection.
    NO lookahead (latency budget is sacred).
13. **Compressor sidechain HPF** (VCA + 1176): both envelope on raw input → low-E
    pumping. Fixed ~100 Hz detector-path HPF, audio path untouched.
14. **Limiter: ADAA soft-knee instead of instant gain snap.** `AutoOutput`
    (hexforge_plugin.cpp:197) hard-clips via per-sample gain division at 0.98 →
    aliasing on every over. Zero-latency 1st-order ADAA tanh knee from ~0.9.
    Also ramp `OutputLimiter.h`'s gain across its 5 ms lookahead window.
15. **DC/subsonic hygiene**: zero-mean user IRs at load (conditionIr never
    removes DC) + one 5-10 Hz 1-pole HP at master output before AutoOutput.
16. **VCA dual-time-constant release**: copy the 1176's grFast/grSlow-min
    structure (already proven in `FETCompressor1176.h:80-82`).

### Modulation
17. **Small Clone + Rotary Hermite parity**: both still read modulated taps with
    linear interp (`SmallClone.cpp:93-98`, `RotaryEffect.h:54-58`) — missed by
    the 2026-07-14 fidelity pass. Copy the existing 4-point Hermite read.
18. **Phaser: exponential sweep + decoupled feedback.** Sweep is linear-in-Hz
    (parks at top perceptually); real JFET sweep is ~exponential. `fc = fLo*(fHi/fLo)^tri`
    + separate fbAmt from depth (script/block toggle). [ElectroSmash Phase 90; Eichas DAFx-14]
19. **Flanger: damped feedback loop** — 1-pole ~4-6 kHz LP (+DC-block HP) inside
    the loop (currently full-bandwidth recirculation at up to 0.85 → glassy ring
    no BBD makes) + exponential delay sweep + negative-mix option.
20. **Octave divider hysteresis**: flip-flop toggles on every rising crossing
    (`OctaveBlock.h:73-81`) — double-triggers on strong 2nd harmonic (user's
    humbuckers!). Schmitt hysteresis ∝ env + reject implied periods <60% of tracked.

---

## TIER 2 — The big shared amp upgrade (the single highest-value project)

All twelve production amps use static memoryless Koren LUT triodes
(`TriodeComponent.cpp:297-309`) — no grid current anywhere in the audio path.
The richer components (PowerTubeStage, PhaseInverter, NFB, OT, speaker Z) exist
but are only wired into SunnModelT + the DR deep model.

**Keystone refactor: give TriodeComponent's LUT input a per-sample bias-offset
term fed by a few slow states.** One S/M change upgrades every amp; audio path
stays LUT+adds. Then:

21. **Grid conduction + coupling-cap charge ("blocking distortion")** — rectify
    grid-knee overshoot into a cap-charge one-pole (charge ~0.1-1 ms, discharge
    Rg·Cc ≈ 22 ms), subtract from LUT input. Pick-attack spit, duty-cycle walk,
    choked recovery — THE feel gap of static waveshapers. ~0.3-0.8%/amp.
    [Cohen & Hélie; Aiken "blocking distortion"; Macak & Schimmel DAFx-10]
22. **Sag into the operating point, not the volume.** Keep tuned envelopes +
    bloomVca loudness, but also scale LUT drive/ceiling and feed the bias input.
    2-node RC supply (reservoir + screen) driven by rectified LUT-output proxy;
    tube vs silicon rectifier becomes source impedance → Recto's rect/variac
    switches turn physical. Decays change harmonic character, not just level.
    ~0.2-0.5%. [ampbooks power-supply DSP; Cohen & Hélie DAFx-10]
23. **Dynamic cathode-bypass bias**: envelope-follow LUT output, tau = Rk·Ck
    (~37 ms), into the same bias input. AC30-style touch compression per stage.
    (Sunn's companion-model version already proves the physics in-repo.)
24. **NFB wrapping the nonlinearity**: current loop applies feedback to
    already-clipped output (`PowerAmpProcessor.cpp:411-424`) = fixed EQ.
    Restructure `drive = presenceHP(in − β·y_prev)` with speaker-resonance tap
    inside the loop → damping collapse & presence-under-drive emerge physically;
    retire the envelope hack at :446-457. Watch stability at high β. ~0.2%.
25. **Class-AB crossover + bias-coupled dead zone**: promote the unused
    PowerTubeStage dual-LUT into PowerAmpProcessor, widen xover with sagEnv +
    biasWalk. "Dirty when quiet" decay texture, ghost-note tendency. ~0.3%.
26. **Flux-domain OT saturation**: integrate→tanh→differentiate (~5 ops) so
    80 Hz saturates ~10× earlier than 800 Hz. LF grind of pushed small iron;
    Hiwatt stays clean. Skip Jiles-Atherton.
27. **Current-dependent mains ripple**: rippleAmt = k·sagEnv injected as bias
    modulation (intermodulates → real ghost notes) instead of fixed −50 dB AM.
    Check against measured rig floor / gates.
28. **Yeh-Smith closed-form interacting tonestack** (per-amp switch, migrate one
    amp at a time): exact 3rd-order TMB with moving mid-notch, CHEAPER than the
    current 4 biquads + passiveScoop heuristic. Most audible while turning knobs.
    ⚠ Biggest preset-migration event in this list. [Yeh & Smith DAFx-06; Guitarix tables]
29. **LTP tail coupling** in the PI (rides on #25): subtract common-mode
    `(a+b)*k_tail` from both grids → level-dependent imbalance. Cranked
    Plexi/AC30 PI crunch.
30. **Drive-dependent Miller cap** (only if #21/#23 envelopes exist): modulate
    grid-stopper LP corner by drive env. Marginal — last.

---

## TIER 3 — Structural per-effect projects (M effort, high payoff)

31. **Spring reverb dispersion ("drip")** — currently ZERO dispersion
    (`DR_SpringReverb.*`: 2 Schroeder APs = diffusion only; it can never drip).
    Cascade of ~50-150 identical 1st-order allpasses (coeff ~0.6-0.75) inside
    each spring loop (re-chirps every recirculation) + ~4-5 kHz transition LP +
    optional bright longitudinal path. 1-2% CPU when active. THE most-cited
    digital-spring failure. [Parker & Bilbao DAFx-09; Abel et al. AES 121]
32. **Uni-Vibe truth pass**: real stage stagger from the actual caps
    (~10/140/450/4500 Hz — not our polite 82/196/440/1020), log-law photocell
    `R = Rmax*(b/bmin)^-γ` (sweep dwells dark, flicks bright = throb),
    rate-dependent LFO skew. Constants + one powf. [geofex; Eventide]
33. **Rotary rewrite (whirl-style)**: ~800 Hz crossover, independent horn/drum
    delay+AM at different speeds, per-rotor accel/decel inertia (horn <1 s,
    drum several s → the staggered bloom), shaped horn AM. Needs slow/fast
    target semantics → preset migration planning. <1% CPU. [setBfree whirl;
    Smith et al. DAFx-02] Later: cabinet reflection taps + mic distance (#11 mod list).
34. **Tremolo shapes**: bias (lagged sine) / opto (port the DR_Tremolo smoothstep
    LDR curve — already in-repo, trapped in the amp) / **harmonic** (LP/HP pair
    ~750 Hz modulated anti-phase — brownface swamp, effectively a new effect).
35. **Wah real transfer**: ours is dry + bandpass (`WahBlock.h:56`) — lows never
    thin. Rebuild as swept 2nd-order (position-dependent LP/BP output mix of the
    existing SVF) + pot taper + falling peak gain toward toe. [ElectroSmash GCB-95]
36. **Chorus clock-domain warp + real BBD band-limit**: LFO through
    `delay = k/(fc0*(1+m*tri))` (asymmetric pitch wobble); replace single
    1-poles with 1-pole+biquad matching CE-2's 3rd-order Sallen-Key cliff.
    (NE570 compander pumping is honestly NOT a gap — CE-2/Small Clone don't have one.)
37. **Big Muff component-exact tone stack**: analytic H(s) of the real
    HP(3.9n/22k) ∥ LP(39k/10n) through the 100k pot, one biquad recomputed on
    knob move, per-era component values. Off-noon travel authenticity.
38. **Muff sustain-pot placement**: attenuate INTO fixed ~25 dB stages (real
    topology) rather than scaling both stages' gain; band-limit pre-clip.
    ⚠ Must re-verify all 6 eras on the muff nam_compare harness.
39. **Klon dual-ganged pot law**: clean contribution must fall as drive rises
    (ours holds clean at 0.85 constant, `KlonCentaur.cpp:61-62`); lift gain
    functions from ChowDSP's ChowCentaur. Harder-kneed 1N34A clip while there.
40. **Speaker drive compression in the cab block** (the one "sounds more like a
    speaker" feature): envelope-driven ~1.2:1 program compression + level-
    dependent LF soft-sat below ~120 Hz (Bl droop) + slow thermal HF tilt.
    Pre-convolution, phenomenological (DAFx-08 Aalto), off = bit-identical.
    ~0.1%. Skip dynamic convolution/Volterra. Lands hardest on fuzz-into-clean
    presets given the line-hot staging.
41. **Studio-voice inter-mic time offset**: second mic + micDist are filter-only
    today; add 0.2-0.6 ms fixed delay on the ribbon blend + 0-0.9 ms micDist
    delay. The cheap 80% of multi-mic realism. Loudness re-check on Studio presets.
42. **Decramped EQ (Orfanidis peak / Massberg shelf)**: coefficient-formula swap
    only, zero runtime cost; fixes bilinear cramping of the 4 kHz peak / 8 kHz
    shelf / 16 kHz high-cut. Apply to user-facing EQs first to avoid re-voicing
    capture-tuned internal curves.
43. **Pitch: WSOLA-style aligned grain restarts**: cross-correlation search
    (±half period) at each grain reset kills the periodic warble on whammy
    sweeps; reuse the octave block's period estimate. <0.5% core.
44. **Plate per-comb shelving absorption** (LF decay ratio ~0.6×) + incommensurate
    per-comb LFO rates in classic mode (ambient already has both fixes' analogs).
    Level re-verify ≪1 dB expected.
45. **Guitar volume-pot / source-impedance interaction for fuzz front-ends**:
    Thevenin source (Rs(pot) + pickup L/C) against per-pedal input Z; in
    ToneBenderMkII, Rs drops into the existing NR solve → real cleanup
    (attenuate + brighten + linearize together). Pair with **per-pedal input-Z
    table on PickupLoadSim** (TS ~500k, Muff ~40k, RAT ~1M, TB/FF 5-10k).
46. **Echorec valve input color** + **velvet-noise input diffusion for plate**
    (~15 adds/sample) — polish tier.
47. **Optical compressor mode** (LA-2A T4 two-stage memory release) — new
    capability; port plumbing across standalone + Hex Forge.
48. **Hum comb 420/480 Hz extension** — ONLY if a new amp_realnoise capture
    shows upper-harmonic content.
49. **Graphic-EQ band-interaction fit** (one-shot least-squares at update time) — low.

## CPU-funding items (do when budget gets tight, not before)
50. **ADAA on triode LUTs / pedal waveshapers** → drop those stages to 2× OS at
    equal alias floor; saves an estimated 30-50% of preamp cost. Not a fidelity
    item; needs amp_alias + nam_compare re-verify (bit-exactness lost).
51. **NEON-vectorize fft128/MAC pass** (~3-4×) in the already-shipped partitioned
    convolver; Gardner non-uniform partitioning only if ≥0.5 s user IRs matter.

## Explicitly NOT worth it (researched and rejected)
- Higher oversampling / steeper AA anywhere (measured done).
- Jiles-Atherton hysteresis (tape or OT) — 5-20× cost, subtle inside band-limited loops.
- Full WDF migration — capture-tuned behavioral + items 28/37/39 capture the
  audible part; revisit only for a from-scratch pedal.
- Dynamic convolution / Volterra cabs — memory ×N, blind-test benefit doubtful.
- Spectral gating (noise-repellent) — ~23 ms latency, disqualifying; floor is
  hum-dominated and item 12 is the right lever.
- True-peak/ISP limiting — <1 dB exposure at 0.98 ceiling, no codec step.
- Full variable-clock BBD emulation — chorus-depth clocks put artifacts above
  audio; items 19/36 get ~90% for ~1% of the cost. Revisit if a BBD *delay* ships.
- NE570 compander modeling for CE-2/Small Clone — those pedals don't have one.
- Fuzz Factory core rebuild — V3 just landed on captures; don't reopen.

## Suggested attack order
1. **Sprint 1 (all S, ~zero CPU):** 12 → 1 → 2 → 6/7 → 8 → 14 → 13 → 17 → 3 → 15.
2. **Sprint 2 (amp keystone):** TriodeComponent bias-offset refactor, then
   21 → 22 → 23 (one verify pass), then 24/25/26/27.
3. **Sprint 3 (character projects):** 31 (spring), 32 (vibe), 34 (harmonic trem),
   35 (wah), 40 (speaker compression), 43 (pitch).
4. **Sprint 4 (knob-travel truth):** 28 (tonestacks, amp-by-amp), 37/38 (Muff),
   39 (Klon), 45 (fuzz cleanup), 33 (rotary rewrite).
Fund with 50/51 whenever the bench crosses ~60%.

Full per-item citations live in the five research reports (session transcripts);
primary sources: DAFx archive (Yeh, Cohen & Hélie, Parker & Bilbao, Raffel &
Smith, Holters & Parker, Eichas, Dempwolf & Zölzer), ElectroSmash circuit
analyses, geofex, Aiken Amps tech pages, ampbooks DSP series, ChowDSP repos
(AnalogTapeModel, KlonCentaur, chowdsp_wdf, ADAA), setBfree whirl, Klippel,
Orfanidis EQ paper.

## DEPLOYED 2026-07-26 (overnight)
Branch fidelity-phase1 built + deployed to the pi-Stomp and MOD restarted (verified:
services active, plugins dlopen-clean, new param strings in the deployed .so).
LIVE/testable on device: gate hum-reject, Small Clone/Rotary Hermite, IR+master DC,
tape cluster (tapeVoice=1), spring drip (0.5), authentic wah (voicing=1), authentic
uni-vibe (authentic=1). Capture-tuned pedal/amp features stay OFF (Phase-2 re-tune).
Build gotcha fixed: removed stale shadow headers src/DS1Distortion.h + src/SuperOverdriveSD1.h
on the Pi (they shadowed include/ and lacked new members). Not committed to repo (Pi-local cruft).

- **2026-07-26 Batch J — DONE, offline-verified (modulation/effects):**
  - #18 Phaser exponential sweep ("voicing" default 1): fc=fLo·(fHi/fLo)^tri (JFET
    log sweep) blended vs the old linear. Verified active.
  - #19 Flanger feedback damping ("voicing" default 1): ~5 kHz 1-pole LP in the loop
    so high feedback blooms instead of ringing glassy (also stabilizing). Verified −1.6 dB
    HF ring, bounded.
  - #34 Tremolo shapes ("shape" 0=bias default / 1=opto / 2=harmonic brownface — LP/HP
    anti-phase). Verified harmonic mode active. NEEDS PORT to select 1/2 (land-only for now).
  - #20 Octave divider de-glitch ("deglitch" default 1): Schmitt hysteresis (±0.12·env)
    + implied-period reject on the flip-flop. Verified with a strong 2nd harmonic the sub
    purity goes 0.000→0.667 (old divider fully double-triggers). Fixes the humbucker glitch.

- **2026-07-26 Batch K — DONE, offline-verified (Tier-3, self-contained):**
  - #33 Rotary two-band Leslie rewrite ("voicing" default 1): horn (highs) and drum
    (lows) are now separate physical rotors — ~800 Hz crossover, own Doppler tap + AM,
    drum spins 0.80× the horn, and ROTOR INERTIA (horn slew ~0.5 s / drum ~2.2 s) so a
    speed change blooms. Shaped (directional) horn AM. voicing 0 = old single-band.
    Verified two-band active + stable (peak 0.75), crossover keeps both bands.
