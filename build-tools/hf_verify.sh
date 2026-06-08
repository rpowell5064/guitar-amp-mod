#!/usr/bin/env bash
# Quick post-deploy sanity for Hex Forge on the Pi.
set -uo pipefail
B=~/.lv2/guitaramp-suite.lv2
echo "=== guitaramp-suite URIs ==="
lv2ls 2>/dev/null | grep guitaramp-suite | sort
echo "=== hexforge modgui files ==="
ls "$B/modgui-hexforge/" | tr '\n' ' '; echo
echo "=== dlopen guitaramp_hexforge.so ==="
python3 - <<'PY'
import ctypes
ctypes.CDLL("/home/pistomp/.lv2/guitaramp-suite.lv2/guitaramp_hexforge.so")
print("dlopen OK")
PY
echo "=== services ==="
systemctl is-active mod-host mod-ui
