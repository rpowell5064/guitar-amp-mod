# ─────────────────────────────────────────────────────────────────────────────
# Hex Forge port generator — single source of truth.
#
# Emits two files that MUST stay in lock-step:
#   lv2/hexforge.ttl                 — LV2 ports + scalePoints + modgui:port list
#   lv2/hexforge/hexforge_ports.h    — HF_* port-index enum used by the DSP code
#
# Hex Forge is one LV2 plugin that hosts the whole Hex Chain in a reorderable
# chain. Input Trim is locked first; the other 9 blocks each carry a `pos`
# (slot 1..9) and `bypass` control so the UI can reorder / toggle them.
# Run from the repo root:  python build-tools/gen_hexforge.py
# ─────────────────────────────────────────────────────────────────────────────
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
URI  = "https://rpowell5064.github.io/guitaramp-suite/hexforge"

# kind → (portProperty list, unit URI or None)
# 'f' float, 'db'/'ms'/'hz' float+unit, 't' toggle, 'i' integer, 'e' enum int.
def fmt(v, integral):
    if integral:
        return str(int(round(v)))
    s = "%g" % v
    if "." not in s and "e" not in s and "E" not in s:
        s += ".0"
    return s

# Block param tables: (suffix, Name, kind, min, max, default, scalepoints)
# scalepoints only for kind 'e'.
IT = [
    ("gain",  "Gain",       "db", -20, 20, 0, None),
    ("phase", "Phase",      "t",  0, 1, 1, None),
    ("hum",   "Hum Filter", "t",  0, 1, 1, None),
    # Single-coil -> humbucker voicing (off by default). When engaged, re-voices a
    # Tele single coil toward one of three target bridge humbuckers (verified in
    # tools/pickup_voicing.py). HB Amount scales the whole voicing 0..100%.
    # NOTE: humbk/hbamt keep their v4 port indices (13/14); hbmodel is appended
    # after hbamt so existing saved boards stay aligned (see preset migration).
    ("humbk",   "Humbucker", "t", 0, 1, 0, None),
    ("hbamt",   "HB Amount", "f", 0, 1, 1.0, None),
    ("hbmodel", "HB Model",  "e", 0, 2, 0,
        [("'59 Bucker", 0), ("Norse Hammer", 1), ("Modern Flux", 2)]),
    # Output boost (off by default): pickup-agnostic level + low-mid "beef" bump.
    # Fattens single coils / thickens humbuckers. Appended after the voicing ports
    # so prior port indices stay put (see preset migration). Amount 0 dB = bypass.
    ("boost",    "Boost",     "t",  0, 1, 0, None),
    ("boostamt", "Boost Amt", "db", 0, 12, 4, None),
]
GT = [
    ("thresh",  "Threshold",  "db", -80, 0, -60, None),
    ("attack",  "Attack",     "ms", 0.1, 50, 0.1, None),
    ("hold",    "Hold",       "ms", 0, 500, 50, None),
    ("release", "Release",    "ms", 10, 2000, 100, None),
    ("hyst",    "Hysteresis", "db", 0, 20, 6, None),
]
CP = [
    ("type",    "Type",      "e", 0, 1, 0, [("5 Creature Amp",0),("Once76",1)]),
    ("thresh",  "Threshold", "db", -60, 0, -18, None),
    ("ratio",   "Ratio",     "e", 0, 4, 1, [("2:1",0),("4:1",1),("8:1",2),("20:1",3),("Limit",4)]),
    ("attack",  "Attack",    "f", 0, 10, 5, None),
    ("release", "Release",   "f", 0, 10, 5, None),
    ("knee",    "Knee",      "f", 0, 10, 3, None),
    ("makeup",  "Makeup",    "f", 0, 10, 0, None),
]
FZ = [
    ("pedal",     "Pedal",      "e", 0, 2, 0, [("Italian Hero",0),("I Know It",1),("Octavia",2)]),
    ("mode",      "Variant",    "e", 0, 5, 2, [("Delta",0),("Ovis",1),("Gotham",2),("Cold War",3),("Red Bear",4),("Boutique",5)]),
    ("sustain",   "Sustain",    "f", 0, 1, 0.55, None),
    ("tone",      "Tone",       "f", 0, 1, 0.5, None),
    ("volume",    "Volume",     "f", 0, 1, 0.65, None),
    ("bias",      "Bias",       "f", 0, 1, 0.5, None),
    ("inputtrim", "Input Trim", "f", 0, 1, 0.5, None),
    ("getemp",    "Ge Temp",    "f", 0, 1, 0.4, None),
]
DR = [
    ("model",  "Model",  "e", 0, 3, 0, [("Green Man",0),("New Dawn",1),("Dear Rodent Boy",2),("Neural (NAM)",3)]),
    ("drive",  "Drive",  "f", 0, 1, 0.5, None),
    ("tone",   "Tone",   "f", 0, 1, 0.5, None),
    ("level",  "Level",  "f", 0, 1, 0.5, None),
    ("mix",    "Mix",    "f", 0, 1, 1.0, None),
    ("octave", "Octave", "f", 0, 1, 0.3, None),
]
AMP = [
    ("model",         "Model",        "e", 0, 7, 1, [("Clean Meanie",0),("Crunchy McCrunchFace",1),("Gainzilla",2),("Doom Daddy",3),("Tangerang",4),("Neural (NAM)",5),("Beardo BE",6),("Hiwatt",7)]),
    ("gain",          "Gain",         "f", 0, 1, 0.5, None),
    ("bass",          "Bass",         "f", 0, 1, 0.5, None),
    ("mid",           "Mid",          "f", 0, 1, 0.5, None),
    ("treble",        "Treble",       "f", 0, 1, 0.5, None),
    ("presence",      "Presence",     "f", 0, 1, 0.5, None),
    ("master",        "Master",       "f", 0, 1, 0.7, None),
    ("sag",           "Sag",          "f", 0, 1, 0.3, None),
    ("channel",       "Channel",      "t", 0, 1, 0, None),
    ("resonance",     "Resonance",    "f", 0, 1, 0, None),
    ("sunn_vol2",     "Brite Vol",    "f", 0, 1, 0.5, None),
    ("sunn_link",     "Ch Link",      "e", 0, 2, 0, [("Independent",0),("Parallel",1),("Series",2)]),
    ("pamp_bypass",   "PA Bypass",    "t", 0, 1, 0, None),
    ("pamp_tube",     "Power Tube",   "e", 0, 3, 1, [("6L6GC",0),("EL34",1),("EL84",2),("KT88",3)]),
    ("pamp_presence", "PA Presence",  "f", 0, 1, 0.55, None),
    ("pamp_depth",    "PA Depth",     "f", 0, 1, 0.18, None),
    ("pamp_sag",      "PA Sag",       "f", 0, 1, 0.33, None),
    ("pamp_master",   "PA Master",    "f", 0, 1, 0.62, None),
    ("pamp_nfb",      "PA NFB",       "f", 0, 1, 0.42, None),
    ("pamp_resonance","PA Resonance", "f", 0, 1, 0.5, None),
    ("pamp_airfeel",  "Air Feel",     "t", 0, 1, 0, None),
    ("pamp_auto",     "PA Auto",      "t", 0, 1, 1, None),
    ("sunn_bass2",    "Brite Bass",   "f", 0, 1, 0.5, None),
    ("sunn_mid2",     "Brite Mid",    "f", 0, 1, 0.5, None),
    ("sunn_treble2",  "Brite Treble", "f", 0, 1, 0.5, None),
    ("sunn_bright1",  "Bright I",     "t", 0, 1, 0, None),
    ("sunn_bright2",  "Bright II",    "t", 0, 1, 0, None),
]
CAB = [
    ("lowcut",  "Low Cut",  "hz", 20, 500, 80, None),
    ("highcut", "High Cut", "hz", 2000, 20000, 16000, None),
    ("mix",     "Mix",      "f", 0, 1, 1.0, None),
]
MD = [
    ("type",  "Type",  "e", 0, 5, 0, [("Lush-2",0),("Uni-Verse",1),("Phaser",2),("Flanger",3),("Tremolo",4),("Rotary",5)]),
    ("rate",  "Rate",  "f", 0, 1, 0.5, None),
    ("depth", "Depth", "f", 0, 1, 0.5, None),
    ("mix",   "Mix",   "f", 0, 1, 0.5, None),
    ("width", "Width", "f", 0, 1, 0.5, None),
]
DL = [
    ("type",    "Type",     "e", 0, 3, 0, [("Digital",0),("Tape",1),("Echo Wreck",2),("Seraph",3)]),
    ("time",    "Time",     "ms", 1, 2000, 250, None),
    ("feedback","Feedback", "f", 0, 0.98, 0.4, None),
    ("mix",     "Mix",      "f", 0, 1, 0.15, None),
    ("width",   "Width",    "f", 0, 1, 0.5, None),
    ("wow",     "Wow",      "f", 0, 0.05, 0.003, None),
    ("flutter", "Flutter",  "f", 0, 0.02, 0.001, None),
    ("heads",   "Heads",    "e", 0, 11, 10, [
        ("1: Head 1",0),("2: Head 2",1),("3: Head 3",2),("4: Head 4",3),
        ("5: Heads 1+2",4),("6: Heads 2+3",5),("7: Heads 3+4",6),
        ("8: Heads 1+2+3",7),("9: Heads 2+3+4",8),("10: Heads 1+3+4",9),
        ("11: All Heads",10),("12: All + Dense",11)]),
    # Seraph (dual-delay) params — shown only when Type=Seraph (c-dl-seraph).
    ("pattern",  "Pattern",   "e", 0, 3, 1, [("Unison",0),("Dotted 8th",1),("Triplet",2),("Eighth",3)]),
    ("ducking",  "Ducking",   "f", 0, 1, 0.0, None),
    ("moddepth", "Mod Depth", "f", 0, 1, 0.0, None),
    ("modrate",  "Mod Rate",  "f", 0, 1, 0.3, None),
]
RV = [
    ("predelay", "Pre-Delay", "ms", 0, 100, 10, None),
    ("decay",    "Decay",     "f", 0.1, 8, 1.5, None),
    ("damping",  "Damping",   "f", 0, 0.99, 0.3, None),
    ("moddepth", "Mod Depth", "f", 0, 1, 0.0, None),
    ("modrate",  "Mod Rate",  "f", 0.01, 5, 0.8, None),
    ("mix",      "Mix",       "f", 0, 1, 0.15, None),
]
WAH = [
    ("type",  "Mode",      "e", 0, 1, 0, [("Auto",0),("Fixed",1)]),
    ("freq",  "Freq",      "f", 0, 1, 0.4, None),
    ("depth", "Range",     "f", 0, 1, 0.7, None),
    ("sens",  "Sens",      "f", 0, 1, 0.5, None),
    ("q",     "Resonance", "f", 0, 1, 0.6, None),
    ("mix",   "Mix",       "f", 0, 1, 0.8, None),
]
OCTAVE = [
    ("up",   "Octave Up",  "f", 0, 1, 0.0, None),
    ("down", "Sub Octave", "f", 0, 1, 0.5, None),
    ("dry",  "Dry",        "f", 0, 1, 1.0, None),
]

