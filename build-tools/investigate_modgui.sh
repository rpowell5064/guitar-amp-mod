#!/usr/bin/env bash
set -uo pipefail
echo "=== mod-ui service / location ==="
systemctl cat mod-ui 2>/dev/null | grep -iE 'ExecStart|WorkingDirectory|Environment' | head
echo
echo "=== mod-ui install dir + default modgui template (used for auto-gen GUIs) ==="
for base in /usr/share/mod /usr/lib/python3*/dist-packages/mod /usr/local/lib/python3*/dist-packages/mod ~/mod-ui /opt/mod-ui; do
    [ -d "$base" ] && echo "BASE: $base" && find "$base" -maxdepth 3 -iname '*.html' 2>/dev/null | grep -iE 'default|modgui|icon' | head
done
find / -maxdepth 8 -iname 'default.html' -path '*modgui*' 2>/dev/null | head
echo
echo "=== plugin modgui.ttl files that USE javascript (show/hide candidates) ==="
n=0
for t in $(find /home/pistomp/.lv2 /usr/lib/lv2 -name 'modgui.ttl' 2>/dev/null); do
    if grep -q 'javascript' "$t" 2>/dev/null; then
        d="$(dirname "$t")"
        js="$(grep -oE 'modgui:javascript <[^>]+>' "$t" | head -1)"
        echo "$t   [$js]"
        n=$((n+1))
        [ $n -ge 25 ] && break
    fi
done
echo
echo "=== which of those have an enumeration/select port in their main TTL? ==="
echo "(those are the best templates for selector-driven show/hide)"
grep -rlE 'enumeration' /home/pistomp/.lv2/*/[!m]*.ttl 2>/dev/null | head -10
echo "=== DONE ==="
