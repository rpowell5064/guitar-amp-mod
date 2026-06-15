# Building Hex Chain for the MOD Dwarf (mod-plugin-builder)

The pi-Stomp release tarball (`guitaramp-suite-vX-aarch64.tar.gz`) is **not** usable on a
MOD Dwarf: it's built on Ubuntu (modern glibc/libstdc++) and the Dwarf's buildroot MOD OS
has an older runtime, so the `.so` files won't load (`GLIBC_2.xx / GLIBCXX_3.4.xx not found`).
The Dwarf must be built with **mod-plugin-builder**, which cross-compiles against the Dwarf's
exact sysroot. This also produces the bundle format used by the MOD Audio plugin store.

> **Architecture:** Dwarf = NXP i.MX8M, quad Cortex-A53, ARMv8-A/aarch64, ≤1.5 GHz, ~1 GB RAM.
> ISA-compatible with our `-march=armv8-a` portable build.
>
> **Reality check (CPU):** the A53 is ~3–5× weaker per core than the Pi 5's A76. The heavy
> plugins — Hex Forge (whole-chain super-block), NAM inference, cab FFT, the Newton-solved
> Sunn preamp (4.5× cost), 4× oversampling — likely **won't run in real-time** on a Dwarf.
> Expect the lighter ones (gate, comp, drives, amp-without-NAM) to be the usable set. Treat a
> first build as "does it load + which plugins fit the CPU", not "ship the whole suite".

## Prerequisites (one-time, needs a Linux environment)

This machine has **no WSL/Docker** — install them first. `wsl --install` needs admin rights
and a reboot, so run these yourself (e.g. type `! wsl --install` in this session, or in an
elevated PowerShell):

```powershell
wsl --install -d Ubuntu          # admin + reboot; then create the Linux user when it boots
```

Inside the Ubuntu (WSL2) shell:

```bash
sudo apt update && sudo apt install -y build-essential git rsync unzip wget cmake \
    python3 libncurses-dev bc

git clone https://github.com/mod-audio/mod-plugin-builder.git ~/mod-plugin-builder
cd ~/mod-plugin-builder
./bootstrap.sh                   # builds the ct-ng toolchain + buildroot — ~1 hr, into ~/mod-workdir
```

## Add this package and build

```bash
mkdir -p ~/mod-plugin-builder/plugins/package/guitaramp-suite
cp <repo>/build-tools/moddwarf/guitaramp-suite.mk \
   ~/mod-plugin-builder/plugins/package/guitaramp-suite/

cd ~/mod-plugin-builder
./build moddwarf guitaramp-suite
# → ~/mod-workdir/moddwarf/plugins/guitaramp-suite.lv2
```

## The NAM-core gotcha (the part that needs iteration)

The build pulls `NeuralAmpModelerCore` (pinned to `d65cf21`, the commit that has
`nam::DSP::ResetAndPrewarm` — `5edb0ba` does **not** and fails) via CMake `FetchContent` at
**configure** time. buildroot's build phase is often offline. Three options, in the `.mk`:

- **A — network at build (try first):** leave the recipe as-is; FetchContent just fetches it.
- **B — offline build:** uncomment the `POST_DOWNLOAD` clone hook + the
  `FETCHCONTENT_SOURCE_DIR_NEURALAMPMODELERCORE` override (the project already honours that
  variable — it's how local/CI builds reuse a NAM core).
- **C — fully reproducible:** add NeuralAmpModelerCore (recursive, incl. its Eigen submodule)
  as a git submodule of this repo and use `MOD_PLUGIN_BUILDER_DOWNLOAD_WITH_SUBMODULES`
  (the pattern `aidadsp-lv2` uses). Cleanest for the store, but a repo change — do it on a
  branch and keep the Pi/CI `FetchContent` path working (gate the submodule use behind a
  CMake option or the `FETCHCONTENT_SOURCE_DIR` override).

## Deploy to a Dwarf for testing

```bash
# Dwarf has SSH (root@<dwarf-ip>, MOD OS). Side-load the cross-built bundle:
scp -r ~/mod-workdir/moddwarf/plugins/guitaramp-suite.lv2 root@<dwarf-ip>:/root/.lv2/
ssh root@<dwarf-ip> 'systemctl restart mod-host mod-ui'   # or reboot
```

Then in the Dwarf web UI, add a **light** plugin first (e.g. the gate) to confirm it loads
(rules out any remaining ABI issue), then check CPU on a heavier one.

## Status

- `guitaramp-suite.mk` here is **authored but UNVERIFIED** — no buildroot/Dwarf was available
  to test on. Expect to iterate on the NAM-core option (A/B/C) and the CPU-headroom question.
- Official store submission (Channel 2) builds on this same recipe.
