#!/usr/bin/env python3
"""Re-lay out preset_base.json / preset_user_saves.json for a port appended to the Hex Forge
layout: insert the new symbol (at the generator's position) with its default into `syms`
and every captured `vals` row, and note it in `relayout`. Values of every existing port
carry over by symbol. Each file keeps its own style (preset_base.json is one compact line,
preset_user_saves.json is indent=1) so the diff shows only the change.

Usage: python build-tools/migrate_preset_layout.py <sym> <default>
(the position is read from gen_hexforge.py's current port order)
"""
import json, os, sys, datetime
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_hexforge as G


def main():
    sym, dflt = sys.argv[1], float(sys.argv[2])
    order = [f.lower() for f in G.FIXED] + [c["sym"] for c in G.ctrl]
    assert sym in order, sym
    here = os.path.dirname(os.path.abspath(__file__))
    for fn in ("preset_base.json", "preset_user_saves.json"):
        p = os.path.join(here, fn)
        raw = open(p, encoding="utf-8").read()
        compact = raw.count("\n") < 5
        d = json.loads(raw)
        syms = d["syms"]
        if sym in syms:
            print(fn, "already has", sym)
            continue
        prev = order[order.index(sym) - 1]          # the symbol that precedes it in the generator order
        at = syms.index(prev) + 1
        syms.insert(at, sym)
        presets = d["presets"] if isinstance(d["presets"], list) else list(d["presets"].values())
        n = 0
        for e in presets:
            v = e.get("vals")
            if isinstance(v, list) and len(v) == len(syms) - 1:
                v.insert(at, dflt)
                n += 1
        d.setdefault("relayout", []).append(
            "%s: re-laid out by symbol for the new port %s at index %d (values carried over; the new port takes its generator default %g)"
            % (datetime.date.today().isoformat(), sym, at, dflt))
        with open(p, "w", encoding="utf-8", newline="\n") as f:
            json.dump(d, f, indent=None if compact else 1)
            if not compact:
                f.write("\n")
        print(fn, "->", sym, "at", at, "rows migrated", n, "syms", len(syms))


if __name__ == "__main__":
    main()
