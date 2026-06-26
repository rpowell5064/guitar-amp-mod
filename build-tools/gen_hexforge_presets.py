# ─────────────────────────────────────────────────────────────────────────────
# Hex Forge factory-preset generator.
#
# Authors the extra band/song factory presets (Banks 2..6) from high-level, NAMED
# block params and emits a C++ header the plugin includes:
#     lv2/hexforge/hexforge_factory_presets.h
#
# Why a generator: a preset is a float vals[HF_N_PORTS] indexed by the HexForgePort
# enum. Hand-typing 135-float rows against that enum is error-prone and silently
# drifts whenever a port is inserted mid-enum (exactly what happened to the original
# four inline presets — see verify_existing() below). This script reuses the SINGLE
# source of truth (build-tools/gen_hexforge.py's ordered `ctrl` list) so every value
# lands on the right port and unspecified params inherit their real block default.
#
# Run from the repo root:  python build-tools/gen_hexforge_presets.py
# ─────────────────────────────────────────────────────────────────────────────
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_hexforge as G          # importing only builds the ctrl tables (its writes are __main__-guarded)

NFIXED  = G.NFIXED                # 6 leading audio/atom ports
CTRL    = G.ctrl
CBY     = G.CTRL_BY_SYM
N_PORTS = NFIXED + len(CTRL) + 1  # + trailing midi_in == HF_N_PORTS
SYM_IDX = {c["sym"]: NFIXED + i for i, c in enumerate(CTRL)}

def _enum_label_map(sym):
    c = CBY[sym]
    if c["kind"] != "e" or not c.get("scale"):
        return None
    return {lbl.lower(): int(val) for lbl, val in c["scale"]}

def _resolve(sym, val):
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
        # fall back to a unique case-insensitive substring match ("Crunchy" -> "Crunchy McCrunchFace")
        hits = [lbl for lbl in m if key in lbl]
        if len(hits) == 1:
            return float(m[hits[0]])
        raise ValueError("port %s has no scalePoint %r (have: %s)"
                         % (sym, val, ", ".join(sorted(m))))
    return float(val)

# Baseline chain: amp + cab on, gate on (mild), everything else off. Authoring then
# only has to turn ON what a sound needs (delay/reverb/mod/drive/fuzz/comp).
_BASE_ENABLE = {"gt": 1, "cp": 0, "fz": 0, "dr": 0, "amp": 1, "cab": 1, "md": 0, "dl": 0, "rv": 0}

def _base_vals():
    v = [0.0] * N_PORTS
    for i, c in enumerate(CTRL):           # seed every control port with its real default
        v[NFIXED + i] = float(c["df"])
    v[SYM_IDX["it_enable"]] = 1.0
    for pfx, en in _BASE_ENABLE.items():
        v[SYM_IDX[pfx + "_enable"]] = float(en)
    return v

def preset(bank, slot, name, out_level=-20.0, **blocks):
    """Build one factory preset. `blocks` maps a block prefix -> {param: value}.
    Param keys are the suffixes from gen_hexforge.py's tables (e.g. amp={'model':'Beardo BE'}).
    Enum params accept either the integer or the scalePoint label string. Use
    enable=1/0 inside a block to engage/bypass it."""
    if not (0 <= bank < G_BANKS and 0 <= slot < G_SLOTS):
        raise ValueError("bank/slot out of range: %d/%d" % (bank, slot))
    v = _base_vals()
    v[SYM_IDX["out_level"]] = float(out_level)
    for pfx, params in blocks.items():
        for key, val in params.items():
            sym = pfx + "_" + key
            v[SYM_IDX[sym]] = _resolve(sym, val)
    return dict(bank=bank, slot=slot, name=name, vals=v)

G_BANKS, G_SLOTS = 8, 4

# ─────────────────────────────────────────────────────────────────────────────
# The presets. Bank index 1 == "Bank 2" in the UI (Bank 1 / index 0 stays the
# stock Clean/Crunch/Rhythm/Lead). Slots 0..3 == A..D. Tones are faithful
# rig-matches (researched real amp/pedals/pickups); user plays humbuckers, so the
# single-coil→humbucker voicing is left OFF everywhere. Names are parody/legal-safe.
# Knob values (0..1 unless the port carries a unit) are sensible starting points,
# meant to be fine-tuned by ear on the device.
# ─────────────────────────────────────────────────────────────────────────────
PRESETS = []
def add(*p): PRESETS.extend(p)

