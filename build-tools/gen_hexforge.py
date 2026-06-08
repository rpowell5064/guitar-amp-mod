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
    ("phase", "Phase",      "t",  0, 1, 0, None),
    ("hum",   "Hum Filter", "t",  0, 1, 0, None),
]
GT = [
    ("thresh",  "Threshold",  "db", -80, 0, -60, None),
    ("attack",  "Attack",     "ms", 0.1, 50, 5, None),
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
    ("pedal",     "Pedal",      "e", 0, 1, 0, [("Italian Hero",0),("I Know It",1)]),
    ("mode",      "Variant",    "e", 0, 5, 2, [("Delta",0),("Ovis",1),("Gotham",2),("Cold War",3),("Red Bear",4),("Boutique",5)]),
    ("sustain",   "Sustain",    "f", 0, 1, 0.55, None),
    ("tone",      "Tone",       "f", 0, 1, 0.5, None),
    ("volume",    "Volume",     "f", 0, 1, 0.65, None),
    ("bias",      "Bias",       "f", 0, 1, 0.5, None),
    ("inputtrim", "Input Trim", "f", 0, 1, 0.5, None),
    ("getemp",    "Ge Temp",    "f", 0, 1, 0.4, None),
]
DR = [
    ("model",  "Model",  "e", 0, 2, 0, [("Green Man",0),("New Dawn",1),("Dear Rodent Boy",2)]),
    ("drive",  "Drive",  "f", 0, 1, 0.5, None),
    ("tone",   "Tone",   "f", 0, 1, 0.5, None),
    ("level",  "Level",  "f", 0, 1, 0.5, None),
    ("mix",    "Mix",    "f", 0, 1, 1.0, None),
    ("octave", "Octave", "f", 0, 1, 0.3, None),
]
AMP = [
    ("model",         "Model",        "e", 0, 4, 1, [("Clean Meanie",0),("Crunchy McCrunchFace",1),("Gainzilla",2),("Doom Daddy",3),("Tangerang",4)]),
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
    ("type",  "Type",  "e", 0, 1, 0, [("Lush-2",0),("Uni-Verse",1)]),
    ("rate",  "Rate",  "f", 0, 1, 0.5, None),
    ("depth", "Depth", "f", 0, 1, 0.5, None),
    ("mix",   "Mix",   "f", 0, 1, 0.5, None),
    ("width", "Width", "f", 0, 1, 0.5, None),
]
DL = [
    ("type",    "Type",     "e", 0, 2, 0, [("Digital",0),("Tape",1),("Echo Wreck",2)]),
    ("time",    "Time",     "ms", 1, 2000, 250, None),
    ("feedback","Feedback", "f", 0, 0.98, 0.4, None),
    ("mix",     "Mix",      "f", 0, 1, 0.3, None),
    ("width",   "Width",    "f", 0, 1, 0.5, None),
    ("wow",     "Wow",      "f", 0, 0.05, 0.003, None),
    ("flutter", "Flutter",  "f", 0, 0.02, 0.001, None),
    ("heads",   "Heads",    "e", 0, 11, 10, [
        ("1: Head 1",0),("2: Head 2",1),("3: Head 3",2),("4: Head 4",3),
        ("5: Heads 1+2",4),("6: Heads 2+3",5),("7: Heads 3+4",6),
        ("8: Heads 1+2+3",7),("9: Heads 2+3+4",8),("10: Heads 1+3+4",9),
        ("11: All Heads",10),("12: All + Dense",11)]),
]
RV = [
    ("predelay", "Pre-Delay", "ms", 0, 100, 10, None),
    ("decay",    "Decay",     "f", 0.1, 8, 1.5, None),
    ("damping",  "Damping",   "f", 0, 0.99, 0.3, None),
    ("moddepth", "Mod Depth", "f", 0, 1, 0.5, None),
    ("modrate",  "Mod Rate",  "f", 0.01, 5, 0.8, None),
    ("mix",      "Mix",       "f", 0, 1, 0.3, None),
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
]

