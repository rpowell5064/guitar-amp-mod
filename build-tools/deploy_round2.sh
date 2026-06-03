#!/usr/bin/env bash
set -uo pipefail
bash ~/build_verify_pi.sh 2>&1 | tail -16
echo "=== instantiate test (all 9, incl. cab w/ resampling) ==="
if g++ -O2 ~/lv2host_test.cpp -o ~/lv2host_test -ldl; then
    ~/lv2host_test | tail -3
fi
echo "=== restart MOD ==="
sudo systemctl restart mod-host mod-ui
sleep 6
for s in jack mod-host mod-ui mod-ala-pi-stomp; do
    echo "$s: $(systemctl is-active "$s")"
done
echo "=== DONE ==="