# Anti-fizz recipe distilled from the user's own good presets: TS/boost drive ~0
# (level high), mids forward (0.6-0.75), bass moderate (0.4-0.5), master LOW
# (0.38-0.50) — NOT 0.7, presence/treble moderate, cab high-cut 8000-8800 for gain
# tones (lower for doom), and a gate on every dirty preset. out_level is a -20
# placeholder here; the measure step rewrites each to loudness-match the stock 4.
OUT = -20.0

# ── Bank 3 — Ghost I (Opus = Orange; Impera = Friedman, user pref) ────────────
add(
  preset(2, 0, "Sermon Crunch", out_level=OUT,           # Ghost — Opus Eponymous rhythm
    # SG -> Orange Thunderverb 50: warm, mid-forward, gain backed off, '70s.
    amp={"model":"Tangerang","gain":0.5,"bass":0.45,"mid":0.68,"treble":0.55,"presence":0.42,"master":0.45,"sag":0.35},
    gt={"enable":1,"thresh":-55,"attack":3,"hold":60,"release":140,"hyst":6},
    cab={"lowcut":90,"highcut":8200}),
  preset(2, 1, "Candlelit Clean", out_level=OUT,         # Ghost — Opus clean
    # Orange clean + dark analog delay (Carbon Copy) + a little room.
    amp={"model":"Tangerang","gain":0.2,"bass":0.5,"mid":0.6,"treble":0.5,"presence":0.4,"master":0.5,"sag":0.3},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-24,"attack":3,"release":5,"knee":3,"makeup":4},  # density/loudness
    dl={"enable":1,"type":"Tape","time":420,"feedback":0.28,"mix":0.16,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"decay":1.8,"damping":0.4,"mix":0.14},
    cab={"lowcut":85,"highcut":8500}),
  preset(2, 2, "Imperial Rhythm", out_level=OUT,         # Ghost — Impera rhythm (Friedman BE, Akesson's BE-100)
    amp={"model":"Beardo BE","fr_channel":"BE","fr_fat":1,"gain":0.55,"bass":0.5,"mid":0.62,"treble":0.55,"presence":0.45,"master":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.03,"tone":0.55,"level":0.85,"mix":1.0},   # TS boost (drive ~0)
    gt={"enable":1,"thresh":-50,"attack":1,"hold":60,"release":150,"hyst":6},
    cab={"lowcut":88,"highcut":8400}),
  preset(2, 3, "Imperial Lead", out_level=OUT,           # Ghost — Impera lead (Friedman HBE)
    amp={"model":"Beardo BE","fr_channel":"HBE","fr_sat":1,"gain":0.62,"bass":0.48,"mid":0.66,"treble":0.56,"presence":0.5,"master":0.46},
    dr={"enable":1,"model":"Green Man","drive":0.04,"tone":0.58,"level":0.8,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":60,"release":150,"hyst":6},
    dl={"enable":1,"type":"Digital","time":400,"feedback":0.3,"mix":0.16},
    rv={"enable":1,"decay":1.6,"mix":0.1},
    cab={"lowcut":88,"highcut":8800}),
)

