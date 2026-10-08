# Hex Chain Release Notes

## v1.21.0 — 2026-10-07

The amp update, plus a practice room. Every component amp was re-measured against its
captures at the level a guitar actually delivers and several were refitted, the Deluxe
Reverb joins the component twins, the Mark V's controls follow the real pots, the Input
Trim learns which guitar is plugged in, and a new plugin, Scratch Pad, puts a four-track
looper and a drum kit on the board.

### Added

- **Scratch Pad.** A four-track looper (two minutes a track, count-in, bar-snapped non-destructive trim, loops survive a reload, a level knob on every lane) with a drum machine: thirty grooves from swing to djent, a kit resynthesised from measured drums as parameters rather than samples, cymbals that keep ringing across strikes, a metronome, a parallel-compressed drum bus with a room, a Scales tab with a readable fretboard and chord grids, and a groove library you can save your own patterns into. It runs stereo with a mono fold and costs about nine percent of a Pi 5 core.
- **Fender Deluxe Reverb (AB763) component twin** on amp 10, fitted to '65 Deluxe captures. Its Volume knob follows the amp's audio taper, and the shared power section now applies a per-amp output-volts calibration so the Deluxe's tail no longer overdrives the open-back speaker model.
- **Input Trim GUITAR selector** (Default / Single Coil / Hot Pickups), in Hex Forge and the standalone Input Trim. A global setting for the guitar plugged in, saved with the board rather than the preset. It writes the Gain, Humbucker, HB Model and HB Amount controls so what it does is visible on the panel, and is re-applied after every preset recall. Default switches the humbucker voicing off and leaves the Gain, your input volume, exactly as set. New HB Model "Hot → PAF" tames a hot modern bridge pickup back to a vintage one.
- **JCM800 twin: the #34 mod is real.** The twin had ignored the mod switch since the component build shipped. It is now built from the circulated spec: the first coupling capacitor out, a 500 pF coupling, a hotter shielded input, the two bigger cathode capacitors and the .47 µF presence capacitor. Lows tighten, the top opens and the first stage clips earlier, which is what the mod does.

### Changed

- **The component twins' power stage is rebuilt.** Screen sag is now per tube (quads had it four times over), the feedback loop closes without a one-sample delay and the phase inverter is solved as a pair. The Plexi and Friedman squeal and 15 kHz ringing are gone, as is a 12 dB drive error on the Friedman. The Pi cost rises between six and twenty-one percent depending on the amp.
- **Friedman and JCM800 component twins clip where the real amps do.** Their input scales had been fitted on saturated rows and started clipping 12-30 dB late; both are now anchored on the measured clipping knee, the JCM800's cathode follower uses the joint grid solve (no more fizz on quiet notes), the Friedman's grid knee matches the calibrated capture, and the Beardo Clean channel holds a sane level across its knob.
- **Mark V twin** follows the real pots: treble on its audio taper, the mid law fitted over all seven channel captures, channel 3 cleans up where the real amp does, the front bass keeps its 100-125 Hz authority (one capacitor had been wired from the wrong node) and the graphic equaliser's 80 Hz band behaves like the sliders. Six Mark V presets were re-dialed the Mesa way.
- **Rockerverb twin fitted to the real knob grid; Friedman to its gain ladder; the power-stage drive calibrated on the JCM800, EVH, Mark V and Rockerverb twins.** The shipped (non-component) stacks had linear pots where the amps use audio tapers; fixed.
- **The Master knob on five twins ran against its level row** (turning it up pulled the preset level down); the rows are zeroed and the knobs read the right way.
- **Presets.** Every factory preset was re-measured and re-levelled after each engine change. Imperial Lead is back on the Plexi as a dimed, jumpered 1959 with sag as drawn; the Regal presets lose their Doubler; the Rockerverb and Sunn "darkness" turned out to be a rig row and a preset, not the amps, and both are fixed; Berlin Wall Pulse's peak cap comes down. Dial-ins saved on the device are preserved verbatim.

### Fixed

- **Cathode followers rectified quiet notes.** The follower stages solved their grid diode against the previous sample, which turned quiet notes into a fizzy half-wave. They solve it in-sample now, on the amps where it matters (JCM800, Rockerverb, Friedman).
- **A delay in front of the amp could spike it.** Hex Forge's amp takes one input now, so a wet second channel no longer doubles into it.

## v1.20.0 — 2026-09-27

