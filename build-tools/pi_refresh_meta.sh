#!/usr/bin/env bash
# Redeploy bundle METADATA only (ttl + modgui resource dirs) and restart MOD.
# No rebuild — used to push version bumps / refreshed thumbnails to the running
# pi-Stomp so the plugin library/search re-indexes. Run on the Pi.
set -uo pipefail
cd ~/guitar-amp-mod || exit 1
B=~/.lv2/guitaramp-suite.lv2
mkdir -p "$B"

# Stage freshly-scp'd files from ~ into the source tree (if present).
[ -f ~/hexmeta.tgz ]      && tar xzf ~/hexmeta.tgz -C lv2/ && echo "extracted hexmeta.tgz"
[ -f ~/package_bundle.sh ] && cp ~/package_bundle.sh build-tools/ && echo "staged package_bundle.sh"
[ -f ~/CMakeLists.txt ]    && cp ~/CMakeLists.txt . && echo "staged CMakeLists.txt"
[ -f ~/LICENSE.new ]       && cp ~/LICENSE.new LICENSE && echo "staged LICENSE"

cp lv2/*.ttl "$B"/
sed -i 's/\r$//' "$B"/*.ttl
for d in lv2/modgui-*; do
    [ -d "$d" ] || continue
    name=$(basename "$d")
    rm -rf "$B/$name"
    cp -r "$d" "$B/$name"
    find "$B/$name" -type f \( -name '*.html' -o -name '*.css' -o -name '*.js' \) \
        -exec sed -i 's/\r$//' {} +
done
echo "deployed ttl=$(ls "$B"/*.ttl | wc -l)  modgui=$(ls -d "$B"/modgui-* | wc -l)"
echo "microVersion now: $(grep -h microVersion "$B"/amp.ttl | head -1 | tr -d ' ')"

sudo systemctl restart mod-host mod-ui
sleep 7
echo "mod-host=$(systemctl is-active mod-host)  mod-ui=$(systemctl is-active mod-ui)"
echo "=== DONE (hard-refresh the browser: Ctrl+Shift+R) ==="