# ── Bank 4 — Ghost Skeleta (Friedman) + A Perfect Circle ─────────────────────
add(
  preset(3, 0, "Cardinal Rhythm", out_level=OUT,         # Ghost — Skeleta rhythm (Friedman HBE, tight modern)
    amp={"model":"Beardo BE","fr_channel":"HBE","fr_fat":1,"gain":0.6,"bass":0.55,"mid":0.6,"treble":0.54,"presence":0.48,"master":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.03,"tone":0.55,"level":0.85,"mix":1.0},
    gt={"enable":1,"thresh":-48,"attack":0.5,"hold":50,"release":140,"hyst":8},
    cab={"lowcut":90,"highcut":8200}),
  preset(3, 1, "Cardinal Lead", out_level=OUT,           # Ghost — Skeleta lead (Friedman HBE + sat)
    amp={"model":"Beardo BE","fr_channel":"HBE","fr_sat":1,"gain":0.66,"bass":0.5,"mid":0.64,"treble":0.56,"presence":0.52,"master":0.46},
    dr={"enable":1,"model":"Green Man","drive":0.04,"tone":0.58,"level":0.82,"mix":1.0},
    gt={"enable":1,"thresh":-48,"attack":0.5,"hold":55,"release":150,"hyst":6},
    dl={"enable":1,"type":"Digital","time":380,"feedback":0.3,"mix":0.14},
    rv={"enable":1,"decay":1.7,"mix":0.1},
    cab={"lowcut":90,"highcut":8700}),
  preset(3, 2, "Vanishing Drive", out_level=OUT,         # A Perfect Circle — Mer de Noms distorted
    # Les Paul -> Friedman-modded Marshall: needs BITE -> HBE + C45 bright cap + TS tighten.
    amp={"model":"Beardo BE","fr_channel":"HBE","fr_c45":1,"fr_fat":1,"gain":0.6,"bass":0.48,"mid":0.66,"treble":0.58,"presence":0.55,"master":0.46},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.6,"level":0.8,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":60,"release":150,"hyst":6},
    md={"enable":1,"type":"Lush-2","rate":0.3,"depth":0.4,"mix":0.25,"width":0.5},   # Howerdel ambience on dirt
    dl={"enable":1,"type":"Digital","time":380,"feedback":0.25,"mix":0.14},
    rv={"enable":1,"decay":1.6,"mix":0.12},
    cab={"lowcut":86,"highcut":8800}),
  preset(3, 3, "Dreamlit Shimmer", out_level=OUT,        # A Perfect Circle — dreamy chorusy clean
    # Howerdel the "effects guy": lush chorus + ambient delay + big reverb on a clean amp.
    amp={"model":"Clean Meanie","gain":0.3,"bass":0.5,"mid":0.55,"treble":0.52,"presence":0.45,"master":0.5,"sag":0.3},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-24,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Lush-2","rate":0.35,"depth":0.5,"mix":0.4,"width":0.6},
    dl={"enable":1,"type":"Digital","time":450,"feedback":0.38,"mix":0.25,"width":0.6},
    rv={"enable":1,"predelay":20,"decay":3.0,"damping":0.35,"mix":0.28},
    cab={"lowcut":80,"highcut":12000}),
)

