#!/usr/bin/env bash
# (Re)construct the guitaramp-suite LV2 bundle in the pi-Stomp plugin dir from the
# freshly-built .so files + the source .ttl manifests, then validate.
# Run on the Pi: bash ~/deploy_pi.sh
set -uo pipefail
cd ~/guitar-amp-mod || exit 1
BUNDLE=~/.lv2/guitaramp-suite.lv2

echo "=== (re)constructing bundle at $BUNDLE ==="
mkdir -p "$BUNDLE"
cp build/guitaramp_*.so "$BUNDLE"/
cp lv2/*.ttl "$BUNDLE"/
echo "copied $(ls "$BUNDLE"/*.so | wc -l) .so and $(ls "$BUNDLE"/*.ttl | wc -l) .ttl"

echo "--- bundle contents ---"
ls -la --time-style=+%Y-%m-%d_%H:%M:%S "$BUNDLE"/

echo "=== manifest sanity: plugin .so referenced by manifest all present? ==="
for so in $(grep -oE 'guitaramp_[a-z]+\.so' "$BUNDLE"/manifest.ttl | sort -u); do
    if [ -f "$BUNDLE/$so" ]; then echo "  OK   $so"; else echo "  MISS $so"; fi
done

echo "=== lv2ls enumeration (expect 9 URIs) ==="
LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep guitaramp-suite | sort

echo "=== running MOD / jack services ==="
systemctl list-units --type=service --state=running --no-legend --no-pager \
    | grep -iE "mod|jack|pistomp|browse" | awk '{print $1}'

echo "=== DONE ==="
