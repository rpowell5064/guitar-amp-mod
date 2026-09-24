# ─────────────────────────────────────────────────────────────────────────────
# Hex Forge factory-preset generator (2026-09-24 rework).
#
# Emits lv2/hexforge/hexforge_factory_presets.h: every factory preset as a full
# float vals[HF_N_PORTS] row at the CURRENT port layout (so the plugin seeds them
# with no migration), plus the built-in cab IR sentinels for Cab 1 / Cab 2.
#
# Two layers:
#   1. build-tools/preset_base.json — the on-device store captured 2026-09-24
#      (every preset exactly as it played then, the user's dial-ins included).
#      Anything the rework does not touch keeps that value.
#   2. build-tools/preset_rework.py — the rework itself: named block params,
#      chain order, cab RIGS (the same rows the Cab panel's Rig selector uses,
#      parsed from script-hexforge.js so there is one source of truth), and a
#      loudness class per preset.
# Loudness: build-tools/preset_levels.json holds each preset's measured RMS/peak
# at a forced out_level of -20 dB (build-tools/hexforge_meas.cpp on the device);
# out_level is derived here so the whole set sits at one level.
#
# Run from the repo root:  python build-tools/gen_hexforge_presets.py
# ─────────────────────────────────────────────────────────────────────────────
import os, sys, json, re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_hexforge as G          # importing only builds the ctrl tables (its writes are __main__-guarded)

NFIXED  = G.NFIXED
CTRL    = G.ctrl
CBY     = G.CTRL_BY_SYM
SYMS    = ["in_l", "in_r", "out_l", "out_r", "control", "notify"][:NFIXED] + [c["sym"] for c in CTRL] + ["midi_in"]
N_PORTS = len(SYMS)
SYM_IDX = {s: i for i, s in enumerate(SYMS)}
LAYOUT_VER = 51                   # hfSerialize blob version this layout corresponds to (kFactoryTableVer)

BANKS, SLOTS = 32, 4

# ── base layer ───────────────────────────────────────────────────────────────
_base = json.load(open(os.path.join(HERE, "preset_base.json")))
assert _base["syms"] == SYMS, "preset_base.json was captured at a different port layout — re-capture or migrate it"
BASE = {}
for p in _base["presets"]:
    if p["used"]:
        BASE[(p["bank"], p["slot"])] = p

def _enum_label_map(sym):
    c = CBY[sym]
    if c["kind"] != "e" or not c.get("scale"):
        return None
    return {lbl.lower(): int(val) for lbl, val in c["scale"]}

def resolve(sym, val):
    """Resolve a user value (number, or enum label string) to a float for vals[]."""
    if sym not in CBY:
        raise KeyError("unknown Hex Forge port symbol: %s" % sym)
    if isinstance(val, str):
        m = _enum_label_map(sym)
        if m is None:
            raise ValueError("port %s is not an enum; got string %r" % (sym, val))
        key = val.lower()
        if key in m:
            return float(m[key])
        hits = [lbl for lbl in m if key in lbl]
        if len(hits) == 1:
            return float(m[hits[0]])
        raise ValueError("port %s has no scalePoint %r (have: %s)" % (sym, val, ", ".join(sorted(m))))
    return float(val)

# ── cab rigs: the Rig selector's factory rows, parsed from the modgui script ─
def _load_rigs():
    js = open(os.path.join(REPO, "lv2/modgui-hexforge/script-hexforge.js"), encoding="utf-8").read()
    m = re.search(r"var RIG_SYMS = \{(.*?)\};", js, re.S)
    syms = {}
    for key in ("cab", "cab2"):
        mm = re.search(key + r":\s*\[(.*?)\]", m.group(1), re.S)
        syms[key] = re.findall(r"'([a-z0-9_]+)'", mm.group(1))
    m = re.search(r"var RIGS = \[(.*?)\n\s*\];", js, re.S)
    rigs = {}
    for row in re.finditer(r"\['([^']*)',\s*'([^']*)',\s*'([^']*)',\s*([^\]]*)\]", m.group(1)):
        variant, note, ir, rest = row.groups()
        vals = [float(x) for x in rest.replace(" ", "").split(",") if x != ""]
        assert len(vals) == 17, (variant, len(vals))
        rigs[(ir, variant)] = vals
    return syms, rigs
RIG_SYMS, RIGS = _load_rigs()

# ── the rework DSL ───────────────────────────────────────────────────────────
PRESETS = {}          # (bank, slot) -> dict(name, cls, vals, ir, ir2)
SANITISED = []        # captured values replaced by port defaults (reported at generation)
CLASSES = {}          # (bank, slot) -> loudness class
BLOCKS = ["it", "gt", "cp", "fz", "dr", "amp", "cab", "md", "dl", "rv", "wh", "oc", "nail", "eq",
          "gt2", "cp2", "fz2", "dr2", "md2", "dl2", "rv2", "wh2", "oc2", "nail2", "eq2"]
