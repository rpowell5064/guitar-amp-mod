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

def preset(bank, slot, name, out_level=-20.0, cab_ir="", **blocks):
    """Build one factory preset. `blocks` maps a block prefix -> {param: value}.
    Param keys are the suffixes from gen_hexforge.py's tables (e.g. amp={'model':'Beardo BE'}).
    Enum params accept either the integer or the scalePoint label string. Use
    enable=1/0 inside a block to engage/bypass it. `cab_ir` picks a built-in cab
    sentinel (e.g. "@vox2x12"); empty = the Factory Cab (@factory / V30)."""
    if not (0 <= bank < G_BANKS and 0 <= slot < G_SLOTS):
        raise ValueError("bank/slot out of range: %d/%d" % (bank, slot))
    v = _base_vals()
    v[SYM_IDX["out_level"]] = float(out_level)
    for pfx, params in blocks.items():
        for key, val in params.items():
            sym = pfx + "_" + key
            v[SYM_IDX[sym]] = _resolve(sym, val)
    return dict(bank=bank, slot=slot, name=name, cab=cab_ir, vals=v)

G_BANKS, G_SLOTS = 32, 4

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

# Presets are grouped by BAND/ARTIST into banks (2026-06-29 user request); bank index
# N == UI "Bank N+1". 6 banks (3-8) for 8 artists, so Ghost spans 2 banks by album and
# the doom singletons (Sleep, Electric Wizard) ride in the Hendrix / Mastodon banks.
# IMPORTANT: the calibration tables below are keyed by NAME (not flat index) so this
# kind of rearrange never has to touch them again.

# ── Bank 2 (index 1) — NIRVANA · Nevermind (+ Regal Sustain packed into A, 2026-07-13, to leave no blank slots) ──
add(
  preset(1, 1, "Nevermind Verse", out_level=OUT,          # Smells Like Teen Spirit — clean verse
    # Clean amp + a subtle Small Clone chorus (Nevermind Chorus) — the jangly verse figure.
    # RESEARCHED (Butch Vig): clean Mesa/Vox platform + EHX Small Clone, depth switch UP. Mustang.
    amp={"model":"Clean Meanie","gain":0.25,"bass":0.55,"mid":0.65,"treble":0.6,"presence":0.5,"master":0.7,"sag":0.3},
    md={"enable":1,"type":"Nevermind Chorus","rate":0.42,"depth":1.0,"mix":0.68,"width":0.6},
    rv={"enable":1,"predelay":8,"decay":0.9,"damping":0.6,"mix":0.14},
    cab={"lowcut":85,"highcut":9500}),
  preset(1, 2, "Nevermind Wall", out_level=OUT,           # USER-PRESERVED 2026-07-07 (reworked on-device)
    # User's on-device rework — Plexiglass amp + a backed-off Grunge DS (DS-1) into it. Baked verbatim.
    # NOTE: when this was dialed, "Plexiglass" was mis-loading as Backline Plus (worker model-clamp bug,
    # fixed 2026-07-07) — so post-fix it plays the REAL Marshall Plexi; loudness may want a re-measure.
    dr={"enable":1,"model":"Grunge DS","drive":0.335,"tone":1.0,"level":0.64,"mix":0.6525},
    md={"enable":1,"type":"Nevermind Chorus","rate":0.3275,"depth":0.8725,"mix":0.22,"width":0.5},
    amp={"model":"Plexiglass","gain":0.7225,"bass":0.3425,"mid":0.70,"treble":0.6825,"presence":0.5,"master":0.8,"sag":0.305},
    gt={"enable":1,"thresh":-48,"attack":1.5,"hold":120,"release":250,"hyst":8},
    cab_ir="@greenback",
    cab={"lowcut":85,"highcut":8200}),
  preset(1, 3, "Come As Water", out_level=OUT,            # Come As You Are — clean, deep chorus
    # The underwater riff: clean amp, neck-pickup warmth, Small Clone (Nevermind Chorus) set DEEP.
    # RESEARCHED (Vig: "AC30 with a Small Clone"): clean Vox + deep Small Clone, watery arpeggio.
    amp={"model":"Chime Thirty","gain":0.3,"bass":0.4,"mid":0.55,"treble":0.6,"presence":0.5,"master":0.65,"sag":0.3},
    md={"enable":1,"type":"Nevermind Chorus","rate":0.38,"depth":1.0,"mix":0.62,"width":0.6},
    rv={"enable":1,"predelay":12,"decay":1.0,"damping":0.55,"mix":0.2},
    cab={"lowcut":80,"highcut":9000}),
)

