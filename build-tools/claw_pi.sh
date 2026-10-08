#!/usr/bin/env bash
# Build + deploy the Claw plugin on the pi-Stomp. claw is a NON-NAM target (like
# fuzz), so it is safe to link at -j2 (the OOM warning is only for NAM-linked
# targets). Reconfigures (CMakeLists added a new target + install entries),
# builds, runs the symbol-isolation gate, folds the .so/.ttl/modgui into the
# bundle, and restarts MOD.
set -uo pipefail
cd ~/guitar-amp-mod || exit 1
B=~/.lv2/guitaramp-suite.lv2

echo "=== reconfigure (new target/install in CMakeLists) ==="
cmake -S . -B build >/dev/null 2>&1 || { echo "!! cmake configure failed"; exit 1; }

echo "=== build guitaramp_claw (-j2, non-NAM) ==="
cmake --build build --target guitaramp_claw -j2 2>&1 | tail -25
if [ ! -f build/guitaramp_claw.so ]; then echo "!! BUILD FAILED: no guitaramp_claw.so"; exit 1; fi

echo "=== symbol-isolation gate (expect 0 undefined — the fuzz/NAM hazard) ==="
U=$(ldd -r build/guitaramp_claw.so 2>/dev/null | grep -c 'undefined symbol')
echo "undefined symbols: $U"
ldd -r build/guitaramp_claw.so 2>/dev/null | grep 'undefined symbol' | head
if [ "$U" != "0" ]; then echo "!! ABORT: claw.so has undefined symbols — not deploying"; exit 1; fi

echo "=== fold .so + ttl + modgui into bundle (atomic copy: never cp over a mapped .so) ==="
mkdir -p "$B"
for so in build/guitaramp_*.so; do n=$(basename "$so"); cp "$so" "$B/$n.new" && mv "$B/$n.new" "$B/$n"; done
cp lv2/*.ttl "$B"/
sed -i 's/\r$//' "$B"/*.ttl
for d in lv2/modgui-*; do
    [ -d "$d" ] || continue
    name=$(basename "$d")
    rm -rf "$B/$name"; cp -r "$d" "$B/$name"
    find "$B/$name" -type f \( -name '*.html' -o -name '*.css' -o -name '*.js' \) \
        -exec sed -i 's/\r$//' {} +
done
echo "bundle now: $(ls "$B"/*.so | wc -l) .so, $(ls "$B"/*.ttl | wc -l) .ttl, modgui-claw=$( [ -d "$B/modgui-claw" ] && echo yes || echo NO )"

echo "=== enumerate (claw should appear) ==="
LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep guitaramp-suite | sort

echo "=== restart MOD ==="
sudo systemctl restart mod-host mod-ui mod-ala-pi-stomp
sleep 6
for s in jack mod-host mod-ui mod-ala-pi-stomp; do echo "  $s: $(systemctl is-active "$s")"; done

CLAWN=$(LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep -c 'guitaramp-suite/claw')
echo "=== claw enumerated: $CLAWN (expect 1) ==="
echo "=== recent mod-host log ==="
journalctl -u mod-host --no-pager -n 5 2>/dev/null | tail -5
echo "=== DONE ==="
