#!/usr/bin/env bash
set -uo pipefail
M=/usr/local/share/mod/html/js/modgui.js
echo "=== funcs object passed to icon JS (around 1660-1715) ==="
sed -n '1655,1715p' "$M"
echo "=== where the user icon function is invoked (jsData/callback) ==="
grep -nE "jsData|icon_function|this\.jsData|\(event, |\(event\)|new Function|customJs|jsCallback" "$M" | head -30
echo "=== how 'change'/'start' events reach the icon JS (event obj build) ==="
grep -nE "type: *'change'|type: *'start'|symbol:|value:|icon:" "$M" | head -30
