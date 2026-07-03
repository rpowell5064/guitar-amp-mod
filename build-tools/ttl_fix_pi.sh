#!/usr/bin/env bash
# Deploy the fixed utility.ttl into the bundle and confirm it actually parses now
# (the @prefix rdf: fix). modgui/TTL-only change -> no .so rebuild; restart mod-ui.
set -uo pipefail
B=~/.lv2/guitaramp-suite.lv2
cp ~/guitar-amp-mod/lv2/utility.ttl "$B"/
sed -i 's/\r$//' "$B"/utility.ttl
echo "=== lv2info (TTL must parse; expect 7 ports incl hb_model/hb_amount) ==="
LV2_PATH=/home/pistomp/.lv2 lv2info https://rpowell5064.github.io/guitaramp-suite/utility 2>&1 \
    | grep -iE "Number of Ports|hb_model|hb_amount|Name:" | head
echo "=== restart mod-ui ==="
sudo systemctl restart mod-ui
sleep 5
for s in mod-host mod-ui; do echo "  $s: $(systemctl is-active "$s")"; done
echo "=== DONE ==="
