#!/usr/bin/env python3
# Surgically inspect / re-seed the Hex Forge preset store.
#
# The store (v9 blob) is the same layout in two places:
#   - the off-instance backup  ~/.config/hexchain/hexforge-presets.dat   (raw)
#   - the pedalboard State      Hex_Forge.pedalboard/effect-2/effect.ttl  (base64 in #preset_blob)
#
# Layout:  u32 ver, u32 banks, u32 slots, u32 nports,
#          then banks*slots entries: u32 used, char name[32], float vals[nports],
#          then 4 length-prefixed paths (ir, ampNam, drNam, cabNam),
#          then u32 curBank, u32 curSlot.
#
# Re-seed works WITHOUT rewriting values: flip a slot's `used` flag 1->0. On the next
# instantiate, psInitDefaults() seeds the NEW factory values and the plugin's merge rule
# (`if (used==0 && pr.used) continue;`) keeps them instead of the stale stored value.
# Banks 1-2 (user data) are left byte-identical.
#
# Usage:
#   python hexforge_reseed_clean.py inspect <file.dat|effect.ttl>
#   python hexforge_reseed_clean.py clear   <file.dat|effect.ttl> <flat,slots,...>
import sys, struct, base64, re

NAME = 32

def _walk(blob):
    """Return (ver,banks,slots,nports, [used_field_offset per flat slot], used_flags)."""
    off = 0
    def u32():
        nonlocal off
        v = struct.unpack_from("<I", blob, off)[0]; off += 4; return v
    ver, banks, slots, nports = u32(), u32(), u32(), u32()
    used_off, used_flag, names, outlvl = [], [], [], []
    for _ in range(banks * slots):
        used_off.append(off)
        used = u32(); used_flag.append(used)
        names.append(blob[off:off+NAME].split(b"\x00")[0].decode("latin1"))
        off += NAME
        # vals starts right after the 32-byte name; out_level=idx7, dl_enable=94, dl_type=95
        vbase = used_off[-1] + 4 + NAME
        outlvl.append((struct.unpack_from("<f", blob, vbase + 7 * 4)[0],
                       struct.unpack_from("<f", blob, vbase + 94 * 4)[0],
                       struct.unpack_from("<f", blob, vbase + 95 * 4)[0]))
        off += nports * 4
        for _p in range(4):
            ln = u32(); off += ln
    cb, cs = u32(), u32()
    assert off == len(blob), "blob walk mismatch: consumed %d of %d" % (off, len(blob))
    return ver, banks, slots, nports, used_off, used_flag, names, cb, cs, outlvl

def _load(path):
    if path.endswith(".ttl"):
        s = open(path, "r", encoding="utf-8", errors="replace").read()
        m = re.search(r'#preset_blob>\s*"""([^"]*)"""\^\^xsd:base64Binary', s, re.DOTALL)
        if not m or not m.group(1).strip():
            return None, s, None
        b64 = "".join(m.group(1).split())
        return bytearray(base64.b64decode(b64)), s, m.span(1)
    return bytearray(open(path, "rb").read()), None, None

def _save(path, blob, ttl_text, span):
    if path.endswith(".ttl"):
        b64 = base64.b64encode(bytes(blob)).decode("ascii")
        new = ttl_text[:span[0]] + b64 + ttl_text[span[1]:]
        open(path, "w", encoding="utf-8", newline="\n").write(new)
    else:
        open(path, "wb").write(bytes(blob))

def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    cmd, path = sys.argv[1], sys.argv[2]
    blob, ttl_text, span = _load(path)
    if blob is None:
        print("  (preset_blob empty or absent in %s)" % path); return
    ver, banks, slots, nports, used_off, used_flag, names, cb, cs, outlvl = _walk(blob)
    print("ver=%d banks=%d slots=%d nports=%d cur=%d/%d  (%d bytes)"
          % (ver, banks, slots, nports, cb, cs, len(blob)))
    DLT = {0: "Digital", 1: "Tape", 2: "EchoWreck", 3: "Seraph"}
    for fl in range(banks * slots):
        if used_flag[fl]:
            ol, den, dty = outlvl[fl]
            dl = DLT.get(int(round(dty)), "?") if den > 0.5 else "-"
            print("  flat %2d  B%d%s  out=%7.1f dB  dl=%-8s  %r"
                  % (fl, fl // slots + 1, "ABCD"[fl % slots], ol, dl, names[fl]))
    if cmd == "dump":
        fl = int(sys.argv[3])
        base = used_off[fl] + 4 + NAME          # vals[] start for this slot
        vals = struct.unpack_from("<16f", blob, base)
        print("  flat %d %r vals[0..15]: %s" % (fl, names[fl], ["%.3g" % v for v in vals]))
        return
    if cmd == "inspect":
        return
    if cmd == "clear":
        targets = [int(x) for x in sys.argv[3].split(",") if x != ""]
        n = 0
        for fl in targets:
            if used_flag[fl]:
                struct.pack_into("<I", blob, used_off[fl], 0)   # used -> 0
                n += 1
                print("  CLEARED flat %2d  B%d%s  %r" % (fl, fl // slots + 1, "ABCD"[fl % slots], names[fl]))
            else:
                print("  (flat %2d already used=0 / empty)" % fl)
        # re-walk to prove the blob is still structurally valid after the edit
        _walk(blob)
        _save(path, blob, ttl_text, span)
        print("wrote %s (%d slots cleared)" % (path, n))

if __name__ == "__main__":
    main()