# ── Bank 3 — GHOST · Opus Eponymous (Orange Thunderverb era) ─────────────────
add(
  preset(2, 0, "Candlelit Clean", out_level=OUT,         # Ghost — Opus clean
    # Orange clean + dark analog delay (Carbon Copy) + a little room.
    # RESEARCHED: Orange Thunderverb 50 clean (Ghoul quote: back off gain, lean on mids), Gibson SG,
    # Greenback 4x12; MXR Analog Chorus + Carbon Copy tape delay + Philosophers-Tone comp.
    amp={"model":"Tangerang","gain":0.2,"bass":0.4,"mid":0.65,"treble":0.55,"presence":0.45,"master":0.65,"sag":0.6},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-24,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Lush-2","rate":0.25,"depth":0.3,"mix":0.3,"width":0.5},
    dl={"enable":1,"type":"Tape","time":380,"feedback":0.25,"mix":0.16,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"predelay":15,"decay":1.6,"damping":0.6,"mix":0.2},
    cab_ir="@greenback",
    cab={"lowcut":85,"highcut":8500}),
  preset(2, 1, "Sermon Crunch", out_level=OUT,           # Ghost — Opus Eponymous rhythm
    # SG -> Orange Thunderverb 50: warm, mid-forward, gain backed off, '70s.
    # RESEARCHED: Thunderverb dirty low-gain + Pigtronix Fat Drive (TS-style) light boost; cranked-70s
    # mid-pushed Orange crunch, NOT high gain. Greenback cab.
    # revoiced 2026-07-11: TS was ATTENUATING (level 0.65 = -3 dB) -> weak push; raised to 0.78 (unity boost)
    # to restore the mid-forward Orange crunch; treble/high-cut eased a touch for the now-brighter front-end.
    amp={"model":"Tangerang","gain":0.42,"bass":0.38,"mid":0.72,"treble":0.5,"presence":0.46,"master":0.7,"sag":0.65},
    dr={"enable":1,"model":"Green Man","drive":0.3,"tone":0.55,"level":0.78,"mix":1.0},
    gt={"enable":1,"thresh":-52,"attack":3,"hold":120,"release":260,"hyst":8},
    cab_ir="@greenback",
    cab={"lowcut":90,"highcut":7900}),
  preset(2, 2, "Sermon Rhythm", out_level=OUT,           # Ghost — Opus rhythm, higher gain than Sermon Crunch
    # Same Orange Thunderverb voicing as Sermon Crunch, pushed: more gain + TS tighten + tighter gate.
    # RESEARCHED: same Orange rig pushed harder (live JCM900 also used for grind); mid-forward.
    # revoiced 2026-07-11: TS level 0.6->0.78 (was attenuating -> weak) to restore the pushed Orange grind;
    # treble/presence + high-cut eased for the brighter front-end.
    amp={"model":"Tangerang","gain":0.56,"bass":0.4,"mid":0.68,"treble":0.51,"presence":0.46,"master":0.72,"sag":0.6},
    dr={"enable":1,"model":"Green Man","drive":0.4,"tone":0.5,"level":0.78,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":130,"release":280,"hyst":8},
    cab_ir="@greenback",
    cab={"lowcut":90,"highcut":7900}),
  preset(2, 3, "Sermon Solo", out_level=OUT,             # Ghost — early (B.C.) album lead: reverb + Seraph delay
    # Singing, mid-forward Opus-era lead over the Orange platform; soaked in Seraph + room.
    # RESEARCHED: Ghost early lead — Orange + light TS boost, MXR chorus, Carbon Copy tape delay
    # (~420ms/3 repeats per Guitar Chalk Lachryma), plate reverb ~30%.
    # revoiced 2026-07-11: TS level 0.7->0.78 (restore push), treble/presence eased for the brighter front-end.
    amp={"model":"Tangerang","gain":0.52,"bass":0.38,"mid":0.65,"treble":0.55,"presence":0.5,"master":0.68,"sag":0.58},
    dr={"enable":1,"model":"Green Man","drive":0.35,"tone":0.6,"level":0.78,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":130,"release":280,"hyst":8},
    md={"enable":1,"type":"Lush-2","rate":0.2,"depth":0.25,"mix":0.25,"width":0.5},
    dl={"enable":1,"type":"Tape","time":420,"feedback":0.35,"mix":0.28,"width":0.3,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"predelay":20,"decay":1.8,"damping":0.5,"mix":0.3},
    cab_ir="@greenback",
    cab={"lowcut":88,"highcut":8800}),
)

# ── Bank 4 — GHOST · Impera & Skeleta (Friedman BE era) ──────────────────────
add(
  preset(3, 0, "Imperial Rhythm", out_level=OUT,         # Ghost — Impera rhythm (Friedman BE, Akesson's BE-100)
    # RESEARCHED (Akesson): Friedman BE-100 rhythm (BE channel) + TS/Sugar-Drive tightener, V30 4x12,
    # tight+punchy dry, layered.
    # revoiced 2026-07-11: Beardo BE was re-voiced brighter (+2 kHz bite) AND the input clip is gone -> harsh;
    # eased treble/presence + high-cut + a touch less gain. TS 0.6->0.74 (was attenuating) to keep the tight push.
    amp={"model":"Beardo BE","fr_channel":"BE","fr_fat":1,"gain":0.58,"bass":0.45,"mid":0.55,"treble":0.52,"presence":0.55,"master":0.68,"sag":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.25,"tone":0.65,"level":0.74,"mix":1.0},
    gt={"enable":1,"thresh":-45,"attack":1,"hold":120,"release":250,"hyst":8},
    cab={"lowcut":88,"highcut":7700}),
  preset(3, 1, "Imperial Lead", out_level=OUT,           # Ghost — Impera lead (Friedman HBE)
    # RESEARCHED (Akesson): Impera lead = Friedman HBE (Plexi for solos) + MXR Sugar Drive (Klon) +
    # MXR Phase 95 (frequent on leads) + melodic dual delay.
    # tamed 2026-07-11: was too gainy ("sounded like a fuzz") — dropped the Sat switch + amp gain 0.72->0.56
    # + the Klon drive 0.35->0.16 (now a light singing boost, not a stacked-gain wall).
    # 2026-07-11 revoice: Beardo re-voice + input-clip removal made the HBE bright/harsh; eased treble 0.65->0.58,
    # presence 0.68->0.6, high-cut 8800->8100 (the singing lead shouldn't be fizzy). Klon boost kept.
    amp={"model":"Beardo BE","fr_channel":"HBE","fr_sat":0,"gain":0.56,"bass":0.4,"mid":0.52,"treble":0.58,"presence":0.6,"master":0.65,"sag":0.4},
    dr={"enable":1,"model":"Gilded Horse","drive":0.16,"tone":0.6,"level":0.7,"mix":1.0},
    md={"enable":1,"type":"Lush-2","rate":0.22,"depth":0.32,"mix":0.20,"width":0.35},  # subtle CE-2 chorus — user pref over the researched Phase 95 (phaser read wrong on-rig)
    gt={"enable":1,"thresh":-45,"attack":1,"hold":130,"release":280,"hyst":8},
    dl={"enable":1,"type":"Seraph","time":320,"feedback":0.28,"mix":0.22,"width":0.45,"pattern":"Dotted 8th","ducking":0.2,"moddepth":0.12,"modrate":0.3},
    rv={"enable":1,"predelay":15,"decay":1.4,"damping":0.45,"mix":0.28},
    cab={"lowcut":88,"highcut":8100}),
  preset(3, 2, "Cardinal Rhythm", out_level=OUT,         # Ghost — Skeleta rhythm (Friedman HBE, tight modern)
    # RESEARCHED (Sound on Sound): Skeleta rhythm = tighter/more-modern than Impera; lower sag,
    # slightly scooped bass for clarity.
    # revoiced 2026-07-11: Beardo BE brighter + input clip gone -> harsh/fizzy; eased gain 0.65->0.6, treble
    # 0.62->0.55, presence 0.65->0.57, high-cut 8200->7600. TS 0.58->0.74 (was attenuating) for the tight push.
    amp={"model":"Beardo BE","fr_channel":"BE","fr_fat":1,"gain":0.6,"bass":0.42,"mid":0.5,"treble":0.55,"presence":0.57,"master":0.7,"sag":0.35},
    dr={"enable":1,"model":"Green Man","drive":0.2,"tone":0.7,"level":0.74,"mix":1.0},
    gt={"enable":1,"thresh":-45,"attack":0.5,"hold":120,"release":260,"hyst":8},
    cab={"lowcut":90,"highcut":7600}),
  preset(3, 3, "Cardinal Lead", out_level=OUT,           # Ghost — Skeleta lead (Friedman HBE + sat)
    # DOD Overdrive Preamp 250 (Preamp 250 — op-amp hard-clip) as a solo boost into the HBE (per Fredrik
    # Åkesson, Skeleta solos). VALUES = the user's own on-device dial-in (2026-07-05, read back from the
    # preset store): amp gain pulled WAY back (0.24) + mids forward (0.73) + DOD drive low (0.178) so it's
    # a clean mid-boost, not a stacked-gain wall. DO NOT re-tune without the user — this is their setting.
    amp={"model":"Beardo BE","fr_channel":"HBE","fr_sat":1,"gain":0.24,"bass":0.25,"mid":0.73,"treble":0.6975,"presence":0.7,"master":0.65,"sag":0.32},
    dr={"enable":1,"model":"Preamp 250","drive":0.1775,"tone":0.6225,"level":0.55,"mix":1.0},
    md={"enable":1,"type":"Lush-2","rate":0.20,"depth":0.30,"mix":0.18,"width":0.30},  # subtle CE-2 chorus — user pref over the researched MXR phaser (phaser read wrong on-rig)
    gt={"enable":1,"thresh":-45,"attack":0.5,"hold":120,"release":260,"hyst":8},
    dl={"enable":1,"type":"Tape","time":350,"feedback":0.3,"mix":0.25,"width":0.35,"wow":0.002,"flutter":0.001},
    rv={"enable":1,"predelay":18,"decay":1.5,"damping":0.42,"mix":0.3},
    cab={"lowcut":90,"highcut":8700}),
)

# ── Bank 5 — PINK FLOYD ──────────────────────────────────────────────────────
add(
  preset(4, 0, "Dark Side Air", out_level=OUT,           # Pink Floyd — Breathe
    # Strat warm clean, compressed, spacious, a hint of vibe.
    # RESEARCHED (Gilmourish): Hiwatt DR103 dead clean into WEM/Fane (@hiwatt), NO comp (Gilmour had
    # none until Animals 1977). Uni-Vibe in chorus mode + Binson (Echo Wreck) ~300ms; plate at mix.
    amp={"model":"Hi-Volt","gain":0.15,"bass":0.55,"mid":0.45,"treble":0.45,"presence":0.55,"master":0.75,"sag":0.3},
    md={"enable":1,"pos":4,"type":"Uni-Verse","rate":0.15,"depth":0.55,"mix":0.5,"width":0.5},   # Gilmour ran the Vibe pre-amp (pos 4 < amp 5); chorus-mode blend
    dl={"enable":1,"type":"Echo Wreck","time":300,"feedback":0.35,"mix":0.25,"width":0.3,"heads":10,"wow":0.002,"flutter":0.001},
    rv={"enable":1,"predelay":20,"decay":1.6,"damping":0.4,"mix":0.2},
    cab_ir="@hiwatt",
    cab={"lowcut":80,"highcut":10000}),
  preset(4, 1, "Berlin Wall Pulse", out_level=OUT,       # Pink Floyd — Run Like Hell
    # Strat, palm-muted, huge dotted delay soaked in chorus.
    # RESEARCHED (Gilmourish/kitrae): Hiwatt bright clean + mild boost, EHX Electric Mistress FLANGER
    # (not chorus), dotted-8th 380ms (117 BPM) dual delay via Seraph, subtle Dynacomp.
    amp={"model":"Hi-Volt","gain":0.2,"bass":0.5,"mid":0.45,"treble":0.6,"presence":0.6,"master":0.7,"sag":0.25},
    dr={"enable":1,"model":"Green Man","drive":0.15,"tone":0.6,"level":0.55,"mix":0.4},
    cp={"enable":1,"type":0,"ratio":1,"thresh":-20,"makeup":1},
    md={"enable":1,"type":"Flanger","rate":0.12,"depth":0.4,"mix":0.4,"width":0.4},
    dl={"enable":1,"type":"Seraph","time":380,"feedback":0.5,"mix":0.4,"width":0.4,"pattern":"Dotted 8th","ducking":0.1,"moddepth":0.05,"modrate":0.2},
    rv={"enable":1,"decay":1.4,"mix":0.1},
    cab_ir="@hiwatt",
    cab={"lowcut":85,"highcut":11000}),
  preset(4, 2, "Numb Sustain", out_level=OUT,            # Pink Floyd — Gilmour Big Muff + Binson Echorec lead
    # Ram's-Head Muff (Ovis) Vol4/Tone6/Sus6 + Dyna Comp + Hiwatt clean platform + Binson (Echo Wreck).
    # RESEARCHED (Gilmourish/kitrae, 1980 photos): Rams-Head Muff (Ovis) Sus~0.75/Tone~0.38 dark/Vol~0.6
    # into a CRANKED Hiwatt (master 0.9 — the violin sustain is amp-compression, not maxed fuzz). Comp
    # BYPASSED for the solo. MXR Digital Delay 450ms (studio). Restrained treble/mid.
    fz={"enable":1,"pedal":"Italian Hero","mode":"Ovis","sustain":0.75,"tone":0.38,"volume":0.6},
    amp={"model":"Hi-Volt","gain":0.4,"bass":0.55,"mid":0.45,"treble":0.4,"presence":0.55,"master":0.9,"sag":0.4},
    gt={"enable":1,"thresh":-56,"attack":2,"hold":200,"release":400,"hyst":8},
    dl={"enable":1,"type":"Echo Wreck","time":450,"feedback":0.45,"mix":0.25,"heads":10,"wow":0.002,"flutter":0.001},
    rv={"enable":1,"predelay":25,"decay":1.8,"damping":0.4,"mix":0.22},
    cab={"lowcut":80,"highcut":9500}),
  preset(4, 3, "Gravity Lead", out_level=-13.0,          # John Mayer — Klon-into-Dumble singing lead
    # Strat neck -> Gilded Horse (Klon) as OD/boost -> warm edge-of-breakup Fender/Two-Rock clean,
    # a touch of compression for the D-style smoothness, subtle tape + room. Gravity / Slow Dancing.
    # RESEARCHED: Klon (Gilded Horse) as a LOW-gain always-on boost (gain~11:00) into a clean Two-Rock/
    # Dumble-family platform; signature = Victoria Reverberato HARMONIC TREMOLO swirl + short analog
    # slapback (Aqua Puss ~110ms, 1 repeat) + present spring reverb. Position-4 Strat.
    amp={"model":"Clean Meanie","gain":0.3,"bass":0.5,"mid":0.6,"treble":0.55,"presence":0.5,"master":0.8,"sag":0.35},
    dr={"enable":1,"model":"Gilded Horse","drive":0.25,"tone":0.6,"level":0.55,"mix":1.0},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-22,"attack":3,"release":5,"knee":3,"makeup":3},
    md={"enable":1,"type":"Tremolo","rate":0.25,"depth":0.5,"mix":0.6,"width":0.4},
    gt={"enable":1,"thresh":-58,"attack":2,"hold":150,"release":300,"hyst":8},
    dl={"enable":1,"type":"Tape","time":110,"feedback":0.15,"mix":0.2,"wow":0.002,"flutter":0.001},
    rv={"enable":1,"predelay":15,"decay":1.4,"damping":0.5,"mix":0.25},
    cab={"lowcut":80,"highcut":9500}),
)

# ── Bank 6 — JIMI HENDRIX (+ Sleep, doom singleton) ──────────────────────────
add(
  preset(5, 0, "Mauve Haze", out_level=OUT,              # Jimi Hendrix — Purple Haze
    # Germanium Fuzz Face (I Know It) into a Marshall Super Lead PLEXI (Plexiglass — the actual
    # Hendrix amp; JCM800 didn't exist until '81). Amp master up for power-amp grind, treble
    # eased (plexi runs bright), gain moderate — the Fuzz Face is the dirt.
    fz={"enable":1,"pedal":"I Know It","sustain":0.7,"volume":0.55,"bias":0.55,"inputtrim":0.45,"getemp":0.4},
    amp={"model":"Plexiglass","gain":0.42,"bass":0.5,"mid":0.6,"treble":0.5,"presence":0.5,"master":0.6,"sag":0.35},
    gt={"enable":1,"thresh":-52,"attack":2,"hold":150,"release":300,"hyst":8},
    cab={"lowcut":80,"highcut":8800},
    rv={"enable":1,"decay":1.3,"mix":0.08}),
  preset(5, 1, "Hazy Solo", out_level=OUT,               # Jimi Hendrix — Purple Haze SOLO
    # Octavia octave-up fuzz + a parked (cocked) wah into a plexi — the ring-modulated
    # octave-fuzz lead. (Mauve Haze, two slots up, is the germanium Fuzz Face riff tone.)
    # un-starved 2026-07-11: the gate (in FRONT of the fuzz) was choking the Octavia's sustain, and the amp
    # was too clean behind it. Opened the gate (thresh -52->-64, longer tail), +Octavia sustain 0.62->0.76,
    # +amp gain 0.35->0.46 so the plexi adds body/sustain under the octave-fuzz.
    wh={"enable":1,"pos":2,"type":"Fixed","freq":0.55,"q":0.55,"mix":0.42},  # cocked-wah honk (tamed)
    fz={"enable":1,"pedal":"Octavia","sustain":0.76,"tone":0.5,"volume":0.46},  # Octavia is near-0dBFS; keep it near guitar level
    amp={"model":"Plexiglass","gain":0.46,"bass":0.5,"mid":0.6,"treble":0.5,"presence":0.52,"master":0.6,"sag":0.35},  # real Plexi adds sustain under the Octavia
    gt={"enable":1,"thresh":-64,"attack":2,"hold":200,"release":420,"hyst":8},
    dl={"enable":1,"type":"Seraph","time":400,"feedback":0.3,"mix":0.15,"width":0.6,"pattern":"Dotted 8th","ducking":0.2,"moddepth":0.15,"modrate":0.3},
    rv={"enable":1,"decay":1.5,"mix":0.1},
    cab={"lowcut":80,"highcut":9000}),
  preset(5, 2, "Little Feather", out_level=OUT,          # Jimi Hendrix — Little Wing
    # Strat neck, warm edge-of-breakup, rotary-speaker swirl (Little Wing was tracked through a
    # rotating cab). Now on the Marshall PLEXI (Hendrix's amp) at low gain/master for a warm,
    # midrange-forward clean rather than the previous Fender voicing.
    amp={"model":"Plexiglass","gain":0.32,"bass":0.55,"mid":0.55,"treble":0.48,"presence":0.45,"master":0.52,"sag":0.4},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-22,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Rotary","rate":0.3,"depth":0.5,"mix":0.5,"width":0.7},
    rv={"enable":1,"predelay":15,"decay":2.2,"mix":0.18},
    cab={"lowcut":82,"highcut":10000}),
  preset(5, 3, "Holy Smoke", out_level=OUT,              # Sleep — Dragonaut / Holy Mountain
    # Les Paul -> Muff-style fuzz + comp-for-sustain -> Orange wall, downtuned, huge low end.
    fz={"enable":1,"pedal":"Italian Hero","mode":"Red Bear","sustain":0.7,"tone":0.45,"volume":0.55},
    cp={"enable":1,"type":0,"ratio":1,"thresh":-22,"makeup":2},
    amp={"model":"Tangerang","gain":0.45,"bass":0.6,"mid":0.55,"treble":0.45,"presence":0.4,"master":0.5,"sag":0.4},
    gt={"enable":1,"thresh":-56,"attack":3,"hold":200,"release":400,"hyst":8},   # gentle (preserve the wall)
    rv={"enable":1,"predelay":30,"decay":3.2,"damping":0.5,"mix":0.18},          # cavernous doom space
    cab={"lowcut":72,"highcut":7000}),
)

# ── Bank 7 — MASTODON · Crack the Skye (+ Electric Wizard, doom singleton) ────
add(
  preset(6, 0, "Skye Crusher", out_level=OUT,            # Mastodon — Crack the Skye rhythm (JCM800 + OD808)
    # RESEARCHED (Kelliher): JCM800 2203 — "bass almost all the way up, mids in the middle, treble 3-4
    # o'clock, DO NOT scoop mids" + TS9/OD808 boost (drive~0, level high) into a Greenback 1960B.
    # revoiced 2026-07-11 (post input-ceiling removal + TS808 re-voice): TS level 0.9->0.74 (near-unity boost;
    # the amp no longer needs the extra push now the input clip is gone) + treble/presence + cab high-cut
    # eased to tame the restored top end (was fizzy/too-gainy).
    amp={"model":"Crunchy","gain":0.45,"bass":0.78,"mid":0.55,"treble":0.66,"presence":0.44,"master":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.55,"level":0.74,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":120,"release":260,"hyst":8},
    md={"enable":1,"type":"Phaser","rate":0.2,"depth":0.45,"mix":0.2},   # Phase 90 subtle, behind the mix
    rv={"enable":1,"decay":1.6,"mix":0.1},
    cab_ir="@greenback",
    cab={"lowcut":85,"highcut":7800}),
  preset(6, 1, "Skye (No Mod)", out_level=OUT,           # Mastodon — Crack the Skye rhythm, phaser removed
    # = Skye Crusher (JCM800 + OD808) with the MXR Phase 90 dropped. Revoiced 2026-07-11 as Skye Crusher.
    amp={"model":"Crunchy","gain":0.45,"bass":0.78,"mid":0.55,"treble":0.66,"presence":0.44,"master":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.55,"level":0.74,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":120,"release":260,"hyst":8},
    rv={"enable":1,"decay":1.6,"mix":0.1},
    cab_ir="@greenback",
    cab={"lowcut":85,"highcut":7800}),
  preset(6, 2, "Skye Soar", out_level=OUT,               # Mastodon — Crack the Skye lead (JCM800 + boost + octave + delay)
    # RESEARCHED (Kelliher EQ + Hinds POG2 on "The Czar"): JCM800 bass-up/mids-mid/treble-up + TS boost,
    # octave-up shimmer in front, RE-20/DD-6 dual delay. Greenback 1960B.
    # revoiced 2026-07-11: TS level 0.85->0.74, treble/presence + high-cut eased (restored top end).
    amp={"model":"Crunchy","gain":0.55,"bass":0.75,"mid":0.55,"treble":0.66,"presence":0.48,"master":0.46},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.55,"level":0.74,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":120,"release":260,"hyst":8},
    oc={"enable":1,"pos":2,"up":0.35,"down":0.0,"dry":1.0},   # "The Czar" POG octave-up, in front of the amp
    dl={"enable":1,"type":"Seraph","time":420,"feedback":0.32,"mix":0.16,"width":0.6,"pattern":"Dotted 8th","ducking":0.25,"moddepth":0.15,"modrate":0.3},
    rv={"enable":1,"decay":1.7,"mix":0.1},
    cab_ir="@greenback",
    cab={"lowcut":85,"highcut":8000}),
  preset(6, 3, "Wizard's Doom", out_level=OUT,           # Electric Wizard — Funeralopolis / Dopethrone
    # SG -> Green-Russian Muff (Red Bear) -> Sunn Model T (series), very downtuned, dark + cavernous.
    # Fuzz pulled back from sustain 0.8/vol 0.55 — it was ~12 dB too hot (excess saturation/noise).
    # RESEARCHED: Dopethrone = Sound City 120 (EL34; Doom Daddy is the closest) + Boss FZ-2 "Fuzz II"
    # which sits in GREEN RUSSIAN territory (Red Bear is the right target). Epiphone SG, B standard. @doom.
    fz={"enable":1,"pedal":"Italian Hero","mode":"Red Bear","sustain":0.62,"tone":0.38,"volume":0.4},
    amp={"model":"Doom Daddy","gain":0.55,"bass":0.72,"mid":0.5,"treble":0.4,"presence":0.38,"master":0.5,"sag":0.45,
         "sunn_link":"Series","sunn_vol2":0.5},
    gt={"enable":1,"thresh":-56,"attack":3,"hold":200,"release":400,"hyst":8},
    cab_ir="@doom",
    cab={"lowcut":66,"highcut":6500},
    rv={"enable":1,"decay":2.2,"damping":0.4,"mix":0.12}),
)

# ── Bank 8 — A PERFECT CIRCLE + PERIPHERY (modern prog/alt) ──────────────────
# Periphery modeled on the user's stock "Rhythm" (Gainzilla/EVH, mids 0.75, master
# 0.35, TS drive 0.02 / level 1.0) — the proven non-fizzy djent recipe in this DSP.
add(
  preset(7, 0, "Vanishing Drive", out_level=OUT,         # A Perfect Circle — Mer de Noms distorted
    # Les Paul -> Friedman-modded Marshall: needs BITE -> HBE + C45 bright cap + TS tighten.
    # RESEARCHED: Howerdels amp = a Dave-FRIEDMAN-modded 1978 Marshall JMP (NOT Mesa) into Greenback
    # 4x12; mid-FORWARD (not djent-scooped), C# standard, layered. Subtle ambience, mostly dry.
    amp={"model":"Beardo BE","fr_channel":"BE","fr_fat":1,"gain":0.62,"bass":0.55,"mid":0.6,"treble":0.55,"presence":0.5,"master":0.7,"sag":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.15,"tone":0.6,"level":0.62,"mix":1.0},
    gt={"enable":1,"thresh":-45,"attack":1,"hold":130,"release":280,"hyst":8},
    md={"enable":1,"type":"Lush-2","rate":0.3,"depth":0.4,"mix":0.22,"width":0.5},
    dl={"enable":1,"type":"Digital","time":380,"feedback":0.25,"mix":0.12},
    rv={"enable":1,"predelay":20,"decay":1.2,"damping":0.55,"mix":0.12},
    cab_ir="@greenback",
    cab={"lowcut":86,"highcut":8800}),
  preset(7, 1, "Dreamlit Shimmer", out_level=OUT,        # A Perfect Circle — dreamy chorusy clean
    # Howerdel the "effects guy": lush chorus + ambient delay + big reverb on a clean amp.
    # RESEARCHED: Howerdel is "an effects guy" — clean Gibson GA-15RV/Vox, warm (presence rolled off),
    # wide CE-1/Lexicon chorus, tape/DL4 delay, huge Strymon/Lexicon hall (The Hollow tails).
    amp={"model":"Clean Meanie","gain":0.2,"bass":0.5,"mid":0.45,"treble":0.55,"presence":0.4,"master":0.75,"sag":0.55},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-24,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Lush-2","rate":0.22,"depth":0.55,"mix":0.5,"width":0.9},
    dl={"enable":1,"type":"Tape","time":480,"feedback":0.38,"mix":0.28,"width":0.8,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"predelay":35,"decay":3.2,"damping":0.4,"mix":0.38},
    cab={"lowcut":80,"highcut":12000}),
  preset(7, 2, "Flatliner", out_level=OUT,               # Periphery — Flatline
    # RESEARCHED (Misha Mansoors own Fractal Tone Tour): PVH 6160 Block = 5150; Drive 6.5/Bass 5/Mid 5/
    # Treble 6/Pres 5/Master 4, minimal sag. TS808 as a CLEAN boost (drive 0, level max). Drop Ab 7-str.
    # revoiced 2026-07-11: TS was a UNITY-level boost (1.0) slamming an untamed front-end (input-clip removed)
    # -> too gainy/fizzy. TS level 1.0->0.72 (still a clean tightening boost, not a gain wall) + treble/presence
    # + high-cut eased. Amp gain kept (the 5150 grind IS the tone; the fix was the boost + brightness).
    amp={"model":"Gainzilla","gain":0.62,"bass":0.5,"mid":0.5,"treble":0.54,"presence":0.46,"master":0.4,"channel":1,"resonance":0.5,"sag":0.2},
    dr={"enable":1,"model":"Green Man","drive":0.0,"tone":0.5,"level":0.72,"mix":1.0},
    gt={"enable":1,"thresh":-42,"attack":0.5,"hold":90,"release":200,"hyst":8},
    cab={"lowcut":85,"highcut":7000}),
  preset(7, 3, "Prayer Djent", out_level=OUT,            # Periphery — Prayer Position
    # RESEARCHED: same PVH 6160 core as Flatliner, slightly more bass/mid for chug weight, EVEN
    # tighter (3 gates live), G# standard 7-str.
    # revoiced 2026-07-11: as Flatliner — TS level 1.0->0.72, gain eased, treble/presence + high-cut tamed.
    amp={"model":"Gainzilla","gain":0.62,"bass":0.55,"mid":0.52,"treble":0.52,"presence":0.44,"master":0.42,"channel":1,"resonance":0.5,"sag":0.18},
    dr={"enable":1,"model":"Green Man","drive":0.0,"tone":0.48,"level":0.72,"mix":1.0},
    gt={"enable":1,"thresh":-40,"attack":0.5,"hold":80,"release":180,"hyst":8},
    cab={"lowcut":90,"highcut":7200}),
)

# ── Bank 9 (index 8) — VIBE & WAVE (Trower / Police / Cure / Chic) ────────────
# Fun, distinctive tones that lean on the suite's oddball gear (Uni-Vibe, chorus,
# flanger, comp) — deliberately NOT the overused modeler defaults.
add(
  preset(8, 0, "Bridge Vibe", out_level=-9.0,            # Robin Trower — Bridge of Sighs
    # RESEARCHED: cranked Marshall JMP-100 (NOT Hiwatt) + Greenback 4x12, Dan Armstrong treble
    # boost driving the input, SLOW liquid Uni-Vibe. Neve studio comp = the thick sustain.
    dr={"enable":1,"model":"Gilded Horse","drive":0.22,"tone":0.6,"level":0.7,"mix":1.0},
    amp={"model":"Crunchy","gain":0.6,"bass":0.45,"mid":0.55,"treble":0.6,"presence":0.5,"master":0.8,"sag":0.7},
    cp={"enable":1,"type":0,"ratio":1,"thresh":-20,"makeup":1},
    md={"enable":1,"pos":3,"type":"Uni-Verse","rate":0.28,"depth":0.72,"mix":0.62,"width":0.5},   # Trower ran the Vibe FIRST — pre-boost, pre-amp (pos 3 < drive 4 < amp 5); chorus-blend mix
    rv={"enable":1,"predelay":15,"decay":1.8,"damping":0.6,"mix":0.22},
    cab={"lowcut":80,"highcut":9000}),
  preset(8, 1, "Bottle Jangle", out_level=OUT,           # The Police / Andy Summers — Message in a Bottle
    # RESEARCHED: Tele w/PAF neck -> clean Fender Twin, MXR Dyna Comp always on, EHX Electric
    # Mistress "more chorus than flange" (Flanger, slow/static), Echoplex slapback. Arpeggiated add9.
    amp={"model":"Clean Meanie","gain":0.3,"bass":0.4,"mid":0.45,"treble":0.65,"presence":0.55,"master":0.6},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-22,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Flanger","rate":0.2,"depth":0.35,"mix":0.7,"width":0.5},
    dl={"enable":1,"type":"Tape","time":120,"feedback":0.18,"mix":0.2,"width":0.6,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"predelay":8,"decay":1.4,"damping":0.45,"mix":0.2},
    cab={"lowcut":85,"highcut":11000}),
  preset(8, 2, "Forest Wash", out_level=OUT,             # The Cure — A Forest
    # RESEARCHED (Mike Hedges): Roland JC-120 dead clean (no sag) + Jazzmaster, SLOW deep flanger
    # (7 env-flangers, approximated static), tape-machine delay building across the song, cavernous verb.
    amp={"model":"Clean Meanie","gain":0.2,"bass":0.5,"mid":0.6,"treble":0.5,"presence":0.4,"master":0.55,"sag":0.1},
    md={"enable":1,"type":"Flanger","rate":0.08,"depth":0.65,"mix":0.55,"width":0.9},
    dl={"enable":1,"type":"Tape","time":480,"feedback":0.45,"mix":0.32,"width":0.7,"wow":0.004,"flutter":0.002},
    rv={"enable":1,"predelay":20,"decay":3.2,"damping":0.55,"mix":0.32},
    cab={"lowcut":90,"highcut":9500}),
  preset(8, 3, "Disco Chuck", out_level=OUT,             # Nile Rodgers / Chic — funk chucking
    # RESEARCHED (Le Freak, Bob Clearmountain): Strat NECK single-coil + DI/Deluxe blend, console
    # EQ = HPF 160Hz + 12k air + 4.8k mid. Bone-dry, BRIGHT, hard comp glue, gate for tight chucks.
    amp={"model":"Clean Meanie","gain":0.25,"bass":0.35,"mid":0.6,"treble":0.75,"presence":0.65,"master":0.55},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-26,"attack":1,"release":4,"knee":2,"makeup":5},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":60,"release":120,"hyst":8},
    cab={"lowcut":110,"highcut":12000}),
)

# ── Bank 10 (index 9) — TWANG & FUZZ (Dick Dale / Shadows / QOTSA / Ronson) ───
add(
  preset(9, 0, "Surf Splash", out_level=OUT,             # Dick Dale — Misirlou
    # RESEARCHED: Misirlou has NO tremolo (that is fast tremolo-PICKING) — the hero is a Fender 6G15
    # outboard SPRING reverb: bright, undamped, very wet. Fender Showman, bridge SC, staccato (gate).
    amp={"model":"Clean Meanie","gain":0.15,"bass":0.45,"mid":0.5,"treble":0.8,"presence":0.75,"master":0.85},
    gt={"enable":1,"thresh":-45,"attack":0.5,"hold":40,"release":90,"hyst":8},
    rv={"enable":1,"predelay":5,"decay":2.6,"damping":0.15,"mix":0.6},
    cab={"lowcut":80,"highcut":10500}),
  preset(9, 1, "Apache Echo", out_level=OUT,             # The Shadows / Hank Marvin — Apache
    # RESEARCHED: Vox AC15 (Chime Thirty) + Meazzi/Binson multi-head echo ~130ms (3 taps swelling).
    # NO tremolo — Marvins "vibrato" is the Strat vibrato ARM. Clean bell-like twang, alnico cab.
    amp={"model":"Chime Thirty","gain":0.2,"bass":0.4,"mid":0.55,"treble":0.7,"presence":0.6,"master":0.65,"sag":0.55},
    dl={"enable":1,"type":"Echo Wreck","time":130,"feedback":0.35,"mix":0.4,"width":0.0,"heads":10,"wow":0.002,"flutter":0.001},
    rv={"enable":1,"predelay":8,"decay":1.4,"damping":0.55,"mix":0.16},
    cab={"lowcut":82,"highcut":9500}),
  preset(9, 2, "Desert Robot", out_level=-15.5,          # QOTSA — No One Knows
    # RESEARCHED (Eric Valentine): Hommes "No One Knows" dirt was a tiny SOLID-STATE Peavey Decade
    # driven into saturation — NOT a Big Muff. Backline Plus (Peavey Backstage+) pushed harder now +
    # a BOSS SD-1 (Super Nova) overdrive out front for the extra gain/grit the tone was missing (user
    # request). SD-1 drive up, amp gain up = mid-forward, tight, saturated desert grind.
    dr={"enable":1,"model":"Super Nova","drive":0.65,"tone":0.5,"level":0.72,"mix":1.0},
    amp={"model":"Backline Plus","gain":0.62,"bass":0.58,"mid":0.82,"treble":0.68,"presence":0.5,"master":0.7},
    gt={"enable":1,"thresh":-52,"attack":1.5,"hold":100,"release":220,"hyst":8},
    rv={"enable":1,"decay":0.9,"damping":0.7,"mix":0.07},
    cab={"lowcut":82,"highcut":8500}),
  preset(9, 3, "Moondust Glam", out_level=-15.5,         # Bowie / Mick Ronson — Moonage Daydream
    # RESEARCHED: Sola Sound Tone Bender MkI germanium -> COCKED Cry Baby -> Marshall Major (KT88)
    # power-amp sat. FIXED (was cutting out + over-distorted): the "cranked Major" = CLEAN preamp +
    # power-amp warmth, so amp gain 0.82->0.38 (fuzz IS the dirt, not a stacked hi-gain preamp).
    # Tamed fuzz (sustain/volume down) + RAISED germanium bias 0.55->0.7 & lowered inputtrim
    # 0.65->0.45 to stop the starve-gate sputter; gate relaxed -50->-58 so it can't clip the sustain.
    # amp = HI-VOLT (Hiwatt) not the JCM800 — user: "I dont have a cleanish Marshall." The Hiwatt is a
    # true high-headroom CLEAN platform (a closer match to Ronson's 200W Marshall MAJOR than a crunch
    # JCM800), so amp+cab alone stays clean + the germanium fuzz is the only dirt. Being high-headroom it
    # is quiet → master pushed HIGH (0.9, clean volume, no power-amp distortion) + low sag (no ducking).
    # Keep the Greenback (Ronson's Marshall 4x12) cab, not the auto @hiwatt.
    # USER DIAL-IN (2026-07-03, tuned by ear to a Gibson Les Paul Custom, captured from the device .dat):
    # Hiwatt gain pushed to 0.81 (high-headroom → stays clean+loud where the JCM800 would crunch),
    # LP-voiced EQ (bass back, mids/treble up), fuzz input-trim up, a touch more reverb.
    wh={"enable":1,"pos":2,"type":"Fixed","freq":0.72,"q":0.58,"mix":0.85},
    fz={"enable":1,"pedal":"I Know It","sustain":0.59,"tone":0.51,"volume":0.38,"bias":0.7,"inputtrim":0.61,"getemp":0.45},
    amp={"model":"Hi-Volt","gain":0.81,"bass":0.41,"mid":0.72,"treble":0.67,"presence":0.59,"master":0.87,"sag":0.3},
    gt={"enable":1,"thresh":-62,"attack":2,"hold":160,"release":320,"hyst":8},
    rv={"enable":1,"decay":1.5,"mix":0.19,"damping":0.3,"predelay":10},
    cab_ir="@greenback",
    cab={"lowcut":80,"highcut":8800}),
)

# ── Bank 11 (index 10) — PSYCH & CHIME (Tame Impala / MBV / U2 / Queen) ───────
# The two Vox (Chime Thirty) showcases live here.
add(
  preset(10, 0, "Innerspeaker Swirl", out_level=-15.5,   # Tame Impala — psych fuzz + phaser
    # RESEARCHED: Fuzz Face-style germanium (Muff Ovis, sustain back) into a MID-FORWARD Vox AC30
    # (Chime Thirty), slow Small Stone Phaser underneath, baked-in reverb. Thick, not swirly. Strat.
    fz={"enable":1,"pedal":"Italian Hero","mode":"Ovis","sustain":0.72,"tone":0.45,"volume":0.42},
    amp={"model":"Chime Thirty","gain":0.6,"bass":0.45,"mid":0.7,"treble":0.55,"presence":0.5,"master":0.75,"sag":0.6},
    md={"enable":1,"type":"Phaser","rate":0.25,"depth":0.6,"mix":0.5},
    gt={"enable":1,"thresh":-52,"attack":2,"hold":150,"release":300,"hyst":8},
    rv={"enable":1,"predelay":15,"decay":1.6,"damping":0.4,"mix":0.28},
    cab={"lowcut":80,"highcut":9000}),
  preset(10, 1, "Glide Wall", out_level=-16.0,           # My Bloody Valentine — shoegaze glide
    # RESEARCHED (Kevin Shields/Loveless): raw open fuzz (Muff Delta, dark) into a CRANKED Vox AC30,
    # tremolo (dual-amp trem character), huge near-reverse reverb wall (0 predelay, long, very wet). Jazzmaster glide.
    fz={"enable":1,"pedal":"Italian Hero","mode":"Delta","sustain":0.82,"tone":0.4,"volume":0.5},
    amp={"model":"Chime Thirty","gain":0.72,"bass":0.5,"mid":0.55,"treble":0.5,"presence":0.4,"master":0.85,"sag":0.7},
    md={"enable":1,"type":"Tremolo","rate":0.52,"depth":0.7,"mix":0.9},
    gt={"enable":1,"thresh":-54,"attack":2,"hold":180,"release":350,"hyst":8},
    rv={"enable":1,"predelay":15,"decay":3.4,"damping":0.35,"mix":0.32},
    cab={"lowcut":82,"highcut":9000}),
  preset(10, 2, "Streets Chime", out_level=OUT,          # The Edge / U2 — dotted-eighth chime (Vox)
    # RESEARCHED: SDD-3000 was a DUAL delay (dotted-8th ~350ms + secondary tap, panned L/R) — the
    # Seraph dual-delay nails this. Pattern "Dotted 8th" @129BPM, 2-3 clean repeats, wide stereo,
    # light ducking so the attack stays clear + a touch of mod for shimmer. Vox AC30 edge-of-breakup.
    amp={"model":"Chime Thirty","gain":0.55,"bass":0.4,"mid":0.55,"treble":0.7,"presence":0.6,"master":0.8,"sag":0.5},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-22,"attack":3,"release":5,"knee":3,"makeup":3},
    dl={"enable":1,"type":"Seraph","time":350,"feedback":0.25,"mix":0.28,"width":0.9,"pattern":"Dotted 8th","ducking":0.15,"moddepth":0.1,"modrate":0.25},
    rv={"enable":1,"predelay":20,"decay":1.2,"damping":0.5,"mix":0.14},
    cab={"lowcut":85,"highcut":11000}),
)

# ── Bank 12 (index 11) — NINE INCH NAILS (the Nail industrial-distortion block; mode ↔ era) ──
# The Nail's five topologies were built for exactly this: Broke=Broken-EP digital crush,
# Dahnward=Downward-Spiral scoop, Delicate=Fragile Muff-wall, Con Molars=With-Teeth cab grind.
# Nail sits at slot 4 (in front of the amp); amps stay tight/moderate so the Nail is the voice.
add(
  preset(11, 0, "March Stabs", out_level=OUT,           # March of the Pigs — The Downward Spiral (1994)
    # The Downward Spiral's scooped, resonant, aggressive stab distortion. Nail "Dahnward" mode
    # (the Downward voicing: scooped hi-gain -> interstage LP -> resonant band-pass) in FRONT of a
    # tight Marshall; hard fast gate for the machine-stab, piano-bridge staccato rhythm.
    nail={"enable":1,"pos":4,"mode":"Dahnward","drive":0.8,"tone":0.46,"texture":0.62,"level":0.55},
    amp={"model":"Crunchy McCrunchFace","gain":0.35,"bass":0.5,"mid":0.5,"treble":0.6,"presence":0.6,"master":0.8,"sag":0.3},
    gt={"enable":1,"thresh":-52,"attack":1.2,"hold":90,"release":180,"hyst":8},
    cab_ir="@greenback",
    cab={"lowcut":95,"highcut":7800}),
  preset(11, 1, "World Went Away", out_level=OUT,       # The Day the World Went Away — The Fragile (1999)
    # The Fragile's crushing wall: fat, saturated, Muff-leaning distortion. Nail "Delicate"
    # (Swollen-Pickle-ish: fat lows, gentle scoop) into a thick amp + the big dark Doom cab with
    # atmospheric reverb. The loud album guitar (not the acoustic "quiet" intro).
    nail={"enable":1,"pos":4,"mode":"Delicate","drive":0.7,"tone":0.42,"texture":0.5,"level":0.5},
    amp={"model":"Crunchy McCrunchFace","gain":0.3,"bass":0.6,"mid":0.4,"treble":0.5,"presence":0.45,"master":0.7,"sag":0.4},
    rv={"enable":1,"predelay":14,"decay":2.2,"damping":0.45,"mix":0.22},
    gt={"enable":1,"thresh":-58,"attack":2,"hold":160,"release":320,"hyst":8},
    cab_ir="@doom",
    cab={"lowcut":80,"highcut":7000}),
  preset(11, 2, "Broken Crush", out_level=OUT,          # Broken EP — Wish / Happiness in Slavery (1992)
    # The angriest NIN: harsh, lo-fi DIGITAL distortion. Nail "Broke" mode (hard-clip -> sample-rate
    # decimation + bit-crush) into a clean/tight solid-state platform so the digital grit stays raw;
    # brutal fast gate for the machine-gun rhythm.
    nail={"enable":1,"pos":4,"mode":"Broke","drive":0.75,"tone":0.55,"texture":0.5,"level":0.32},
    amp={"model":"Backline Plus","gain":0.25,"bass":0.5,"mid":0.6,"treble":0.6,"presence":0.55,"master":0.55,"sag":0.3},
    gt={"enable":1,"thresh":-50,"attack":1.0,"hold":70,"release":150,"hyst":8},
    cab_ir="@american-ob",
    cab={"lowcut":90,"highcut":8500}),
  preset(11, 3, "Con Molars", out_level=OUT,            # With Teeth — The Hand That Feeds / Only (2005)
    # The 2005 return to a raw, direct live-band rock tone. Nail "Con Molars" (bright aggressive
    # clip -> speaker/cab voicing: low-cut + mid push) into a real cranked Marshall for the
    # in-the-room grind. Tighter, less scooped than the '90s eras.
    nail={"enable":1,"pos":4,"mode":"Con Molars","drive":0.7,"tone":0.55,"texture":0.5,"level":0.58},
    amp={"model":"Crunchy McCrunchFace","gain":0.5,"bass":0.5,"mid":0.58,"treble":0.58,"presence":0.55,"master":0.82,"sag":0.35},
    gt={"enable":1,"thresh":-54,"attack":1.5,"hold":120,"release":220,"hyst":8},
    cab_ir="@greenback",
    cab={"lowcut":90,"highcut":8800}),
)

# ── Bank 13 (index 12) — MICROTONAL / RADIOHEAD / GOJIRA ─────────────────────
# Slots A/B: microtonal shimmer + Radiohead. Slots C/D: Gojira (moved here 2026-07-12 to
# keep the bank full after "There There" was dropped) — clean tapping + the EVH rhythm.
# Slot A: the Octave block's pitch-tracked single-sideband microtonal shimmer (24-TET
# quarter-tone beating lead — the "Angine de Poitrine" voicing; a 12-fret guitar can't be
# re-fretted by an effect, so a detuned SSB voice beating a quarter-tone against the dry
# note is the honest version). Slots B/C: Radiohead guitar tones matched to the REAL rigs.
add(
  preset(12, 0, "Quarter-Tone Lead", out_level=-16.0,
    # The signature beating quarter-tone lead: mid-forward crunch (Turkish-rock voicing),
    # shimmer detuned +50c so every note beats ~6 Hz, soaked in loop-friendly tape delay.
    amp={"model":"Crunchy McCrunchFace","gain":0.5,"bass":0.5,"mid":0.72,"treble":0.6,"presence":0.5,"master":0.6,"sag":0.3},
    oc={"enable":1,"pos":7,"micro":0.5,"interval":"1/4 Up","up":0.0,"down":0.0,"dry":1.0},
    gt={"enable":1,"thresh":-50,"attack":1.5,"hold":120,"release":250,"hyst":8},
    dl={"enable":1,"type":"Tape","time":400,"feedback":0.35,"mix":0.2,"width":0.4,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"predelay":15,"decay":1.6,"damping":0.45,"mix":0.18},
    cab_ir="@greenback",
    cab={"lowcut":85,"highcut":8500}),
  preset(12, 1, "Anyone Can Play Guitar", out_level=-15.5,   # Radiohead — Pablo Honey (1993)
    # RESEARCHED (Guitar.com / Equipboard): the ENTIRE Pablo Honey rig was Telecaster Plus ->
    # Marshall ShredMaster -> Fender Eighty-Five (solid-state). The ShredMaster is the dirt on
    # every track; the amp stays clean + bright. Dear Rodent Boy (RAT) is the standard ShredMaster
    # proxy (aggressive, mid-forward op-amp distortion). Bright jangly Fender clean, driven, anthemic.
    amp={"model":"Clean Meanie","gain":0.3,"bass":0.45,"mid":0.55,"treble":0.72,"presence":0.6,"master":0.6,"sag":0.3},
    dr={"enable":1,"model":"Dear Rodent Boy","drive":0.55,"tone":0.6,"level":0.7,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1.5,"hold":110,"release":230,"hyst":8},
    md={"enable":1,"type":"Lush-2","rate":0.3,"depth":0.32,"mix":0.22,"width":0.6},   # subtle jangle shimmer
    dl={"enable":1,"type":"Digital","time":300,"feedback":0.2,"mix":0.12,"width":0.5},
    rv={"enable":1,"predelay":15,"decay":1.6,"damping":0.5,"mix":0.2},
    cab_ir="@american-ob",
    cab={"lowcut":85,"highcut":9500}),
  # (Gojira moved here 2026-07-12 so Bank 12 has no blank slots after "There There" was removed.)
  preset(12, 2, "Winterborn", out_level=-16.0,   # Gojira — "Born in Winter"-style clean/tapping intro (Magma)
    # The melancholic clean tapping figure: bright, spacious clean channel + compression for even taps,
    # a subtle chorus, eighth-note delay and a big room. Gentle gate so tap tails ring out.
    amp={"model":"Clean Meanie","gain":0.22,"bass":0.45,"mid":0.5,"treble":0.62,"presence":0.5,"master":0.72,"sag":0.4},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-24,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Lush-2","rate":0.2,"depth":0.3,"mix":0.25,"width":0.5},
    dl={"enable":1,"type":"Digital","time":410,"feedback":0.3,"mix":0.2,"width":0.5},
    rv={"enable":1,"predelay":20,"decay":2.2,"damping":0.45,"mix":0.28},
    gt={"enable":1,"thresh":-64,"attack":2,"hold":200,"release":400,"hyst":8},
    cab_ir="@greenback",
    cab={"lowcut":80,"highcut":10500}),
  preset(12, 3, "Castaway Groove", out_level=-17.0,   # Gojira — "Stranded"-style groovy main-riff RHYTHM
    # EVH 5150 III (Gainzilla), red channel — a touch more low end + gain for the groovy main riff; tight gate.
    amp={"model":"Gainzilla","gain":0.6,"bass":0.55,"mid":0.48,"treble":0.54,"presence":0.48,"master":0.42,"channel":1,"resonance":0.5,"sag":0.18},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.48,"level":0.74,"mix":1.0},
    gt={"enable":1,"thresh":-44,"attack":0.5,"hold":80,"release":170,"hyst":8},
    rv={"enable":1,"decay":1.4,"mix":0.07},
    cab_ir="@factory",
    cab={"lowcut":84,"highcut":7300}),
)

