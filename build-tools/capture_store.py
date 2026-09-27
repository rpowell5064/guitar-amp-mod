#!/usr/bin/env python3
# Capture the on-device Hex Forge preset store (.dat) as JSON in the shape
# build-tools/preset_base.json uses, so a store can be diffed against the factory
# table and any user-saved slot baked back in before a kFactoryRev bump (a reseed
# overwrites every factory slot, so this must happen BEFORE the bump).
#
# Runs anywhere Python 3 does — on the Pi over ssh, or locally on a copied .dat.
#   python build-tools/capture_store.py <store.dat> [out.json]
# Default store path on the pi-Stomp: ~/.config/hexchain/hexforge-presets.dat
#
# Values are carried by SYMBOL: the store records how many ports the binary that
# wrote it had, and a store written by an older build is mapped onto the current
# layout by name (ports it never had take the generator's default). That keeps a
# capture usable across port appends instead of silently shifting every value.
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_hexforge as G  # noqa: E402  (import only builds the port tables)

NFIXED = G.NFIXED
SYMS = ["in_l", "in_r", "out_l", "out_r", "control", "notify"][:NFIXED] \
       + [c["sym"] for c in G.ctrl] + ["midi_in"]
DEF = {c["sym"]: float(c["df"]) for c in G.ctrl}
# Ports appended over time, newest LAST: a store with fewer ports than the current
# layout is missing exactly the tail of this list, so drop that many from the end.
TAIL_ORDER = ["it_mains"]
PATH_KEYS = ["ir", "an", "dn", "cn", "a2", "i2", "d2n"]   # hfSerialize order


def syms_for(nports):
    """The symbol list a store with `nports` ports was written at."""
    if nports == len(SYMS):
        return SYMS
    missing = len(SYMS) - nports
    if missing < 0 or missing > len(TAIL_ORDER):
        raise SystemExit("store has %d ports, this build has %d — add the appended "
                         "symbols to TAIL_ORDER (newest last)" % (nports, len(SYMS)))
    drop = set(TAIL_ORDER[len(TAIL_ORDER) - missing:])
    return [s for s in SYMS if s not in drop]


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: capture_store.py <store.dat> [out.json]")
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    b = open(path, "rb").read()
    off = 0

    def u32():
        nonlocal off
        v = struct.unpack_from("<I", b, off)[0]
        off += 4
        return v

    ver, banks, slots, nports = u32(), u32(), u32(), u32()
    factory_rev = u32() if ver >= 11 else 0
    stored = syms_for(nports)
    sidx = {s: i for i, s in enumerate(stored)}

    presets = []
    for flat in range(banks * slots):
        used = u32()
        name = b[off:off + 32].split(b"\0")[0].decode("latin1")
        off += 32
        vals = struct.unpack_from("<%df" % nports, b, off)
        off += 4 * nports
        paths = {}
        for k in PATH_KEYS:
            ln = u32()
            paths[k] = b[off:off + ln].decode("latin1")
            off += ln
        presets.append({
            "bank": flat // slots, "slot": flat % slots,
            "used": 1 if used else 0, "name": name,
            "vals": [float(vals[sidx[s]]) if s in sidx else DEF.get(s, 0.0) for s in SYMS],
            "paths": paths,
        })
    cur_bank = u32() if off + 4 <= len(b) else 0
    cur_slot = u32() if off + 4 <= len(b) else 0

    doc = {
        "captured": "capture_store.py from %s (blob v%d, factoryRev %d, %d ports -> %d)"
                    % (os.path.basename(path), ver, factory_rev, nports, len(SYMS)),
        "syms": SYMS,
        "presets": presets,
    }
    used_n = sum(p["used"] for p in presets)
    sys.stderr.write("blob v%d  banks %d  slots %d  ports %d  factoryRev %d  used %d  cursor %d/%d\n"
                     % (ver, banks, slots, nports, factory_rev, used_n, cur_bank, cur_slot))
    text = json.dumps(doc)
    if out:
        open(out, "w", encoding="utf-8", newline="\n").write(text + "\n")
        sys.stderr.write("wrote %s\n" % out)
    else:
        sys.stdout.write(text + "\n")


if __name__ == "__main__":
    main()