# Movable blocks in canonical default order. (prefix, Title, params, default-pos)
MOVABLE = [
    ("gt",  "Gate",       GT,  1),
    ("cp",  "Comp",       CP,  2),
    ("fz",  "Fuzz",       FZ,  3),
    ("dr",  "Drive",      DR,  4),
    ("amp", "Amp",        AMP, 5),
    ("cab", "Cabinet",    CAB, 6),
    ("md",  "Modulation", MD,  7),
    ("dl",  "Delay",      DL,  8),
    ("rv",  "Reverb",     RV,  9),
    ("wh",  "Wah",        WAH, 10),
    ("oc",  "Octave",     OCTAVE, 11),
]

UNIT = {"db": "units:db", "ms": "units:ms", "hz": "units:hz"}

# ── Build the ordered control-port list ───────────────────────────────────────
# Each entry: dict(enum, sym, name, kind, mn, mx, df, scale)
def mkport(enum, sym, name, kind, mn, mx, df, scale, short=None, hidden=False, out=False):
    # hidden=True → pprops:notOnGUI: kept out of MOD's generic/advanced control list
    # (still driven by the custom modgui). Used for the chain-plumbing slot ports.
    # out=True → an OutputPort (plugin -> UI), e.g. the clip indicator.
    return dict(enum=enum, sym=sym, name=name, kind=kind, mn=mn, mx=mx, df=df,
                scale=scale, short=short or name, hidden=hidden, out=out)