The pedal update. Four of the drive and fuzz voicings were re-measured and corrected, the
suite now behaves properly on generic-UI hosts like Zynthian, the mains-hum filters work in
50 Hz countries, and a batch of factory presets was fixed or replaced.

### Added

- **50 Hz mains.** Every hum filter in the suite is switchable between 60 Hz and 50 Hz: the Input Trim's notch comb (in Hex Forge and standalone), the noise gate's hum detector, the amp's input comb and the calibration reference. Default stays 60 Hz, so nothing changes unless you switch it.
- **A designated block-enable port on all thirteen standalone plugins.** Hosts that build their own control panels can now bind a plugin's bypass properly instead of showing it as an ordinary parameter, and it sits at the END of the parameter list where such hosts expect it. Note the sense is inverted relative to the existing Bypass control: enabled means processing is on. The existing Bypass port is unchanged and unmoved, so saved boards are unaffected.
- **Named parameter groups.** Every control port now declares which part of the chain it belongs to. On hosts that page through parameters four at a time, Hex Forge went from a long anonymous list to named groups per block.
- **Correct plugin categories.** The cabinet declares itself a simulator and the octaver a pitch plugin, so hosts file them in the right place instead of lumping them under "other".
- **Andalusian Slice** — a new preset in the Modern Rock bank: raw, wiry late-eighties alternative rock on the JCM800 with a studio plate.

### Changed

- **The Green Man's level is honest again.** Last release rescaled its drive-0 gain floor to tame what sounded like too much boost. That was the wrong lever: the floor is the real feedback ratio and belongs where it was, and the actual fault was output level — the pedal ran about 3 dB hot at every Drive setting. The floor is restored and the level corrected. The circuit-accurate clipping path is now the default too, so the clean low end passes through instead of being squashed with everything else, which is what the circuit does.
- **Dear Rodent Boy's Filter reaches dark.** At the top of its rotation the control could not get as dark as the circuit does, and it was losing harmonic content up there. Its range is extended in the top third of the rotation only, so the bright half is unchanged.
- **Preamp 250 output level** corrected — it ran quiet across its whole Gain range.
- **Octavius' Drive knob no longer changes how loud it is.** It used to climb about 9 dB from minimum to maximum; the output is now flat across the knob, with the tone untouched.
- **Output-transformer saturation** in the component amps is rewritten as a flux-domain limit, which makes it the low-frequency effect it physically is, anchored to each amp's rated output. Several amps' speaker-resonance voicing was re-fitted alongside it.
- **Presets.** Run Like Hell gets its dual-delay machine back (it had been split into two plain digital delays, which is not the same thing). The Alice in Chains and Tool presets move to the Friedman's hottest channel — the Tool preset had been standing on a mid-scooped amp to imitate a tight, mid-present one, and was undoing that with an equaliser. The Rage Against the Machine preset was replaced. Existing dial-ins saved on the device are preserved.

### Fixed

- **The Input Trim's humbucker voicing did nothing.** The standalone plugin shipped with the voicing amount defaulting to zero, which is a true bypass, while the Humbucker switch defaulted to on — so neither the switch nor the model selector had any effect at any setting. Now the switch is off by default and the amount is full, matching Hex Forge. Boards you already saved keep their stored values.
- **Switching the Modulation block to Uni-Verse no longer causes a brief dropout.** Its start-up state is worked out once, off the audio thread, instead of being solved the moment you select it.
- A duplicate control slot on the amp panel (two controls claimed the same position) and a preset-file reader that mis-read every entry after the first.

## v1.19.0 — 2026-09-24

The component update. Nine amps are now built as circuits from their own drawings and run
that way by default, the cabinet stage becomes a physical speaker with a second mic, a
room and a rig selector, every factory preset was rebuilt for the new engine, the amp
panels got real cabinet artwork, and the suite runs on Windows and macOS as a plugin and a
standalone app.

### Added