# ── Bank 5 — Pink Floyd + Hendrix (classic / psych) ──────────────────────────
add(
  preset(4, 0, "Berlin Wall Pulse", out_level=OUT,       # Pink Floyd — Run Like Hell
    # Strat, palm-muted, huge dotted delay soaked in chorus.
    amp={"model":"Clean Meanie","gain":0.4,"bass":0.45,"mid":0.5,"treble":0.6,"presence":0.55,"master":0.55},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-22,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Lush-2","rate":0.4,"depth":0.55,"mix":0.4,"width":0.7},
    dl={"enable":1,"type":"Digital","time":440,"feedback":0.4,"mix":0.28,"width":0.6},
    rv={"enable":1,"decay":1.8,"mix":0.12},
    cab={"lowcut":85,"highcut":11000}),
  preset(4, 1, "Dark Side Air", out_level=OUT,           # Pink Floyd — Breathe
    # Strat warm clean, compressed, spacious, a hint of vibe.
    amp={"model":"Clean Meanie","gain":0.3,"bass":0.55,"mid":0.5,"treble":0.48,"presence":0.45,"master":0.5},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-24,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Uni-Verse","rate":0.25,"depth":0.3,"mix":0.22},
    dl={"enable":1,"type":"Digital","time":420,"feedback":0.3,"mix":0.16},   # Breathe's gentle echo
    rv={"enable":1,"predelay":25,"decay":2.6,"damping":0.35,"mix":0.24},
    cab={"lowcut":80,"highcut":10000}),
  preset(4, 2, "Numb Sustain", out_level=OUT,            # Pink Floyd — Gilmour Big Muff + Binson Echorec lead
    # Ram's-Head Muff (Ovis) Vol4/Tone6/Sus6 + Dyna Comp + Hiwatt clean platform + Binson (Echo Wreck).
    fz={"enable":1,"pedal":"Italian Hero","mode":"Ovis","sustain":0.6,"tone":0.6,"volume":0.45},
    cp={"enable":1,"type":0,"ratio":1,"thresh":-20,"makeup":1},
    amp={"model":"Hiwatt","gain":0.4,"bass":0.6,"mid":0.5,"treble":0.6,"presence":0.6,"master":0.6},
    gt={"enable":1,"thresh":-58,"attack":2,"hold":200,"release":400,"hyst":6},   # tame Muff hiss, keep sustain
    dl={"enable":1,"type":"Echo Wreck","time":430,"feedback":0.33,"mix":0.22,"heads":10,"wow":0.002,"flutter":0.001},
    rv={"enable":1,"decay":2.0,"mix":0.12},
    cab={"lowcut":80,"highcut":9500}),
  preset(4, 3, "Little Feather", out_level=OUT,          # Jimi Hendrix — Little Wing
    # Strat neck, warm clean with light breakup, rotary-speaker swirl (Little Wing was tracked
    # through a rotating cab).
    amp={"model":"Clean Meanie","gain":0.38,"bass":0.5,"mid":0.55,"treble":0.5,"presence":0.48,"master":0.55},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-22,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Rotary","rate":0.3,"depth":0.5,"mix":0.5,"width":0.7},
    rv={"enable":1,"predelay":15,"decay":2.2,"mix":0.18},
    cab={"lowcut":82,"highcut":10000}),
)

