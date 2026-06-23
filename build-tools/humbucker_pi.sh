#!/usr/bin/env bash
# Build + deploy the Input Trim humbucker port: standalone utility gains the
# single-coil->humbucker voicing (shared lv2/common/PickupVoicer.h), and Hex Forge
# is refactored to use that same shared header. Builds BOTH (utility is non-NAM,
# safe at -j2; hexforge whole-archives NamCore so build it ALONE at -j1 — two
# NAM-linked targets at -j2 has OOM'd this 2 GB Pi). Symbol-gates both, folds the
# bundle, restarts MOD.
set -uo pipefail
cd ~/guitar-amp-mod || exit 1
B=~/.lv2/guitaramp-suite.lv2

echo "=== reconfigure ==="
cmake -S . -B build >/dev/null 2>&1 || { echo "!! cmake configure failed"; exit 1; }

echo "=== build guitaramp_utility (-j2, non-NAM) ==="
cmake --build build --target guitaramp_utility -j2 2>&1 | tail -8
[ -f build/guitaramp_utility.so ] || { echo "!! utility build FAILED"; exit 1; }

echo "=== build guitaramp_hexforge (-j1, NAM-linked, OOM-safe) ==="
cmake --build build --target guitaramp_hexforge -j1 2>&1 | tail -8
[ -f build/guitaramp_hexforge.so ] || { echo "!! hexforge build FAILED"; exit 1; }

echo "=== symbol-isolation gate (both expect 0 undefined) ==="
for so in guitaramp_utility guitaramp_hexforge; do
    u=$(ldd -r "build/$so.so" 2>/dev/null | grep -c 'undefined symbol')
    echo "  $so: undefined=$u"
    [ "$u" = "0" ] || { echo "!! ABORT: $so has undefined symbols"; exit 1; }
done

echo "=== fold .so + ttl + modgui into bundle ==="
cp build/guitaramp_*.so "$B"/
cp lv2/*.ttl "$B"/
sed -i 's/\r$//' "$B"/*.ttl
for d in lv2/modgui-*; do
    [ -d "$d" ] || continue
    name=$(basename "$d")
    rm -rf "$B/$name"; cp -r "$d" "$B/$name"
    find "$B/$name" -type f \( -name '*.html' -o -name '*.css' -o -name '*.js' \) \
        -exec sed -i 's/\r$//' {} +
done
echo "bundle: $(ls "$B"/*.so | wc -l) .so, $(ls "$B"/*.ttl | wc -l) .ttl"

echo "=== restart MOD ==="
sudo systemctl restart mod-host mod-ui
sleep 6
for s in jack mod-host mod-ui mod-ala-pi-stomp; do echo "  $s: $(systemctl is-active "$s")"; done
echo "=== utility port count (expect Input Trim now has hb_model + hb_amount) ==="
LV2_PATH=/home/pistomp/.lv2 lv2info https://rpowell5064.github.io/guitaramp-suite/utility 2>/dev/null | grep -iE "Number of Ports|hb_" | head
echo "=== DONE ==="
