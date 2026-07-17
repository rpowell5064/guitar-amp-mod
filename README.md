# Hex Chain — guitar amp & effects suite (LV2 / MOD)

A complete guitar rig as LV2 plugins for the MOD / pi-Stomp platform: five
algorithmic amp models + power-amp stage, a multi-era fuzz, drive/boost pedals,
compressor, noise gate, modulation, delay, reverb, cabinet (IR), an input-trim
utility — and **Hex Forge**, a single prewired, reorderable super-block that hosts
the whole chain in one plugin. Amp and Drive can also load your own
**NAM (Neural Amp Modeler)** captures (both the legacy and new `.nam` formats);
the Cabinet is pure IR/convolution (built-in cabs + your own `.wav` IRs).

All DSP is original work; amp/pedal model names are trademark-clean parody names.

## Licensing — open source **and** commercial

This project is **dual-licensed** (see [`LICENSING.md`](LICENSING.md)):

- **GPL-3.0-or-later** ([`LICENSE`](LICENSE)) — free for the community to use,
  study, modify, and share. Any product that incorporates this code must also be
  released as open source under the GPL.
- **Commercial license** — to ship this in a closed-source/proprietary product
  without the GPL's copyleft obligations. **Contact Ryan Powell
  <rpowell5064@gmail.com>** to purchase.

This keeps the project genuinely open while ensuring companies either contribute
back under the GPL or buy a commercial license — they can't take it closed for
free. Third-party components are listed in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md); contributing terms are in
[`CONTRIBUTING.md`](CONTRIBUTING.md).

© 2026 Ryan Powell.

## Building

CMake project; the DSP engine is the `deps/guitar-amp-simulator` submodule, and the
NAM core / Eigen are fetched at configure time. Targets install into one bundle,
`guitaramp-suite.lv2`. See `build-tools/` for the pi-Stomp deploy and offline
analysis tooling. (On a 2 GB Pi, build the NAM-linked targets one at a time with
`-j1`.)

## Distribution

- **pi-Stomp / MOD (tarball):** the portable aarch64 bundle is published on GitHub
  Releases — extract on your Pi and run `install.sh`
  (`bash build-tools/package_bundle.sh <ver>` builds it locally).
- **PatchStorage.com:** the suite also ships as individual per-plugin listings
  (plus the Hex Forge flagship) for the three LV2 build targets PatchStorage
  supports — `linux-amd64` (desktop hosts), `rpi-aarch64` (Pi 4/5, 64-bit MODEP,
  Aida DSP OS, MOD), and `patchbox-os-arm32` (32-bit MODEP on Pi 3/4). NAM is
  exposed on the two 64-bit targets and hidden on 32-bit (stripped from the UI;
  the algorithmic models remain). See [`build-tools/patchstorage/README.md`](build-tools/patchstorage/README.md)
  for the build → metadata → upload runbook and how to add a new device/target.
