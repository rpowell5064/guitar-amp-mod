#!/usr/bin/env bash
set -uo pipefail
H=/usr/local/share/mod/html
echo "=== $H top ==="; ls "$H" 2>/dev/null
echo "=== default modgui templates ==="
find "$H" -iname '*.html' 2>/dev/null | grep -iE 'default|icon|pedal|modgui' | head
echo "=== default knob / control assets (images) ==="
find "$H" -iregex '.*\(knob\|slider\|switch\|control\).*\.\(png\|svg\)' 2>/dev/null | head -20
echo "=== default css/js for modgui ==="
find "$H" -maxdepth 3 -iname '*.css' 2>/dev/null | grep -iE 'pedal|modgui|default|effect' | head
echo
echo "=== a DEFAULT icon template, if present (this is the clean base) ==="
df=$(find "$H" -iname '*.html' 2>/dev/null | grep -iE 'default' | head -1); echo "FILE: $df"
[ -n "$df" ] && sed -n '1,60p' "$df"
echo
echo "=== Ratatouille JS (FULL — show/hide + file-loader pattern) ==="
cat /home/pistomp/.lv2/Ratatouille.lv2/modgui/script-ratatouille.js
echo
echo "=== Ratatouille CSS: how .mod-knob-image gets its picture ==="
grep -nE 'mod-knob-image|\.mod-knob|background|control-group|mod-pedal-rata' /home/pistomp/.lv2/Ratatouille.lv2/modgui/stylesheet-ratatouille.css | head -25
echo "=== DONE ==="