# ── Bank 14 (index 13) — MESA MARK (Metallica · BTBAM — Cali V IIC+/Mark IV) ──
# Both are the Mesa/Boogie Mark sound: Master of Puppets = a Mark IIC+ with the graphic-EQ
# "smiley" scoop; BTBAM Colors = a Mark IV, tighter + more mid-present. The Cali V IS a Mark.
add(
  preset(13, 0, "Marionette Master", out_level=-15.0,   # Metallica — "Master of Puppets"-style Mesa Mark IIC+ scoop
    # Mesa Mark IIC+ "smiley" scoop (750 Hz slammed down, treble up), tight downpicking, no boost.
    # DE-BASSED 2026-07-11 (user: "very bassy"): amp bass 0.6->0.42, GEQ 80 Hz +4->+1 & 240 Hz flat, cab
    # low-cut 85->95 — the low end was flubby; kept the mid scoop + treble lift. Cali V IIC+, E standard.
    # HISS RETUNE 2026-07-13: per real-MarkV EQ behavior — Bass 0.42->0.28 (tight, Bass 1-3 on high gain),
    # pre-gain Treble 0.62->0.52 (it adds saturation/hiss), brightness moved to the POST-gain GEQ 6.6 kHz
    # 0.708->0.75 (no hiss penalty); gain 0.72->0.80 offsets the model's ~3 dB high-gain trim.
    amp={"model":"Cali V","mv_mode":"IIC+","gain":0.80,"bass":0.28,"mid":0.35,"treble":0.52,"presence":0.6,"master":0.6,"sag":0.32,
         "mv_geq0":0.542,"mv_geq1":0.5,"mv_geq2":0.167,"mv_geq3":0.583,"mv_geq4":0.75},
    gt={"enable":1,"thresh":-46,"attack":0.5,"hold":100,"release":200,"hyst":8},
    rv={"enable":1,"decay":1.3,"mix":0.06},
    cab_ir="@greenback",
    cab={"lowcut":95,"highcut":7800}),
  preset(13, 1, "Spectrum Rhythm", out_level=-16.0,   # Between the Buried and Me — "Colors"-style rhythm (Mesa Mark IV)
    # RESEARCHED: Paul Waggoner / Dustie Waring ran Mesa/Boogie Mark-series into V30s (PRS) for Colors
    # (2007) — tight, articulate high gain for the complex prog, LESS scooped than thrash (mids kept for
    # note definition). Cali V Mark IV + a MODERATE GEQ scoop + a light TS to tighten. V30 (factory) cab.
    # HISS RETUNE 2026-07-13: Bass 0.55->0.30 (was well into the "woofy" zone for high gain), pre-gain
    # Treble 0.6->0.52, brightness -> POST-gain GEQ 6.6 kHz 0.625->0.68; gain 0.68->0.76 offsets model trim.
    amp={"model":"Cali V","mv_mode":"Mk IV","gain":0.76,"bass":0.30,"mid":0.5,"treble":0.52,"presence":0.55,"master":0.55,"sag":0.3,
         "mv_geq0":0.583,"mv_geq1":0.5,"mv_geq2":0.375,"mv_geq3":0.583,"mv_geq4":0.68},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.5,"level":0.74,"mix":1.0},
    gt={"enable":1,"thresh":-44,"attack":0.5,"hold":90,"release":180,"hyst":8},
    rv={"enable":1,"decay":1.5,"mix":0.08},
    cab_ir="@factory",
    cab={"lowcut":86,"highcut":7900}),
  preset(13, 2, "Spectrum Lead", out_level=-15.0,   # Between the Buried and Me — "Colors"-style lead (Mesa Mark IV, singing)
    # Paul Waggoner's smooth, sustaining, mid-forward legato lead: Mark IV pushed with a Klon-style boost,
    # mids UP (GEQ mid-boost, not scooped), soaked in dotted delay + a plate. PRS, neck/bridge blend.
    # HISS RETUNE 2026-07-13: Bass 0.45->0.35, pre-gain Treble 0.62->0.55 (leads keep a touch more), GEQ
    # 6.6 kHz 0.542->0.60 for post-gain air; gain 0.7->0.78 offsets model trim. Mid-forward voicing kept.
    amp={"model":"Cali V","mv_mode":"Mk IV","gain":0.78,"bass":0.35,"mid":0.6,"treble":0.55,"presence":0.6,"master":0.6,"sag":0.35,
         "mv_geq0":0.458,"mv_geq1":0.542,"mv_geq2":0.625,"mv_geq3":0.583,"mv_geq4":0.60},
    dr={"enable":1,"model":"Gilded Horse","drive":0.2,"tone":0.6,"level":0.7,"mix":1.0},
    gt={"enable":1,"thresh":-52,"attack":1,"hold":130,"release":280,"hyst":8},
    dl={"enable":1,"type":"Seraph","time":420,"feedback":0.3,"mix":0.2,"width":0.5,"pattern":"Dotted 8th","ducking":0.2,"moddepth":0.12,"modrate":0.3},
    rv={"enable":1,"predelay":18,"decay":1.8,"damping":0.42,"mix":0.26},
    cab_ir="@factory",
    cab={"lowcut":88,"highcut":8200}),
)