UNIT = {"db": "units:db", "ms": "units:ms", "hz": "units:hz"}

# ── Build the ordered control-port list ───────────────────────────────────────
# Each entry: dict(enum, sym, name, kind, mn, mx, df, scale)
def mkport(enum, sym, name, kind, mn, mx, df, scale, short=None):
    return dict(enum=enum, sym=sym, name=name, kind=kind, mn=mn, mx=mx, df=df,
                scale=scale, short=short or name)

ctrl = []
# global bypass first
ctrl.append(mkport("BYPASS", "bypass", "Bypass", "t", 0, 1, 0, None))
# master output level — applied LAST in the chain (the "Output" stage that feeds
# the device). Shown in the top Output strip, not as a chain tile.
ctrl.append(mkport("OUT_LEVEL", "out_level", "Output Level", "f", 0, 1, 1.0, None, "Master"))
# input trim (locked): enable + params, no pos. Per-block toggles use ENABLE
# semantics (1 = on/active, default on) so a lit tile switch means "block engaged".
ctrl.append(mkport("IT_ENABLE", "it_enable", "Input Trim Enable", "t", 0, 1, 1, None, "On"))
for suf, nm, kind, mn, mx, df, sc in IT:
    ctrl.append(mkport("IT_" + suf.upper(), "it_" + suf, "IT " + nm, kind, mn, mx, df, sc, nm))
# movable blocks: pos, enable, params
for pfx, title, params, dpos in MOVABLE:
    P = pfx.upper()
    posscale = [(str(k), k) for k in range(1, 10)]   # 1..9 dropdown for reordering
    ctrl.append(mkport(P + "_POS",    pfx + "_pos",    title + " Position", "e", 1, 9, dpos, posscale, "Slot"))
    ctrl.append(mkport(P + "_ENABLE", pfx + "_enable", title + " Enable",   "t", 0, 1, 1, None, "On"))
    for suf, nm, kind, mn, mx, df, sc in params:
        ctrl.append(mkport(P + "_" + suf.upper(), pfx + "_" + suf, title + " " + nm, kind, mn, mx, df, sc, nm))

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
    lines.append("    HF_N_PORTS")
    lines.append("};")
    lines.append("static_assert(HF_N_PORTS == %d, \"port count drift\");" % idx)
    lines.append("")
    return "\n".join(lines)

