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

- **2026-10-30 Phase 3 — the remaining open items (branch fidelity-phase3).** Status
  check first: #24, #28, #36, #40, #41, #42 had already landed in July (see git log);
  this round closes the rest of the effect-side list. Every new behaviour has its own
  setParameter id, off = bit-identical, and every random source re-seeds on reset()
  (identical input => identical output, the dual-mono contract).
  - #11 Echorec swirl ("drumWear", default 0.5): rotation-locked speed modulation
    (1/rev + 2nd harmonic, rate = 1000/timeMs) + ±0.5 dB per-head AM at the drum angle.
  - #46a Echorec valve record input ("valveColor", default 0.3): asymmetric
    grounded-cathode 12AX7 knee + coupling-cap DC block, repeats only.
  - #10 self-oscillation on Tape + Echorec ("selfOsc", default on): feedback <= 0.9 is
    untouched; 0.9..0.98 (the port top) spreads to loop gain 0.9..1.15, bounded by the
    loop tanh.
  - **Echorec loop-gain bug fixed ("loopUnity", default on):** the loop clip was the
    un-normalised tanh(2.5x), so the loop gain was 2.5 x feedback -- unity at 0.4, the
    default. Every setting from 0.4 up built into a never-ending wash (from 0.6 a
    near-full-scale one, peaks above 1.0). Now tanh(2.5x)/2.5 like the tape loop:
    the knob is the loop gain. Only Dark Side Air (fb 0.45) is affected among the
    factory presets; re-voiced to feedback 0.8 (kFactoryRev 200).
  - #44 plate: per-comb Jot low shelf ("lfshelf", default OFF -- re-levels the wet)
    + incommensurate per-comb LFO rates in classic mode ("lfospread", default ON).
  - #46b plate velvet-noise input diffusion ("velvet", default ON after a by-ear A/B on
    the device; it moves short-window preset loudness by up to ±1.6 dB in the golden).
  - #43 pitch: WSOLA-aligned grain restarts ("align", default on), no added latency.
    PitchBlock is not used by any LV2 plugin (submodule/JUCE chain only).
  - #45 (second half) first pedal's input impedance loads the pickup: re-loading
    biquad H_Zin/H_1M in PickupLoadSim + a Zin table, wired in the Hex Forge engine
    (the only place that knows the chain order). Engine flag only, OFF until a port.
  - Golden (all 128 Hex Forge presets) vs main: 10 rows change, max 0.26 dB RMS
    (Dark Side Air: swirl + valve); the rest are hash-only re-seed carry-overs.