ctrl = []
# global bypass first
ctrl.append(mkport("BYPASS", "bypass", "Bypass", "t", 0, 1, 0, None))
# master output level — applied LAST in the chain (the "Output" stage that feeds
# the device). dB-scaled like the stock MOD gain block: 0 dB = unity, with up to
# +12 dB make-up boost; the linear knob throw is a natural dB taper. Default
# -13 dB ~= the old 0.23 linear gain so loudness is unchanged. Shown in the top
# Output strip, not as a chain tile. (The plugin converts dB -> linear gain.)
ctrl.append(mkport("OUT_LEVEL", "out_level", "Output Level", "db", -60, 12, -20, None, "Master"))
# output clip indicator (plugin -> UI): 1 while the output is hitting full scale.
ctrl.append(mkport("CLIP", "clip", "Clip", "f", 0, 1, 0, None, "Clip", out=True))
# input trim (locked): enable + params, no pos. Per-block toggles use ENABLE
# semantics (1 = on/active, default on) so a lit tile switch means "block engaged".
ctrl.append(mkport("IT_ENABLE", "it_enable", "Input Trim Enable", "t", 0, 1, 1, None, "On"))
for suf, nm, kind, mn, mx, df, sc in IT:
    ctrl.append(mkport("IT_" + suf.upper(), "it_" + suf, "IT " + nm, kind, mn, mx, df, sc, nm))
# movable blocks: pos, enable, params
# Fresh Hex Forge starts with a usable core chain engaged: Input Trim, Gate, Amp,
# Cabinet, Delay and Reverb. Comp, Fuzz, Drive and Mod start bypassed.
OFF_BY_DEFAULT = {"cp", "fz", "dr", "md", "wh", "oc"}   # off: Comp, Fuzz, Drive, Mod, Wah, Octave
for pfx, title, params, dpos in MOVABLE:
    P = pfx.upper()
    posscale = [(str(k), k) for k in range(1, 12)]   # 1..11 dropdown for reordering
    en_def = 0 if pfx in OFF_BY_DEFAULT else 1
    ctrl.append(mkport(P + "_POS",    pfx + "_pos",    title + " Position", "e", 1, 11, dpos, posscale, "Slot", hidden=True))
    ctrl.append(mkport(P + "_ENABLE", pfx + "_enable", title + " Enable",   "t", 0, 1, en_def, None, "On"))
    for suf, nm, kind, mn, mx, df, sc in params:
        ctrl.append(mkport(P + "_" + suf.upper(), pfx + "_" + suf, title + " " + nm, kind, mn, mx, df, sc, nm))

# Beardo BE (Friedman) amp controls — its own 3-way channel + Fat/C45/Sat toggles.
# Appended at the END of the param range (right before the preset command ports) so
# the existing preset blob layout is preserved (the cab/mod/delay/reverb block ports
# keep their indices); old presets simply default these to Clean/off.
AMP_FR = [
    ("fr_channel", "BE Chan", "e", 0, 2, 1, [("Clean", 0), ("BE", 1), ("HBE", 2)]),
    ("fr_fat",     "Fat",     "t", 0, 1, 0, None),
    ("fr_c45",     "C45",     "t", 0, 1, 0, None),
    ("fr_sat",     "Sat",     "t", 0, 1, 0, None),
]
for suf, nm, kind, mn, mx, df, sc in AMP_FR:
    ctrl.append(mkport("AMP_" + suf.upper(), "amp_" + suf, "Amp " + nm, kind, mn, mx, df, sc, nm))

# Per-block real BYPASS (active vs greyed-bypassed, for live A/B). Default 0 = active.
# Appended as a contiguous group at the END of the param range (after AMP_FR, before the
# preset command ports) so every existing param index is unchanged and isParamPort still
# captures them; old preset blobs leave these as trailing zero-fill = 0 = active. Order
# matches MOVABLE (= the C++ Block enum) so kBypassPort[] aligns 1:1. Visible (not hidden)
# so a footswitch/MIDI can be addressed to a block's bypass in MOD, like sw_a..d.
# Membership (in-chain vs palette) stays on <pfx>_enable; the DSP runs a block iff
# enable==1 && bypass==0. Input Trim is locked in the chain, so its it_enable doubles as
# its active/bypass dot (no it_bypass).
for pfx, title, params, dpos in MOVABLE:
    P = pfx.upper()
    ctrl.append(mkport(P + "_BYPASS", pfx + "_bypass", title + " Bypass", "t", 0, 1, 0, None, "Byp"))

# ── Preset / bank command + status ports ──────────────────────────────────────
# A/B/C/D recall switches: a rising edge recalls that slot in the current bank.
# These are left visible/addressable (NOT hidden) so the four physical
# footswitches can be mapped to them in MOD's addressing UI (Phase B). The
# bank/save/move commands are pulsed by the custom modgui only, so they're hidden
# from the generic control list. ps_bank/ps_slot are outputs the UI mirrors.
N_PRESET_CMD_FIRST = len(ctrl)   # first preset-command port (for the C++ engine)
for sw, nm in (("a", "A"), ("b", "B"), ("c", "C"), ("d", "D")):
    ctrl.append(mkport("SW_" + sw.upper(), "sw_" + sw, "Preset " + nm, "t", 0, 1, 0, None, "Preset " + nm))
ctrl.append(mkport("PS_BANK_UP", "ps_bank_up", "Bank Up",      "t", 0, 1, 0, None, "Bank+", hidden=True))
ctrl.append(mkport("PS_BANK_DN", "ps_bank_dn", "Bank Down",    "t", 0, 1, 0, None, "Bank-", hidden=True))
ctrl.append(mkport("PS_SAVE",    "ps_save",    "Save Preset",  "t", 0, 1, 0, None, "Save",  hidden=True))
ctrl.append(mkport("PS_MOVE_UP", "ps_move_up", "Move Earlier", "t", 0, 1, 0, None, "Move+", hidden=True))
ctrl.append(mkport("PS_MOVE_DN", "ps_move_dn", "Move Later",   "t", 0, 1, 0, None, "Move-", hidden=True))
# Backup / restore the WHOLE preset store (all 32) to a file outside the plugin
# instance ($HOME/.config/hexchain/hexforge-presets.dat), so presets survive
# deleting+re-adding the plugin and bundle updates. The store is also auto-backed
# up on every save and auto-restored on a fresh instance — these just give the
# user explicit control. Pulsed by the custom modgui only → hidden.
ctrl.append(mkport("PS_BACKUP",  "ps_backup",  "Backup Presets",  "t", 0, 1, 0, None, "Backup",  hidden=True))
ctrl.append(mkport("PS_RESTORE", "ps_restore", "Restore Presets", "t", 0, 1, 0, None, "Restore", hidden=True))
# Jump directly to a flat preset index (bank*4+slot). The UI list sets this; the
# plugin recalls when it changes to a value >= 0. -1 = idle.
ctrl.append(mkport("PS_GOTO", "ps_goto", "Go To Preset", "i", -1, 31, -1, None, "GoTo", hidden=True))
ctrl.append(mkport("PS_BANK", "ps_bank", "Active Bank", "i", 0, 7, 0, None, "Bank", out=True))
ctrl.append(mkport("PS_SLOT", "ps_slot", "Active Slot", "i", 0, 3, 0, None, "Slot", out=True))
# Output auto-limit: when on, a transparent peak limiter on the master output keeps
# it from clipping (ceiling ~0.95). Added at the END so it's outside the preset
# param range (it's a global preference, and keeps the preset blob layout stable).
ctrl.append(mkport("OUT_AUTO", "out_auto", "Output Auto-Limit", "t", 0, 1, 1, None, "Auto"))
# Input / output level meters (plugin -> UI). Output ports, appended at the very END so
# the param/preset layout is untouched (not preset-captured). 0..1 dB-scaled (-60..0 dB).
ctrl.append(mkport("IN_METER",  "in_meter",  "Input Level",  "f", 0, 1, 0, None, "In",  out=True))
ctrl.append(mkport("OUT_METER", "out_meter", "Output Level", "f", 0, 1, 0, None, "Out", out=True))