# ── Bank 6 — Prog / Djent (Periphery + Mastodon) ─────────────────────────────
# Modeled on the user's stock "Rhythm" (Gainzilla/EVH, mids 0.75, master 0.35,
# TS drive 0.02 / level 1.0) — the proven non-fizzy djent recipe in this DSP.
add(
  preset(5, 0, "Flatliner", out_level=OUT,               # Periphery — Flatline
    amp={"model":"Gainzilla","gain":0.55,"bass":0.42,"mid":0.72,"treble":0.6,"presence":0.45,"master":0.38,"channel":1,"resonance":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.02,"tone":0.55,"level":1.0,"mix":1.0},   # TS tighten
    gt={"enable":1,"thresh":-45,"attack":0.1,"hold":40,"release":120,"hyst":8},           # surgical djent gate
    cab={"lowcut":100,"highcut":8000}),
  preset(5, 1, "Prayer Djent", out_level=OUT,            # Periphery — Prayer Position
    amp={"model":"Gainzilla","gain":0.55,"bass":0.42,"mid":0.7,"treble":0.6,"presence":0.45,"master":0.38,"channel":1,"resonance":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.02,"tone":0.58,"level":1.0,"mix":1.0},
    gt={"enable":1,"thresh":-45,"attack":0.1,"hold":40,"release":110,"hyst":8},
    cab={"lowcut":95,"highcut":8200}),
  preset(5, 2, "Skye Crusher", out_level=OUT,            # Mastodon — Crack the Skye rhythm (JCM800 + OD808)
    amp={"model":"Crunchy","gain":0.45,"bass":0.42,"mid":0.7,"treble":0.62,"presence":0.45,"master":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.55,"level":0.9,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":50,"release":130,"hyst":6},
    md={"enable":1,"type":"Phaser","rate":0.2,"depth":0.45,"mix":0.3},   # Crack the Skye MXR Phase 90
    rv={"enable":1,"decay":1.6,"mix":0.1},
    cab={"lowcut":85,"highcut":8500}),
  preset(5, 3, "Skye Soar", out_level=OUT,               # Mastodon — Crack the Skye lead (JCM800 + boost + octave + delay)
    amp={"model":"Crunchy","gain":0.55,"bass":0.45,"mid":0.72,"treble":0.62,"presence":0.5,"master":0.46},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.55,"level":0.85,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":55,"release":150,"hyst":6},
    oc={"enable":1,"pos":2,"up":0.35,"down":0.0,"dry":1.0},   # "The Czar" POG octave-up, in front of the amp
    dl={"enable":1,"type":"Digital","time":420,"feedback":0.32,"mix":0.16},
    rv={"enable":1,"decay":1.7,"mix":0.1},
    cab={"lowcut":85,"highcut":8800}),
)

# ── Bank 7 — Fuzz & Doom ─────────────────────────────────────────────────────
add(
  preset(6, 0, "Mauve Haze", out_level=OUT,              # Jimi Hendrix — Purple Haze
    # Germanium Fuzz Face (I Know It / Tone Bender) into a lower-gain Marshall plexi.
    fz={"enable":1,"pedal":"I Know It","sustain":0.7,"volume":0.55,"bias":0.55,"inputtrim":0.45,"getemp":0.4},
    amp={"model":"Crunchy","gain":0.4,"bass":0.5,"mid":0.62,"treble":0.6,"presence":0.5,"master":0.5},
    gt={"enable":1,"thresh":-52,"attack":2,"hold":150,"release":300,"hyst":6},
    cab={"lowcut":80,"highcut":8800},
    rv={"enable":1,"decay":1.3,"mix":0.08}),
  preset(6, 1, "Holy Smoke", out_level=OUT,              # Sleep — Dragonaut / Holy Mountain
    # Les Paul -> Muff-style fuzz + comp-for-sustain -> Orange wall, downtuned, huge low end.
    fz={"enable":1,"pedal":"Italian Hero","mode":"Red Bear","sustain":0.7,"tone":0.45,"volume":0.55},
    cp={"enable":1,"type":0,"ratio":1,"thresh":-22,"makeup":2},
    amp={"model":"Tangerang","gain":0.45,"bass":0.6,"mid":0.55,"treble":0.45,"presence":0.4,"master":0.5,"sag":0.4},
    gt={"enable":1,"thresh":-56,"attack":3,"hold":200,"release":400,"hyst":6},   # gentle (preserve the wall)
    rv={"enable":1,"predelay":30,"decay":3.2,"damping":0.5,"mix":0.18},          # cavernous doom space
    cab={"lowcut":72,"highcut":7000}),
  preset(6, 2, "Wizard's Doom", out_level=OUT,           # Electric Wizard — Funeralopolis / Dopethrone
    # SG -> Green-Russian Muff (Red Bear) -> Sunn Model T (series), very downtuned, dark + cavernous.
    # Fuzz pulled back from sustain 0.8/vol 0.55 — it was ~12 dB too hot (excess saturation/noise).
    fz={"enable":1,"pedal":"Italian Hero","mode":"Red Bear","sustain":0.62,"tone":0.4,"volume":0.4},
    amp={"model":"Doom Daddy","gain":0.55,"bass":0.7,"mid":0.5,"treble":0.4,"presence":0.38,"master":0.5,"sag":0.45,
         "sunn_link":"Series","sunn_vol2":0.5},
    gt={"enable":1,"thresh":-56,"attack":3,"hold":200,"release":400,"hyst":6},
    cab={"lowcut":66,"highcut":6500},
    rv={"enable":1,"decay":2.2,"damping":0.4,"mix":0.12}),
  preset(6, 3, "Hazy Solo", out_level=OUT,               # Jimi Hendrix — Purple Haze SOLO
    # Octavia octave-up fuzz + a parked (cocked) wah into a plexi — the ring-modulated
    # octave-fuzz lead. (Mauve Haze, two slots up, is the germanium Fuzz Face riff tone.)
    wh={"enable":1,"pos":2,"type":"Fixed","freq":0.55,"q":0.65,"mix":0.6},   # cocked-wah honk
    fz={"enable":1,"pedal":"Octavia","sustain":0.7,"tone":0.5,"volume":0.6},
    amp={"model":"Crunchy","gain":0.45,"bass":0.5,"mid":0.62,"treble":0.62,"presence":0.55,"master":0.55},
    gt={"enable":1,"thresh":-52,"attack":2,"hold":150,"release":300,"hyst":6},
    rv={"enable":1,"decay":1.5,"mix":0.1},
    cab={"lowcut":80,"highcut":9000}),
)

# ── Loudness calibration ─────────────────────────────────────────────────────
# Measured on-device 2026-06-25 with build-tools/hexforge_meas.cpp (a worker-capable
# LV2 host; DI from build-tools/gen_di.py): each preset recalled, a sustained DI
# power chord run through it with the
# output auto-limiter OFF, output RMS captured in dBFS at out_level = -20. Because
# out_level is an exact linear post-gain, out_level_final = -20 + (target - measured)
# lands each preset on the target loudness in one pass. Targets replicate the stock
# set the user dialed by ear: clean ~ -7.3 dBFS RMS, dirty/dense ~ -13.5 dBFS RMS
# (the stock Crunch/Rhythm/Lead average). Re-run hf_meas and update this table if a
# preset's voicing/gain changes (which shifts its intrinsic level).
MEAS_RMS_AT_M20 = {
    8:-11.46, 9:-11.07, 10:-15.64, 11:-15.05, 12:-15.57, 13:-15.84, 14:-15.67,
    15:-3.99, 16:-4.66, 17:-5.52, 18:-0.17, 19:-9.55, 20:-13.56, 21:-13.44,
    22:-16.21, 23:-15.91, 24:-13.81, 25:-11.16, 26:5.80, 27:-14.35,
    # 15/16/17/19 Clean Meanie +3.7dB, 18 Numb/Hiwatt +11.5dB, 26 Wizard/Sunn +7.5dB (clean-amp makeup boost)
}
CLEAN_FLAT = {9, 15, 16, 17, 19}   # clean/edge presets target the stock Clean level
TARGET_CLEAN, TARGET_DIRTY = -3.5, -13.1   # cleans pushed hotter (denser via comp + clean-amp
                                           # makeup boost) so they don't feel quiet next to distortion
def _flat(p): return p["bank"] * G_SLOTS + p["slot"]
for _p in PRESETS:
    _fl = _flat(_p)
    if _fl in MEAS_RMS_AT_M20:
        _target = TARGET_CLEAN if _fl in CLEAN_FLAT else TARGET_DIRTY
        _out = -20.0 + (_target - MEAS_RMS_AT_M20[_fl])
        _p["vals"][SYM_IDX["out_level"]] = round(max(-40.0, min(6.0, _out)), 1)

# ─────────────────────────────────────────────────────────────────────────────
# Verification: decode the ORIGINAL four inline presets to prove whether they sit
# on the current (v7) port layout or an older one. Values are copied verbatim from
# hexforge_plugin.cpp's kFactoryVals. If any param port falls outside its declared
# [min,max], the row is mis-aligned for the current enum (a latent bug).
# ─────────────────────────────────────────────────────────────────────────────
EXISTING = {
 "Clean":  [0,0,0,0,0,0,0,-20.04,0,1,0,0,1,1,0,-60,5,50,100,6,2,0,0,-20,1,5,5,3,0,3,0,0,2,0.55,0.5,0.65,0.5,0.5,0.4,4,1,0,0.19,0.5,0.58,1,0.3,5,1,0,0.5775,0.615,0.635,0.605,0.5,0.7525,0.3,0,0,0.5,0,0,1,0.55,0.18,0.33,0.62,0.42,0.5,0,1,0.5,0.5,0.5,0,0,6,1,80,16000,1,7,0,0,0.5,0.5,0.5,0.5,8,1,2,250,0.21315,0.3,0.5,0.003,0.001,3,9,1,10,1.5,0.3,0,0.01,0.335,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
}

def _isParam(i):
    return i >= SYM_IDX["out_level"] and i < SYM_IDX["sw_a"] and i != SYM_IDX["clip"]

def _migrate(old, src_ver):
    # Mirror of migratePorts() in hexforge_plugin.cpp.
    vdef = [0.0, 1.0, 0.0, 0.0, 4.0]   # humbk,hbamt,hbmodel,boost,boostamt
    ddef = [1.0, 0.0, 0.0, 0.3]        # pattern,ducking,moddepth,modrate
    wodef = [10.0, 0.0, 0.0, 0.4, 0.7, 0.5, 0.6, 0.8, 11.0, 0.0, 0.0, 0.5, 1.0]  # Wah(8)+Octave(5)
    it_existing = 0 if src_ver < 4 else 2 if src_ver == 4 else 3 if src_ver == 5 else 5
    it_at = SYM_IDX["it_humbk"] + it_existing
    it_end = SYM_IDX["it_humbk"] + 5
    dl_gap = src_ver < 7
    dl_at = SYM_IDX["dl_pattern"]; dl_end = dl_at + 4
    wo_gap = src_ver < 8
    wo_at = SYM_IDX["wh_pos"]; wo_end = wo_at + 13
    out = [0.0] * N_PORTS; o = 0
    for i in range(N_PORTS):
        if it_at <= i < it_end:
            out[i] = vdef[i - SYM_IDX["it_humbk"]]
        elif dl_gap and dl_at <= i < dl_end:
            out[i] = ddef[i - dl_at]
        elif wo_gap and wo_at <= i < wo_end:
            out[i] = wodef[i - wo_at]
        else:
            out[i] = old[o] if o < len(old) else 0.0; o += 1
    return out

def _range_violations(vals):
    bad = []
    for i in range(N_PORTS):
        if not _isParam(i): continue
        c = CTRL[i - NFIXED]
        if not (c["mn"] - 1e-6 <= vals[i] <= c["mx"] + 1e-6):
            bad.append((c["sym"], vals[i], c["mn"], c["mx"]))
    return bad

def verify_existing():
    print("-- Verifying the original 4 inline factory presets --")
    for name, row in EXISTING.items():
        row = list(row) + [0.0] * (N_PORTS - len(row))
        as_is = _range_violations(row)
        best = None
        for ver in (3, 4, 5, 6, 7):
            viol = _range_violations(_migrate(row, ver))
            if not viol:
                best = ver; break
        print("  %-8s direct(v7): %d range violations; clean after migrate from v%s"
              % (name, len(as_is), best if best is not None else "?"))
        if as_is and best is not None and best < 7:
            print("    -> LATENT BUG: inline row is a v%d blob; psInitDefaults memcpy's it"
                  " WITHOUT migration, so a fresh install loads it shifted." % best)

# ── Emit the C++ header ──────────────────────────────────────────────────────
def _fmt(x):
    if x == int(x) and abs(x) < 1e15:
        return str(int(x))
    return "%g" % x

def emit_header():
    L = []
    L.append("// AUTO-GENERATED by build-tools/gen_hexforge_presets.py — do not edit by hand.")
    L.append("// Extra band/song factory presets, Banks 2..6 (Bank 1 stays the stock 4).")
    L.append("#pragma once")
    L.append("#include \"hexforge_ports.h\"")
    L.append("struct HfFactoryPreset { int bank; int slot; const char* name; float vals[HF_N_PORTS]; };")
    L.append("static const HfFactoryPreset kFactoryExtra[] = {")
    for p in PRESETS:
        vals = ", ".join(_fmt(x) for x in p["vals"])
        L.append('  { %d, %d, "%s", { %s } },' % (p["bank"], p["slot"], p["name"], vals))
    L.append("};")
    L.append("static const int kFactoryExtraCount = %d;" % len(PRESETS))
    L.append("")
    return "\n".join(L)

if __name__ == "__main__":
    assert N_PORTS == 148, "port count drift: got %d" % N_PORTS
    # guard against typos / duplicate slots
    seen = set()
    for p in PRESETS:
        key = (p["bank"], p["slot"])
        assert key not in seen, "duplicate bank/slot %s" % (key,)
        seen.add(key)
        v = _range_violations(p["vals"])
        assert not v, "%s has out-of-range params: %s" % (p["name"], v)
    verify_existing()
    out = os.path.join(REPO, "lv2", "hexforge", "hexforge_factory_presets.h")
    with open(out, "w", newline="\n", encoding="utf-8") as f:
        f.write(emit_header())
    print("wrote", os.path.relpath(out, REPO), "with", len(PRESETS), "presets")
