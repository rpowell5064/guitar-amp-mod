#!/usr/bin/env bash
# Find the mod-ui JS and discover the modgui custom-JS "funcs" API — specifically
# whether a port-write (set_port_value / setPortValue) is available to icon scripts,
# and how the user function is invoked. Decides the Hex Forge reorder mechanism.
set -uo pipefail

echo "=== locate mod-ui html/js ==="
DIRS=$(find / -type d -path '*mod-ui*' -name js 2>/dev/null; \
       find / -type d -path '*modui*' -name js 2>/dev/null)
echo "$DIRS"

echo "=== grep for port-write API in modgui handling ==="
for pat in set_port_value setPortValue 'gui.js' ; do :; done
grep -rIn --include=*.js -e 'set_port_value' -e 'setPortValue' / 2>/dev/null \
  | grep -i mod | grep -vi node_modules | head -20

echo "=== how the icon custom JS is called (the 'funcs' object passed to it) ==="
F=$(grep -rIl --include=*.js -e 'jsCallback' -e "event.icon" -e "'start'" / 2>/dev/null | grep -i mod | head -3)
echo "candidate files: $F"
for f in $F; do
  echo "----- $f -----"
  grep -nE "set_port_value|setPortValue|jsData|jsCallback|funcs|function *\(event" "$f" 2>/dev/null | head -25
done
