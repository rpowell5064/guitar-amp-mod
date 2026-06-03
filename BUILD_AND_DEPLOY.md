# GuitarAmp Suite — Build & Deploy for pi-Stomp v3

LV2 port of the GuitarAmpSimulator VST, packaged as a single MOD bundle
(`guitaramp-suite.lv2`) with 9 plugins: gate, comp, drive, modfx, delay,
reverb, amp, cab, utility.

## Target hardware (confirmed)

| Item        | Value                                                |
|-------------|------------------------------------------------------|
| Device      | pi-Stomp v3                                           |
| Board       | **Raspberry Pi 5, 2 GB** (BCM2712, Cortex-A76, aarch64) |
| OS          | 64-bit Raspberry Pi OS Lite                           |
| Host        | MOD (`mod-host` + `mod-ui`), web UI at `pistomp.local`|
| User        | `pistomp`                                             |
| Plugin dir  | `/home/pistomp/.lv2/` (symlinked from `.../data/.lv2`)|

The Cortex-A76 build flags in `CMakeLists.txt` and `aarch64-toolchain.cmake`
(`-march=armv8.2-a+dotprod -mtune=cortex-a76`) are **correct for the Pi 5**.
⚠️ They are NOT compatible with a Pi 4/3 — those would crash with SIGILL. Only
build with these flags for a Pi 5 target.

---

## Prerequisite: populate the DSP submodule

The `deps/guitar-amp-simulator/` directory holds the DSP engine
(`GuitarAmpSim` + `NamCore`) and **must be populated before building** — it is
currently empty. The source lives in the sibling `GuitarAmpSimulator` repo.

If this folder is a git repo with the submodule registered:

```bash
git submodule update --init --recursive
```

Otherwise, copy/clone the DSP source into place so that
`deps/guitar-amp-simulator/CMakeLists.txt` exists.

> The DSP build uses `FetchContent` to clone `NeuralAmpModelerCore` from GitHub,
> so the build machine needs **internet access** the first time. (NAM is linked
> but not exercised by these plugins — see Known limitations.)

---

## Option A — Native build on the Pi 5 (recommended)

Simplest path: no sysroot, no cross toolchain. The Pi 5 compiles this in a few
minutes. `GuitarAmpSim` uses `-march=native`, which on the Pi 5 resolves to the
correct Cortex-A76 target automatically.

```bash
# 1. Build deps (one-time). libsndfile is NOT required — it was removed.
sudo apt update
sudo apt install -y build-essential cmake pkg-config git lv2-dev

# 2. Configure + build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

# 3. Stage the bundle, then drop it into the pi-stomp plugin dir
cmake --install build --prefix "$PWD/stage"
cp -r stage/lib/lv2/guitaramp-suite.lv2 /home/pistomp/.lv2/
```

## Option B — Cross-compile from a Linux host (or WSL2)

The supplied `aarch64-toolchain.cmake` targets aarch64 with a Pi 5 sysroot.
This needs a Linux host — it will not work from native Windows. Use WSL2 or a
Linux box.

```bash
# Requires: gcc-aarch64-linux-gnu, g++-aarch64-linux-gnu, and a Pi OS sysroot
# containing lv2-dev (copy /usr from the Pi, or build with multistrap).
cmake -S . -B build-arm64 \
  -DCMAKE_TOOLCHAIN_FILE=aarch64-toolchain.cmake \
  -DCMAKE_SYSROOT=$HOME/pi5-sysroot \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm64 -j

cmake --install build-arm64 --prefix "$PWD/stage"
# Copy stage/lib/lv2/guitaramp-suite.lv2 to /home/pistomp/.lv2/ on the device
# (scp, rsync, USB, etc.)
scp -r stage/lib/lv2/guitaramp-suite.lv2 pistomp@pistomp.local:/home/pistomp/.lv2/
```

---

## Validate the bundle (do this before trusting it on stage)

On the Pi (or any Linux box with the LV2 tools):

