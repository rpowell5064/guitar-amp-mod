# Hex Chain Release Notes

## v1.15.0 — 2026-08-01

The vintage-voltage update: a brown-sound variac, a tube-tape echo and its JFET preamp, a studio hot-rod amp mod, a script-era phaser, a new classic-rock/metal preset bank, and a power-amp fix that finally lets three amps sound the way their captures do.

### New

- **Variac (brown)** on the Plexiglass amp (main rig and Rig B): a toggle that drops the amp's effective wall voltage, reaching the power tubes' saturation knee earlier and softer, with deeper, spongier supply sag and a browner top — the classic dropped-voltage trick. Every stage scales from one physically-derived voltage ratio; off is bit-identical to the stock voicing.
- **Vintage Echo** (Delay) and **Echo Primer** (Drive): a new valve-warm tube-tape echo delay with its own JFET record-preamp front end and an **Age** control that runs from serviced-machine clean to thrashed, dark and hissy (with circulating tape hiss and wow/flutter that grow as the machine ages). The same JFET preamp is also a standalone Drive voicing — a subtle front-end that fattens and pushes whatever amp follows it.
- **SIR #34 mod** on the Crunchy amp: a toggle that adds a cold-biased extra gain stage, a recathoded second stage and a bright-cap / feedback voicing shift — the studio-rental hot-rod bite. Off is stock.
- **Script Phaser** modulation type: a smooth, feedback-free four-stage script-era phaser — a pure sine sweep and soft, musical notches, warmer and rounder than the resonant block-voiced Phaser.

### Presets

- **New bank: classic rock & metal** — a bone-dry deep-scooped high-gain thrash rhythm, a variac'd brown-sound rig with a script phaser out front and tape echo behind, an at-the-edge non-master crunch that lives on your pick hand, and a modded-British-head lead that flips to a clean cascading intro-delay voice.
- The stock **Clean / Crunch / Rhythm / Lead** and several band rigs carry fresh player-tuned dial-ins captured from the device — including the Crunch preset running the new SIR #34 mod.

### Fixed

- **Modulation type selector in Hex Forge**: the type list was capped one option short, so selecting the deepest modulation voice silently ran the analog chorus instead. Every modulation type now selects correctly.
- **Power-amp drive on the main rig** (Gainzilla, Tangerang and Chime Thirty): the per-amp power-amp drive that tames these three to their real captures was only reaching the parallel Rig B, so on the main rig they ran over-saturated — a fizzy high-gain lead with a clipped attack swell, a harsh jangle amp on hard picking. It's now applied everywhere: the high-gain amp gets its bloom and swell back, all three sit where their captures put them. Preset loudness is unchanged.

---

## v1.14.0 — 2026-07-25

The ambient update: a third reverb machine, a graphic EQ block, two new amps, two new preset banks, and a deep round of feel/stability fixes.

### New

- **Ambient reverb type** (Reverb pedal + Hex Forge): a blooming cinematic pad reverb alongside the plate and the three-spring tank. A long, heavily diffused tail whose density *grows* after you stop playing, driven by a new **Bloom** control that scales smear, late-tail regeneration, slow evolving motion and stereo width together. Non-pitchy modulation (no chorus wobble), mono-safe width, loudness-matched to the other types.
- **6-band graphic EQ block** (Hex Forge): 100 Hz–3.2 kHz vertical faders with a live response scope and preset curves (Clean Sparkle, De-Mud, Classic Rock, Metal Rhythm, Lead Cut, Cocked Wah). Movable anywhere in the chain — classic tone shaping in front, or post-everything mix sculpting at the end.
- **Two new amp voicings** (Amp pedal + Hex Forge): **Diamond Plate**, an 8-mode three-channel high-gain head with Bold/Spongy variac and Silicon/Tube rectifier feel switches, and **Tremont 15**, a tight percussive lunchbox head with Clean/Crunch/Lead channels and a bright switch. Both tuned against real capture sets.
- **Seasick Vibe** modulation type: a deep tape-warble chorus with a true pitch-heave crossfade mix and subtle tape drift.
- **Recorded-sound tools** (Cab): Room/Studio cab voice (Studio adds a second virtual mic, console curve and bus glue), an optional stereo output **Doubler** in Hex Forge, plus Reverb Density (Classic/Dense tank), Cab Room Density, pickup-loading and speaker-coupling feel controls.
- **Preset browser search** (Hex Forge): type-to-filter across all 128 slots, with a one-click clear.

### Presets (64 factory rigs, up from 54)

- **New bank: heavy modern** — four drop-tuned rigs built on the Diamond Plate (loose-and-scooped, tight-and-produced, mid-forward staccato, plus a compressed shimmer-clean) and a fifth wall-of-sound slot.
- **New bank: ambient** — a jangling dotted-eighth anthem rig, a watery floating-space clean, a post-rock glass rig, and a maxed-Bloom showcase pad.
- **Lead Cut EQ** applied across all lead presets — every lead now steps out front with the same rising presence curve.
- Industrial-rock presets gained end-of-chain mix-EQ curves; the surf preset was rebuilt around the spring tank; several presets carry player-tuned dial-ins captured from the device.

### Fixed

- **Amp-model swap race**: recalling a preset with a different amp model could leave the *previous* model running until the next switch. The swap now always lands within the seamless mute-ramp.
- **Hard-pick dropout**: every amp model's supply-sag could momentarily collapse to silence on a hard attack into a cranked, boosted channel. All sag stages are now floored — the springy compression stays, dropouts are gone.
- **Seamless switching**: preset recalls, block toggles and model changes are wrapped in a short mute-ramp with stale-tail clearing — no pops, no leftover delay/reverb audio replaying into the new sound.
- **Reverb pedal Type selector**: the Plate/Spring selector was missing from the standalone pedal's port list and never worked; it's fixed and now includes Ambient.
- **Standalone panels**: the Cabinet pedal no longer overflows its enclosure (wider layout, Voice and Room Density on their own rows) and the Reverb pedal gained Tank controls (Type / Density / Bloom).
- **Reverb determinism**: tank modulation phases now reset on recall, so a preset sounds identical every time it's loaded.

### Performance

- **Faster NAM inference** everywhere (fast-tanh activations in the shared engine) — biggest gains on LSTM-class captures.
- **Partitioned convolution** for IR files: long cabinet IRs no longer spike CPU (fixed small-FFT partitions replace one huge FFT per block).
- Suite-wide gain-knob audit: every amp cleans up properly at the bottom of its gain range.

---

## v1.13.0 — 2026-07-17

(previous release — see the GitHub release page)