- **Component builds for nine amps.** Gainzilla, Crunchy McCrunchFace, Beardo BE, Cali V, Diamond Plate, Tangerang, Chime Thirty, Blue Liner and Plexiglass each gain a second engine traced stage by stage from the amp's own schematic — every tube, coupling cap, tone network and the power section with its output transformer — instead of a fitted model. The **Component Build** switch is on by default in Hex Forge and the standalone amp; switch it off to get the previous models. An **Engine Quality** choice (Standard 4× oversampling / Eco 2×) trades a little top-end cleanliness for about half the CPU, and the shared triode evaluation is table-driven, which took 25–40 % off the component amps' CPU on a Pi.
- **Dynamic Load.** The component power sections can drive a large-signal speaker model as their load instead of a fixed impedance curve: the cone's excursion, back-EMF, inductance and coil heating pull on the output stage the way a real cab does. Transparent at small signal; on by default.
- **Citrus 200** (amp model 16) — the suite's second bass amp: a 200-watt British bass head with four 6550s and a long-tail-pair phase splitter, built from its drawings and carrying its own power section.
- **Three pedals rebuilt as circuits.** The Tube Chauffeur is the tube-driver circuit as drawn in its patent; the Uni-Verse is the photocell phase-shifter from the original 1969 unit's schematic (the stages are not all-pass filters, and the LFO shape falls out of the lamp); the Echo Primer is the tape echo's input preamp from its service manual.
- **The cabinet is a speaker now.** Speaker Drive gains a **Physical** mode — a state-based driver with excursion limits, suspension stiffening, cone breakup and cone cry, calibrated in volts against each amp's real output — plus **Cab Mic 2** (a second virtual mic with its own type, position, distance, blend, time alignment and polarity), a **Space** room density (an image-source room with early reflections), and a **Rig** selector: eleven factory rigs (mic pair, room and speaker settings per built-in cab) and your own rigs, saved on the device. The cab panel splits into CABINET | MIC & ROOM; when you load your own IR file the panel shows the basics only.
- **Amp head artwork.** Each amp model has its own covered cabinet — tolex, rolled edge, corner protectors — with the faceplate set into it, per-model knob families, and neon piping that cycles colour and lights the panel, in Hex Forge and the standalone amp.
- **Windows and macOS builds.** Hex Forge now ships as a VST3 (and AU on macOS) plus a standalone app, built by CI into a rolling "desktop-latest" pre-release on every push. The macOS app asks for microphone access so it can hear the interface.
- **Hex Forge lands on the last preset after a restart.** The cursor is remembered across power cycles.
- **The standalone amp** gets the Component Build, Dynamic Load and Engine Quality controls in an ENGINE group on its Power Amp tab.
- **pi-Stomp integration** (installer in build-tools): the LCD title shows the Hex Forge bank and preset name, the four footswitch LEDs follow the active preset, and each footswitch press selects its preset reliably. The hook re-applies itself after pi-Stomp updates.

### Changed

