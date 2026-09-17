#!/bin/bash
set -e
cd /mnt/c/Development/Projects/guitar-amp-mod/build-tools
cmake -S . -B out_wsl -DCMAKE_BUILD_TYPE=Release > /tmp/gcfg.log 2>&1 || { echo "CONFIG FAIL"; tail -15 /tmp/gcfg.log; exit 1; }
cmake --build out_wsl --target hexforge_golden -j4 > /tmp/gbld.log 2>&1 || { echo "BUILD FAIL"; tail -25 /tmp/gbld.log; exit 1; }
BIN=$(find out_wsl -name hexforge_golden -type f -perm -u+x | head -1)
echo "golden built: $BIN"
"$BIN" > /tmp/golden_wsl.txt 2>&1
echo "lines: $(wc -l < /tmp/golden_wsl.txt)"
grep '^ALL' /tmp/golden_wsl.txt