```bash
sudo apt install -y lv2-dev lilv-utils   # provides lv2ls; lv2lint is separate

# Bundle/TTL sanity:
sord_validate /home/pistomp/.lv2/guitaramp-suite.lv2/*.ttl

# Confirm the host can enumerate all 9 plugin URIs:
LV2_PATH=/home/pistomp/.lv2 lv2ls | grep guitaramp-suite

# Optional, stricter lint (install lv2lint if available):
lv2lint -s lv2_descriptor http://.../guitaramp-suite/amp   # repeat per URI
```

You should see all nine `https://rpowell5064.github.io/guitaramp-suite/<name>`
URIs listed.

## Load on the device

After copying the bundle, make `mod-ui` rescan plugins — restart the pi-stomp /
mod service or reboot the Pi, then open `http://pistomp.local` and search the
plugin list for **brand "GuitarAmp Suite"**. The nine plugins should appear with
MOD's generic control UI.

---

## Amp plugin: NAM model + power amp

The `amp` plugin mirrors the VST's amp section:

- **Model** selector has 6 entries: Fender, Marshall, EVH, **Neural (NAM)**,
  Sunn, Rockerverb.
- Selecting **Neural** and loading a model: mod-ui shows a file browser for the
  "NAM Model" parameter (`mod:fileTypes "nam,nammodel"`). Pick any `.nam` file;
  it's loaded on the LV2 **worker thread** and swapped in without an audio-thread
  allocation. Selecting Neural before loading a file = clean passthrough.
- The **power-amp stage** (`PowerAmpProcessor`) runs after the amp with its own
  controls (Power Tube, PA Presence/Depth/Sag/Master/NFB/Resonance, Air Feel) and
  a **Power Amp Bypass** toggle. It is **auto-bypassed whenever Neural is active**
  (a NAM capture already includes the power amp), matching the VST.
- **PA Auto** (default ON): the power-amp voicing follows the selected amp's
  `getDefaultsForModel()` values and the power tube auto-switches per model
  (Fender/Sunn → 6L6GC, Marshall/EVH/Rockerverb → EL34). Turn PA Auto **off** to
  drive the `pamp_*` knobs manually. (LV2 plugins cannot move the host's knobs
  like the VST did, so this is a mode toggle rather than the knobs snapping to
  new positions; in Auto mode the on-screen `pamp_*` knob positions are ignored.)
- The loaded model path is persisted via LV2 `state:interface`, so it survives
  pedalboard save/load.

> NAM models are expected at 48 kHz — run the pi-Stomp engine at 48 kHz.

## Known limitations (not blockers, but be aware)

1. **Model switching is fully RT-safe.** Both NAM loads *and* amp-model changes
   (incl. Sunn/Rockerverb oversampling-wrapper allocation) are built on the LV2
   worker thread and swapped in with a single pointer store — the audio thread
   never allocates. There's a sub-10 ms window after a switch where the previous
   model keeps playing until the new instance is ready (no glitch, just latency).

2. **PA Auto can't visually move the knobs.** With PA Auto on (default), the
   power amp uses per-model defaults + auto tube, but — unlike the VST — the
   on-screen `pamp_*` knobs don't snap to the new values (LV2 plugins can't write
   their own control input ports). The sound is correct; the knobs just don't
   reflect it until you switch PA Auto off and take manual control.

3. **No custom MOD GUI.** There is no `lv2/modgui` bundle, so plugins use MOD's
   auto-generated control UI (including the NAM file browser). Functional, not branded.

4. **NAM fetched from `main` (unpinned).** The DSP lib's CMake uses
   `GIT_TAG main` for NeuralAmpModelerCore. Pin it to a specific commit for
   reproducible builds.

5. **Stereo I/O.** Plugins expose L/R in and out. Guitar input is mono; in MOD
   you'll typically feed the mono input to both, or just use the left channel.

6. **Must be compiled and validated on-device.** These LV2 sources (especially
   the amp's atom/worker/state file-loading) were written but not compiled in
   this environment — build on the Pi 5 and run `sord_validate` + `lv2ls` before
   relying on them live.