CTRL_BY_SYM = {c["sym"]: c for c in ctrl}

# ── Fixed leading ports (audio + atom) ────────────────────────────────────────
FIXED = ["IN_L", "IN_R", "OUT_L", "OUT_R", "CONTROL", "NOTIFY"]
NFIXED = len(FIXED)

# ── Emit the C++ enum header ──────────────────────────────────────────────────
def emit_header():
    lines = []
    lines.append("// AUTO-GENERATED by build-tools/gen_hexforge.py — do not edit by hand.")
    lines.append("#pragma once")
    lines.append("enum HexForgePort {")
    idx = 0
    lines.append("    HF_IN_L = 0, HF_IN_R, HF_OUT_L, HF_OUT_R, HF_CONTROL, HF_NOTIFY,")
    idx = NFIXED
    for c in ctrl:
        lines.append("    HF_%s," % c["enum"])
        idx += 1
    lines.append("    HF_MIDI_IN,")   # MIDI input atom port (last index)
    idx += 1
    lines.append("    HF_N_PORTS")
    lines.append("};")
    lines.append("static_assert(HF_N_PORTS == %d, \"port count drift\");" % idx)
    lines.append("")
    # Port symbol table (index -> lv2:symbol). The preset engine emits the recalled
    # snapshot as "sym=val;.." so the modgui can re-sync each knob by symbol.
    syms = ['"in_l"', '"in_r"', '"out_l"', '"out_r"', '"control"', '"notify"']
    syms += ['"%s"' % c["sym"] for c in ctrl]
    syms += ['"midi_in"']
    lines.append("static const char* const HF_PORT_SYM[HF_N_PORTS] = {")
    for i in range(0, len(syms), 6):
        lines.append("    " + ", ".join(syms[i:i+6]) + ",")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)

# ── Emit one TTL control-port block ───────────────────────────────────────────
def ttl_ctrl(c, index):
    integral = c["kind"] in ("t", "i", "e")
    out = []
    out.append("    ] , [")
    out.append("        a lv2:ControlPort, lv2:%s ;" % ("OutputPort" if c.get("out") else "InputPort"))
    out.append('        lv2:index %d ; lv2:symbol "%s" ; lv2:name "%s" ;' % (index, c["sym"], c["name"]))
    if c["kind"] in UNIT:
        out.append("        units:unit %s ;" % UNIT[c["kind"]])
    props = []
    if   c["kind"] == "t": props = ["lv2:toggled"]
    elif c["kind"] == "i": props = ["lv2:integer"]
    elif c["kind"] == "e": props = ["lv2:integer", "lv2:enumeration"]
    if c.get("hidden"):    props.append("pprops:notOnGUI")
    if props:
        out.append("        lv2:portProperty %s ;" % ", ".join(props))
    out.append("        lv2:default %s ; lv2:minimum %s ; lv2:maximum %s%s" % (
        fmt(c["df"], integral), fmt(c["mn"], integral), fmt(c["mx"], integral),
        " ;" if c["kind"] == "e" else ""))
    if c["kind"] == "e":
        sps = []
        for label, val in c["scale"]:
            sps.append('        lv2:scalePoint [ rdfs:label "%s" ; rdf:value %d ]' % (label, val))
        out.append(" ;\n".join(sps))
    return "\n".join(out)