# ── Emit one TTL control-port block ───────────────────────────────────────────
def ttl_ctrl(c, index):
    integral = c["kind"] in ("t", "i", "e")
    out = []
    out.append("    ] , [")
    out.append("        a lv2:ControlPort, lv2:InputPort ;")
    out.append('        lv2:index %d ; lv2:symbol "%s" ; lv2:name "%s" ;' % (index, c["sym"], c["name"]))
    if c["kind"] in UNIT:
        out.append("        units:unit %s ;" % UNIT[c["kind"]])
    if c["kind"] == "t":
        out.append("        lv2:portProperty lv2:toggled ;")
    elif c["kind"] == "i":
        out.append("        lv2:portProperty lv2:integer ;")
    elif c["kind"] == "e":
        out.append("        lv2:portProperty lv2:integer, lv2:enumeration ;")
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
    L.append("<%s>" % URI)
    L.append("    a lv2:Plugin, lv2:AmplifierPlugin ;")
    L.append('    rdfs:label "Hex Chain — Hex Forge" ;')
    L.append('    mod:label  "Hex Forge" ;')
    L.append('    mod:brand  "Hex Chain" ;')
    L.append("    doap:license <http://opensource.org/licenses/MIT> ;")
    L.append('    doap:maintainer [ a foaf:Person ; foaf:name "Ryan Powell" ;')
    L.append("                      foaf:homepage <https://rpowell5064.github.io/guitaramp-suite/> ] ;")
    L.append("    lv2:minorVersion 1 ;")
    L.append("    lv2:microVersion 0 ;")
    L.append("")
    L.append("    # Amp model rebuilds + cab IR loads run on the worker thread.")
    L.append("    lv2:requiredFeature urid:map , work:schedule ;")
    L.append("    lv2:optionalFeature lv2:hardRTCapable ;")
    L.append("    lv2:extensionData state:interface , work:interface ;")
    L.append("")
    L.append("    patch:writable <%s#irfile> ;" % URI)
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
    L.append("        rsz:minimumSize 8192 ;")
    L.append('        lv2:index 5 ; lv2:symbol "notify" ; lv2:name "Notify"')
    # control ports
    for i, c in enumerate(ctrl):
        L.append(ttl_ctrl(c, NFIXED + i))
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
TABLES = {"it":IT,"gt":GT,"cp":CP,"fz":FZ,"dr":DR,"amp":AMP,"cab":CAB,"md":MD,"dl":DL,"rv":RV}
# (prefix, tile title, accent, key-param suffixes shown always; rest go to "More")
TILES = [
    ("it",  "Input Trim", "#7d8590", ["gain","phase","hum"]),
    ("gt",  "Gate",       "#19e0ff", ["thresh","release"]),
    ("cp",  "Comp",       "#38d39f", ["type","thresh","ratio","makeup"]),
    ("fz",  "Fuzz",       "#eb5046", ["pedal","mode","sustain","tone","volume"]),
    ("dr",  "Drive",      "#ff8a3d", ["model","drive","tone","level"]),
    ("amp", "Amp",        "#ff2bd6", ["model","gain","bass","mid","treble","presence","master"]),
    ("cab", "Cabinet",    "#b07cff", ["lowcut","highcut","mix"]),
    ("md",  "Mod FX",     "#4db5ff", ["type","rate","depth","mix"]),
    ("dl",  "Delay",      "#ffd23d", ["type","time","feedback","mix"]),
    ("rv",  "Reverb",     "#5ce6c8", ["decay","damping","mix"]),
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
    # power-amp: whole section hidden for Sunn; manual knobs hidden when PA Auto on
    "amp_pamp_bypass":"c-amp-pa", "amp_pamp_auto":"c-amp-pa",
    "amp_pamp_resonance":"c-amp-pa", "amp_pamp_airfeel":"c-amp-pa",
    "amp_pamp_tube":"c-amp-paman", "amp_pamp_presence":"c-amp-paman", "amp_pamp_depth":"c-amp-paman",
    "amp_pamp_sag":"c-amp-paman", "amp_pamp_master":"c-amp-paman", "amp_pamp_nfb":"c-amp-paman",
    # fuzz — Italian Hero(0) vs I know it(1)
    "fz_mode":"c-fz-ih", "fz_tone":"c-fz-ih",
    "fz_bias":"c-fz-tb", "fz_inputtrim":"c-fz-tb", "fz_getemp":"c-fz-tb",
    # drive — Octave only on New Dawn (model 1)
    "dr_octave":"c-dr-oct",
    # delay — Wow/Flutter for Tape(1)/Echo Wreck(2); Heads for Echo Wreck(2)
    "dl_wow":"c-dl-tape", "dl_flutter":"c-dl-tape", "dl_heads":"c-dl-heads",
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

IR_PICKER = ('<div class="hf-ir"><span class="hf-sel-label">Impulse Response</span>'
    '{{#effect.parameters.0}}{{#path}}'
    '<div class="hf-sel mod-enumerated" mod-role="input-parameter" mod-parameter-uri="{{uri}}" mod-widget="custom-select-path">'
    '<div mod-role="input-parameter-value" rata-role="Ir" mod-parameter-uri="{{uri}}" class="mod-enumerated-selected">-- choose an IR file --</div>'
    '<div class="mod-enumerated-list">{{#files}}<div mod-role="enumeration-option" mod-parameter-value="{{fullname}}">{{basename}}</div>{{/files}}</div>'
    '</div>{{/path}}{{/effect.parameters.0}}</div>')

def render_pos(pfx):
    c = CTRL_BY_SYM[pfx + "_pos"]
    opts = "".join('<div mod-role="enumeration-option" mod-port-value="%d">%s</div>' % (v, lbl) for lbl, v in c["scale"])
    return ('<div class="hf-posn mod-enumerated" mod-role="input-control-port" mod-port-symbol="%s" mod-widget="custom-select" title="Slot">'
            '<div mod-role="input-control-value" mod-port-symbol="%s" class="mod-enumerated-selected"></div>'
            '<div class="mod-enumerated-list">%s</div></div>') % (c["sym"], c["sym"], opts)

def render_enable(pfx):
    return ('<div class="hf-on" title="Block on/off"><div class="hf-on-img" mod-role="input-control-port" '
            'mod-port-symbol="%s_enable" mod-widget="switch"></div></div>') % pfx

def tile(pfx, title, accent, keys):
    table = TABLES[pfx]
    locked = (pfx == "it")
    head = ['<div class="hf-thead">']
    if locked:
        head.append('<span class="hf-grip hf-grip-lock">⌗</span>')
        head.append('<span class="hf-posn hf-posn-lock">0</span>')   # locked pre-block = slot 0
    else:
        head.append('<span class="hf-grip" title="Drag to reorder">⋮⋮</span>')
        head.append(render_pos(pfx))
    head.append('<span class="hf-name">%s</span>' % title)
    head.append(render_enable(pfx))
    head.append('</div>')

    # Key controls show inline; everything else lives behind a floating "More" popup
    # (overlay, so it doesn't grow the tile). Conditional controls are hidden by
    # script-hexforge.js when they don't apply.
    keyhtml  = "".join(render_ctrl(CTRL_BY_SYM[pfx + "_" + r[0]]) for r in table if r[0] in keys)
    if pfx == "cab":
        keyhtml = IR_PICKER + keyhtml
    morehtml = "".join(render_ctrl(CTRL_BY_SYM[pfx + "_" + r[0]]) for r in table if r[0] not in keys)

    cls = "hf-tile hf-locked" if locked else "hf-tile"
    posattr = "" if locked else ' data-pos="%d"' % CTRL_BY_SYM[pfx + "_pos"]["df"]
    out = ['<div class="%s" data-block="%s"%s style="--acc:%s">' % (cls, pfx, posattr, accent)]
    out += head
    out.append('<div class="hf-key clearfix">%s</div>' % keyhtml)
    if morehtml:
        out.append('<div class="hf-morewrap"><button type="button" class="hf-morebtn">More ▾</button>'
                   '<div class="hf-more clearfix">%s</div></div>' % morehtml)
    out.append('</div>')
    return "".join(out)

def emit_icon():
    tiles = "\n      ".join(tile(*t) for t in TILES)
    return ('<div class="mod-pedal mod-pedal-guitaramp-hexforge{{{cns}}} ">\n'
        '  <div mod-role="drag-handle" class="mod-drag-handle"></div>\n'
        '  <div class="mod-powerswitch" mod-role="bypass"><div class="mod-powerswitch-image" mod-role="bypass-light"></div></div>\n'
        '  <div class="hf-header">\n'
        '    <div class="hx-brand"><span class="hx-mark"></span>HEX CHAIN</div>\n'
        '    <div class="hf-title">HEX FORGE</div>\n'
        '    <div class="hf-sub">prewired rig · drag a tile or pick its slot to reorder · per-block on/off</div>\n'
        '    <div class="hx-logo"></div>\n'
        '  </div>\n'
        '  <div class="hf-out">\n'
        '    <span class="hf-out-mark"></span>\n'
        '    <span class="hf-out-name">Output</span>\n'
        '    <span class="hf-out-sub">last in chain &rarr; default device</span>\n'
        '    <span class="hf-out-spacer"></span>\n'
        '    ' + render_ctrl(CTRL_BY_SYM["out_level"]) + '\n'
        '  </div>\n'
        '  <div class="hf-rack">\n      ' + tiles + '\n  </div>\n'
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
