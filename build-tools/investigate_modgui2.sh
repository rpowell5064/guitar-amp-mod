#!/usr/bin/env bash
set -uo pipefail
MOD=/usr/local/lib/python3.11/dist-packages/mod
echo "=== mod-ui default modgui template + default resources ==="
find "$MOD" -maxdepth 5 -iname '*.html' 2>/dev/null | grep -iE 'default|modgui|icon|template' | head
find / -maxdepth 9 -type d -name 'default' -path '*modgui*' 2>/dev/null | head
find / -maxdepth 9 -iname 'icon-*.html' -path '*default*' 2>/dev/null | head
echo "--- candidate html asset dirs ---"
find / -maxdepth 8 -type d \( -name 'modgui' -o -name 'html' \) -path '*mod*' 2>/dev/null | grep -vi '.lv2' | head
echo
echo "=== Ratatouille modgui.ttl (FULL) ==="
cat /home/pistomp/.lv2/Ratatouille.lv2/modgui.ttl
echo
echo "=== Ratatouille resources dir ==="
ls -la /home/pistomp/.lv2/Ratatouille.lv2/modgui/
echo
echo "=== Ratatouille HTML icon template (head 80) ==="
f=$(find /home/pistomp/.lv2/Ratatouille.lv2/modgui -iname '*.html' | head -1); echo "FILE: $f"; sed -n '1,80p' "$f"
echo "=== DONE ==="