def emit_ttl():
    L = []
    L.append("@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .")
    L.append("@prefix mod:   <http://moddevices.com/ns/mod#> .")
    L.append("@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .")
    L.append("@prefix rdf:   <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .")
    L.append("@prefix units: <http://lv2plug.in/ns/extensions/units#> .")
    L.append("@prefix atom:  <http://lv2plug.in/ns/ext/atom#> .")
    L.append("@prefix patch: <http://lv2plug.in/ns/ext/patch#> .")
    L.append("@prefix state: <http://lv2plug.in/ns/ext/state#> .")
    L.append("@prefix work:  <http://lv2plug.in/ns/ext/worker#> .")
    L.append("@prefix urid:  <http://lv2plug.in/ns/ext/urid#> .")
    L.append("@prefix rsz:   <http://lv2plug.in/ns/ext/resize-port#> .")
    L.append("@prefix pprops:<http://lv2plug.in/ns/ext/port-props#> .")
    L.append("@prefix midi:  <http://lv2plug.in/ns/ext/midi#> .")
    L.append("@prefix modgui:<http://moddevices.com/ns/modgui#> .")
    L.append("@prefix doap:  <http://usefulinc.com/ns/doap#> .")
    L.append("@prefix foaf:  <http://xmlns.com/foaf/0.1/> .")
    L.append("")
    L.append("# ── Cabinet IR file parameter (same as the standalone Cab) ───────────────────")
    L.append("<%s#irfile>" % URI)
    L.append("    a lv2:Parameter ;")
    L.append('    rdfs:label "Cabinet IR" ;')
    L.append("    rdfs:range atom:Path ;")
    L.append('    mod:fileTypes "cabsim,ir,wav,audio" .')
    L.append("")
    L.append("# NAM neural-capture file params (Amp slot 5 / Drive slot 3 / Cab dual-mode).")
    for frag, lbl in (("ampnam", "Amp NAM"), ("drnam", "Drive NAM"), ("cabnam", "Cabinet NAM")):
        L.append("<%s#%s>" % (URI, frag))
        L.append("    a lv2:Parameter ;")
        L.append('    rdfs:label "%s" ;' % lbl)
        L.append("    rdfs:range atom:Path ;")
        L.append('    mod:fileTypes "nammodel,nam" .')
        L.append("")
    L.append("# Preset-engine string params. The plugin owns the 8x4 preset store in")
    L.append("# State; these carry only small status/command strings over the atom ports.")
    L.append("#   ps_name  : UI<->plugin — renames the active preset.")
    L.append('#   ps_index : plugin->UI — "bank|slot|name0|..|name31" for the list.')
    L.append('#   ps_apply : plugin->UI — "sym=val;.." snapshot so the UI re-syncs knobs.')
    L.append("# Declared writable so mod-ui delivers their patch:Set notify messages to the")
    L.append("# custom modgui exactly like the file-picker params (alphabetical indices:")
    L.append("# ampnam=0 cabnam=1 drnam=2 irfile=3 meters=4 ps_apply=5 ps_index=6 ps_name=7).")
    for frag, lbl in (("ps_name", "Preset Name"), ("ps_index", "Preset Index"), ("ps_apply", "Preset Apply"), ("meters", "Level Meters")):
        L.append("<%s#%s>" % (URI, frag))
        L.append("    a lv2:Parameter ;")
        L.append('    rdfs:label "%s" ;' % lbl)
        L.append("    rdfs:range atom:String .")
        L.append("")
    L.append("<%s>" % URI)
    L.append("    a lv2:Plugin, lv2:AmplifierPlugin ;")
    L.append('    rdfs:label "Hex Chain — Hex Forge" ;')
    L.append('    doap:name  "Hex Forge" ;')
    L.append('    mod:label  "Hex Forge" ;')
    L.append('    mod:brand  "Hex Chain" ;')
    # http:// (not https://) — the PatchStorage uploader's licenses.json keys the
    # GPL-3 mapping on the http form; https fails prepare with "Missing license ID".
    L.append("    doap:license <http://www.gnu.org/licenses/gpl-3.0.html> ;")
    L.append('    doap:maintainer [ a foaf:Person ; foaf:name "Ryan Powell" ;')
    L.append("                      foaf:homepage <https://rpowell5064.github.io/guitaramp-suite/> ] ;")
    L.append("    lv2:minorVersion 1 ;")
    L.append("    lv2:microVersion 17 ;")
    L.append("")
    L.append("    # Amp model rebuilds + cab IR loads run on the worker thread.")
    L.append("    lv2:requiredFeature urid:map , work:schedule ;")
    L.append("    lv2:optionalFeature lv2:hardRTCapable ;")
    L.append("    lv2:extensionData state:interface , work:interface ;")
    L.append("")
    # Order here = effect.parameters order in the modgui: 0 irfile,1 ampnam,2 drnam,3 cabnam.
    L.append("    patch:writable <%s#irfile> , <%s#ampnam> , <%s#drnam> , <%s#cabnam> ," % (URI, URI, URI, URI))
    L.append("                   <%s#ps_name> , <%s#ps_index> , <%s#ps_apply> , <%s#meters> ;" % (URI, URI, URI, URI))
    L.append("")
    # audio ports
    L.append("    lv2:port [")
    L.append("        a lv2:AudioPort, lv2:InputPort ;")
    L.append('        lv2:index 0 ; lv2:symbol "in_l" ; lv2:name "In L"')
    L.append("    ] , [")
    L.append("        a lv2:AudioPort, lv2:InputPort ;")
    L.append('        lv2:index 1 ; lv2:symbol "in_r" ; lv2:name "In R"')
    L.append("    ] , [")
    L.append("        a lv2:AudioPort, lv2:OutputPort ;")
    L.append('        lv2:index 2 ; lv2:symbol "out_l" ; lv2:name "Out L"')
    L.append("    ] , [")
    L.append("        a lv2:AudioPort, lv2:OutputPort ;")
    L.append('        lv2:index 3 ; lv2:symbol "out_r" ; lv2:name "Out R"')
    L.append("    ] , [")
    L.append("        a lv2:InputPort, atom:AtomPort ;")
    L.append("        atom:bufferType atom:Sequence ;")
    L.append("        atom:supports patch:Message ;")
    L.append("        lv2:designation lv2:control ;")
    L.append("        rsz:minimumSize 8192 ;")
    L.append('        lv2:index 4 ; lv2:symbol "control" ; lv2:name "Control"')
    L.append("    ] , [")
    L.append("        a lv2:OutputPort, atom:AtomPort ;")
    L.append("        atom:bufferType atom:Sequence ;")
    L.append("        atom:supports patch:Message ;")
    L.append("        lv2:designation lv2:control ;")
    L.append("        rsz:minimumSize 16384 ;")
    L.append('        lv2:index 5 ; lv2:symbol "notify" ; lv2:name "Notify"')
    # control ports
    for i, c in enumerate(ctrl):
        L.append(ttl_ctrl(c, NFIXED + i))
    # MIDI input (last port; index after all control ports so none shift). The
    # pi-Stomp footswitches emit CC 60..63 — Hex Forge reads them here for preset
    # recall + bank combos. Kept off the modgui:port list (it's not a control).
    L.append("    ] , [")
    L.append("        a lv2:InputPort, atom:AtomPort ;")
    L.append("        atom:bufferType atom:Sequence ;")
    L.append("        atom:supports midi:MidiEvent ;")
    L.append("        rsz:minimumSize 4096 ;")
    L.append('        lv2:index %d ; lv2:symbol "midi_in" ; lv2:name "MIDI In"' % (NFIXED + len(ctrl)))
    L.append("    ] ;")
    L.append("")
    # modgui
    L.append("    modgui:gui [")
    L.append("        modgui:resourcesDirectory <modgui-hexforge> ;")
    L.append("        modgui:iconTemplate <modgui-hexforge/icon-hexforge.html> ;")
    L.append("        modgui:stylesheet <modgui-hexforge/stylesheet-hexforge.css> ;")
    L.append("        modgui:javascript <modgui-hexforge/script-hexforge.js> ;")
    L.append("        modgui:screenshot <modgui-hexforge/screenshot-hexforge.png> ;")
    L.append("        modgui:thumbnail <modgui-hexforge/thumbnail-hexforge.png> ;")
    L.append('        modgui:brand "Hex Chain" ;')
    L.append('        modgui:label "Hex Forge" ;')
    L.append('        modgui:model "mod-pedal-guitaramp-hexforge" ;')
    rows = []
    for j, c in enumerate(ctrl):
        rows.append('            lv2:index %d ; lv2:symbol "%s" ; lv2:name "%s"' % (j, c["sym"], c["name"]))
    L.append("        modgui:port [\n" + "\n        ] , [\n".join(rows) + "\n        ]")
    L.append("    ] .")
    L.append("")
    return "\n".join(L)

