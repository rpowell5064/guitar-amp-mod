#!/bin/bash
# Install the Hex Forge pi-Stomp hook so it survives pi-Stomp upgrades (run ON the Pi):
#   - the patch script goes to /usr/local/lib/hexforge/
#   - a systemd drop-in re-applies it every time mod-ala-pi-stomp starts (ExecStartPre), so a
#     pi-Stomp update that replaces modalapi/modhandler.py is patched again on its next start.
#     The script is idempotent and fails soft ("-" prefix): if a future pi-Stomp changes the
#     code the patch anchors on, the UI still starts, unpatched, and the journal says why.
# Usage:  scp build-tools/{install_pistomp_hook.sh,patch_pistomp_lcd.py} pistomp@pistomp.local:/tmp/
#         ssh pistomp@pistomp.local "bash /tmp/install_pistomp_hook.sh"
set -e
SRC="$(dirname "$0")/patch_pistomp_lcd.py"
[ -f "$SRC" ] || { echo "patch_pistomp_lcd.py must sit next to this script"; exit 1; }
sudo mkdir -p /usr/local/lib/hexforge
sudo install -m 644 "$SRC" /usr/local/lib/hexforge/patch_pistomp_lcd.py
sudo sed -i 's/\r$//' /usr/local/lib/hexforge/patch_pistomp_lcd.py
sudo mkdir -p /etc/systemd/system/mod-ala-pi-stomp.service.d
printf '[Service]\n# Hex Forge (guitaramp-suite): re-apply the LCD/LED/footswitch hook to pi-Stomp before every start\nExecStartPre=-+/usr/bin/python3 /usr/local/lib/hexforge/patch_pistomp_lcd.py\n' \
    | sudo tee /etc/systemd/system/mod-ala-pi-stomp.service.d/hexforge-hook.conf > /dev/null
sudo systemctl daemon-reload
sudo python3 /usr/local/lib/hexforge/patch_pistomp_lcd.py || true
sudo systemctl restart mod-ala-pi-stomp
sleep 6
echo "mod-ala-pi-stomp: $(systemctl is-active mod-ala-pi-stomp)"
grep -q HEXFORGE_PRESET_CCS /opt/pistomp/pi-stomp/modalapi/modhandler.py && echo "hook present in modhandler.py" || echo "hook NOT present"
echo "installed: the hook re-applies itself on every pi-Stomp UI start"
