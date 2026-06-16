#!/usr/bin/env bash
# Session deploy: cab volume fix (DefaultCabIR.h -> rebuild cab + hexforge) +
# delay Seraph modgui resources (copy only). NAM-linked targets built ONE AT A
# TIME at -j1 to avoid OOM on the 2 GB Pi 5.
set -uo pipefail
cd ~/guitar-amp-mod || exit 1
B=~/.lv2/guitaramp-suite.lv2

echo "=== build guitaramp_cab (-j1) ==="
if cmake --build build --target guitaramp_cab -j1 >/tmp/build_cab.log 2>&1; then
    echo "  CAB_OK"
else
    echo "  CAB_FAILED:"; grep -iE "error:|undefined reference|No such file" /tmp/build_cab.log | tail -20; exit 1
fi

echo "=== build guitaramp_hexforge (-j1) ==="
if cmake --build build --target guitaramp_hexforge -j1 >/tmp/build_hf.log 2>&1; then
    echo "  HEXFORGE_OK"
else
    echo "  HEXFORGE_FAILED:"; grep -iE "error:|undefined reference|No such file" /tmp/build_hf.log | tail -20; exit 1
fi

echo "=== deploy rebuilt .so ==="
mkdir -p "$B"
cp build/guitaramp_cab.so build/guitaramp_hexforge.so "$B"/
echo "  copied cab + hexforge .so"

echo "=== deploy ttl + modgui resource dirs ==="
cp lv2/*.ttl "$B"/
sed -i 's/\r$//' "$B"/*.ttl
for d in lv2/modgui-*; do
    [ -d "$d" ] || continue
    name=$(basename "$d")
    rm -rf "$B/$name"
    cp -r "$d" "$B/$name"
    find "$B/$name" -type f \( -name '*.html' -o -name '*.css' -o -name '*.js' \) \
        -exec sed -i 's/\r$//' {} +
    echo "  $name -> $(find "$B/$name" -type f | wc -l) files"
done

echo "=== lv2ls (expect 9 guitaramp-suite URIs) ==="
LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep -c guitaramp-suite

echo "=== restart mod-host + mod-ui ==="
sudo systemctl restart mod-host mod-ui
sleep 6
for s in jack mod-host mod-ui mod-ala-pi-stomp; do
    echo "  $s: $(systemctl is-active "$s")"
done

echo "=== mod-ui scan check (delay/cab/hexforge, no template/traceback) ==="
journalctl -u mod-ui --since "30 sec ago" --no-pager 2>/dev/null \
    | grep -iE 'guitaramp-suite/(cab|delay|hexforge)|template|traceback' | tail -12
echo "=== DONE ==="
