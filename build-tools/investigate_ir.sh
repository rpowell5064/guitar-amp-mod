#!/usr/bin/env bash
set -uo pipefail
echo "=== MOD user-files locations ==="
sudo find / -maxdepth 7 -type d -iname "user-files" 2>/dev/null
echo "=== contents of any user-files dir (look for IR/cab folders) ==="
for d in $(sudo find / -maxdepth 7 -type d -iname "user-files" 2>/dev/null); do
    echo "--- $d ---"; ls -la "$d"
done
echo "=== mod:fileTypes used by installed plugins (what MOD recognizes) ==="
grep -rhoE 'fileTypes[^;]*' ~/.lv2 /usr/lib/lv2 /usr/local/lib/lv2 2>/dev/null | sort -u | head -40
echo "=== plugins that load IR/cab files (for the exact fileTypes string) ==="
grep -rlE 'cabsim|"ir"|,ir|ir,' ~/.lv2 /usr/lib/lv2 /usr/local/lib/lv2 2>/dev/null | grep -i ttl | head
echo "=== where existing cab/IR wavs live on disk ==="
sudo find / -maxdepth 7 -type d \( -iname "*speaker*" -o -iname "*impulse*" -o -iname "*cabinet*" \) 2>/dev/null | head
echo "=== DONE ==="
