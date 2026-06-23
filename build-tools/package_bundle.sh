#!/usr/bin/env bash
# Build a portable aarch64 redistribution tarball of the Hex Chain LV2 suite.
# Run on an aarch64 host (e.g. the pi-Stomp):
#     bash build-tools/package_bundle.sh [VERSION]
# Produces: dist/guitaramp-suite-v<VERSION>-aarch64.tar.gz
set -euo pipefail

VER="${1:-1.0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD="build-dist"
STAGE="$(mktemp -d)"
DIST="$ROOT/dist"
PKG="guitaramp-suite-v${VER}-aarch64"

cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

echo "== configure (portable: -march=armv8-a) =="
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DGUITARAMP_PORTABLE=ON >/dev/null

echo "== build =="
# Memory-aware parallelism: the NAM/Eigen TUs are RAM-hungry (~1.5 GB each at -O3),
# so -j nproc OOM-kills cc1plus on a 2 GB Pi. Scale jobs to available RAM.
JOBS="${JOBS:-}"
if [ -z "$JOBS" ]; then
    memkb=$(awk '/MemTotal/{print $2}' /proc/meminfo 2>/dev/null || echo 0)
    if   [ "$memkb" -lt 3000000 ]; then JOBS=1
    elif [ "$memkb" -lt 6000000 ]; then JOBS=2
    else JOBS=$(nproc); fi
fi
echo "   building with -j$JOBS (MemTotal=$((memkb/1024)) MB)"
cmake --build "$BUILD" -j"$JOBS"

echo "== install to staging =="
cmake --install "$BUILD" --prefix "$STAGE" >/dev/null
BUNDLE="$STAGE/lib/lv2/guitaramp-suite.lv2"

echo "== sanity-check staged bundle =="
so=$(find "$BUNDLE"  -maxdepth 1 -name '*.so'        | wc -l)
ttl=$(find "$BUNDLE" -maxdepth 1 -name '*.ttl'       | wc -l)
gui=$(find "$BUNDLE" -maxdepth 1 -type d -name 'modgui-*' | wc -l)
# Expected plugin count = manifest plugin stanzas (one lv2:binary each). The bundle
# must hold one .so + one modgui dir per plugin, and one .ttl per plugin + manifest.ttl.
# Derived (not hardcoded) so adding/removing a plugin never trips this.
want=$(grep -c 'lv2:binary' "$BUNDLE/manifest.ttl")
echo "   .so=$so  ttl=$ttl  modgui=$gui   (expect $want / $((want+1)) / $want)"
if [ "$want" -lt 1 ] || [ "$so" -ne "$want" ] || [ "$ttl" -ne $((want+1)) ] || [ "$gui" -ne "$want" ]; then
    echo "!! staged bundle incomplete — aborting"; exit 1
fi
# every modgui dir must carry its rendered art + stylesheet (+ a branding logo)
for d in "$BUNDLE"/modgui-*; do
    m=$(basename "$d" | sed 's/^modgui-//')
    for f in "stylesheet-$m.css" "icon-$m.html" "screenshot-$m.png" "thumbnail-$m.png"; do
        [ -f "$d/$f" ] || { echo "!! missing modgui-$m/$f"; exit 1; }
    done
    # branding image: logo.jpg on the pedals, logo.png on the Hex Forge container
    [ -f "$d/logo.jpg" ] || [ -f "$d/logo.png" ] || { echo "!! missing modgui-$m logo"; exit 1; }
done
echo "   modgui assets present for all $gui plugins"

echo "== assemble tarball =="
rm -rf "$DIST/$PKG"; mkdir -p "$DIST/$PKG"
cp -r "$BUNDLE" "$DIST/$PKG/"
# Factory pedalboard(s) — ship Hex Forge pre-wired so the pi-Stomp footswitches
# (CC 60-63 -> A/B/C/D) work on load with no MIDI setup by the user.
if [ -d "$ROOT/pedalboards" ]; then
    cp -r "$ROOT/pedalboards" "$DIST/$PKG/pedalboards"
    echo "   included $(find "$ROOT/pedalboards" -maxdepth 1 -name '*.pedalboard' | wc -l) factory pedalboard(s)"
fi
cp "$ROOT/build-tools/pkg/install.sh"  "$DIST/$PKG/install.sh"
cp "$ROOT/build-tools/pkg/README.txt"  "$DIST/$PKG/README.txt"
cp "$ROOT/LICENSE"                     "$DIST/$PKG/LICENSE"
chmod +x "$DIST/$PKG/install.sh"
( cd "$DIST" && tar czf "$PKG.tar.gz" "$PKG" )
rm -rf "$DIST/$PKG"

echo "== DONE =="
ls -lh "$DIST/$PKG.tar.gz"
echo "Distribute dist/$PKG.tar.gz — users extract and run install.sh on their Pi."