# ── PACKING FILLERS (2026-07-13) — the old "QUEEN + GRUNGE" bank was dissolved to eliminate blank slots
# (user: "fill the banks with what I have, don't worry about genre/band; no blanks before the last bank").
# These 3 are now packed into the gaps of earlier banks: Regal Sustain -> bank 1/A, Regal Solo -> bank 11/D,
# Grunge Drop -> bank 14/D. (They keep their own tone; only their bank/slot changed.)
add(
  preset(1, 0, "Regal Sustain", out_level=-13.0,   # Brian May / Queen — treble-boosted AC30 sing [packed into bank 1/A to fill the gap]
    # Red Special -> treble booster -> CRANKED Vox AC30 = the singing power-amp sustain. The Input-Trim
    # clean boost (it_boost, ~6 dB) is ON to push the AC30 into real Queen sustain (on top of the TS boost).
    it={"boost":1,"boostamt":6},
    dr={"enable":1,"model":"Green Man","drive":0.2,"tone":0.8,"level":0.95,"mix":1.0},
    amp={"model":"Chime Thirty","gain":0.9,"bass":0.55,"mid":0.6,"treble":0.8,"presence":0.65,"master":0.9,"sag":0.78},
    gt={"enable":1,"thresh":-52,"attack":2,"hold":150,"release":300,"hyst":8},
    rv={"enable":1,"predelay":10,"decay":1.0,"damping":0.5,"mix":0.13},
    cab_ir="@vox2x12",
    cab={"lowcut":85,"highcut":9500}),
  preset(10, 3, "Regal Solo", out_level=-13.5,   # Brian May / Queen — the singing harmonised LEAD [packed into bank 11/D to fill the gap]
    # As Regal Sustain + a tape delay for the layered-solo feel. Input-Trim clean boost (it_boost) ON.
    it={"boost":1,"boostamt":6},
    dr={"enable":1,"model":"Green Man","drive":0.2,"tone":0.82,"level":0.95,"mix":1.0},
    amp={"model":"Chime Thirty","gain":0.88,"bass":0.5,"mid":0.62,"treble":0.8,"presence":0.6,"master":0.92,"sag":0.8},
    gt={"enable":1,"thresh":-54,"attack":2,"hold":160,"release":320,"hyst":8},
    dl={"enable":1,"type":"Tape","time":310,"feedback":0.34,"mix":0.2,"width":0.4,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"predelay":15,"decay":1.4,"damping":0.45,"mix":0.18},
    cab_ir="@vox2x12",
    cab={"lowcut":85,"highcut":9500}),
  preset(13, 3, "Grunge Drop", out_level=-15.0,   # Soundgarden / Alice in Chains — thick detuned mid-gain + cocked wah [packed into bank 14/D to fill the gap]
    # Detuned, thick mid-gain (cranked Marshall) with a parked/cocked wah for the nasal honk; heavy, not scooped.
    amp={"model":"Crunchy","gain":0.52,"bass":0.6,"mid":0.6,"treble":0.5,"presence":0.45,"master":0.55},
    wh={"enable":1,"pos":2,"type":"Fixed","freq":0.5,"q":0.55,"mix":0.35},
    dr={"enable":1,"model":"Green Man","drive":0.12,"tone":0.5,"level":0.7,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":120,"release":250,"hyst":8},
    rv={"enable":1,"decay":1.4,"mix":0.1},
    cab_ir="@greenback",
    cab={"lowcut":76,"highcut":8000}),
)

# ── Bank 15 (index 14) — MUSE · the LAST bank (allowed partial): Plug In Baby + Knights of Cydonia ──
# Matt Bellamy's Z.Vex Fuzz Factory ("Fizz Factory") showcase. This is the final populated bank, so it's
# allowed to be non-full (2 presets). Everything before it is packed full.
add(
  preset(14, 0, "Plug-In Junior", out_level=-18.0,   # Muse — "Plug In Baby"-style ZVex Fuzz Factory riff (Origin of Symmetry)
    # THE Bellamy sound: a Z.Vex Fuzz Factory (Fizz Factory) — aggressive, gated, spitty velcro fuzz —
    # into a bright cranked Marshall (JCM800). RESEARCHED: Origin-of-Symmetry-era Bellamy ran the Fuzz
    # Factory into a Marshall JMP/JCM; the riff is high-on-the-neck, cutting, mid-present, tight staccato.
    # FF knob map: Sustain=Drive (high, the scream), Bias=Comp (up = the octave-y velcro spit), Input
    # Trim=Gate (moderate cleanup), Ge Temp=Stab (LOW — a hint of edge, NOT a full self-oscillation; the
    # runaway scream is for the noise breaks/solos, not the main riff), Volume moderate. Gate to tighten
    # the staccato; a touch of room. NOTE: the Fizz Factory is a tune-by-ear voicing (see fuzz-factory-wip)
    # — these are sensible riff settings, expect to fine-tune Drive/Comp/Stab on the device.
    fz={"enable":1,"pedal":"Fuzz Zachary","sustain":0.92,"bias":0.5,"inputtrim":0.42,"getemp":0.2,"volume":0.55},   # aggressive + CONTROLLED: Stab 0.2 sits BELOW the self-oscillation onset (~0.28) so the PIB riff doesn't squeal; hot Drive carries the aggression. Crank Stab past ~0.3 for the unruly squeal.
    amp={"model":"Crunchy","gain":0.45,"bass":0.45,"mid":0.6,"treble":0.62,"presence":0.55,"master":0.62,"sag":0.3},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":110,"release":230,"hyst":8},
    rv={"enable":1,"predelay":10,"decay":1.3,"damping":0.5,"mix":0.1},
    cab_ir="@greenback",
    cab={"lowcut":85,"highcut":8500}),
  preset(14, 1, "Cavalier Charge", out_level=-16.5,   # Muse — "Knights of Cydonia"-style galloping fuzz riff + epic lead
    # Bellamy's spaghetti-western epic: the Fizz Factory driving a bright cranked Marshall, dotted-8th delay for
    # the galloping ride, and enough Stab edge for the unhinged solo. RESEARCHED: Knights = Fuzz Factory + big
    # delay (+ a Whammy octave on the solo harmonies, approximated here by the delay/room width). Bright, cutting.
    fz={"enable":1,"pedal":"Fuzz Zachary","sustain":0.88,"bias":0.55,"inputtrim":0.4,"getemp":0.25,"volume":0.55},
    amp={"model":"Crunchy","gain":0.5,"bass":0.45,"mid":0.6,"treble":0.62,"presence":0.55,"master":0.65,"sag":0.3},
    gt={"enable":1,"thresh":-48,"attack":1,"hold":110,"release":230,"hyst":8},
    dl={"enable":1,"type":"Seraph","time":400,"feedback":0.4,"mix":0.22,"width":0.6,"pattern":"Dotted 8th","ducking":0.2,"moddepth":0.12,"modrate":0.3},
    rv={"enable":1,"predelay":15,"decay":1.6,"damping":0.45,"mix":0.2},
    cab_ir="@greenback",
    cab={"lowcut":85,"highcut":8800}),
)

# ── Loudness calibration ─────────────────────────────────────────────────────
# Measured on-device with build-tools/hexforge_meas.cpp (a worker-capable LV2 host;
# DI from build-tools/gen_di.py): each preset recalled, a sustained DI power chord run
# through it with the output auto-limiter OFF, output RMS captured in dBFS at out_level
# = -20. Because out_level is an exact linear post-gain, out_level_final = -20 + (target
# - measured) lands each preset on the target loudness in one pass. Dirty/dense target
# ~ -13.1 dBFS RMS (the stock Crunch/Rhythm/Lead average).
# KEYED BY PRESET NAME (not flat index) so rearranging banks never invalidates it.
# Re-run hf_meas + update an entry only when a preset's voicing/gain changes.
# Re-measured 2026-07-09 with build-tools/hexforge_meas AFTER the amp-makeup PARITY re-level
# (Hiwatt/Friedman/CaliV/Plexi etc. boosted) — every value here is the preset's RMS @ out_level=-20.
MEAS_RMS_AT_M20 = {
    "Nevermind Verse":-24.00, "Nevermind Wall":-12.31, "Come As Water":-17.18,
    "Candlelit Clean":-22.62, "Sermon Crunch":-15.35, "Sermon Rhythm":-14.23, "Sermon Solo":-14.08,
    "Imperial Rhythm":-16.52, "Imperial Lead":-14.86, "Cardinal Rhythm":-16.42, "Cardinal Lead":-14.96,
    "Dark Side Air":-25.35, "Berlin Wall Pulse":-21.57, "Numb Sustain":-9.22, "Gravity Lead":-30.29,
    "Mauve Haze":-11.42, "Hazy Solo":-12.62, "Little Feather":-19.61, "Holy Smoke":-10.50,
    "Skye Crusher":-14.50, "Skye (No Mod)":-13.91, "Skye Soar":-13.60, "Wizard's Doom":-8.21,
    "Vanishing Drive":-16.28, "Dreamlit Shimmer":-28.01, "Flatliner":-11.81, "Prayer Djent":-11.97,
    "Bridge Vibe":-14.68, "Bottle Jangle":-32.16, "Forest Wash":-29.32, "Disco Chuck":-31.55,
    "Surf Splash":-25.72, "Apache Echo":-20.53, "Desert Robot":-8.08, "Moondust Glam":-6.75,
    "Innerspeaker Swirl":-13.96, "Glide Wall":-15.53, "Streets Chime":-20.34, "Regal Sustain":-14.25,
    "March Stabs":-14.70, "World Went Away":-10.13, "Broken Crush":-13.07, "Con Molars":-13.58,
    "Quarter-Tone Lead":-12.13, "Anyone Can Play Guitar":-16.77, "There There":-19.64,
}
# Peak dBFS at out_level=-20 (same run) — CAP out_level so high-crest clean/lead transients stay
# below the limiter ceiling (prevents pumping/crush on peaky presets).
MEAS_PEAK_AT_M20 = {
    "Nevermind Verse":-11.50, "Nevermind Wall":-1.38, "Come As Water":-3.90,
    "Candlelit Clean":-13.19, "Sermon Crunch":-5.89, "Sermon Rhythm":-4.99, "Sermon Solo":-3.16,
    "Imperial Rhythm":-7.09, "Imperial Lead":-4.66, "Cardinal Rhythm":-6.85, "Cardinal Lead":-4.96,
    "Dark Side Air":-12.23, "Berlin Wall Pulse":-7.90, "Numb Sustain":-0.33, "Gravity Lead":-17.30,
    "Mauve Haze":-2.33, "Hazy Solo":-3.14, "Little Feather":-8.92, "Holy Smoke":-2.82,
    "Skye Crusher":-3.92, "Skye (No Mod)":-4.32, "Skye Soar":-3.75, "Wizard's Doom":-1.03,
    "Vanishing Drive":-6.35, "Dreamlit Shimmer":-15.53, "Flatliner":-3.57, "Prayer Djent":-3.51,
    "Bridge Vibe":-4.28, "Bottle Jangle":-19.60, "Forest Wash":-16.45, "Disco Chuck":-20.01,
    "Surf Splash":-14.77, "Apache Echo":-8.65, "Desert Robot":0.44, "Moondust Glam":2.12,
    "Innerspeaker Swirl":-3.35, "Glide Wall":-3.55, "Streets Chime":-8.69, "Regal Sustain":-4.66,
    "March Stabs":-4.36, "World Went Away":-1.62, "Broken Crush":-2.75, "Con Molars":-4.53,
    "Quarter-Tone Lead":-1.42, "Anyone Can Play Guitar":-5.37, "There There":-5.04,
}
CLEAN_NAMES = {"Candlelit Clean", "Dreamlit Shimmer", "Berlin Wall Pulse", "Dark Side Air", "Little Feather",
               "Nevermind Verse", "Come As Water",
               "Bottle Jangle", "Forest Wash", "Disco Chuck", "Surf Splash", "Apache Echo", "Streets Chime"}
SUNN_NAMES  = {"Wizard's Doom"}   # Sunn's dark voicing reads quiet -> small perceptual lift
# ── LOUDNESS PARITY (2026-07-09) ─────────────────────────────────────────────
# User: "clean and high-gain presets should all be the SAME volume; the new factory
# presets are louder than my Bank-1 originals." Their Bank 1 sits at ~-12.5 dBFS RMS
# (dirty) / ~-13.0 (clean) — clean & dirty ~equal, which they set by ear as "equal feel".
# So EVERY factory preset is leveled to that: dirty -12.5, clean -13.0, Sunn -11.5 (+1 dB
# perceptual). The old CLEAN_OUT -5.9 pin (cleans ran ~2 dB hot) and the +8 dB "pushed-loud"
# MANUAL_OUT leads are GONE — that loudness spread was the complaint. out_level is post-
# everything volume, so re-leveling changes NO preset's tone. Each value is RMS-target-then-
# PEAK-capped (PEAK_CEIL) so a high-crest clean/lead can't slam the master limiter.
TARGET_DIRTY, TARGET_CLEAN, TARGET_SUNN, PEAK_CEIL = -12.5, -13.0, -11.5, 0.0
MANUAL_OUT = {}   # no hand-locks — full parity re-level (bump kFactoryRev to push to users)
for _p in PRESETS:
    _nm = _p["name"]
    if _nm in MANUAL_OUT:
        _p["vals"][SYM_IDX["out_level"]] = MANUAL_OUT[_nm]
    elif _nm in MEAS_RMS_AT_M20:
        _tgt = TARGET_CLEAN if _nm in CLEAN_NAMES else TARGET_SUNN if _nm in SUNN_NAMES else TARGET_DIRTY
        _out_rms  = -20.0 + (_tgt - MEAS_RMS_AT_M20[_nm])
        _out_peak = PEAK_CEIL - 20.0 - MEAS_PEAK_AT_M20.get(_nm, -6.0)   # cap: peak stays under the ceiling
        _p["vals"][SYM_IDX["out_level"]] = round(max(-40.0, min(6.0, min(_out_rms, _out_peak))), 1)

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
    by_gap = src_ver < 9                                  # v9: 11 per-block bypass toggles
    by_at = SYM_IDX["gt_bypass"]; by_end = by_at + 11     # default 0 = active
    naildef = [12.0, 0.0, 2.0, 0.6, 0.5, 0.4, 0.5, 0.0]   # v12: Nail block (pos,en,mode,drive,tone,texture,level,byp)
    nail_gap = src_ver < 12
    nail_at = SYM_IDX["nail_pos"]; nail_end = nail_at + 8
    syncdef = [0.0, 5.0, 0.0, 2.0]                         # v13: tempo sync (dl_sync,dl_div,md_sync,md_div)
    sync_gap = src_ver < 13
    sync_at = SYM_IDX["dl_sync"]; sync_end = sync_at + 4
    ocdef = [0.0, 0.0]                                     # v14: Octave shimmer (oc_micro, oc_interval)
    oc_gap = src_ver < 14
    oc_at = SYM_IDX["oc_micro"]; oc_end = oc_at + 2
    mv_gap = src_ver < 15                                  # v15: Cali V (Mesa) mode
    mv_at = SYM_IDX["amp_mv_mode"]; mv_end = mv_at + 1
    gv_gap = src_ver < 16                                  # v16: Cali V 5-band graphic EQ
    gv_at = SYM_IDX["amp_mv_geq0"]; gv_end = gv_at + 5
    eq_gap = src_ver < 17                                  # v17: Cali V graphic-EQ preset selector
    eq_at = SYM_IDX["amp_mv_eqpreset"]; eq_end = eq_at + 1
    out = [0.0] * N_PORTS; o = 0
    for i in range(N_PORTS):
        if it_at <= i < it_end:
            out[i] = vdef[i - SYM_IDX["it_humbk"]]
        elif dl_gap and dl_at <= i < dl_end:
            out[i] = ddef[i - dl_at]
        elif wo_gap and wo_at <= i < wo_end:
            out[i] = wodef[i - wo_at]
        elif by_gap and by_at <= i < by_end:
            out[i] = 0.0
        elif nail_gap and nail_at <= i < nail_end:
            out[i] = naildef[i - nail_at]
        elif sync_gap and sync_at <= i < sync_end:
            out[i] = syncdef[i - sync_at]
        elif oc_gap and oc_at <= i < oc_end:
            out[i] = ocdef[i - oc_at]
        elif mv_gap and mv_at <= i < mv_end:
            out[i] = 6.0
        elif gv_gap and gv_at <= i < gv_end:
            out[i] = 0.5
        elif eq_gap and eq_at <= i < eq_end:
            out[i] = 0.0
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
    L.append("struct HfFactoryPreset { int bank; int slot; const char* name; const char* cabIr; float vals[HF_N_PORTS]; };")
    L.append("static const HfFactoryPreset kFactoryExtra[] = {")
    for p in PRESETS:
        vals = ", ".join(_fmt(x) for x in p["vals"])
        cab = ('"%s"' % p["cab"]) if p.get("cab") else "nullptr"
        L.append('  { %d, %d, "%s", %s, { %s } },' % (p["bank"], p["slot"], p["name"], cab, vals))
    L.append("};")
    L.append("static const int kFactoryExtraCount = %d;" % len(PRESETS))
    L.append("")
    return "\n".join(L)

if __name__ == "__main__":
    assert N_PORTS == 194, "port count drift: got %d" % N_PORTS   # 186: Cali V graphic EQ; 187: Cali V EQ preset; 188: md_offset (v18); 194: +6 NAM gain/level trims (v19)
    # ── Auto-match a built-in cab to each preset from its amp model ──────────────
    # Explicit preset(cab_ir=...) wins; unmatched amps (EVH/Orange/Beardo/NAM) keep
    # the Factory Cab (@factory / V30). See CabModels.h for the voicings.
    _AMP_CAB = {0: "@american-ob",   # Clean Meanie (Fender)  → American open-back
                1: "@greenback",     # Crunchy (JCM800)       → Greenback 4x12
                3: "@doom",          # Doom Daddy (Sunn)      → big dark 4x12
                7: "@hiwatt",        # Hi-Volt (Hiwatt)       → Hiwatt/Fane
                8: "@vox2x12"}       # Chime Thirty (Vox)     → Vox alnico 2x12
    _ai, _ae = SYM_IDX["amp_model"], SYM_IDX["amp_enable"]
    for _p in PRESETS:
        if _p.get("cab") or _p["vals"][_ae] < 0.5:
            continue
        _m = int(round(_p["vals"][_ai]))
        if _m in _AMP_CAB:
            _p["cab"] = _AMP_CAB[_m]
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