MOVABLE = ["gt", "cp", "fz", "dr", "amp", "cab", "md", "dl", "rv", "wh", "oc", "nail", "eq"]   # canonical order

def _blank_vals():
    v = [0.0] * N_PORTS
    for i, c in enumerate(CTRL):
        v[NFIXED + i] = float(c["df"])
    return v

def preset(bank, slot, name, cls="dirty", base=True, chain=None, rig=None, rig2=None,
           cab_ir=None, cab2_ir=None, **blocks):
    """Define one factory preset.
    base=True starts from the 2026-09-24 store capture of that slot (falls back to
    port defaults when the slot was empty); base=False starts from port defaults.
    chain: list of block prefixes in signal order (input trim is always first);
           every listed block is enabled, every other movable block disabled.
    rig / rig2: (ir, variant) from the Cab panel's Rig selector, e.g. ("@factory", "Studio Pair").
    cab_ir / cab2_ir: built-in sentinel or empty (overrides the rig's IR).
    blocks: prefix -> {param: value}; enum params accept scalePoint labels."""
    if not (0 <= bank < BANKS and 0 <= slot < SLOTS):
        raise ValueError("bank/slot out of range: %d/%d" % (bank, slot))
    key = (bank, slot)
    src = BASE.get(key) if base else None
    v = list(src["vals"]) if src else _blank_vals()
    if src:   # a captured value outside its port's range (a port that post-dates the capture's
              # migration, e.g. fv_locut = 0 on a 40..140 port) falls back to the port default
        for i, c in enumerate(CTRL):
            x = v[NFIXED + i]
            if x < c["mn"] - 1e-6 or x > c["mx"] + 1e-6:
                v[NFIXED + i] = float(c["df"])
                SANITISED.append("%s %s=%g -> %g" % (name, c["sym"], x, c["df"]))
    ir  = (src["paths"]["ir"] if src else "") or ""
    ir2 = (src["paths"]["i2"] if src else "") or ""
    if not src:
        v[SYM_IDX["it_enable"]] = 1.0; v[SYM_IDX["amp_enable"]] = 1.0; v[SYM_IDX["cab_enable"]] = 1.0; v[SYM_IDX["gt_enable"]] = 1.0
    v[SYM_IDX["out_doubler"]] = 0.0   # no factory preset ships the output doubler (user 2026-09-24); a block may still opt in
    if chain is not None:
        # Positions are 1-based over the 13 movable blocks (input trim has none);
        # every movable block keeps a unique position even when disabled, and the
        # X2 clones stay parked at their captured slots (15..24) unless listed.
        listed = [b for b in chain if b != "it"]
        for b in listed:
            assert b in BLOCKS and b != "it", "unknown chain block %r" % b
        rest = [b for b in MOVABLE if b not in listed]
        seq = listed + rest
        for b in BLOCKS:
            if b == "it": continue
            en = b + "_enable"
            if en in SYM_IDX:
                v[SYM_IDX[en]] = 1.0 if b in listed else 0.0
        for pos, b in enumerate(seq, start=1):
            v[SYM_IDX[b + "_pos"]] = float(pos)
    for scope, r in (("cab", rig), ("cab2", rig2)):
        if r is None: continue
        if r not in RIGS:
            raise KeyError("no rig %r (have %s)" % (r, sorted(RIGS)))
        for sym, val in zip(RIG_SYMS[scope], RIGS[r]):
            v[SYM_IDX[sym]] = float(val)
        if scope == "cab": ir = r[0]
        else:              ir2 = r[0]
    for pfx, params in blocks.items():
        for k, val in params.items():
            sym = pfx + "_" + k
            v[SYM_IDX[sym]] = resolve(sym, val)
    if cab_ir is not None:  ir  = cab_ir
    if cab2_ir is not None: ir2 = cab2_ir
    if ir  in ("@factory", "@builtin"): ir  = ""      # empty = the Factory Cab
    if ir2 in ("@factory", "@builtin"): ir2 = ""
    if len(name.encode("utf-8")) > 31:
        raise ValueError("preset name too long (max 31 bytes): %r" % name)
    PRESETS[key] = dict(bank=bank, slot=slot, name=name, vals=v, ir=ir, ir2=ir2)
    CLASSES[key] = cls
    return PRESETS[key]