# ── Emit the modgui icon HTML (10 logo-free tiles) ────────────────────────────
TABLES = {"it":IT,"gt":GT,"cp":CP,"fz":FZ,"dr":DR,"amp":AMP,"cab":CAB,"md":MD,"dl":DL,"rv":RV,"wh":WAH,"oc":OCTAVE}
# (prefix, tile title, accent, key-param suffixes shown always; rest go to "More")
TILES = [
    # Accents match each standalone pedal's brand color (its .hx-title / border-top)
    # so the Hex Forge tiles read as the same effects, not a different palette.
    ("it",  "Input Trim", "#6eaf87", ["gain","phase","hum","humbk","hbmodel","hbamt","boost","boostamt"]),  # utility
    ("gt",  "Gate",       "#8c9baf", ["thresh","release"]),                                   # gate
    ("cp",  "Comp",       "#4687eb", ["type","thresh","ratio","makeup"]),                     # comp
    ("fz",  "Fuzz",       "#ff4d9e", ["pedal","mode","sustain","tone","volume"]),             # fuzz
    ("dr",  "Drive",      "#eb5046", ["model","drive","tone","level"]),                       # drive
    ("amp", "Amp",        "#ff963c", ["model","gain","bass","mid","treble","presence","master"]),  # amp
    ("cab", "Cabinet",    "#56aaff", ["lowcut","highcut","mix"]),                             # cab
    ("md",  "Mod FX",     "#a56eeb", ["type","rate","depth","mix"]),                          # modfx
    ("dl",  "Delay",      "#3cc8be", ["type","time","feedback","mix"]),                       # delay
    ("rv",  "Reverb",     "#5f73e1", ["decay","damping","mix"]),                              # reverb
    ("wh",  "Wah",        "#c46eff", ["type","freq","depth","sens","q","mix"]),               # wah
    ("oc",  "Octave",     "#5fd0a0", ["up","down","dry"]),                                    # octave
]

# Conditional-visibility classes: a control is shown/hidden by script-hexforge.js
# based on the relevant selector, exactly like the standalone pedals.
COND = {
    # amp — Sunn (Doom Daddy = model 3) only
    "amp_sunn_vol2":"c-amp-sunn", "amp_sunn_link":"c-amp-sunn", "amp_sunn_bass2":"c-amp-sunn",
    "amp_sunn_mid2":"c-amp-sunn", "amp_sunn_treble2":"c-amp-sunn",
    "amp_sunn_bright1":"c-amp-sunn", "amp_sunn_bright2":"c-amp-sunn",
    "amp_channel":"c-amp-chan",          # EVH(2)/Rockerverb(4)
    "amp_resonance":"c-amp-reso",        # EVH(2)
    # Beardo BE / Friedman (model 6): 3-way channel + Fat/C45/Sat
    "amp_fr_channel":"c-amp-be", "amp_fr_fat":"c-amp-be",
    "amp_fr_c45":"c-amp-be", "amp_fr_sat":"c-amp-be",
    # power-amp: whole section hidden for Sunn; manual knobs hidden when PA Auto on
    "amp_pamp_bypass":"c-amp-pa", "amp_pamp_auto":"c-amp-pa",
    "amp_pamp_resonance":"c-amp-pa", "amp_pamp_airfeel":"c-amp-pa",
    "amp_pamp_tube":"c-amp-paman", "amp_pamp_presence":"c-amp-paman", "amp_pamp_depth":"c-amp-paman",
    "amp_pamp_sag":"c-amp-paman", "amp_pamp_master":"c-amp-paman", "amp_pamp_nfb":"c-amp-paman",
    # fuzz — Variant only on Italian Hero(0); Tone shared by Italian Hero + Octavia(2)
    # (always shown — Tone Bender just ignores it); Bias/Trim/Temp only on I Know It(1).
    "fz_mode":"c-fz-ih",
    "fz_bias":"c-fz-tb", "fz_inputtrim":"c-fz-tb", "fz_getemp":"c-fz-tb",
    # drive — Octave only on New Dawn (model 1)
    "dr_octave":"c-dr-oct",
    # delay — Wow/Flutter for Tape(1)/Echo Wreck(2); Heads for Echo Wreck(2);
    # Pattern/Ducking/Mod for Seraph(3)
    "dl_wow":"c-dl-tape", "dl_flutter":"c-dl-tape", "dl_heads":"c-dl-heads",
    "dl_pattern":"c-dl-seraph", "dl_ducking":"c-dl-seraph",
    "dl_moddepth":"c-dl-seraph", "dl_modrate":"c-dl-seraph",
}

def render_ctrl(c):
    sym, nm, k = c["sym"], c["short"], c["kind"]
    cc = (" " + COND[sym]) if sym in COND else ""
    if k == "t":
        return ('<div class="hf-sw%s" title="%s"><div class="hf-sw-img" mod-role="input-control-port" '
                'mod-port-symbol="%s" mod-widget="switch"></div><span class="hf-sw-t">%s</span></div>') % (cc, nm, sym, nm)
    if k == "e":
        opts = "".join('<div mod-role="enumeration-option" mod-port-value="%d">%s</div>' % (v, lbl) for lbl, v in c["scale"])
        return ('<div class="hf-sel-wrap%s"><span class="hf-sel-label">%s</span>'
                '<div class="hf-sel mod-enumerated" mod-role="input-control-port" mod-port-symbol="%s" mod-widget="custom-select" title="%s">'
                '<div mod-role="input-control-value" mod-port-symbol="%s" class="mod-enumerated-selected"></div>'
                '<div class="mod-enumerated-list">%s</div></div></div>') % (cc, nm, sym, nm, sym, opts)
    return ('<div class="hf-knob%s" title="%s"><div class="hf-knob-img" mod-role="input-control-port" '
            'mod-port-symbol="%s"></div><span class="hf-knob-t" rata-role="lbl-%s">%s</span>'
            '<span class="hf-knob-v" mod-role="input-control-value" mod-port-symbol="%s"></span></div>') % (cc, nm, sym, sym, nm, sym)

# IMPORTANT: mod-ui sorts effect.parameters ALPHABETICALLY by URI, so the array
# indices are: ampnam=0, cabnam=1, drnam=2, irfile=3 (NOT declaration order).
# The Factory Cab is a static, always-present option (sentinel "@factory" → the
# built-in DefaultCabIR). It heads the list so it's selectable even after a user
# loads their own IR. The placeholder shows it too, since empty path = default.
IR_PICKER = ('<div class="hf-ir"><span class="hf-sel-label">Impulse Response</span>'
    '{{#effect.parameters.3}}{{#path}}'
    '<div class="hf-sel mod-enumerated" mod-role="input-parameter" mod-parameter-uri="{{uri}}" mod-widget="custom-select-path">'
    '<div mod-role="input-parameter-value" rata-role="Ir" mod-parameter-uri="{{uri}}" class="mod-enumerated-selected">Factory Cab (built-in)</div>'
    '<div class="mod-enumerated-list"><div mod-role="enumeration-option" mod-parameter-value="@factory">Factory Cab (built-in)</div>'
    '{{#files}}<div mod-role="enumeration-option" mod-parameter-value="{{fullname}}">{{basename}}</div>{{/files}}</div>'
    '</div>{{/path}}{{/effect.parameters.3}}</div>')

