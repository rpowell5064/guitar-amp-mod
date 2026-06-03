#!/usr/bin/env bash
# Reconfigure (CMakeLists changed: version script + NAM-only-where-used), rebuild,
# redeploy the bundle, and verify exported-symbol isolation + NAM removal.
set -uo pipefail
cd ~/guitar-amp-mod || exit 1

echo "=== reconfigure ==="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/tmp/cfg.log 2>&1
echo "configure rc=$?"; tail -2 /tmp/cfg.log

echo "=== build ==="
if cmake --build build -j4 >/tmp/build.log 2>&1; then
    echo "BUILD_OK"
else
    echo "BUILD_FAILED -- error/undefined-symbol tail:"
    grep -iE "undefined reference|error:|nam" /tmp/build.log | tail -30
    exit 1
fi

B=~/.lv2/guitaramp-suite.lv2
echo "=== deploy (.so + .ttl) ==="
mkdir -p "$B"
cp build/guitaramp_*.so "$B"/
cp lv2/*.ttl "$B"/

echo "=== verify: size / exported-symbol count / NAM symbol count ==="
printf "%-26s %8s %10s %9s\n" "plugin" "size" "exported" "nam_syms"
for so in "$B"/*.so; do
    kb=$(( $(stat -c%s "$so") / 1024 ))
    nexp=$(nm -D --defined-only "$so" 2>/dev/null | wc -l)
    nnam=$(nm "$so" 2>/dev/null | grep -ciE 'nam::|ConfigParser|Eigen')
    printf "%-26s %6s KB %10s %9s\n" "$(basename "$so")" "$kb" "$nexp" "$nnam"
done

echo "=== exported dynamic symbols of gate (expect only lv2 entry points) ==="
nm -D --defined-only "$B/guitaramp_gate.so" 2>/dev/null

echo "=== lv2ls (expect 9) ==="
LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep -c guitaramp-suite
echo "=== DONE ==="
