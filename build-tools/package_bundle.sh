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
cmake --build "$BUILD" -j"$(nproc)"

echo "== install to staging =="
cmake --install "$BUILD" --prefix "$STAGE" >/dev/null
BUNDLE="$STAGE/lib/lv2/guitaramp-suite.lv2"

echo "== sanity-check staged bundle =="
so=$(find "$BUNDLE"  -maxdepth 1 -name '*.so'        | wc -l)
ttl=$(find "$BUNDLE" -maxdepth 1 -name '*.ttl'       | wc -l)
gui=$(find "$BUNDLE" -maxdepth 1 -type d -name 'modgui-*' | wc -l)
echo "   .so=$so  ttl=$ttl  modgui=$gui   (expect 9 / 10 / 9)"
if [ "$so" -ne 9 ] || [ "$ttl" -ne 10 ] || [ "$gui" -ne 9 ]; then
    echo "!! staged bundle incomplete — aborting"; exit 1
fi
# every modgui dir must carry its rendered art + stylesheet
for m in amp cab drive delay gate comp modfx reverb utility; do
    for f in "stylesheet-$m.css" "icon-$m.html" "screenshot-$m.png" "thumbnail-$m.png" "logo.jpg"; do
        [ -f "$BUNDLE/modgui-$m/$f" ] || { echo "!! missing modgui-$m/$f"; exit 1; }
    done
done
echo "   modgui assets present for all 9 pedals"

echo "== assemble tarball =="
rm -rf "$DIST/$PKG"; mkdir -p "$DIST/$PKG"
cp -r "$BUNDLE" "$DIST/$PKG/"
cp "$ROOT/build-tools/pkg/install.sh"  "$DIST/$PKG/install.sh"
cp "$ROOT/build-tools/pkg/README.txt"  "$DIST/$PKG/README.txt"
cp "$ROOT/LICENSE"                     "$DIST/$PKG/LICENSE"
chmod +x "$DIST/$PKG/install.sh"
( cd "$DIST" && tar czf "$PKG.tar.gz" "$PKG" )
rm -rf "$DIST/$PKG"

echo "== DONE =="
ls -lh "$DIST/$PKG.tar.gz"
echo "Distribute dist/$PKG.tar.gz — users extract and run install.sh on their Pi."