- **Every factory preset was rebuilt** for the component engines, the dynamic load and the cab rigs, from per-song rig research and then measured and loudness-levelled on the device. The high-gain Diamond Plate presets all use its Modern channel 3. Second amps and cabs are gone from every preset. The old nu-metal bank is now **MODERN ROCK**; Jungle Sleaze is the famous delayed intro; Regal Solo runs one tape echo; the output doubler is off everywhere. **This refresh rewrites the 80 factory slots** (banks 1–20). Presets you saved in those slots are replaced — save copies into banks 21–32 or take a backup before updating.
- **The amps were re-voiced against real-amp captures**: the 5150-class Red and Blue channels' presence and gain taper, the 2203-class low-mid hump, the Cali V channel 3 low-mids, per-mode voicing and power-amp drive on the Diamond Plate, the Tangerang's low resonance and dirty presence, the Beardo BE's presence shelf, four passes on the Tube Chauffeur, the Chime Thirty's Top Boost coupling (it had been wired to the Normal channel's cap), the Blue Liner's low end (a network traced to the wrong node), and the Plexiglass variac, which now glides instead of rebuilding the amp.
- **The Cali V graphic EQ is the real slider circuit**, not five peaking filters.
- **Standalone panels** were measured live and resized so nothing hangs off its plate: the amp is taller with the control groups centred on the faceplate, the cab fits MIC 2 open, modulation and nail grew, the rig selector and group titles are readable.
- **The standalone amp defaults Component Build and Dynamic Load to on**, matching Hex Forge.

### Fixed

- **A crash in the audio host.** Loading a cabinet while the audio thread was under an xrun burst could put the loader and the audio thread on the same convolver, corrupting memory; the host died with "double free or corruption". The cab now hands IRs and speaker rows across with a three-slot lock-free scheme. The same change cures a related bug where the physical speaker could keep the previous cabinet's cone on some presets.
- **A distorted tail on clean amps into the open-back cab.** The clean amps had no volts calibration of their own for the physical speaker and were driving a 30-watt open-back cone like a 210-watt head. Each shared-power-amp model now carries its measured calibration.
- **Aliasing and "ring modulator" artefacts** on several component builds (a missing Miller capacitance, an input limiter that had been a lookup table, oversampling that had been halved), the tone network being wiped every audio block on one amp, a bass-amp follower solve that fell into bisection under load, and the Tube Chauffeur's scratchiness.
- **Preset recalls read their rig once the IR lands**, and a blank or unchanged rename is ignored (a pedalboard snapshot could blank a preset's name).
- **Reverb in mono rigs** keeps its voiced wet/dry balance.
- **The preset compressor helper had attack and release backwards** on the generator side; every compressed preset was re-measured.

---

## v1.18.0 — 2026-09-05

The bass update — and a deep re-voice pass on the guitar side. The suite grows a
complete bass rig (amp, four cabs, a bass drive, two banks), the guitar amps were re-tuned
against a fresh reference measurement set, every song preset was rebuilt
from rig research and then hand-finished by ear, and there's a new germanium treble
booster.

### Added

- **Blue Liner — the suite's first BASS amp.** A classic all-tube 300-watt US bass head: Ultra-Lo and Ultra-Hi voicing switches, a 3-position mid selector (220 / 800 / 3000 Hz) and a six-tube output section. The Power Amp tube menu gains the **6550** to go with it.
- **Four bass cabinets** — the sealed 8×10 fridge with its 500–900 Hz grind, a deep ported 4×10 with a horn top, a tight mid-forward 2×10, and a round Motown-voiced 1×15 flip-top. Same synthesized measured-IR anatomy as the guitar cabs, and loudness-matched against them so switching between guitar and bass rigs doesn't jump.
- **Helsinki Grind** (drive model 10) — a modern parallel-blend bass preamp/overdrive. Mix is the pedal's signature Blend: a clean low path always runs underneath the drive path, phase-coherent, so you keep your fundamentals while the grind sits on top. Tone is the fat↔tight lever — how much low end gets into the distortion in the first place.
- **Treble Ranger** (drive model 11) — a germanium treble booster, the little box that lit the fuse on British blues-rock. It is modeled as the real circuit behaves rather than as a filter-plus-clipper: the transistor is biased near cutoff so the bite comes on with your picking, it loads your pickups the way the real one loads them, and its bias shifts under sustained playing. Runs a dark amp from clean-and-cutting into singing sustain.
- **LOW END** (bank 19) and **HELSINKI** (bank 20) — eight bass rigs built on the new amp, cabs and drive.
- **Output polarity, ON by default** — an absolute-phase inversion at the very end of the chain, after every block and the master output, which is how the big modelers ship. It's a global setting rather than a per-preset one, so it costs nothing to try: if your rig already inverts somewhere, switch it off.

### Changed

- **Amps re-tuned against reference measurements.** A large new measurement set was fitted amp by amp and the models re-tuned to it. Re-voiced: **Gainzilla** (the modern high-gain head, rebuilt around its real master-volume taper), **Crunchy McCrunchFace** (the British hot-rod), **Diamond Plate** across its Modern modes and then its Vintage ones, **Cali V** on the Mark-IV mode — with its third-channel knob laws re-lawed to the real dials, which had been running about twice as hot (every saved preset is sound-preserved through that change, so your dial positions still mean what they meant) — and **Chime Thirty**. In each case the old hand-fitted correction layers came out and the underlying model does the work now.
- **Two amps measured correct and were left alone.** **Tangerang** came back the closest match in the suite and **Beardo BE**’s model measured right as it stood — tonestack, mid taper and low-end voicing all confirmed. Neither was touched tonally; both only gained a corrected gain-knob loudness law, so the dial now spans the range the real amp spans.
- **Chime Thirty's rail now ramps with gain.** The clean end had been running honest-to-measurement but too hot, so the amp's own floor was already saturating; the supply now ramps the way the real amp's does, and the dirt presets were re-matched at their honest dial positions.
- **Every song preset rebuilt — then hand-finished.** All 53 song presets were re-authored from per-song rig documentation against the re-voiced amps. The rebuild was then A/B'd by ear: the ones that landed were kept, the rest were rolled back to their previous definitions and hand-fixed on the device, and those hand dial-ins are baked in here verbatim.
- **Quieter high-gain amps.** Two amps were amplifying the input noise floor with a boost applied late in the chain, where it lifts hiss and hum along with the tone: Gainzilla's power-amp presence and Chime Thirty's input stage span both came down. Measured *better* against the reference takes without them — the noise was a tax with nothing bought.
- **The FRFR Output Voice now defaults to OFF.** It's a rig-specific voicing for full-range flat-response cabs, so it shouldn't be in the path until you ask for it. Headphone and amp-in-the-room users get the unvoiced signal by default; if you had it on, it stays on.
- **Fuzz Wall** now runs the Red Bear voicing — the Green-Russian-era bass-heavy Muff, which is what that wall of sound actually wants.
- **Bass cabs and the Studio cab voice.** The Studio 'recorded' chain brackets at 78 Hz, which is voiced for guitar and eats a bass cab's fundamentals; the bass presets ship on the Room voice, and the plugin description now says so.

### Fixed

- **The Tube Chauffeur ran as a Green Man inside Hex Forge.** A drive-model lookup table was one entry short — the entry had been swallowed into the comment above it — so selecting the tube-driver voicing in the Forge silently gave you the green mid-hump overdrive instead. It has been wrong since that model shipped. The standalone Drive plugin was always correct.
- **A factory knob-law remap could rewrite your own presets.** A sound-preserving remap that ships with a re-voiced amp was being applied to every slot with that amp selected, including user-built ones, where it silently moved their knobs. It is now scoped to factory slots only.
- **Presets saved before this release** pick up the new output polarity correctly instead of loading it as zero.

---

## v1.17.0 — 2026-08-23

The correct-tubes update: every amp re-measured against the player's own DI takes, the right power tubes in the right amps, a room-speaker voice, and a big chain-UI overhaul.

### Added

- **Auto-Calibrate wizard** — a guided three-phase input measurement (silence / hands-on / hard strums) that measures *your* guitar and interface, then applies a global input-trim and gate-floor offset so every preset's gates and levels sit right on your rig. Global layer: your presets are untouched.
- **Output Voice (FRFR toggle)** — a one-switch room-speaker voice for FRFR cabs (tuned in-room on a Tone Master FRFR-10): de-close-mics the signal with low-cut, proximity, presence and fizz controls you can fine-tune, and auto-mutes the stereo doubler for single-speaker rigs. Headphones stay exactly as before when it's off.
- **Tube Chauffeur** (drive model 10) — a Butler Tube Driver: starved-triode warm overdrive and clean boost, the classic Gilmour sustain staple. Boosts even with the gain floored.
- **Green Man boost trick** — the TS-style overdrive now has the real circuit's gain floor, so the classic "drive at zero, level up" clean-push setting actually pushes, like the pedal it honors.
- **Advanced panel** — Auto-Limit, Mono, Doubler, FRFR voice and Calibrate moved off the toolbar into a gear-button panel; the tuner button moved up top.
- **Chain drag-and-drop overhaul** — dragging a block shows a live dashed landing slot that the row smoothly parts around; drag from the palette to place, drag out to remove. New jack-style IN/OUT end caps: the OUT jack LED is now the plugin on/off switch and the IN jack LED is a master mute.

### Changed

- **Correct power tubes everywhere (measured improvement, not just trivia):** the Fender-family amp now runs 6V6 output tubes (it had been on 6L6 its whole life — a brand-new 6V6 model was built for it), and the modern high-gain head moved from EL34 to its real 6L6. Every capture fit improved with the correct glass. The Power Amp tube menu gains 6V6.
- **DI re-measure + capture-anchored amp fits:** all NAM-checked amp models were re-measured against the player's own DI takes with new perceptual error metrics, then the outliers were re-voiced and A/B'd by ear before baking: the modern high-gain head, the British hot-rod crunch pair, and the rectifier head's modern channel (fit against a newly-sourced trusted capture ladder).
- **Doom Daddy (Model T-style amp) re-voice** — the missing harmonic overtones are back: a preamp even-harmonic exciter, opened-up top end (air extended 3 kHz → 16 kHz), presence bite and a tighter low end. No more woof.
- **Dual-amp presets on mono rigs** — fixed the "wooshing" comb-filter artifacts when running two amps into a mono output: the cab room collapses to a single bank in mono, and the second-rig blend gains a low-cut that kills the low-mid cancellation pump.
- **Nine Inch Nails bank pass** — all four presets re-compensated after the amp re-voices moved them (the direct/no-cab industrial stab chain, the dark wall layer, the rectifier crunch and the fuzz-spit re-amp are back at their approved operating points).
- **Lower latency** — the recommended JACK period drops to 32 frames (~2.3–2.6 ms round trip), verified xrun-free on the Pi 5.
- **Cheaper cabs** — the IR convolver is NEON-vectorized on ARM; long IRs cost about half what they did.
- **Even-harmonic power-amp warmth.** Several amps now generate the 2nd/4th-order even content of a real driven push-pull output — a rounder, warmer, more "tube" character on power-amp breakup instead of the odd-order digital edge, tuned against real amp captures. Two mechanisms, applied per amp only where they measurably match the capture: push-pull **duty asymmetry** on the tighter lunchbox / rectifier / chime-class amps, and a **post-distortion even generator** on the boutique hot-rod and British lead-crunch heads. Loudness-neutral — no preset levels shift.
- **Beardo BE (hot-rod head) re-voice** — three fixes:
  - **HBE channel** no longer hollows out or drops on hard hits (a preamp stage was saturating into fundamental-cancellation); it now stays solid and clearly steps up over the BE channel.
  - **Consistent gain** — the amp used to clean up when you played softly; it now saturates evenly whether you dig in or play gently, matching the real amp's flat saturation.
  - **Tamed the high-end hiss** on the note decay — the decay noise-conditioning is darker and keyed to the amplified noise floor, so hiss fades into the tail instead of hanging on top of it.

### Fixed

- **PatchStorage listing descriptions** — the descriptions now live inside each plugin (plugin-level metadata), so re-listing a plugin publishes its real description instead of wiping it to "No description available." (This is what actually happened to the Modulation listing — the v1.16.0 re-list attempt couldn't work without this.) The Modulation copy also now credits all nine voicings.
- **Chain reorder storm** — moving a block could send every open browser (phones included) into an endless flashing re-order loop that survived refresh; fixed on both sides (a stale slot ceiling on the EQ block widened, and the UI no longer echoes position writes back at the host).
- **User cabs silently falling back to factory** — a pedalboard save could mangle the built-in synthetic-cab paths into broken links, silently swapping every non-factory cab to the default V30; cabs now load by name and survive saves.
- **High-gain hum gates re-tuned to the measured rig floor** and a self-oscillating feedback squeal in the chime amp's negative-feedback loop eliminated.

---

## v1.16.0 — 2026-08-03

The high-voltage update: the variac now runs the other way, the modern high-gain amp gets a full overhaul, and a round of player-tuned preset reworks.

### Changed

- **Variac now overvolts** (Plexiglass amp): the variac has been re-modeled to *raise* the effective wall voltage toward the "magic" setting instead of dropping it — as you turn it up it gets louder, more saturated and tighter in the low end, tuned against a real variac'd-Plexi capture set. Off is still bit-identical to the stock voicing. Note: this reverses the previous dropped-voltage direction, so any rig using the variac will read more aggressive than before.
- **Gainzilla (modern high-gain amp) overhaul:**
  - **Rhythm channel** gained its missing fourth gain stage — it was short on gain and about 12 dB quieter than the lead channel; it now has proper drive on tap and matches the lead channel's level, while still sitting a step below it.
  - **Lead channel** gained a fast, dynamic supply-sag node so it breathes and responds to pick attack instead of feeling stiff.
  - Fixed a **cranked-bass stutter** — with the bass maxed, the amp could run away into a stuttering over-saturation and drop level.
  - Tamed the amplified high-frequency **hiss** on the high-gain rhythm presets.

### Presets

- Player-tuned reworks around the new amps and effects: the **brown-sound '84** rig rebuilt on the overvolt variac; a British-clean "Wall" lead moved off a poorly-matched drive onto a snarling rodent-style distortion; the modern **djent** rhythm presets voiced to specific prog-metal songs plus a groove-metal chug; a new **drone/doom monolith** (a wall of dropped, dark, octave-tinged saturation with a blooming ambient wash) replacing the old doom preset; and the treble-boosted **chime-amp** lead/rhythm pair re-tuned for the current jangle-amp voicing.

### Fixed

- The **Modulation** plugin's PatchStorage listing was missing its description — it now publishes.
- Removed a duplicate author credit from the Amp listing description.

---

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
