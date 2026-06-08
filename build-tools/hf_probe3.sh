#!/usr/bin/env bash
set -uo pipefail
M=/usr/local/share/mod/html/js/modgui.js
echo "=== modgui.js: parameters array construction / ordering ==="
grep -nE "parameters" "$M" | grep -iE "sort|order|push|=\s*\[|for |\.parameters" | head -40
echo "=== mod-ui python: how effect parameters list is built (sort?) ==="
for f in $(grep -rIl "parameters" /usr/local/lib/python3*/dist-packages/mod /usr/lib/python3*/dist-packages/mod 2>/dev/null | head); do
  echo "-- $f --"
  grep -nE "parameters|sorted|\.sort" "$f" | grep -iE "sort|parameters\[|append|=\s*\[|order" | head
done
echo "=== stock NAM modgui icon: does it render the file picker at all? ==="
sed -n '1,40p' ~/.lv2/neural_amp_modeler.lv2/modgui/icon-nam.html
