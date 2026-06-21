# PatchStorage rollout — Hex Chain

Ship the suite to [PatchStorage.com](https://patchstorage.com/platform/lv2-plugins/)
as **individual per-plugin listings** (11 plugins + the **Hex Forge** flagship),
across all three LV2 build targets PatchStorage supports. Each plugin gets its own
search result / likes / downloads, which is the point — maximum discoverability.

## Target matrix

| PatchStorage target | Reaches | Arch / build | NAM |
|---|---|---|---|
| `linux-amd64` | Desktop LV2 hosts (Carla, Ardour, Zrythm, Reaper+LV2, MOD-on-desktop) | x86-64-v2, native | ✅ on |
| `rpi-aarch64` | Pi 4/5, pi-Stomp, 64-bit MODEP, Aida DSP OS, MOD-on-Pi | armv8-a, native | ✅ on |
| `patchbox-os-arm32` | 32-bit MODEP / Patchbox OS on Pi 3/4 (largest hobbyist base) | armv7-a+neon hard-float | ❌ off |

NAM (Neural Amp Modeler) is the heaviest DSP and depends on NamCore + Eigen. On the
32-bit target it would never run in realtime, so it is **hidden**: NAM is still
built into the binary (a true compile-out is unsafe — `AmpBlock.h`/`NamOverdrive.h`
embed `NamModel` by value, so stubbing it only in the plugin TUs would be an ABI
mismatch vs GuitarAmpSim), but the "Neural (NAM)" model option and the NAM file
pickers are **stripped from the TTL** at packaging time (`split_bundle.py
--strip-nam`), so the 32-bit UI never exposes NAM. All algorithmic amp/drive/cab
models stay intact.

## Files

- `split_bundle.py` — splits the built `guitaramp-suite.lv2` into per-plugin
  `<name>.lv2` bundles (own `manifest.ttl`, `.so`, `<name>.ttl`, `modgui-<name>`);
  with `--strip-nam` it removes the NAM params / writables / "Neural (NAM)"
  scalePoints from amp/drive/cab/hexforge (keeps cab/hexforge `#irfile`).
- `plugins_meta.json` — **single source of truth** for listing metadata (title,
  categories, tags, short/long description, NAM flag). Edit this when copy changes.
- `generate_metadata.py` — renders `plugins_meta.json` into, per target:
  - `plugins.json` (the uploader's bundle-keyed override: source_code_url /
    donate_url / license / categories), and
  - `LISTINGS.md` (paste-ready title + description, with the right NAM note).
- `build_target.sh` — configure → build → install → split, for one target.

## Build all targets locally

```bash
# 64-bit, NAM on (run each on the matching arch, or in a matching container)
bash build-tools/patchstorage/build_target.sh linux-amd64
bash build-tools/patchstorage/build_target.sh rpi-aarch64

# 32-bit, NAM off — either cross-compile with an armhf sysroot:
bash build-tools/patchstorage/build_target.sh patchbox-os-arm32 /path/to/armhf-sysroot
# …or natively inside an armv7 container:
docker run --rm -v "$PWD:/src" -w /src arm32v7/debian:bookworm bash -c \
  'apt-get update && apt-get install -y cmake build-essential pkg-config lv2-dev python3 git && \
   bash build-tools/patchstorage/build_target.sh patchbox-os-arm32'

# then the listing metadata for each target:
python3 build-tools/patchstorage/generate_metadata.py --target <target> \
        --out dist/patchstorage/<target>
```

Output: `dist/patchstorage/<target>/<plugin>.lv2/` + `plugins.json` + `LISTINGS.md`.

CI (`.github/workflows/release.yml`, job `patchstorage`) does all of this on every
tagged release / manual dispatch and uploads each target as an artifact.

## Upload to PatchStorage

Upload stays a manual, credentialed step (it needs your PatchStorage account):

```bash
git clone https://github.com/patchstorage/patchstorage-lv2-uploader
# copy the built bundles into the uploader's per-target layout:
#   patchstorage-lv2-uploader/plugins/<target>/<plugin>.lv2/
# merge our generated plugins.json into the uploader's plugins.json
python ./uploader.py prepare all
python ./uploader.py push all --username <patchstorage_username>
```

`LISTINGS.md` has the description text to paste; screenshots come from each
plugin's `modgui-<name>/screenshot-<name>.png`.

## Adding a new device / target

1. If it's one of the three existing PatchStorage targets, nothing new is
   needed — it's already built. If it's a genuinely new ABI:
2. Add a toolchain file (mirror `armhf-toolchain.cmake` / `aarch64-toolchain.cmake`)
   and an arch branch in the top-level `CMakeLists.txt` (`GUITARAMP_ARCH_FLAGS`).
3. Add a `case` in `build_target.sh` (set the arch profile, `--strip-nam` if the
   target should hide NAM).
4. Add a matrix entry in `.github/workflows/release.yml` (job `patchstorage`).
5. If NAM-disabled, add the target name to nothing else — `generate_metadata.py`
   keys NAM off any target not in its `NAM_TARGETS` set (update that set if the
   new target *does* run NAM).

Keep `plugins_meta.json` as the one place listing copy lives; never hand-edit the
generated `plugins.json` / `LISTINGS.md`.
