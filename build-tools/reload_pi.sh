#!/usr/bin/env bash
# Restart the MOD host/UI so the freshly-built guitaramp .so files are loaded.
set -uo pipefail
echo "=== restarting mod-host + mod-ui ==="
sudo systemctl restart mod-host mod-ui
sleep 6
echo "--- service states ---"
for s in jack mod-host mod-ui mod-ala-pi-stomp; do
    echo "$s: $(systemctl is-active "$s")"
done
echo "--- mod-host: is the new amp .so mapped right now? ---"
mh="$(pgrep -x mod-host | head -1)"
if [ -n "$mh" ]; then
    if sudo lsof -p "$mh" 2>/dev/null | grep -q 'guitaramp_amp.so'; then
        echo "  yes - guitaramp_amp.so mapped into mod-host"
    else
        echo "  not mapped yet (mod-host loads it on demand when the amp is on a pedalboard)"
    fi
fi
echo "--- recent mod-host log ---"
journalctl -u mod-host --no-pager -n 6 2>/dev/null | tail -6
echo "=== DONE ==="
