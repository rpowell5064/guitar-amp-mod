#!/usr/bin/env bash
set -uo pipefail
URI="https://rpowell5064.github.io/guitaramp-suite/cab"
echo "=== lv2info present? ==="
command -v lv2info || echo "(lv2info not installed)"
echo "=== cab plugin: patch/atom/parameter info ==="
if command -v lv2info >/dev/null; then
    LV2_PATH=/home/pistomp/.lv2 lv2info "$URI" 2>/dev/null \
        | grep -iE 'irfile|writable|cabinet ir|filetype|atom|control|notify|Parameter' || echo "(no matching lines)"
fi
echo "=== installed cab.ttl shows the IR param? ==="
grep -iE 'irfile|fileTypes|patch:writable|atom:AtomPort' /home/pistomp/.lv2/guitaramp-suite.lv2/cab.ttl | sed 's/^[[:space:]]*//'
echo "=== Speaker Cabinets IRs folder ==="
ls -la "/home/pistomp/data/user-files/Speaker Cabinets IRs/"
echo "=== any IR/wav files under user-files ==="
find /home/pistomp/data/user-files -type f \( -iname '*.wav' -o -iname '*.ir' \) 2>/dev/null | head -30
echo "=== DONE ==="