def nam_picker(idx, rata, cls):
    # NAM file picker bound to effect.parameters.<idx> (1=amp,2=drive,3=cab).
    return ('<div class="hf-ir %s"><span class="hf-sel-label">NAM Model</span>'
            '{{#effect.parameters.%d}}{{#path}}'
            '<div class="hf-sel mod-enumerated" mod-role="input-parameter" mod-parameter-uri="{{uri}}" mod-widget="custom-select-path">'
            '<div mod-role="input-parameter-value" rata-role="%s" mod-parameter-uri="{{uri}}" class="mod-enumerated-selected">-- choose a NAM file --</div>'
            '<div class="mod-enumerated-list">{{#files}}<div mod-role="enumeration-option" mod-parameter-value="{{fullname}}">{{basename}}</div>{{/files}}</div>'
            '</div>{{/path}}{{/effect.parameters.%d}}</div>') % (cls, idx, rata, idx)

def render_pos(pfx):
    c = CTRL_BY_SYM[pfx + "_pos"]
    opts = "".join('<div mod-role="enumeration-option" mod-port-value="%d">%s</div>' % (v, lbl) for lbl, v in c["scale"])
    return ('<div class="hf-posn mod-enumerated" mod-role="input-control-port" mod-port-symbol="%s" mod-widget="custom-select" title="Slot">'
            '<div mod-role="input-control-value" mod-port-symbol="%s" class="mod-enumerated-selected"></div>'
            '<div class="mod-enumerated-list">%s</div></div>') % (c["sym"], c["sym"], opts)

def render_enable(pfx):
    return ('<div class="hf-on" title="Block on/off"><div class="hf-on-img" mod-role="input-control-port" '
            'mod-port-symbol="%s_enable" mod-widget="switch"></div></div>') % pfx

# ── Chain node (small clickable card in the signal strip) ─────────────────────
# AxeFX/Quad-Cortex-style: each block is a node. Click a node to show its controls
# in the detail panel below; click the power dot to bypass it (greyed, dry, settings
# kept); drag to reorder. Removed blocks live in the "+ Add" palette. Input Trim is
# locked first (no remove/reorder); its dot toggles it_enable (it has no bypass port).
def node(pfx, title, accent):
    locked = (pfx == "it")
    posattr = "" if locked else ' data-pos="%d"' % CTRL_BY_SYM[pfx + "_pos"]["df"]
    drag    = "" if locked else ' draggable="true"'
    cls     = "hf-node hf-node-lock" if locked else "hf-node"
    out = ['<div class="%s" data-block="%s"%s style="--acc:%s" title="%s"%s>' % (cls, pfx, posattr, accent, title, drag)]
    # power/bypass dot — JS-driven (script-hexforge.js writes the port + greys the node);
    # IT has no bypass port so its dot toggles it_enable (locked, can't be removed).
    out.append('<div class="hf-node-dot" title="Bypass (A/B)"></div>')
    out.append('<span class="hf-node-name">%s</span>' % title)
    # Hidden mod-widget binds so external/footswitch/preset changes to these ports still
    # echo to the modgui change handler (which keeps the node grey/membership in sync).
    binds = ['<div mod-role="input-control-port" mod-port-symbol="%s" mod-widget="switch"></div>'
             % ("it_enable" if locked else (pfx + "_bypass"))]
    if not locked:
        binds.append('<div mod-role="input-control-port" mod-port-symbol="%s_enable" mod-widget="switch"></div>' % pfx)
    out.append('<div class="hf-node-bind">' + "".join(binds) + '</div>')
    out.append('</div>')
    return "".join(out)

# ── Detail panel (the selected block's full controls, shown at the bottom) ─────
# Reuses render_ctrl + the COND conditional-visibility classes. Only one panel is
# visible at a time (the selected node's). There's room for every control now, so
# the Amp's power-amp / Beardo / Sunn controls render inline — no "More" popup.
def panel(pfx, title, accent, keys):
    locked = (pfx == "it")
    table = TABLES[pfx]
    head = ['<div class="hf-dhead">']
    head.append('<span class="hf-dname">%s</span>' % title)
    if not locked:
        head.append('<span class="hf-dslot"><span class="hf-dslot-lbl">SLOT</span>%s</span>' % render_pos(pfx))
        head.append('<button type="button" class="hf-dremove" title="Remove this effect from the chain">REMOVE ✕</button>')
    head.append('</div>')
    # Split controls so the wide dropdowns (model/type/etc. + file pickers) sit on their
    # own top row, and the knobs/switches form a uniform grid below — every knob row then
    # left-aligns, instead of row 2 starting under a wide selector in row 1.
    if pfx == "amp":
        syms = ["amp_" + r[0] for r in table] + ["amp_" + s[0] for s in AMP_FR]
        pickers = nam_picker(0, "AmpNam", "c-amp-nam")
    elif pfx == "dr":
        syms = ["dr_" + r[0] for r in table]; pickers = nam_picker(2, "DrNam", "c-dr-nam")
    elif pfx == "cab":
        syms = ["cab_" + r[0] for r in table]; pickers = IR_PICKER + nam_picker(1, "CabNam", "")
    else:
        syms = [pfx + "_" + r[0] for r in table]; pickers = ""
    sels  = "".join(render_ctrl(CTRL_BY_SYM[s]) for s in syms if CTRL_BY_SYM[s]["kind"] == "e")
    knobs = "".join(render_ctrl(CTRL_BY_SYM[s]) for s in syms if CTRL_BY_SYM[s]["kind"] != "e")
    selrow = pickers + sels
    body = ""
    if selrow: body += '<div class="hf-dselects clearfix">%s</div>' % selrow
    if knobs:  body += '<div class="hf-dknobs clearfix">%s</div>' % knobs
    if pfx == "it":   # live input level meter on the Input Trim block (front of chain)
        body = ('<div class="hf-inmeter"><span class="hf-inmeter-lbl">INPUT LEVEL</span>'
                '<div class="hf-meter hf-meter-h"><div class="hf-meter-fill" rata-role="imeter"></div></div>'
                '</div>') + body
    cls = "hf-detail-panel hf-detail-lock" if locked else "hf-detail-panel"
    return ('<div class="%s" data-block="%s" style="--acc:%s">%s<div class="hf-dbody">%s</div></div>'
            % (cls, pfx, accent, "".join(head), body))

