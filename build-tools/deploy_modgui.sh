#!/usr/bin/env bash
# Deploy custom modgui resources + updated .ttl into the guitaramp-suite bundle.
# GUI-only: no .so rebuild needed. Copies from the Pi source tree into the bundle,
# strips CRLF from text resources (PNGs untouched), then restarts mod-ui.
set -uo pipefail
SRC=~/guitar-amp-mod/lv2
B=~/.lv2/guitaramp-suite.lv2

echo "=== copy modgui dirs + ttl into bundle ==="
for d in "$SRC"/modgui-*; do
    [ -d "$d" ] || continue
    name=$(basename "$d")
    rm -rf "$B/$name"
    cp -r "$d" "$B/$name"
    # strip CRLF on text resources only
    find "$B/$name" -type f \( -name '*.html' -o -name '*.css' -o -name '*.js' \) \
        -exec sed -i 's/\r$//' {} +
    echo "  $name -> $(ls "$B/$name" | wc -l) entries"
done
cp "$SRC"/*.ttl "$B"/
sed -i 's/\r$//' "$B"/*.ttl
echo "copied ttls: $(ls "$B"/*.ttl | wc -l)"

echo "=== bundle modgui resources ==="
find "$B" -maxdepth 2 -path '*modgui-*' -type f -printf '%P  %s\n' | sort

echo "=== lv2ls (expect 9 guitaramp-suite URIs) ==="
LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep -c guitaramp-suite

echo "=== restart mod-ui (modgui is read by mod-ui, not mod-host) ==="
sudo systemctl restart mod-ui
sleep 5
for s in jack mod-host mod-ui mod-ala-pi-stomp; do
    echo "$s: $(systemctl is-active "$s")"
done
echo "=== DONE ==="
