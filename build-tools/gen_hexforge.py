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
import os, math

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
    # default FIXED 1->0 (2026-07-14): 1 = INVERT (x * -1 in the DSP), and the old default meant every
    # instance + all factory presets ran polarity-flipped — mismatching the standalone Input Trim
    # (phase_invert default 0) AND every offline NAM calibration (which feeds models at normal polarity).
    # Absolute polarity is near-inaudible alone, but our asymmetric nonlinearities (FF positive-tip
    # conduction, germanium/tube bias) care which way the attack leads. Toggle kept for DI-blend rigs.
    ("phase", "Phase",      "t",  0, 1, 0, None),
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
    ("attack",  "Attack",     "ms", 0.1, 50, 2, None),
    ("hold",    "Hold",       "ms", 0, 500, 120, None),
    ("release", "Release",    "ms", 10, 2000, 250, None),
    ("hyst",    "Hysteresis", "db", 0, 20, 8, None),
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
    ("pedal",     "Pedal",      "e", 0, 3, 0, [("Italian Hero",0),("I Know It",1),("Octavius",2),("Fuzz Zachary",3)]),
    ("mode",      "Variant",    "e", 0, 5, 2, [("Delta",0),("Ovis",1),("Gotham",2),("Cold War",3),("Red Bear",4),("Boutique",5)]),
    ("sustain",   "Sustain",    "f", 0, 1, 0.55, None),
    ("tone",      "Tone",       "f", 0, 1, 0.5, None),
    ("volume",    "Volume",     "f", 0, 1, 0.65, None),
    ("bias",      "Bias",       "f", 0, 1, 0.5, None),
    ("inputtrim", "Input Trim", "f", 0, 1, 0.5, None),
    ("getemp",    "Ge Temp",    "f", 0, 1, 0.4, None),
]
DR = [
    ("model",  "Model",  "e", 0, 7, 0, [("Green Man",0),("New Dawn",1),("Dear Rodent Boy",2),("Neural (NAM)",3),("Grunge DS",4),("Gilded Horse",5),("Super Nova",6),("Preamp 250",7)]),
    ("drive",  "Drive",  "f", 0, 1, 0.5, None),
    ("tone",   "Tone",   "f", 0, 1, 0.5, None),
    ("level",  "Level",  "f", 0, 1, 0.5, None),
    ("mix",    "Mix",    "f", 0, 1, 1.0, None),
    ("octave", "Octave", "f", 0, 1, 0.3, None),
]
AMP = [
    ("model",         "Model",        "e", 0, 13, 1, [("Clean Meanie",0),("Crunchy McCrunchFace",1),("Gainzilla",2),("Doom Daddy",3),("Tangerang",4),("Neural (NAM)",5),("Beardo BE",6),("Hi-Volt",7),("Chime Thirty",8),("Backline Plus",9),("Plexiglass",10),("Cali V",11),("Diamond Plate",12),("Tremont 15",13)]),
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
    ("type",  "Type",  "e", 0, 6, 0, [("Lush-2",0),("Uni-Verse",1),("Phaser",2),("Flanger",3),("Tremolo",4),("Rotary",5),("Nevermind Chorus",6)]),
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
# Nail — industrial distortion (NailDistortion, an OverdriveBase). 5 real topologies.
NAIL = [
    ("mode",    "Mode",    "e", 0, 4, 2, [("Broke",0),("Dahnward",1),("Delicate",2),("Con Molars",3),("Tusk",4)]),
    ("drive",   "Drive",   "f", 0, 1, 0.6, None),
    ("tone",    "Tone",    "f", 0, 1, 0.5, None),
    ("texture", "Texture", "f", 0, 1, 0.4, None),
    ("level",   "Level",   "f", 0, 1, 0.5, None),
]

# Movable blocks in canonical default order. (prefix, Title, params, default-pos)
MOVABLE = [
    ("gt",  "Gate",       GT,  1),
    ("cp",  "Comp",       CP,  2),
    ("fz",  "Fuzz",       FZ,  3),
    ("dr",  "Drive",      DR,  4),
    ("amp", "Amp",        AMP, 5),
    ("cab", "Cabinet",    CAB, 6),
    ("md",  "Modulation", MD,  7),   # md_offset (Center Delay) is appended out-of-table, before commands
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
    posscale = [(str(k), k) for k in range(1, 13)]   # 1..12 dropdown for reordering (incl. Nail)
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

# ── Nail block (12th movable) — appended HERE, after the per-block bypass toggles and
# BEFORE the preset-command ports, so it stays inside the preset param range yet leaves
# every existing param index untouched. pos/enable/params + its own bypass are one
# contiguous group [NAIL_POS..NAIL_BYPASS]; old blobs migrate them in (v12). Default OFF,
# default slot 12 (end of chain) so it doesn't alter the stock sound until enabled.
NAIL_POS_DEFAULT = 12
ctrl.append(mkport("NAIL_POS",    "nail_pos",    "Nail Position", "e", 1, 12, NAIL_POS_DEFAULT,
                   [(str(k), k) for k in range(1, 13)], "Slot", hidden=True))
ctrl.append(mkport("NAIL_ENABLE", "nail_enable", "Nail Enable",   "t", 0, 1, 0, None, "On"))
for suf, nm, kind, mn, mx, df, sc in NAIL:
    ctrl.append(mkport("NAIL_" + suf.upper(), "nail_" + suf, "Nail " + nm, kind, mn, mx, df, sc, nm))
ctrl.append(mkport("NAIL_BYPASS", "nail_bypass", "Nail Bypass", "t", 0, 1, 0, None, "Byp"))

# ── Tempo sync (tap-tempo / MIDI clock) for the time-based effects — appended AFTER Nail,
# before the preset commands, so every existing preset index is preserved (migrated in v13).
# Per-effect: Delay + Mod each get a sync toggle + a note-division selector. Default OFF =
# the manual Time/Rate knobs. When ON, the plugin locks Delay time / Mod LFO to host BPM.
DIV_SCALE = [("1/2",0),("1/4.",1),("1/4",2),("1/4T",3),("1/8.",4),("1/8",5),("1/8T",6),("1/16",7)]
ctrl.append(mkport("DL_SYNC", "dl_sync", "Delay Clock Sync", "t", 0, 1, 0, None, "Sync"))
ctrl.append(mkport("DL_DIV",  "dl_div",  "Delay Division",   "e", 0, 7, 5, DIV_SCALE, "Div"))
ctrl.append(mkport("MD_SYNC", "md_sync", "Mod Clock Sync",   "t", 0, 1, 0, None, "Sync"))
ctrl.append(mkport("MD_DIV",  "md_div",  "Mod Division",     "e", 0, 7, 2, DIV_SCALE, "Div"))

# ── Octave microtonal shimmer — a pitch-tracked single-sideband frequency-shift voice on
# the Octave block (micro = level, interval = 24-TET step). Appended AFTER tempo-sync and
# BEFORE the preset commands so every existing preset index is preserved (migrated in v14).
# Default OFF (micro 0) so old boards sound identical. Interval 0 = quarter-tone up (the
# "Angine de Poitrine" beating shimmer). Two contiguous ports [OC_MICRO, OC_INTERVAL].
ctrl.append(mkport("OC_MICRO",    "oc_micro",    "Octave Microtonal", "f", 0, 1, 0.0, None, "Micro"))
ctrl.append(mkport("OC_INTERVAL", "oc_interval", "Octave Interval",   "e", 0, 5, 0,
    [("1/4 Up",0),("1/4 Dn",1),("Neutral 2nd",2),("Neutral 3rd",3),("Neutral 6th",4),("Octave +1/4",5)], "Interval"))

# ── Mesa Mark V (Cali V) MODE selector — the amp's 9 modes across 3 channels. Appended after
# the Octave shimmer and BEFORE the preset commands so every existing preset index is preserved
# (migrated in v15). Shown only when Amp model = Cali V (COND c-amp-mesa). Default 6 = Mark IIC+.
ctrl.append(mkport("AMP_MV_MODE", "amp_mv_mode", "Amp Mesa Mode", "e", 0, 8, 6,
    [("Clean",0),("Fat",1),("Tweed",2),("Edge",3),("Crunch",4),("Mk I",5),("IIC+",6),("Mk IV",7),("Xtreme",8)], "Mode"))

# Cali V (Mesa Mark V) 5-band GRAPHIC EQ — 80/240/750/2200/6600 Hz, each ±12 dB (0.5 = flat). The
# iconic Mark V "V". Conditional (c-amp-mesa), appended after the mode selector; migrated in v16.
for _gs, _gl in [("mv_geq0","80"),("mv_geq1","240"),("mv_geq2","750"),("mv_geq3","2.2k"),("mv_geq4","6.6k")]:
    ctrl.append(mkport("AMP_" + _gs.upper(), "amp_" + _gs, "Amp EQ " + _gl, "f", 0.0, 1.0, 0.5, None, _gl))
# Cali V graphic-EQ PRESET selector — 0 Custom (the 5 sliders) or a baked Mesa "V" curve. Migrated v17.
ctrl.append(mkport("AMP_MV_EQPRESET", "amp_mv_eqpreset", "Amp EQ Preset", "e", 0, 5, 0,
    [("Custom",0),("Flat",1),("V-Scoop",2),("Deep V",3),("Mid Boost",4),("Bright",5)], "EQ Preset"))

# Modulation "Center Delay" — pushes the modulation centre out 0..100 ms (delay-line types:
# Chorus/Flanger/Small Clone). Appended here (before the preset commands) so every existing
# preset index is preserved; a pre-v18 blob defaults it to 0 (= stock voicing). Migrated v18.
ctrl.append(mkport("MD_OFFSET", "md_offset", "Mod Center Delay", "ms", 0, 100, 0, None, "Center Delay"))

# ── NAM input/output trims (Amp / Drive / Cab neural slots) — a dedicated Gain (input drive
# into the capture) + Level (output trim) per NAM slot, both dB (0 = unity). Appended here,
# before the preset commands, so every existing preset index is preserved; pre-v19 blobs
# default them to 0 dB. Six contiguous ports [HF_AMP_NAM_GAIN..HF_CAB_NAM_VOL]. Migrated v19.
for _pfx, _lbl in (("amp", "Amp"), ("dr", "Drive"), ("cab", "Cab")):
    ctrl.append(mkport(_pfx.upper() + "_NAM_GAIN", _pfx + "_nam_gain", _lbl + " NAM Gain",  "db", -20, 20, 0, None, "NAM Gain"))
    ctrl.append(mkport(_pfx.upper() + "_NAM_VOL",  _pfx + "_nam_vol",  _lbl + " NAM Level", "db", -20, 20, 0, None, "NAM Level"))

# ── Plexiglass Vol II (Normal channel, jumpered 1959) — the Super Lead's second volume
# (2026-07-14). The base Plexi path stays the capture-anchored "CH I High Jumped" voicing
# (= gain knob = Vol I); this ADDS a parallel Normal-channel V1 half blended in by Vol II
# for the classic jumpered fatness. Appended before the preset commands so every existing
# index is preserved; default 0 = old blobs/boards sound BIT-IDENTICAL (no migration).
# Shown only when Amp model = Plexiglass (COND c-amp-plexi).
ctrl.append(mkport("AMP_PL_VOL2", "amp_pl_vol2", "Amp Vol II", "f", 0, 1, 0.0, None, "Vol II"))

# ── Cab MIC PLACEMENT (2026-07-14) — post-convolution mic-position/distance morphs on the
# Cabinet block (CabinetBlock micpos/micdist). One-sided from the voiced baseline: 0/0 is
# bit-identical to before, so old blobs zero-fill safely (append-only, no migration).
#   Mic Pos  0 = cap edge (as voiced) → 1 = cone edge (darker, bite recedes, more body)
#   Mic Dist 0 = close (as voiced) → 1 = ~30 cm back (proximity bass falls away, less air)
ctrl.append(mkport("CAB_MICPOS",  "cab_micpos",  "Cab Mic Position", "f", 0, 1, 0.0, None, "Mic Pos"))
ctrl.append(mkport("CAB_MICDIST", "cab_micdist", "Cab Mic Distance", "f", 0, 1, 0.0, None, "Mic Dist"))

# ── Cab ROOM ambience (2026-07-14) — a small Schroeder room AFTER the cab convolution
# ("amp in a room"). Toggle + Mix (wet blend) + Amount (size/decay, booth→live room).
# Default ON at a SUBTLE small-to-medium setting (user 2026-07-14) — every factory preset
# inherits it via the seeded defaults; OLD user blobs zero-fill roomon=0, so their saved
# sounds are untouched. Preset-savable.
ctrl.append(mkport("CAB_ROOMON",  "cab_roomon",  "Cab Room",        "t", 0, 1, 1,    None, "Room"))
ctrl.append(mkport("CAB_ROOMMIX", "cab_roommix", "Cab Room Mix",    "f", 0, 1, 0.12, None, "Room Mix"))
ctrl.append(mkport("CAB_ROOMAMT", "cab_roomamt", "Cab Room Amount", "f", 0, 1, 0.35, None, "Room Size"))

# ── Diamond Plate (Mesa Dual Rectifier, amp model 12) — 8-mode selector across the Solo
# Head's 3 channels + the power-section feel switches: Variac (Bold/Spongy) and rectifier
# type (Silicon/Tube). Appended before the preset commands so every existing index is
# preserved; pre-v20 blobs migrate to the defaults {7 CH3 Modern, Bold, Silicon}.
# Shown only when Amp model = Diamond Plate (COND c-amp-recto). Migrated v20.
ctrl.append(mkport("AMP_RC_MODE", "amp_rc_mode", "Amp Recto Mode", "e", 0, 7, 7,
    [("CH1 Clean",0),("CH1 Pushed",1),("CH2 Raw",2),("CH2 Vintage",3),("CH2 Modern",4),("CH3 Raw",5),("CH3 Vintage",6),("CH3 Modern",7)], "Mode"))
ctrl.append(mkport("AMP_RC_VARIAC", "amp_rc_variac", "Amp Recto Variac", "e", 0, 1, 0,
    [("Bold",0),("Spongy",1)], "Variac"))
ctrl.append(mkport("AMP_RC_RECT", "amp_rc_rect", "Amp Recto Rectifier", "e", 0, 1, 0,
    [("Silicon",0),("Tube",1)], "Rectifier"))

# ── Tremont 15 (PRS MT15, amp model 13) — Clean/Crunch/Lead channel + the clean/crunch
# bright switch. Appended before the preset commands; pre-v21 blobs migrate to the
# defaults {2 Lead, bright off}. Shown only when Amp model = Tremont 15 (COND c-amp-mt15).
ctrl.append(mkport("AMP_MT_MODE", "amp_mt_mode", "Amp MT Channel", "e", 0, 2, 2,
    [("Clean",0),("Crunch",1),("Lead",2)], "Channel"))
ctrl.append(mkport("AMP_MT_BRIGHT", "amp_mt_bright", "Amp MT Bright", "e", 0, 1, 0,
    [("Off",0),("On",1)], "Bright"))

# ── Cab Voice + output Doubler (2026-07-22) — the "recorded sound" option. Voice:
# Room (0, bit-identical legacy path) / Studio (1: second virtual mic blend, bracketing
# HPF/LPF, console curve, bus compression; room ambience forced off inside the block).
# Doubler: micro-delayed detuned copy on the right — the fake double-track. Appended
# before the preset commands; pre-v22 blobs migrate to {Room, Off}. Migrated v22.
ctrl.append(mkport("CAB_VOICE", "cab_voice", "Cab Voice", "e", 0, 1, 0,
    [("Room",0),("Studio",1)], "Voice"))
ctrl.append(mkport("OUT_DOUBLER", "out_doubler", "Output Doubler", "t", 0, 1, 0, None, "Doubler"))

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
ctrl.append(mkport("PS_GOTO", "ps_goto", "Go To Preset", "i", -1, 127, -1, None, "GoTo", hidden=True))
ctrl.append(mkport("PS_BANK", "ps_bank", "Active Bank", "i", 0, 31, 0, None, "Bank", out=True))
ctrl.append(mkport("PS_SLOT", "ps_slot", "Active Slot", "i", 0, 3, 0, None, "Slot", out=True))
# Output auto-limit: when on, a transparent peak limiter on the master output keeps
# it from clipping (ceiling ~0.95). Added at the END so it's outside the preset
# param range (it's a global preference, and keeps the preset blob layout stable).
ctrl.append(mkport("OUT_AUTO", "out_auto", "Output Auto-Limit", "t", 0, 1, 1, None, "Auto"))
# Input / output level meters (plugin -> UI). Output ports, appended at the very END so
# the param/preset layout is untouched (not preset-captured). 0..1 dB-scaled (-60..0 dB).
ctrl.append(mkport("IN_METER",  "in_meter",  "Input Level",  "f", 0, 1, 0, None, "In",  out=True))
ctrl.append(mkport("OUT_METER", "out_meter", "Output Level", "f", 0, 1, 0, None, "Out", out=True))
# Output MONO SUM: when on, collapse L+R -> 0.5*(L+R) at the output so a MONO rig
# (pi-Stomp -> one amp) never loses panned / stereo-widened content. DEFAULT ON (mono).
# Added at the ABSOLUTE END of the ctrl list so old preset blobs stay index-aligned
# (blob v10 migration defaults it ON for pre-v10 saves).
ctrl.append(mkport("OUT_MONO", "out_mono", "Output Mono Sum", "t", 0, 1, 1, None, "Mono"))

# ── Strobe tuner ── a toggleable chromatic tuner strip at the bottom of the Forge. ALL global
# (appended after the preset param range → NOT preset-captured, so no blob migration). tuner_on
# shows + enables it; tuner_mute silences the output while tuning. note (-1=no pitch, 0..11 =
# C..B) + cents (-50..+50) are OUTPUT ports the strobe UI reads to drive the disc + readout.
ctrl.append(mkport("TUNER_ON",    "tuner_on",    "Tuner",       "t", 0, 1, 0, None, "Tuner"))
ctrl.append(mkport("TUNER_MUTE",  "tuner_mute",  "Tuner Mute",  "t", 0, 1, 0, None, "Mute"))
ctrl.append(mkport("TUNER_NOTE",  "tuner_note",  "Tuner Note",  "i", -1, 11, -1, None, "Note",  out=True))
ctrl.append(mkport("TUNER_CENTS", "tuner_cents", "Tuner Cents", "f", -50, 50, 0, None, "Cents", out=True))

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
    L.append("@prefix time:  <http://lv2plug.in/ns/ext/time#> .")
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
    L.append("    lv2:microVersion 134 ;")
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
    L.append("        atom:supports time:Position ;")
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
TABLES = {"it":IT,"gt":GT,"cp":CP,"fz":FZ,"dr":DR,"amp":AMP,"cab":CAB,"md":MD,"dl":DL,"rv":RV,"wh":WAH,"oc":OCTAVE,"nail":NAIL}
# (prefix, tile title, accent, key-param suffixes shown always; rest go to "More")
TILES = [
    # Accents match each standalone pedal's brand color (its .hx-title / border-top)
    # so the Hex Forge tiles read as the same effects, not a different palette.
    ("it",  "Input Trim", "#6eaf87", ["gain","phase","hum","humbk","hbmodel","hbamt","boost","boostamt"]),  # utility
    ("gt",  "Gate",       "#8c9baf", ["thresh","release"]),                                   # gate
    ("cp",  "Comp",       "#4687eb", ["type","thresh","ratio","makeup"]),                     # comp
    ("fz",  "Fuzz",       "#ff4d9e", ["pedal","mode","sustain","tone","volume"]),             # fuzz
    ("dr",  "Drive",      "#eb5046", ["model","drive","tone","level"]),                       # drive
    ("nail","Nail",       "#d0343a", ["mode","drive","tone","texture","level"]),              # nail (industrial)
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
    # Plexiglass (model 10): the 1959's second (Normal-channel) volume — jumpered blend
    "amp_pl_vol2":"c-amp-plexi",
    # Cali V / Mesa Mark V (model 11): 9-mode selector + 5-band graphic EQ
    "amp_mv_mode":"c-amp-mesa",
    "amp_mv_geq0":"c-amp-mesa", "amp_mv_geq1":"c-amp-mesa", "amp_mv_geq2":"c-amp-mesa",
    "amp_mv_geq3":"c-amp-mesa", "amp_mv_geq4":"c-amp-mesa", "amp_mv_eqpreset":"c-amp-mesa",
    # Diamond Plate / Mesa Dual Rectifier (model 12): 8-mode selector + Variac/Rectifier
    "amp_rc_mode":"c-amp-recto", "amp_rc_variac":"c-amp-recto", "amp_rc_rect":"c-amp-recto",
    # Tremont 15 / PRS MT15 (model 13): channel + bright switch
    "amp_mt_mode":"c-amp-mt15", "amp_mt_bright":"c-amp-mt15",
    # power-amp: whole section hidden for Sunn; manual knobs hidden when PA Auto on
    "amp_pamp_bypass":"c-amp-pa", "amp_pamp_auto":"c-amp-pa",
    "amp_pamp_resonance":"c-amp-pa", "amp_pamp_airfeel":"c-amp-pa",
    "amp_pamp_tube":"c-amp-paman", "amp_pamp_presence":"c-amp-paman", "amp_pamp_depth":"c-amp-paman",
    "amp_pamp_sag":"c-amp-paman", "amp_pamp_master":"c-amp-paman", "amp_pamp_nfb":"c-amp-paman",
    # fuzz — Variant only on Italian Hero(0); Tone shared by Italian Hero + Octavia(2)
    # (always shown — Tone Bender just ignores it); Bias/Trim/Temp only on I Know It(1).
    "fz_mode":"c-fz-ih",
    "fz_bias":"c-fz-tb", "fz_inputtrim":"c-fz-tb", "fz_getemp":"c-fz-tb",
    # drive — Octave only on New Dawn (model 1); Drive/Tone/Level are algorithmic-only (a NAM
    # capture has its own Gain/Level), so they hide in Neural mode (model 3); the NAM Gain/Level
    # knobs (c-dr-nam) + picker show only in Neural mode. The model dropdown (c-dr-int) shows only
    # in Internal mode (the MODE toggle switches int<->Neural, mirroring the standalone drive).
    # Mix stays for both.
    "dr_model":"c-dr-int",
    "dr_octave":"c-dr-oct",
    "dr_drive":"c-dr-alg", "dr_tone":"c-dr-alg", "dr_level":"c-dr-alg",
    "dr_nam_gain":"c-dr-nam", "dr_nam_vol":"c-dr-nam",
    # cab — SOURCE toggle (Cabinet IR <-> Neural): the IR picker (c-cab-ir) and the NAM picker +
    # Gain/Level (c-cab-nam) are mutually exclusive; Low/High cut + Mix stay for both. Mirrors the
    # standalone cab. A loaded NAM overrides the IR in the DSP, so loading one flips the toggle.
    "cab_nam_gain":"c-cab-nam", "cab_nam_vol":"c-cab-nam",
    # amp NAM input/output trims live in the Neural TAB panel (tab-gated) — no COND class needed.
    # modulation — Center Delay only affects the delay-line types (Chorus 0 / Flanger 3 / Small Clone 6)
    "md_offset":"c-md-delay",
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
# ── Cab MIC PAD (2026-07-14): a draggable side-view of the cab — drag the mic ACROSS the
# cone (vertical = Mic Pos, cap→edge) and AWAY from the grille (horizontal = Mic Dist).
# The two real ports render as HIDDEN standard controls inside (mod-ui bindings/echo keep
# working; script-hexforge.js drives set_port_value from the drag + mirrors changes back).
MICPAD = (
    '<div class="hf-agroup hf-micpad" rata-role="micpad"><span class="hf-agroup-title">MIC PLACEMENT</span>'
    '<div class="hf-agroup-body">'
    # viewBox 140x126 = the original 140x84 side-view stretched x1.5 vertically (centre 63,
    # mic travel +-42) so the pad's height balances the stacked CABINET+ROOM column.
    '<svg viewBox="0 0 140 126" class="hf-mp-svg" rata-role="micsvg">'
    '<title>Drag the mic — double-click resets</title>'
    # fixed-anchor readouts inside the pad (no layout reflow as the text changes)
    '<text x="30" y="11" class="hf-mp-ro"><tspan class="hf-mp-ro-l">POS&#160;&#160;</tspan>'
    '<tspan rata-role="micposv">CAP EDGE</tspan></text>'
    '<text x="134" y="11" class="hf-mp-ro hf-mp-ro-r"><tspan class="hf-mp-ro-l">DIST&#160;&#160;</tspan>'
    '<tspan rata-role="micdistv">CLOSE</tspan></text>'
    # cab body + baffle edge
    '<rect x="1" y="2" width="16" height="122" rx="3" class="hf-mp-wall"/>'
    '<rect x="14.6" y="7.5" width="2.6" height="111" rx="1.2" class="hf-mp-baffle"/>'
    # speaker: curved cone profile + dust cap + surround beads
    '<path d="M17.2 19.5 Q7.5 39 6 63 Q7.5 87 17.2 106.5" class="hf-mp-cone"/>'
    '<circle cx="8.6" cy="63" r="6" class="hf-mp-cap"/>'
    '<circle cx="17.2" cy="19.5" r="1.7" class="hf-mp-bead"/>'
    '<circle cx="17.2" cy="106.5" r="1.7" class="hf-mp-bead"/>'
    # grille cloth line
    '<line x1="21.5" y1="6" x2="21.5" y2="120" class="hf-mp-grille"/>'
    # travel guides + position ticks (center = dust cap, extremes = cone edge)
    '<line x1="26" y1="63" x2="122" y2="63" class="hf-mp-guide"/>'
    '<line x1="26" y1="21" x2="26" y2="105" class="hf-mp-guide"/>'
    '<line x1="24" y1="63" x2="28" y2="63" class="hf-mp-tick"/>'
    '<line x1="24" y1="21" x2="28" y2="21" class="hf-mp-tick"/>'
    '<line x1="24" y1="105" x2="28" y2="105" class="hf-mp-tick"/>'
    '<text x="31" y="65.5" class="hf-mp-lbl">CAP</text>'
    # distance ruler ticks along the bottom (~0/10/20/30 cm)
    '<line x1="26" y1="115.5" x2="26" y2="120" class="hf-mp-tick"/>'
    '<line x1="58" y1="115.5" x2="58" y2="120" class="hf-mp-tick"/>'
    '<line x1="90" y1="115.5" x2="90" y2="120" class="hf-mp-tick"/>'
    '<line x1="122" y1="115.5" x2="122" y2="120" class="hf-mp-tick"/>'
    '<text x="122" y="112.5" class="hf-mp-lbl hf-mp-lbl-r">30 CM</text>'
    # the mic: soft shadow, aim line, head, tapered body + accent band
    '<g rata-role="micdot" class="hf-mp-mic" transform="translate(28,63)">'
    '<ellipse cx="5" cy="8.6" rx="10" ry="2" class="hf-mp-shadow"/>'
    '<line x1="-1.5" y1="0" x2="-7.5" y2="0" class="hf-mp-aim"/>'
    '<circle r="5" class="hf-mp-michead"/>'
    '<path d="M3.8 -3.2 L16.5 -2.1 Q18.8 0 16.5 2.1 L3.8 3.2 Z" class="hf-mp-micbody"/>'
    '<rect x="8.2" y="-1.2" width="4.6" height="2.4" rx="1" class="hf-mp-micband"/>'
    '</g></svg>'
    '<div class="hf-mp-hidden">%s%s</div>'
    '</div></div>')

IR_PICKER = ('<div class="hf-ir"><span class="hf-sel-label">Impulse Response</span>'
    '{{#effect.parameters.3}}{{#path}}'
    '<div class="hf-sel mod-enumerated" mod-role="input-parameter" mod-parameter-uri="{{uri}}" mod-widget="custom-select-path">'
    '<div mod-role="input-parameter-value" rata-role="Ir" mod-parameter-uri="{{uri}}" class="mod-enumerated-selected">Factory Cab (built-in)</div>'
    '<div class="mod-enumerated-list"><div mod-role="enumeration-option" mod-parameter-value="@factory">Factory 4x12 (Thirty-Something)</div>'
    '<div mod-role="enumeration-option" mod-parameter-value="@vox2x12">Chime 2x12 (Vox)</div>'
    '<div mod-role="enumeration-option" mod-parameter-value="@american-ob">American Open-Back 2x12</div>'
    '<div mod-role="enumeration-option" mod-parameter-value="@greenback">Cashback 4x12</div>'
    '<div mod-role="enumeration-option" mod-parameter-value="@hiwatt">Hi-Volt 4x12</div>'
    '<div mod-role="enumeration-option" mod-parameter-value="@doom">Doom 4x12</div>'
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

# Segmented MODE / SOURCE toggle (Internal<->Neural for drive, Cabinet IR<->Neural for cab).
# script-hexforge.js wires the buttons (rata-role) to switch the block's NAM mode + highlights
# the active one (.hf-mode-on). Mirrors the standalone drive/cab moderow.
def mode_seg(rata, label, opts, on0=0):
    btns = "".join('<div class="hf-modebtn%s" rata-role="%s" data-mode="%s">%s</div>'
                   % ((" hf-mode-on" if i == on0 else ""), rata, m, t)
                   for i, (m, t) in enumerate(opts))
    return ('<div class="hf-moderow"><span class="hf-modelabel">%s</span>'
            '<div class="hf-modeseg">%s</div></div>') % (label, btns)

def render_pos(pfx):
    c = CTRL_BY_SYM[pfx + "_pos"]
    opts = "".join('<div mod-role="enumeration-option" mod-port-value="%d">%s</div>' % (v, lbl) for lbl, v in c["scale"])
    return ('<div class="hf-posn mod-enumerated" mod-role="input-control-port" mod-port-symbol="%s" mod-widget="custom-select" title="Slot">'
            '<div mod-role="input-control-value" mod-port-symbol="%s" class="mod-enumerated-selected"></div>'
            '<div class="mod-enumerated-list">%s</div></div>') % (c["sym"], c["sym"], opts)

def render_enable(pfx):
    return ('<div class="hf-on" title="Block on/off"><div class="hf-on-img" mod-role="input-control-port" '
            'mod-port-symbol="%s_enable" mod-widget="switch"></div></div>') % pfx

# ── Node icon glyphs (inline SVG, one per block) ──────────────────────────────
# Simple stroke-only line icons; CSS colours the stroke with the block accent
# (var(--acc)). fill:none + stroke are set in stylesheet-hexforge.css (.hf-node-ico svg).
ICON = {
    "it":  '<svg viewBox="0 0 24 24"><line x1="8" y1="4" x2="8" y2="20"/><circle cx="8" cy="15" r="2.5"/><line x1="16" y1="4" x2="16" y2="20"/><circle cx="16" cy="9" r="2.5"/></svg>',
    "gt":  '<svg viewBox="0 0 24 24"><path d="M8 4H5v16h3"/><path d="M16 4h3v16h-3"/><path d="M9 12l3-2.5 3 2.5-3 2.5z"/></svg>',
    "cp":  '<svg viewBox="0 0 24 24"><line x1="4" y1="5" x2="20" y2="5"/><line x1="4" y1="19" x2="20" y2="19"/><path d="M8 9l4 3 4-3"/><path d="M8 15l4-3 4 3"/></svg>',
    "fz":  '<svg viewBox="0 0 24 24"><path d="M3 12h3l2-7 3 14 2-10 2 6 2-3h4"/></svg>',
    "dr":  '<svg viewBox="0 0 24 24"><path d="M2 12h2c1.5-8 4.5-8 6 0s4.5 8 6 0h2"/></svg>',
    "nail":'<svg viewBox="0 0 24 24"><path d="M6 5h12"/><path d="M9 5l1.4 9L12 20l1.6-6L15 5"/></svg>',
    "amp": '<svg viewBox="0 0 24 24"><path d="M7 16V9a5 5 0 0110 0v7z"/><path d="M12 6.5v6.5"/><path d="M9.6 12.6q2.4 2 4.8 0"/><path d="M9 16v2.6M12 16v3.4M15 16v2.6"/></svg>',
    "cab": '<svg viewBox="0 0 24 24"><rect x="4" y="3" width="16" height="18" rx="2"/><circle cx="12" cy="14" r="4"/><circle cx="12" cy="7" r="1.4"/></svg>',
    "md":  '<svg viewBox="0 0 24 24"><path d="M3 12c1.5-6 4.5-6 6 0s4.5 6 6 0 4.5-6 6 0"/></svg>',
    "dl":  '<svg viewBox="0 0 24 24"><path d="M5 8v8"/><path d="M10 5v14"/><path d="M15 9v6"/><path d="M20 11v2"/></svg>',
    "rv":  '<svg viewBox="0 0 24 24"><circle cx="12" cy="13" r="1.8"/><path d="M7 13a5 5 0 0110 0"/><path d="M3.5 13a8.5 8.5 0 0117 0"/></svg>',
    "wh":  '<svg viewBox="0 0 24 24"><path d="M3 18c4 0 4.5-12 9-12s5 12 9 12"/></svg>',
    "oc":  '<svg viewBox="0 0 24 24"><path d="M7 15V6M4 9l3-3 3 3"/><path d="M17 9v9M14 15l3 3 3-3"/></svg>',
}
# Node subtitle source. Model-bearing blocks are JS-driven (rata-role=nv-<pfx>,
# script-hexforge.js maps the model index → the parody label). Scalar blocks bind a
# representative live value via mod-role so MOD populates value+unit automatically.
NODE_VAL_SYM = {"it": "it_gain", "gt": "gt_thresh", "cp": "cp_thresh",
                "rv": "rv_decay", "wh": "wh_freq", "oc": "oc_down", "nail": "nail_drive"}

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
    out = ['<div class="%s" data-block="%s"%s style="--acc:%s" role="button" tabindex="0" aria-label="%s effect block — activate to edit its controls" title="%s"%s>'
           % (cls, pfx, posattr, accent, title, title, drag)]
    # power/bypass dot — JS-driven (script-hexforge.js writes the port + greys the node);
    # IT has no bypass port so its dot toggles it_enable (locked, can't be removed).
    out.append('<div class="hf-node-dot" role="button" tabindex="0" aria-label="Toggle %s bypass" title="Bypass (A/B)"></div>' % title)
    out.append('<div class="hf-node-ico" aria-hidden="true">%s</div>' % ICON[pfx])
    out.append('<span class="hf-node-name">%s</span>' % title)
    # subtitle: scalar blocks bind a live value; model blocks get a JS-set label span
    if pfx in NODE_VAL_SYM:
        out.append('<span class="hf-node-val" mod-role="input-control-value" mod-port-symbol="%s"></span>' % NODE_VAL_SYM[pfx])
    else:
        out.append('<span class="hf-node-val" rata-role="nv-%s"></span>' % pfx)
    # Hidden mod-widget binds so external/footswitch/preset changes to these ports still
    # echo to the modgui change handler (which keeps the node grey/membership in sync).
    binds = ['<div mod-role="input-control-port" mod-port-symbol="%s" mod-widget="switch"></div>'
             % ("it_enable" if locked else (pfx + "_bypass"))]
    if not locked:
        binds.append('<div mod-role="input-control-port" mod-port-symbol="%s_enable" mod-widget="switch"></div>' % pfx)
    out.append('<div class="hf-node-bind">' + "".join(binds) + '</div>')
    out.append('</div>')
    return "".join(out)

# ── Amp faceplate: realistic per-model panel with grouped knob sections ───────
# The amp block gets a special body: a "faceplate" (brushed panel + glowing tube row
# + a big parody model badge + a tone-snapshot readout) housing the knobs in labelled
# groups (Preamp / Tone Stack / Power Amp / channel-specific). script-hexforge.js swaps
# the per-model skin class (hf-face-mN) + badge text, and drives the tone bars. Groups
# whose whole contents are model-conditional carry the matching c-amp-* class on the
# wrapper so applyAmp() hides the entire group (title included), not just the knobs.
# Amp layout mirrors the standalone amp block: the front-panel faceplate holds only PREAMP +
# TONE STACK, and each secondary section (Power Amp / Brite / Beardo) gets its OWN tolex chassis
# below — a multi-panel "rig" look. Conditional visibility rides the per-control COND classes
# (render_ctrl) plus the group's c-amp-* on each chassis wrapper (script-hexforge.js applyAmp()).
SCREWS4 = ('<div class="hf-screw hf-screw-tl"></div><div class="hf-screw hf-screw-tr"></div>'
           '<div class="hf-screw hf-screw-bl"></div><div class="hf-screw hf-screw-br"></div>')

def amp_body():
    # Top row: just the amp MODEL selector. The NAM file picker + Gain/Level live in the NEURAL tab,
    # whose tab doubles as the internal<->Neural mode switch (mirrors the standalone amp).
    selrow = '<div class="hf-dselects clearfix">%s</div>' % render_ctrl(CTRL_BY_SYM["amp_model"])
    nam_panel = ('<div class="hf-pa-face hf-nam-face"><div class="hf-pa-title">Neural (NAM)</div>'
                 '<div class="hf-mv-group">' + nam_picker(0, "AmpNam", "")
                 + '<div class="hf-onerow">' + render_ctrl(CTRL_BY_SYM["amp_nam_gain"])
                 + render_ctrl(CTRL_BY_SYM["amp_nam_vol"]) + '</div></div></div>')
    def rc(sufs):
        return "".join(render_ctrl(CTRL_BY_SYM["amp_" + s]) for s in sufs if ("amp_" + s) in CTRL_BY_SYM)
    def onerow(sufs):   # all controls in ONE centered row (.hf-onerow), no sub-group boxes
        return '<div class="hf-onerow">%s</div>' % rc(sufs)
    # Front-panel faceplate: preamp + tone in ONE centered row (channel/resonance ride their COND class).
    face = ('<div class="hf-amp-face hf-face-m1">'
            '<div class="hf-amp-tubes"><div class="hf-amp-grille"></div></div>'
            '<div class="hf-amp-plate">' + SCREWS4 +
            '<div class="hf-amp-badge" rata-role="amp-badge">Crunchy McCrunchFace</div>'
            + onerow(["gain", "pl_vol2", "master", "sag", "channel", "resonance", "bass", "mid", "treble", "presence"])
            + '</div></div>')
    # Power Amp — ONE centered row of controls (switches + knobs). The c-amp-paman items
    # (Tube + the valve-stage knobs) hide when PA Auto is on, leaving a clean short row.
    pa = ('<div class="hf-pa-face c-amp-pa"><div class="hf-pa-title">Power Amp</div>'
          + onerow(["pamp_bypass", "pamp_auto", "pamp_tube", "pamp_presence", "pamp_depth",
                    "pamp_sag", "pamp_master", "pamp_nfb", "pamp_resonance", "pamp_airfeel"])
          + '</div>')
    # Brite Channel (Sunn) + Beardo BE — each its own chassis, one centered row.
    def chassis(gcls, gtitle, sufs):
        return ('<div class="hf-pa-face %s"><div class="hf-pa-title">%s</div>%s</div>') % (gcls, gtitle, onerow(sufs))
    brite = chassis("c-amp-sunn", "Brite Channel",
                    ["sunn_vol2", "sunn_bass2", "sunn_mid2", "sunn_treble2", "sunn_bright1", "sunn_bright2", "sunn_link"])
    beardo = chassis("c-amp-be", "Beardo BE", ["fr_channel", "fr_fat", "fr_c45", "fr_sat"])
    # Cali V (Mesa Mark V): TWO-TIER segmented selector over the single amp_mv_mode port (0..8).
    # A Channel row (Clean/Crunch/Lead) + a Mode row (3 channel-specific labels). Nothing to drop
    # down, so nothing clips. The hidden mod-enumerated bind keeps preset/MIDI writes echoing to the
    # JS change handler; the visible buttons write amp_mv_mode = channel*3 + submode (script-hexforge.js).
    def mesa_body():
        # Cali V: DROPDOWNS (Mode + EQ Preset) like the standalone amp, over the graphic-EQ faders.
        def eqslider(suf, freq):
            sym = "amp_" + suf; c = CTRL_BY_SYM[sym]
            return ('<div class="hf-eqslider" title="%s"><div class="hf-eqslider-fader" mod-role="input-control-port" mod-port-symbol="%s"></div>'
                    '<span class="hf-eqslider-t">%s</span><span class="hf-eqslider-v" mod-role="input-control-value" mod-port-symbol="%s"></span></div>') % (c["name"], sym, freq, sym)
        geq = ('<div class="hf-mv-geqlbl">Graphic EQ</div><div class="hf-eqrow">'
               + "".join(eqslider(s, f) for s, f in [("mv_geq0","80"),("mv_geq1","240"),("mv_geq2","750"),("mv_geq3","2.2k"),("mv_geq4","6.6k")])
               + '</div>')
        return ('<div class="hf-pa-face c-amp-mesa"><div class="hf-pa-title">Cali V</div>'
                + '<div class="hf-mv-group">'
                + '<div class="hf-mv-selrow">'
                + render_ctrl(CTRL_BY_SYM["amp_mv_mode"])
                + render_ctrl(CTRL_BY_SYM["amp_mv_eqpreset"])
                + '</div>'
                + '<div class="hf-mv-geqwrap">' + geq + '</div>'
                + '</div>'
                + '</div>')
    mesa = mesa_body()
    # Diamond Plate (Mesa Dual Rectifier): Mode + Variac + Rectifier dropdowns, one row —
    # the Cali V dropdown treatment exactly (no extra chrome).
    recto = ('<div class="hf-pa-face c-amp-recto"><div class="hf-pa-title">Diamond Plate</div>'
             + '<div class="hf-mv-group"><div class="hf-mv-selrow">'
             + render_ctrl(CTRL_BY_SYM["amp_rc_mode"])
             + render_ctrl(CTRL_BY_SYM["amp_rc_variac"])
             + render_ctrl(CTRL_BY_SYM["amp_rc_rect"])
             + '</div></div></div>')
    # Tremont 15 (PRS MT15): Channel + Bright dropdowns, one row.
    mt15 = ('<div class="hf-pa-face c-amp-mt15"><div class="hf-pa-title">Tremont 15</div>'
            + '<div class="hf-mv-group"><div class="hf-mv-selrow">'
            + render_ctrl(CTRL_BY_SYM["amp_mt_mode"])
            + render_ctrl(CTRL_BY_SYM["amp_mt_bright"])
            + '</div></div></div>')
    # Fold the stacked chassis into TABS (Amp / Voicing / Power Amp) — only one panel shows at
    # a time, so the amp detail is no longer a giant vertical stack (mirrors the standalone Amp
    # pedal). script-hexforge.js wires the tab clicks and hides tabs that don't apply to the
    # current model (applyAmp): Voicing only for Sunn/Beardo/Cali V; Power Amp hidden for Sunn/NAM.
    tabs = ('<div class="hf-atabs" rata-role="atabs" role="tablist" aria-label="Amp sections">'
            '<div class="hf-atab hf-atab-on" rata-role="atab" data-tab="amp" role="tab" tabindex="0" aria-selected="true">Amp</div>'
            '<div class="hf-atab" rata-role="atab" data-tab="voice" role="tab" tabindex="0" aria-selected="false">Voicing</div>'
            '<div class="hf-atab" rata-role="atab" data-tab="power" role="tab" tabindex="0" aria-selected="false">Power Amp</div>'
            '<div class="hf-atab" rata-role="atab" data-tab="nam" role="tab" tabindex="0" aria-selected="false">Neural</div>'
            '</div>')
    panels = ('<div class="hf-atabpanels">'
              '<div class="hf-atabpanel hf-atab-on" rata-role="apanel" data-tab="amp" role="tabpanel" aria-label="Amp">' + face + '</div>'
              '<div class="hf-atabpanel" rata-role="apanel" data-tab="voice" role="tabpanel" aria-label="Voicing">' + brite + beardo + mesa + recto + mt15 + '</div>'
              '<div class="hf-atabpanel" rata-role="apanel" data-tab="power" role="tabpanel" aria-label="Power Amp">' + pa + '</div>'
              '<div class="hf-atabpanel" rata-role="apanel" data-tab="nam" role="tabpanel" aria-label="Neural">' + nam_panel + '</div>'
              '</div>')
    return selrow + tabs + panels

# ── Shared "module plate" design for EVERY block ──────────────────────────────
# The amp has its tube bay + control plate; every other block gets the SAME premium
# brushed-metal plate (tinted with the block accent), corner screws, and its controls
# ORGANISED into labelled groups. One consistent design language across the whole chain.
SCREWS = ('<div class="hf-screw hf-screw-tl"></div><div class="hf-screw hf-screw-tr"></div>'
          '<div class="hf-screw hf-screw-bl"></div><div class="hf-screw hf-screw-br"></div>')

def hex_rgba(h, a):
    h = h.lstrip("#")
    return "rgba(%d,%d,%d,%s)" % (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)

def darken(h, f):
    """Scale a hex colour toward black by factor f — a painted-enclosure tone from the accent."""
    h = h.lstrip("#")
    return "#%02x%02x%02x" % (min(255, int(int(h[0:2], 16) * f)),
                              min(255, int(int(h[2:4], 16) * f)),
                              min(255, int(int(h[4:6], 16) * f)))

# Per-block control grouping (title, conditional-class-or-None, [param suffixes]). Any
# param not listed falls into a trailing "MORE" group so nothing is ever dropped.
BLOCK_GROUPS = {
    "it":  [("INPUT", None, ["gain", "phase", "hum"]),
            ("HUMBUCKER VOICING", None, ["humbk", "hbmodel", "hbamt"]),
            ("CLEAN BOOST", None, ["boost", "boostamt"])],
    "gt":  [("NOISE GATE", None, ["thresh", "attack", "hold", "release", "hyst"])],
    "cp":  [("COMPRESSOR", None, ["type", "ratio", "thresh", "attack", "release", "knee", "makeup"])],
    "fz":  [("PEDAL", None, ["pedal", "mode"]),
            ("VOICE", None, ["sustain", "tone", "volume", "bias", "inputtrim", "getemp"])],
    "dr":  [("DRIVE", None, ["model", "drive", "tone", "level", "mix", "octave"])],
    "nail":[("NAIL", None, ["mode", "drive", "tone", "texture", "level"])],
    "cab": [("CABINET", None, ["voice", "lowcut", "highcut", "mix"]),
            ("ROOM", None, ["roomon", "roommix", "roomamt"])],   # mic placement renders as the MICPAD widget (below), not knobs
    "md":  [("MODULATION", None, ["type", "rate", "depth", "mix", "width", "offset"]),
            ("CLOCK SYNC", None, ["sync", "div"])],
    "dl":  [("DELAY", None, ["type", "time", "feedback", "mix", "width"]),
            ("CLOCK SYNC", None, ["sync", "div"]),
            ("CHARACTER", "c-dl-tape", ["wow", "flutter", "heads"]),
            ("SERAPH DUAL", "c-dl-seraph", ["pattern", "ducking", "moddepth", "modrate"])],
    "rv":  [("REVERB", None, ["predelay", "decay", "damping", "mix"]),
            ("MODULATION", None, ["moddepth", "modrate"])],
    "wh":  [("WAH", None, ["type", "freq", "depth", "sens", "q", "mix"])],
    "oc":  [("OCTAVE", None, ["up", "down", "dry"]),
            ("MICROTONAL SHIMMER", None, ["micro", "interval"])],
}

def render_agroups(pfx, group_defs, all_sufs):
    out, covered = [], set()
    for gtitle, gcls, sufs in group_defs:
        ctrls = ""
        for s in sufs:
            sym = pfx + "_" + s
            if sym in CTRL_BY_SYM:
                ctrls += render_ctrl(CTRL_BY_SYM[sym]); covered.add(s)
        if ctrls:
            wrap = "hf-agroup" + ((" " + gcls) if gcls else "")
            out.append('<div class="%s"><span class="hf-agroup-title">%s</span>'
                       '<div class="hf-agroup-body">%s</div></div>' % (wrap, gtitle, ctrls))
    leftover = "".join(render_ctrl(CTRL_BY_SYM[pfx + "_" + s]) for s in all_sufs
                       if s not in covered and (pfx + "_" + s) in CTRL_BY_SYM)
    if leftover:
        out.append('<div class="hf-agroup"><span class="hf-agroup-title">MORE</span>'
                   '<div class="hf-agroup-body">%s</div></div>' % leftover)
    return '<div class="hf-agroups">' + "".join(out) + '</div>'

# ── Detail panel (the selected block's full controls, shown at the bottom) ─────
def panel(pfx, title, accent, keys):
    locked = (pfx == "it")
    table = TABLES[pfx]
    head = ['<div class="hf-dhead">']
    head.append('<span class="hf-dname" role="heading" aria-level="2">%s</span>' % title)
    if not locked:
        head.append('<span class="hf-dslot"><span class="hf-dslot-lbl">SLOT</span>%s</span>' % render_pos(pfx))
        head.append('<button type="button" class="hf-dremove" aria-label="Remove %s from the chain" title="Remove this effect from the chain">REMOVE ✕</button>' % title)
    head.append('</div>')
    if pfx == "amp":
        body = amp_body()            # amp keeps its tube bay + per-model faceplate
    else:
        all_sufs = [r[0] for r in table]
        # Drive/Cab NAM slots get a Gain (input) + Level (output) trim beside their picker, plus a
        # segmented MODE/SOURCE toggle that switches between the algorithmic/IR path and the Neural
        # (NAM) path (mirrors the standalone drive/cab). The trims carry the c-*-nam COND class.
        nam_trims = (render_ctrl(CTRL_BY_SYM[pfx + "_nam_gain"]) + render_ctrl(CTRL_BY_SYM[pfx + "_nam_vol"])
                     if (pfx + "_nam_gain") in CTRL_BY_SYM else "")
        if pfx == "dr":
            # MODE toggle (Internal / Neural); the NAM picker + trims live in Neural mode (c-dr-nam),
            # the model dropdown in Internal mode (c-dr-int, applied via COND on dr_model).
            pickers = (mode_seg("drmodebtn", "MODE", [("int", "Internal"), ("nam", "Neural")])
                       + '<div class="hf-nampick c-dr-nam">' + nam_picker(2, "DrNam", "") + '</div>' + nam_trims)
        elif pfx == "cab":
            # Cabinets load IMPULSE RESPONSES only — NAM models amps/pedals, not cabs (user 2026-07-13:
            # removing the NAM source was the fix). Just the IR picker, no SOURCE toggle, no NAM picker/trims.
            pickers = IR_PICKER
        else:
            pickers = ""
        inner = SCREWS
        if locked:   # live input level meter on the Input Trim block (front of chain)
            inner += ('<div class="hf-inmeter"><span class="hf-inmeter-lbl">INPUT LEVEL</span>'
                      '<div class="hf-meter hf-meter-h"><div class="hf-meter-fill" rata-role="imeter"></div></div></div>')
        if pickers:
            inner += '<div class="hf-dselects clearfix">%s</div>' % pickers
        if pfx == "cab":
            # Two-column cab layout (user 2026-07-14): CABINET stacked over ROOM on the LEFT,
            # the mic drag pad on the RIGHT — no more pad sprawling across the bottom.
            groups = render_agroups(pfx, BLOCK_GROUPS[pfx], all_sufs)
            pad = MICPAD % (render_ctrl(CTRL_BY_SYM["cab_micpos"]), render_ctrl(CTRL_BY_SYM["cab_micdist"]))
            inner += '<div class="hf-cab-cols"><div class="hf-cab-left">%s</div><div class="hf-cab-right">%s</div></div>' % (groups, pad)
        else:
            inner += render_agroups(pfx, BLOCK_GROUPS.get(pfx, [(title.upper(), None, all_sufs)]), all_sufs)
        body = '<div class="hf-plate">%s</div>' % inner
    style = "--acc:%s" % accent
    if pfx != "amp":                 # painted pedal-enclosure colours derived from the accent
        style += ";--encl1:%s;--encl2:%s" % (darken(accent, 0.42), darken(accent, 0.24))
    cls = "hf-detail-panel hf-detail-lock" if locked else "hf-detail-panel"
    return ('<div class="%s" data-block="%s" style="%s">%s<div class="hf-dbody">%s</div></div>'
            % (cls, pfx, style, "".join(head), body))

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

# Electric connector = the ORIGINAL live vector helix, made cheap. Four glowing strands (gentle
# phase-offset sine waves) travel through the node strip. The fans previously spun on the SVG
# drop-shadow BLUR filters, not the motion — so here the glow is faked with stacked wide/mid
# low-opacity strokes (no filters), and it travels via a compositor CSS transform on <g> (one
# wavelength = seamless), not a per-frame JS loop. The paths are static; CSS does all the motion.
def _sine(ph):
    pts = ["%.1f %.1f" % (x, 60 + 30 * math.sin(2 * math.pi * x / 750.0 + ph))
           for x in range(-750, 1951, 16)]
    return "M" + " L ".join(pts)

_HXC = [("cyan", "#4ffcff"), ("blue", "#3a6bff"), ("purple", "#b44cff"), ("orange", "#ff7b2f")]
def _layer(cls):
    return "".join('<path class="hf-hs hf-hs-%s hf-hx-%s" d="%s"/>' % (cls, n, _sine(i * 0.7))
                   for i, (n, _c) in enumerate(_HXC))
HELIX_SVG = ('<div class="hf-helix" aria-hidden="true"><svg class="hf-helix-svg" viewBox="0 0 1200 120" '
             'preserveAspectRatio="none"><path class="hf-helix-base" d="M0 60 L1200 60"/>'
             '<g class="hf-helix-spin">' + _layer("glow") + _layer("core") + '</g></svg></div>')

def emit_icon():
    nodes  = "".join(node(t[0], t[1], t[2]) for t in TILES)
    panels = "\n      ".join(panel(*t) for t in TILES)
    chain = (
        '  <div class="hf-chain" role="group" aria-label="Signal chain">\n'
        '    <span class="hf-end hf-in" aria-hidden="true">IN</span>\n'
        '    <div class="hf-nodes" rata-role="nodes" role="group" aria-label="Effect chain in signal order — drag to reorder">' + HELIX_SVG + nodes + '</div>\n'
        '    <span class="hf-end hf-out2" aria-hidden="true">OUT</span>\n'
        '    <div class="hf-addwrap">\n'
        '      <button type="button" class="hf-add" aria-label="Add an effect to the chain" aria-haspopup="true" aria-expanded="false" title="Add an effect to the chain">＋ ADD</button>\n'
        '      <div class="hf-palette" rata-role="palette"></div>\n'
        '    </div>\n'
        '  </div>\n')
    detail = '  <div class="hf-detail" rata-role="detail" role="region" aria-label="Selected effect controls">\n      ' + panels + '\n  </div>\n'
    # Strobe tuner overlay — floats over the bottom of the detail panel when engaged (tuner_on).
    # The disc spins at a rate/direction set by cents (still + green = in tune); note reads the pitch.
    tuner = ('  <div class="hf-tuner mod-hidden" rata-role="tuner" role="dialog" aria-label="Strobe tuner">\n'
        '    <div class="hf-tuner-note" rata-role="tunernote" role="status" aria-live="polite" aria-label="Detected note">–</div>\n'
        '    <div class="hf-tuner-meterwrap">\n'
        '      <div class="hf-tuner-scale"><span class="hf-tuner-lab">♭ flat</span><span class="hf-tuner-lab">in tune</span><span class="hf-tuner-lab">sharp ♯</span></div>\n'
        '      <div class="hf-tuner-meter">\n'
        '        <div class="hf-tuner-zone"></div>\n'
        '        <div class="hf-tuner-mid"></div>\n'
        '        <div class="hf-tuner-needle" rata-role="tunerdisc"></div>\n'
        '      </div>\n'
        '      <div class="hf-tuner-cents" rata-role="tunercents" role="status" aria-live="polite" aria-label="Cents deviation">no signal</div>\n'
        '    </div>\n'
        '    <div class="hf-tuner-mute" title="Mute output while tuning">' + render_ctrl(CTRL_BY_SYM["tuner_mute"]) + '</div>\n'
        '    <button type="button" class="hf-tunerclose" rata-role="tunerclose" aria-label="Close tuner" title="Close tuner">✕</button>\n'
        '  </div>\n')
    return ('<div class="mod-pedal mod-pedal-guitaramp-hexforge{{{cns}}} ">\n'
        '  <div mod-role="drag-handle" class="mod-drag-handle"></div>\n'
        # UNIFIED TOOLBAR — brand (left) · preset recall (centre) · master output + latency (right).
        # Consolidates the old header/output/presets strips into one bar. All wired controls kept.
        '  <div class="hf-bar" role="toolbar" aria-label="Hex Forge toolbar">\n'
        '    <div class="hf-bar-brand"><span class="hx-mark" aria-hidden="true"></span><span class="hf-title">HEX FORGE</span></div>\n'
        '    <div class="hf-bar-presets" role="group" aria-label="Preset recall">\n'
        '      <button type="button" class="hf-ps-btn hf-ps-bankdn" aria-label="Previous bank" title="Bank down (hold A+B on the pedal)">◀</button>\n'
        '      <span class="hf-ps-bank" rata-role="psbank" role="status" aria-live="polite" aria-label="Current bank">BANK 1</span>\n'
        '      <button type="button" class="hf-ps-btn hf-ps-bankup" aria-label="Next bank" title="Bank up (hold C+D on the pedal)">▶</button>\n'
        '      <span class="hf-ps-slots" role="group" aria-label="Preset slots">\n'
        '        <button type="button" class="hf-ps-slot" data-slot="0" aria-label="Recall preset A">A</button>\n'
        '        <button type="button" class="hf-ps-slot" data-slot="1" aria-label="Recall preset B">B</button>\n'
        '        <button type="button" class="hf-ps-slot" data-slot="2" aria-label="Recall preset C">C</button>\n'
        '        <button type="button" class="hf-ps-slot" data-slot="3" aria-label="Recall preset D">D</button>\n'
        '      </span>\n'
        '      <input type="text" class="hf-ps-name" rata-role="psname" maxlength="31" spellcheck="false" placeholder="(unnamed)" aria-label="Preset name" title="Preset name — type and press Enter to rename" />\n'
        '      <button type="button" class="hf-ps-btn hf-ps-save" aria-label="Save current preset" title="Save over the current preset">SAVE</button>\n'
        '      <button type="button" class="hf-ps-btn hf-ps-toggle" aria-label="Browse and manage all presets" aria-haspopup="true" aria-expanded="false" title="Browse all presets &amp; manage">≡</button>\n'
        # ≡ dropdown: a manage header (reorder + backup/restore) above the full 8×4 preset grid.
        # Keeps those low-frequency actions out of the always-visible bar.
        '      <div class="hf-ps-menu" rata-role="psmenu" role="menu" aria-label="Preset management">\n'
        '        <div class="hf-ps-menuhead">\n'
        '          <button type="button" class="hf-ps-btn hf-ps-mvup" aria-label="Move preset earlier" title="Move this preset earlier in the list">▲ Earlier</button>\n'
        '          <button type="button" class="hf-ps-btn hf-ps-mvdn" aria-label="Move preset later" title="Move this preset later in the list">▼ Later</button>\n'
        '          <span class="hf-ps-menusep" aria-hidden="true"></span>\n'
        '          <button type="button" class="hf-ps-btn hf-ps-backup" aria-label="Back up all presets to disk" title="Back up all 32 presets to disk (survives delete/re-add &amp; updates)">⭳ Backup</button>\n'
        '          <button type="button" class="hf-ps-btn hf-ps-restore" aria-label="Restore all presets from disk" title="Restore all presets from the last backup on disk">⭱ Restore</button>\n'
        '        </div>\n'
        '        <div class="hf-ps-list" rata-role="pslist" role="listbox" aria-label="All presets"></div>\n'
        '      </div>\n'
        '    </div>\n'
        '    <div class="hf-bar-out" role="group" aria-label="Master output">\n'
        '      <button type="button" class="hf-tunerbtn" rata-role="tunerbtn" aria-label="Strobe tuner" aria-pressed="false" title="Strobe tuner — click to open / close">'
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M8 3 L8 10 Q8 13.5 12 13.5 Q16 13.5 16 10 L16 3"/><path d="M12 13.5 L12 21"/></svg></button>\n'
        '      <span class="hf-bar-sep" aria-hidden="true"></span>\n'
        '      <span class="hf-out-name">OUT</span>\n'
        '      <div class="hf-meter hf-meter-h" role="meter" aria-label="Output level meter" title="Output level"><div class="hf-meter-fill" rata-role="ometer"></div></div>\n'
        '      <span class="hf-clip" rata-role="clip" role="status" aria-live="assertive" aria-label="Output clipping indicator">CLIP</span>\n'
        '      <span class="hf-clipval mod-hidden" mod-role="input-control-value" mod-port-symbol="clip"></span>\n'
        '      ' + render_ctrl(CTRL_BY_SYM["out_auto"]) + '\n'
        '      ' + render_ctrl(CTRL_BY_SYM["out_mono"]) + '\n'
        '      ' + render_ctrl(CTRL_BY_SYM["out_doubler"]) + '\n'
        '      ' + render_ctrl(CTRL_BY_SYM["out_level"]) + '\n'
        '      <div class="mod-powerswitch" mod-role="bypass" role="switch" aria-label="Global bypass" title="Global bypass · latency &lt;1 ms"><div class="mod-powerswitch-image" mod-role="bypass-light"></div></div>\n'
        '    </div>\n'
        '  </div>\n'
        + chain + detail + tuner +
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