# ── loudness ─────────────────────────────────────────────────────────────────
# Targets (dBFS RMS on the reference DI, out_level forced to -20 during measurement):
TARGET = {"dirty": -12.5, "clean": -13.0, "sunn": -11.5, "bass": -13.0}
PEAK_CAP = {"dirty": -1.0, "clean": -1.0, "sunn": -1.0, "bass": 5.0}   # bass: the guitar DI's pick thump is
                                                                        # not bass programme; the output limiter covers it
def apply_levels():
    path = os.path.join(HERE, "preset_levels.json")
    if not os.path.exists(path):
        print("preset_levels.json missing — out_level left as captured/authored")
        return
    lv = json.load(open(path))
    hit = miss = 0
    for key, p in PRESETS.items():
        m = lv.get(p["name"])
        if not m:
            miss += 1; continue
        cls = CLASSES[key]
        out = -20.0 + (TARGET[cls] - m["rms"])
        cap = PEAK_CAP[cls]
        if m["peak"] + (out + 20.0) > cap:               # peak cap wins
            out = -20.0 + (cap - m["peak"])
        out = max(-60.0, min(12.0, out))
        p["vals"][SYM_IDX["out_level"]] = round(out, 2)
        hit += 1
    print("levels applied: %d, unmeasured: %d" % (hit, miss))

# ── checks + emit ────────────────────────────────────────────────────────────
def _range_violations(vals):
    bad = []
    for i, c in enumerate(CTRL):
        x = vals[NFIXED + i]
        if x < c["mn"] - 1e-6 or x > c["mx"] + 1e-6:
            bad.append("%s=%g (%g..%g)" % (c["sym"], x, c["mn"], c["mx"]))
    return bad

def _fmt(x):
    if x == int(x) and abs(x) < 1e15:
        return "%d.0f" % int(x)
    return "%.9gf" % x

def emit_header():
    L = []
    L.append("// AUTO-GENERATED by build-tools/gen_hexforge_presets.py — do not edit by hand.")
    L.append("// Every factory preset as a full row at the current port layout (kFactoryTableVer).")
    L.append("#pragma once")
    L.append("#include \"hexforge_ports.h\"")
    L.append("#include <cstdint>")
    L.append("static constexpr uint32_t kFactoryTableVer = %d;   // blob layout the rows below were generated at" % LAYOUT_VER)
    L.append("struct HfFactoryPreset { int bank; int slot; const char* name; const char* cabIr; const char* cabIr2; float vals[HF_N_PORTS]; };")
    L.append("static const HfFactoryPreset kFactoryExtra[] = {")
    for key in sorted(PRESETS):
        p = PRESETS[key]
        vals = ", ".join(_fmt(x) for x in p["vals"])
        ir  = ('"%s"' % p["ir"])  if p["ir"]  else "nullptr"
        ir2 = ('"%s"' % p["ir2"]) if p["ir2"] else "nullptr"
        L.append('  { %d, %d, "%s", %s, %s, { %s } },' % (p["bank"], p["slot"], p["name"], ir, ir2, vals))
    L.append("};")
    L.append("static const int kFactoryExtraCount = %d;" % len(PRESETS))
    L.append("")
    return "\n".join(L)

def emit_manifest():
    L = ["# bank slot idx name class  (idx = bank*4+slot, the hfmeas argument)"]
    for key in sorted(PRESETS):
        p = PRESETS[key]
        L.append("%d %d %d %s|%s" % (p["bank"], p["slot"], p["bank"] * SLOTS + p["slot"], p["name"], CLASSES[key]))
    return "\n".join(L) + "\n"

if __name__ == "__main__":
    # preset_rework binds to the IMPORTED module instance (not this __main__ one), so
    # everything below reads state through that instance.
    import gen_hexforge_presets as M
    import preset_rework          # defines every preset through preset(...)
    preset_rework.build()
    for key, p in M.PRESETS.items():
        bad = M._range_violations(p["vals"])
        assert not bad, "%s has out-of-range params: %s" % (p["name"], bad)
    M.apply_levels()
    if M.SANITISED:
        from collections import Counter
        cnt = Counter(x.split(" ", 1)[1].split("=")[0] for x in M.SANITISED)
        print("sanitised captured values: " + ", ".join("%s x%d" % kv for kv in cnt.most_common()))
    out = os.path.join(REPO, "lv2/hexforge/hexforge_factory_presets.h")
    open(out, "w", encoding="utf-8", newline="\n").write(M.emit_header())
    open(os.path.join(HERE, "preset_manifest.txt"), "w", encoding="utf-8", newline="\n").write(M.emit_manifest())
    print("wrote %s (%d presets, layout v%d, %d ports)" % (out, len(M.PRESETS), LAYOUT_VER, N_PORTS))