- **2026-11-03 — HT supply, heaters, microphony, valve spread on the component twins
  (#22 supply side, #27, + new).** Every PushPullPowerV twin (Deluxe, JCM800, Plexi, AC30,
  BE-100, Mark V, Recto, Rockerverb, SVT, AD200; not the EVH, which has its own section).
  Model ids "supply" / "ripple" / "heaterhum" / "micro" / "spread", all 0 = off.
  - PowerSupplyV: the HT secondary (|sin| at the mains rate) into the rectifier (silicon or
    valve source resistance) and the reservoir, then choke -> screen node -> PI node ->
    preamp nodes as a tree (the AC30's branches hang off the screen node). Node voltages are
    the drawings'; each series R is derived from the printed drop and the idle current, so
    the stages see zero deviation at rest. The output stage's live current loads it.
  - Effects: plates move toward a second load-line table at 0.8 vb (the ceiling sags and
    carries the ripple: the clipped-note sidebands); the screen node adds to the per-tube
    screen droop; the PI and preamp triodes take their node as a rail offset.
  - Sag vs ripple split at 70 Hz (2nd-order). Triodes take the slow share as an operating-
    point move only (its DC plate shift is removed, as the coupling cap would): fed as
    signal, the choke/filter resonance (10-20 Hz) motorboated the high-gain twins. The Mark V
    still closed the loop through node C's and node E's triodes, so those stay on the
    printed rail (its PI, loop stage, screens and plates take the supply).
  - Heater hum: mains-locked 60 Hz at V1's grid, per-amp level (DC / elevated heaters low).
    Microphony: a 3.2 kHz Q40 resonator from the speaker node into V1, bounded; inaudible
    at physical coupling on the heads, set to a third of the howl point on the two combos.
    Spread: a fixed deterministic set of preamp triode current tolerances + output-pair
    idle mismatch.
  - Measured at gain 0.6, 220 Hz burst: 1.0-2.1 dB bloom (late vs early), plate sag 20-75 V,
    ripple sidebands -35 dBc (valve-rectified combos) to -60 dBc; no idle oscillation on any
    twin up to gain 1.0. CPU +5 % on the twin.
  - -ffast-math: the extras' on path lives in processSampleHt (own translation units) and
    the stages' ...Ht builds, dispatched by OversamplingWrapper via AmpModelBase::htActive().
    The off path is the original code, but the extra code in the twins' TUs still moves the
    compiler's inlining: the golden changes in 16 rows by <= 0.0009 dB RMS (rounding only).
  - Device A/B: TEMPORARY flag files supply_on / valve_on in the Hex Forge config dir (the
    twins run whenever the Component Model toggle is on, its shipped default).
  - **Decided by ear the same day: all ON.** Hex Forge sends supply / ripple / heaterhum /
    micro / spread = 1 to both rigs every block; the flag poll is gone. The models' own
    defaults stay 0 (the other plugins and the lab harnesses are unchanged).

- **2026-11-03 — twin CPU pass (no change in sound).** Measured per twin through the 4x
  wrapper (callgrind for where, wall clock for how much); every change nulls below the same
  twin's response to -140 dBFS input noise (the Recto's and Plexi's high-gain edges shift
  by a hair under ANY perturbation, so the comparison is against that, not against zero).
  - softplusFast(): log1p(exp(x)) + sigmoid from a 960-point cubic-Hermite table (~3e-10 of
    the value). It was 5-21 % of a twin: the soft grid knee on every CCStageV and
    CFStageV, and korenEvalP (the SVT's runtime valves).
  - TaperMemo: per-sample audioTaper (log + pow) on smoothed knobs recomputes only when the
    knob moves (bit-identical).
  - PI: the joint Newton's converged Jacobian replaces ltpLinearize's two extra valve
    evaluations (same partials, at a point within 10 nA).
  - Screen droop ^1.5 as x*sqrt(x); OT core tanh(ph)/ph by its series below 0.02 and by
    one exp above.
  - x86 (10 twins, extras on): 137.9 -> 120.4 % of a core (-13 %), below the 130.1 % the
    twins cost before the HT supply existed. Pi 5 (same bench, one core): 362 -> 318 %.
  - Pi golden refreshed with the extras on: 112 of 128 rows move; twin presets lose level
    to the sag (JCM800 -2.3 dB mean, BE-100 -2.4, Recto -3.8 with World Went Away -5.6,
    Plexi -1.6, Mark V -1.6; EVH/Hiwatt/NAM unchanged). OPEN: the linear rectifier R was
    fitted at moderate drive and over-sags at full power (Recto plate node -132 V on the
    valve rectifier, -66 V on silicon at master 0.6); a Child-law valve drop + winding R
    and a full-power calibration should come before any preset re-level.
- **2026-11-03 — twin extras, second set (valve swaps, dynamic Miller, supply fix, PI
  mismatch; OT resonance tried and dropped).** All off / stock by default; the three physics
  switches ride a TEMPORARY Hex Forge A/B (<configDir>/miller_on, pispread_on, rectlaw_on).
  - Valve swaps (Hex Forge blob v54, mv285: amp_v1tube / amp_patube / rb_v1tube / rb_patube,
    shown for component builds): V1 = 12AX7 / 5751 / 12AY7 / 12AT7 / 12AU7 (Koren's own
    sets for 12AT7/12AU7, guitarix's for 12AY7, a 5751 fitted to the RCA point -- no
    published set); power valves = Stock / EL34 / 6L6GC / 6V6GT / EL84 / KT88 with each
    type's twin gm scale, re-biased to 65 % of rated dissipation (90 % cathode-biased).
    Rebuilt on the worker (7-80 ms x86). Every twin x every choice renders finite.
  - Dynamic Miller (CCStageV::setMiller, HT build): the static Miller caps the twins already
    carry move with the stage's live gain (A collapses as the stage clips); the JCM800,
    Deluxe and AD200 V1 stoppers get the full Cgk + Cgp(1 + A). Changes -24..-42 dB re signal
    on the bench chord, no level change, no measurable CPU.
  - Supply fix (PowerSupplyV::setFix): silicon drop or Child-law valve rectifier
    (GZ34 17 V @ 250 mA, 5U4GB 44 V @ 225 mA per plate), the HT winding at measured
    Hammond-replacement figures (Marshall-type ~20 ohm, Deluxe-type ~81 ohm), the choke at
    its own DCR (ESTIMATE 70-150 ohm), UK twins on 50 Hz mains. FINDING (corrects the OPEN
    note above): the plate-node sag was NOT overstated -- a square-wave-driven 100 W silicon
    stage on a 50 uF reservoir really drops 12-16 % (Schade), consistent with the one
    published full-power figure found (a JTM50 on a GZ34: -52 V at 47 W). The excess was the
    SCREEN node: the choke R derived from the printed drop / modelled idle current was
    2-5x a real choke, and the screens' full-power current ran through it (JCM800 screen
    -21 V below the plates -> -8 V; BE-100 -43 -> -9). Full-drive level change vs the
    linear supply: -0.7..+1.0 dB per twin. Preset re-level waits for the A/B verdict.
  - PI mismatch (pispread): the LTP halves at the spread control's -4 / +7 %, the pair
    re-solved at rest. -34..-47 dB re signal; the SVT (a cathodyne) is unaffected.
  - OT leakage / winding-capacitance resonance: DROPPED. No guitar OT's peak is published
    (Hammond's AC30 replacements: 4.6-5.1 mH leakage). Inside the twins' loops a 40 kHz /
    Q 1.2 estimate made the BE-100 loop unstable (+78 dB at 36 kHz, +9 dB at 2 kHz) and the
    Plexi ring (+29 dB at 22 kHz); even at 80 kHz / Q 0.7 presence 0 still rang. The twins'
    fitted HF loop terms already carry the OT's top; adding it needs a re-fit of those terms
    against the captures, not an estimate on top. (Done the right way the same night, below.)
- **2026-11-03 — twin extras, third set (cab as the load, bias supply, OT as a second-order
  section, corrected valve types).** All off by default; TEMPORARY A/B files otres_on,
  cabload_on, biassup_on, tubefix_on beside the second set's.
  - Cab as the load: the power stage carries the ratio of the rig's cab to the amp's native
    cab (Params::nativeCab), each built from its CabModels speaker row as the plates see it
    -- Z against a pentode source of ~4x the load, so a +20 dB impedance peak reads ~+11 dB,
    where the twins' anchored curves sit. Native cab = exactly unchanged; e.g. the Recto into
    an open-back 1x12 +4..6 dB at 50-80 Hz, -2.4 dB at 150; the Deluxe into a closed 4x12
    gains the box bump at 80-150 Hz. The engine publishes each cab slot's speaker row
    (user IR / No Cab / Cab 1 off = the amp's own); Amp 2 follows Cab 2 when it is in.
  - Bias supply: fixed-bias stages' bias droops with the transformer primary (half the
    winding resistance, ESTIMATE) under the HT load, through a 150 ms filter, plus 50 mV of
    mains ripple. Small by nature: ~0.2 V on a cranked 100 W twin (-50 dB re signal).
  - OT: the fitted pole IS the winding capacitance against raa; with the leakage (5-8 mH
    ESTIMATE) the section becomes raa / (s^2 L C + s raa C + 1) -- the same below ~5 kHz
    (<= 0.5 dB), the transformer's own roll-off above. Stable at presence 0 / .5 / 1 on the
    JCM800, Mark V, Recto, Deluxe, SVT and AD200; above the band on the AC30 / Rockerverb;
    OFF on the BE-100 and Plexi, whose loops at presence 0 rang with any in-band leakage.
  - Corrected valves: the Deluxe's V6 PI is a 12AT7 (Params::piTube; -0.8 dB), the
    Rockerverb's V7 an ECC81, and the SVT / AD200 run Koren's 6550 set instead of the KT88's,
    re-landed on the same idle bias (SVT +3.2 dB: its 6550s have more gm at that bias).
    OPEN: the Rockerverb twin's drawings print 4x 6V6 while its captures are MKIII (2x EL34).

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
51. ~~**NEON-vectorize fft128/MAC pass**~~ **DONE 2026-08-19** — tail complex-MAC +
    head FIR NEON-intrinsified (kBins 65 → kStride 68 padded rows kill the scalar
    remainder; head taps stored reversed for a forward contiguous dot; scalar
    fallback kept for amd64/MSVC). Measured on the Pi (conv_check, per channel):
    irLen 2048: 190→270× RT (1.42×), 24000: 33→74× (2.25×), 96000: 8.8→17.6× (2.0×).
    A 1 s user IR now ~2× cheaper. fft128 itself left scalar (~2% of worst case —
    deliberate). Gardner non-uniform partitioning still only if ≥0.5 s IRs matter.

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

## PHASE-2 CAPTURE-TUNING (2026-07-26, nam_compare on Pi vs Downloads captures)
Enabled + VERIFIED (measured, not guessed):
- Amp triode keystone: REWORKED (blocking = transient fast-slow bias so it barks on
  attack and settles to 0; cathode = SYMMETRIC gain reduction, bypassed stages only,
  floored 30ms env) -> no steady even-harmonic regression. block 0.20 / cathode 0.38.
  Verified: Fender clean compression -6.2 (was stiff) + harmonics match; EVH h2 2.6
  (NAM 3.1); JCM800 h2 25.3 < its keystone-OFF baseline 40.6 (helps, not hurts).
- Power-amp xover 0.12 + fluxOT ON: Cali V h3 37.9->41.9 (closer to NAM 49.4).
- Comp sidechain HPF 85 Hz ON (CompressorBlock); ADAA limiter soft ON (hexforge).
- DS-1 dynamic bark ON (depth 0.40): static-neutral (h2 0.0, transient settles).
LEFT OFF (measured to REGRESS the already-tuned pedal captures — kept tuned voicings):
- TS-808 cleanPath (+3.5 dB hot + FR shift vs the tuned +-1.4dB/-1.1dB), RAT gainLegs
  (-14dB bass shelf shifts FR), Octavia geThresh (shifts octave harmonics). These were
  speculative circuit-additions layered on pedals ALREADY nam_compare-matched.
- Tremolo opto/harmonic shapes still need a port to select (modgui task).
KEY LESSON: dynamic bias/blocking must be TRANSIENT + cathode comp SYMMETRIC or it adds
steady even harmonics the static NAM captures don't have (looks like a regression). And
high-gain harmonic readouts are NOISY (~50% THD) — always measure the keystone-OFF
baseline before calling a delta a regression.

## FULL AMP RE-VOICE SWEEP (2026-07-26 night, vs the user's Downloads captures)
101 curated DI captures staged to Pi ~/dl_caps/amps2/ (10 amps). Methodology: baseline
at the capture's documented knobs (or best-fit when unlabeled), fix only knob-robust
defects, verify every change with nam_compare, commit per amp. nam_compare supports ALL
14 amps (the usage string was stale).

RE-VOICED (committed + deployed):
- JCM800 (knob-matched capture): softened cascade + bright-cap pre-emph + input HPF
  60->130 + opened top + body restore. Fizz h7 30->10 / h9 25->6, THD@110 78->51,
  h2 19->10. USER-APPROVED ("sounds great").
- Rockerverb dirty (knob-exact dimed capture): couplings 0.50/0.45/0.42 ->
  0.70/0.65/0.60 (cascade actually saturates now: THD@110 36 vs target 38-43), bright
  pre-emph 800/+8, stage 2/3/4 asym bias (h2 1.7->5.5; full 17 needs PA duty asym),
  dimed bass shelf 11->9.5. FR within ~2 dB. GOTCHA: --channel 0 = dirty (1 = clean)!
- Backline Plus / Peavey (labeled capture, Sat3->gain 0.3 mapping): clipper ran
  ~10.5 dB too quiet -> drove to the capture's operating point (x3.2 pre, x1.9 out).
  THD in range, 223 Hz harmonic profile now overlays the capture.
- Hiwatt (labeled ALL-12/Master-10): THD already matched; FR-only fix (bodyShelf
  -1.5 -> lowshelf 90/+5.5, 180 Hz -2.6 dip in the unused brightShelf slot).
  FR within ~1.4 dB (was -5.6 @50).

VERIFIED ALREADY-GOOD (no change): Plexi CH I (THD 54/51 vs 48/50, FR ~2 dB),
Mark V IIC+ HG bal (FR within 0.4 dB, 223 Hz harmonics overlay), Recto CH3 Modern
(FR dead-on incl +20 dB @50 resonance), Friedman BE gain-sweep (its tuning ref),
MT15 vs Metal_Sweet_Spot gain 0.7 (FR +-0.1 dB — its tuning ref; satDrive push
tried + REVERTED: made odd-square, cost 6 dB level).

DEFERRED (documented, needs supervised session):
- EVH: LF 2x over-distorted + mids half + lows dark, CONFIRMED capture-robust across
  two independent packs (Beji Red + Blue) — but the fix needs inter-stage filter
  restructure on the flagship high-gain amp with unknown-knob captures.
- Vox AC30: resists the pattern (1-pole tighten no-op at 110, post-shelf eaten by the
  limiter), capture labels ambiguous (M8? Cut?). Was previously NAM-tuned.
- Sunn: no DI capture (Shift Line pack is full-rig IRs).

UNIVERSAL STRUCTURAL FINDING (every amp): the models are ODD-dominant where real amps
are EVEN-rich (h2/h4/h6), and THD@1k runs ~half the captures' (flat-with-level = PA
crossover/duty-asymmetry signature). Both need power-amp-level structural work
(per-amp duty asymmetry / crossover), not per-amp preamp tuning. The Rockerverb
stage-4 bias experiment quantified it: LUT-stage offsets max out around h2 5-6%.

MEASUREMENT GOTCHAS (hard-won): match the capture's knobs BEFORE reading deltas
(the "10 dB Fender compression gap" was a knob mismatch); scp Windows->Pi can land
mtimes BEHIND build objects (clock skew) — always touch after scp; --channel
conventions differ per amp (Rockerverb 0=dirty); some captures are pedal-boosted
(808/TS9/SD1 in the name = skip for amp voicing).
