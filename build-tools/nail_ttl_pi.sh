#!/usr/bin/env bash
# Sync the renamed nail.ttl (mode 5 label "Tusk" -> "Yo, Hey Adrian!") into the
# bundle. TTL-only -> no .so rebuild; verify it PARSES, then restart mod-ui.
set -uo pipefail
B=~/.lv2/guitaramp-suite.lv2
cp ~/guitar-amp-mod/lv2/nail.ttl "$B"/
sed -i 's/\r$//' "$B"/nail.ttl
echo "=== lv2info (must parse; mode scalePoints incl the new label) ==="
LV2_PATH=/home/pistomp/.lv2 lv2info https://rpowell5064.github.io/guitaramp-suite/nail 2>&1 \
    | grep -iE "Name:|Adrian|Con Molars|Delicate" | head
echo "=== restart mod-ui ==="
sudo systemctl restart mod-ui
sleep 5
for s in mod-host mod-ui; do echo "  $s: $(systemctl is-active "$s")"; done
echo "=== DONE ==="
