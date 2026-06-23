#!/usr/bin/env bash
# Build + deploy the Nail plugin on the pi-Stomp. nail is a NON-NAM target (like
# fuzz), so it is safe to link at -j2 (the OOM warning is only for NAM-linked
# targets). Reconfigures (CMakeLists added a new target + install entries),
# builds, runs the symbol-isolation gate, folds the .so/.ttl/modgui into the
# bundle, and restarts MOD.
set -uo pipefail
cd ~/guitar-amp-mod || exit 1
B=~/.lv2/guitaramp-suite.lv2

echo "=== reconfigure (new target/install in CMakeLists) ==="
cmake -S . -B build >/dev/null 2>&1 || { echo "!! cmake configure failed"; exit 1; }

echo "=== build guitaramp_nail (-j2, non-NAM) ==="
cmake --build build --target guitaramp_nail -j2 2>&1 | tail -25
if [ ! -f build/guitaramp_nail.so ]; then echo "!! BUILD FAILED: no guitaramp_nail.so"; exit 1; fi

echo "=== symbol-isolation gate (expect 0 undefined — the fuzz/NAM hazard) ==="
U=$(ldd -r build/guitaramp_nail.so 2>/dev/null | grep -c 'undefined symbol')
echo "undefined symbols: $U"
ldd -r build/guitaramp_nail.so 2>/dev/null | grep 'undefined symbol' | head
if [ "$U" != "0" ]; then echo "!! ABORT: nail.so has undefined symbols — not deploying"; exit 1; fi

echo "=== fold .so + ttl + modgui into bundle ==="
mkdir -p "$B"
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
echo "bundle now: $(ls "$B"/*.so | wc -l) .so, $(ls "$B"/*.ttl | wc -l) .ttl, modgui-nail=$( [ -d "$B/modgui-nail" ] && echo yes || echo NO )"

echo "=== enumerate (nail should appear) ==="
LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep guitaramp-suite | sort

echo "=== restart MOD ==="
sudo systemctl restart mod-host mod-ui
sleep 6
for s in jack mod-host mod-ui mod-ala-pi-stomp; do echo "  $s: $(systemctl is-active "$s")"; done

NAILN=$(LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep -c 'guitaramp-suite/nail')
echo "=== nail enumerated: $NAILN (expect 1) ==="
echo "=== recent mod-host log ==="
journalctl -u mod-host --no-pager -n 5 2>/dev/null | tail -5
echo "=== DONE ==="
