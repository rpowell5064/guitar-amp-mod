#!/usr/bin/env bash
set -uo pipefail
echo "=== compile + run all-9 instantiate test (incl. new cab) ==="
g++ -O2 ~/lv2host_test.cpp -o ~/lv2host_test -ldl 2>&1 | head
if [ -x ~/lv2host_test ]; then ~/lv2host_test; echo "EXIT=$?"; else echo "COMPILE_FAILED"; fi
echo
echo "=== user IR .wav format (must be PCM16/24/32 or float32 for the loader) ==="
python3 - <<'PY'
import struct, os, glob
d = "/home/pistomp/data/user-files/Speaker Cabinets IRs"
for p in glob.glob(d + "/*.wav"):
    with open(p,'rb') as f: b = f.read(128)
    i = b.find(b'fmt ')
    if i < 0: print(os.path.basename(p), "no fmt chunk"); continue
    fmt,ch,sr,br,ba,bits = struct.unpack('<HHIIHH', b[i+8:i+8+16])
    kind = {1:"PCM-int",3:"IEEE-float",0xFFFE:"extensible"}.get(fmt, "fmt%d"%fmt)
    print("%-32s %s ch=%d rate=%d bits=%d  size=%dB%s" % (
        os.path.basename(p), kind, ch, sr, bits, os.path.getsize(p),
        "" if sr==48000 else "  <-- NOT 48k (engine runs 48k; IR will be off)"))
PY
echo "=== DONE ==="
