#!/bin/bash
# Build + run the Hex Forge migration test and the golden under WSL (SAC-free).
set -e
cd /mnt/c/Development/Projects/guitar-amp-mod/build-tools
cmake -S . -B out_wsl -DCMAKE_BUILD_TYPE=Release > /tmp/gcfg.log 2>&1 || { echo "CONFIG FAIL"; tail -15 /tmp/gcfg.log; exit 1; }
cmake --build out_wsl --target hexforge_migrate_test -j4 > /tmp/mbld.log 2>&1 || { echo "MIGRATE BUILD FAIL"; grep -E "error|static assert" /tmp/mbld.log | head -20; exit 1; }
BIN=$(find out_wsl -name hexforge_migrate_test -type f -perm -u+x | head -1)
"$BIN" 2>&1 | tail -12
cmake --build out_wsl --target hexforge_golden -j4 > /tmp/gbld.log 2>&1 || { echo "GOLDEN BUILD FAIL"; grep -E "error" /tmp/gbld.log | head -20; exit 1; }
GB=$(find out_wsl -name hexforge_golden -type f -perm -u+x | head -1)
"$GB" > /tmp/golden_wsl_new.txt 2>&1
echo "golden lines: $(wc -l < /tmp/golden_wsl_new.txt)"
if [ -f /tmp/golden_wsl_ref.txt ]; then
    diff -q /tmp/golden_wsl_ref.txt /tmp/golden_wsl_new.txt > /dev/null && echo "GOLDEN (WSL): identical to the saved WSL reference" || { echo "GOLDEN (WSL): DIFFERS from the saved WSL reference"; diff /tmp/golden_wsl_ref.txt /tmp/golden_wsl_new.txt | head -10; }
else
    echo "no WSL golden reference saved (/tmp/golden_wsl_ref.txt) - Pi golden is the gate"
fi
