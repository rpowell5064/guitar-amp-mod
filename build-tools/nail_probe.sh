#!/usr/bin/env bash
# Compile + run the Nail offline numeric probe on the Pi against the already-built
# static lib. NailDistortion pulls in NO NAM, so linking GuitarAmpSim alone should
# resolve; NamCore is appended only if the first link fails on missing symbols.
set -uo pipefail
cd ~/guitar-amp-mod || exit 1

LIB=$(find build -name 'libGuitarAmpSim.a' | head -1)
NAM=$(find build -name 'libNamCore.a' | head -1)
echo "GuitarAmpSim: ${LIB:-NOT FOUND}"
[ -z "$LIB" ] && { echo "!! no libGuitarAmpSim.a — build first"; exit 1; }

INC="-I deps/guitar-amp-simulator/include"
SRC="tools/nail_probe.cpp"
OUT=/tmp/nail_probe

echo "=== compile (GuitarAmpSim only) ==="
if g++ -std=c++17 -O2 $INC "$SRC" "$LIB" -o "$OUT" -lm 2>/tmp/nail_probe_err; then
    echo "linked with GuitarAmpSim alone."
else
    echo "first link failed; retrying with NamCore (${NAM:-none})..."
    head -5 /tmp/nail_probe_err
    g++ -std=c++17 -O2 $INC "$SRC" "$LIB" ${NAM:+"$NAM"} -o "$OUT" -lm || {
        echo "!! compile failed"; cat /tmp/nail_probe_err; exit 1; }
fi

echo "=== run ==="
"$OUT"
echo "=== DONE ==="
