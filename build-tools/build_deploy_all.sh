#!/usr/bin/env bash
# Rebuild (amp .so changed: NAM removed) + redeploy bundle incl. all modgui dirs,
# then restart mod-host + mod-ui and verify.
set -uo pipefail
cd ~/guitar-amp-mod || exit 1
B=~/.lv2/guitaramp-suite.lv2

echo "=== build ==="
if cmake --build build -j4 >/tmp/build.log 2>&1; then
    echo "BUILD_OK"
else
    echo "BUILD_FAILED — error tail:"
    grep -iE "error:|undefined reference|No such file" /tmp/build.log | tail -30
    exit 1
fi

echo "=== deploy .so + ttl ==="
mkdir -p "$B"
cp build/guitaramp_*.so "$B"/
cp lv2/*.ttl "$B"/
sed -i 's/\r$//' "$B"/*.ttl

echo "=== deploy modgui resource dirs ==="
for d in lv2/modgui-*; do
    [ -d "$d" ] || continue
    name=$(basename "$d")
    rm -rf "$B/$name"
    cp -r "$d" "$B/$name"
    find "$B/$name" -type f \( -name '*.html' -o -name '*.css' -o -name '*.js' \) \
        -exec sed -i 's/\r$//' {} +
    echo "  $name -> $(find "$B/$name" -type f | wc -l) files"
done

echo "=== lv2ls (expect 9) ==="
LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep -c guitaramp-suite

echo "=== restart mod-host + mod-ui ==="
sudo systemctl restart mod-host mod-ui
sleep 6
for s in jack mod-host mod-ui mod-ala-pi-stomp; do
    echo "  $s: $(systemctl is-active "$s")"
done

echo "=== amp scan check (expect 'Finished scanning' for amp, no template error) ==="
journalctl -u mod-ui --since "30 sec ago" --no-pager 2>/dev/null \
    | grep -iE 'guitaramp-suite/(amp|drive|delay)|template|traceback' | tail -12
echo "=== DONE ==="
