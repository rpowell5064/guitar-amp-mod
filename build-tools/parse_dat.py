#!/usr/bin/env python3
# Dump the Hex Forge preset store (.dat): flat idx, bank/slot, used, name, out_level (vals[7]).
import struct, sys
p = sys.argv[1] if len(sys.argv) > 1 else "/home/pistomp/.config/hexchain/hexforge-presets.dat"
b = open(p, "rb").read()
o = 0
def u32():
    global o; v = struct.unpack_from("<I", b, o)[0]; o += 4; return v
ver = u32(); banks = u32(); slots = u32(); nports = u32(); frev = u32()
print(f"ver={ver} banks={banks} slots={slots} nports={nports} factoryRev={frev}")
for flat in range(banks*slots):
    used = u32()
    name = b[o:o+32].split(b'\0')[0].decode('latin1'); o += 32
    vals = struct.unpack_from("<%df" % nports, b, o); o += 4*nports
    for _ in range(4):
        ln = u32(); o += ln
    out_level = vals[7]
    if used:
        print(f"{flat:2d}  B{flat//slots+1}{'ABCD'[flat%slots]}  used={used}  out={out_level:7.2f}  {name}")