# ── Preset / bank strip ───────────────────────────────────────────────────────
# Plain buttons; script-hexforge.js wires their clicks to pulse the command ports
# (set_port_value 1 then 0) and renders the active bank/slot/name + the 32-preset
# list from the ps_bank/ps_slot/ps_index notifications. Rename is done in MOD's
# generic "Preset Name" parameter field (no JS parameter setter exists).
PRESETS_PANEL = (
    '  <div class="hf-presets">\n'
    '    <span class="hf-ps-mark"></span>\n'
    '    <span class="hf-ps-title">PRESETS</span>\n'
    '    <button type="button" class="hf-ps-btn hf-ps-bankdn" title="Bank down (hold A+B on the pedal)">◀</button>\n'
    '    <span class="hf-ps-bank" rata-role="psbank">BANK 1</span>\n'
    '    <button type="button" class="hf-ps-btn hf-ps-bankup" title="Bank up (hold C+D on the pedal)">▶</button>\n'
    '    <span class="hf-ps-slots">\n'
    '      <button type="button" class="hf-ps-slot" data-slot="0">A</button>\n'
    '      <button type="button" class="hf-ps-slot" data-slot="1">B</button>\n'
    '      <button type="button" class="hf-ps-slot" data-slot="2">C</button>\n'
    '      <button type="button" class="hf-ps-slot" data-slot="3">D</button>\n'
    '    </span>\n'
    '    <input type="text" class="hf-ps-name" rata-role="psname" maxlength="31" spellcheck="false" placeholder="(unnamed)" title="Preset name — type and press Enter to rename" />\n'
    '    <span class="hf-ps-spacer"></span>\n'
    '    <button type="button" class="hf-ps-btn hf-ps-save" title="Save over the current preset">SAVE</button>\n'
    '    <button type="button" class="hf-ps-btn hf-ps-mvup" title="Move preset earlier in the list">▲</button>\n'
    '    <button type="button" class="hf-ps-btn hf-ps-mvdn" title="Move preset later in the list">▼</button>\n'
    '    <button type="button" class="hf-ps-btn hf-ps-toggle" title="Show all presets">≡</button>\n'
    '    <button type="button" class="hf-ps-btn hf-ps-backup" title="Back up all 32 presets to disk (survives delete/re-add &amp; updates)">⭳</button>\n'
    '    <button type="button" class="hf-ps-btn hf-ps-restore" title="Restore all presets from the last backup on disk">⭱</button>\n'
    '    <div class="hf-ps-list" rata-role="pslist"></div>\n'
    '  </div>\n'
)

def emit_icon():
    nodes  = "".join(node(t[0], t[1], t[2]) for t in TILES)
    panels = "\n      ".join(panel(*t) for t in TILES)
    chain = (
        '  <div class="hf-chain">\n'
        '    <span class="hf-end hf-in">IN</span>\n'
        '    <div class="hf-nodes" rata-role="nodes">' + nodes + '</div>\n'
        '    <span class="hf-end hf-out2">OUT</span>\n'
        '    <div class="hf-addwrap">\n'
        '      <button type="button" class="hf-add" title="Add an effect to the chain">＋ ADD</button>\n'
        '      <div class="hf-palette" rata-role="palette"></div>\n'
        '    </div>\n'
        '  </div>\n')
    detail = '  <div class="hf-detail" rata-role="detail">\n      ' + panels + '\n  </div>\n'
    return ('<div class="mod-pedal mod-pedal-guitaramp-hexforge{{{cns}}} ">\n'
        '  <div mod-role="drag-handle" class="mod-drag-handle"></div>\n'
        '  <div class="mod-powerswitch" mod-role="bypass"><div class="mod-powerswitch-image" mod-role="bypass-light"></div></div>\n'
        '  <div class="hf-header">\n'
        '    <div class="hx-brand"><span class="hx-mark"></span>HEX CHAIN</div>\n'
        '    <div class="hf-title">HEX FORGE</div>\n'
        '    <div class="hf-sub">click a node to edit it · ＋ to add · power dot bypasses · drag to reorder</div>\n'
        '    <div class="hx-logo"></div>\n'
        '  </div>\n'
        '  <div class="hf-out">\n'
        '    <span class="hf-out-mark"></span>\n'
        '    <span class="hf-out-name">Output</span>\n'
        '    <div class="hf-meter hf-meter-h" title="Output level"><div class="hf-meter-fill" rata-role="ometer"></div></div>\n'
        '    <span class="hf-clip" rata-role="clip">CLIP</span>\n'
        '    <span class="hf-clipval mod-hidden" mod-role="input-control-value" mod-port-symbol="clip"></span>\n'
        '    ' + render_ctrl(CTRL_BY_SYM["out_auto"]) + '\n'
        '    ' + render_ctrl(CTRL_BY_SYM["out_level"]) + '\n'
        '  </div>\n'
        + PRESETS_PANEL + chain + detail +
        '  <div class="mod-pedal-input">\n'
        '    {{#effect.ports.audio.input}}\n'
        '    <div class="mod-input mod-input-disconnected" title="{{name}}" mod-role="input-audio-port" mod-port-symbol="{{symbol}}"><div class="mod-pedal-input-image"></div></div>\n'
        '    {{/effect.ports.audio.input}}\n'
        '  </div>\n'
        '  <div class="mod-pedal-output">\n'
        '    {{#effect.ports.audio.output}}\n'
        '    <div class="mod-output mod-output-disconnected" title="{{name}}" mod-role="output-audio-port" mod-port-symbol="{{symbol}}"><div class="mod-pedal-output-image"></div></div>\n'
        '    {{/effect.ports.audio.output}}\n'
        '  </div>\n'
        '</div>\n')

if __name__ == "__main__":
    hdr = emit_header()
    ttl = emit_ttl()
    icon = emit_icon()
    os.makedirs(os.path.join(REPO, "lv2", "hexforge"), exist_ok=True)
    with open(os.path.join(REPO, "lv2", "hexforge", "hexforge_ports.h"), "w", newline="\n", encoding="utf-8") as f:
        f.write(hdr)
    with open(os.path.join(REPO, "lv2", "hexforge.ttl"), "w", newline="\n", encoding="utf-8") as f:
        f.write(ttl)
    os.makedirs(os.path.join(REPO, "lv2", "modgui-hexforge"), exist_ok=True)
    with open(os.path.join(REPO, "lv2", "modgui-hexforge", "icon-hexforge.html"), "w", newline="\n", encoding="utf-8") as f:
        f.write(icon)
    print("ports:", NFIXED + len(ctrl), "(fixed %d + control %d)" % (NFIXED, len(ctrl)))
    print("wrote lv2/hexforge.ttl, hexforge_ports.h, modgui-hexforge/icon-hexforge.html")
