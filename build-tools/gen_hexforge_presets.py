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
    cab_ir="@american-ob",   # 2026-07-14 research: SLTS verse = Mustang -> Fender Bassman (groundguitar/guitar.com) — Fender-ish open-back, not a V30 4x12
    cab={"micpos":0.20,"micdist":0.10,"lowcut":85,"highcut":9500}),
  preset(1, 2, "Nevermind Wall", out_level=OUT,           # Smells Like Teen Spirit — the distorted WALL
    # REBUILT 2026-07-14 (user: the old on-device rework "is trash — carefully construct it; pick the
    # correct gear"). RESEARCHED (Ground Guitar / Vig): '69 Mustang -> Boss DS-1 with KURT'S DOCUMENTED
    # SETTINGS — tone ~10 o'clock (0.35), distortion at 4 (0.4), LEVEL MAXED — slamming a Fender BASSMAN;
    # the wall's roar is the Fender front-end getting shoved, not a high-gain pedal. NO chorus on the
    # wall (the Small Clone lives on the verse/solo — that's Nevermind Verse). Vig's SM57 close on the
    # Bassman speaker, slightly off-cap; a touch of Sound City room. Wall thickness = the doubled takes
    # on the record; the pushed Fender + open-back 2x12 gets the character.
    dr={"enable":1,"model":"Grunge DS","drive":0.4,"tone":0.35,"level":1.0,"mix":1.0},
    amp={"model":"Clean Meanie","gain":0.6,"bass":0.5,"mid":0.55,"treble":0.55,"presence":0.5,"master":0.8,"sag":0.4},
    gt={"enable":1,"thresh":-48,"attack":1.5,"hold":120,"release":250,"hyst":8},
    rv={"enable":1,"predelay":12,"decay":1.1,"damping":0.5,"mix":0.08},
    cab_ir="@american-ob",
    cab={"lowcut":80,"highcut":8500,"micpos":0.25,"micdist":0.05}),
  preset(1, 3, "Come As Water", out_level=OUT,            # Come As You Are — clean, deep chorus
    # The underwater riff: clean amp, neck-pickup warmth, Small Clone (Nevermind Chorus) set DEEP.
    # RESEARCHED (Vig: "AC30 with a Small Clone"): clean Vox + deep Small Clone, watery arpeggio.
    amp={"model":"Chime Thirty","gain":0.3,"bass":0.4,"mid":0.55,"treble":0.6,"presence":0.5,"master":0.65,"sag":0.3},
    md={"enable":1,"type":"Nevermind Chorus","rate":0.38,"depth":1.0,"mix":0.62,"width":0.6},
    rv={"enable":1,"predelay":12,"decay":1.0,"damping":0.55,"mix":0.2},
    cab_ir="@vox2x12",   # 2026-07-14 research: Vig confirms AC30 + Small Clone — the Vox 2x12 exists exactly for this
    cab={"micpos":0.20,"micdist":0.10,"lowcut":80,"highcut":9000}),
)

# ── Bank 3 — GHOST · Opus Eponymous (Orange Thunderverb era) ─────────────────
add(
  preset(2, 0, "Candlelit Clean", out_level=OUT,         # Ghost — Opus clean
    # Orange clean + dark analog delay (Carbon Copy) + a little room.
    # RESEARCHED: Orange Thunderverb 50 clean (Ghoul quote: back off gain, lean on mids), Gibson SG,
    # Greenback 4x12; MXR Analog Chorus + Carbon Copy tape delay + Philosophers-Tone comp.
    amp={"model":"Tangerang","gain":0.2,"bass":0.4,"mid":0.65,"treble":0.55,"presence":0.45,"master":0.65,"sag":0.6},
    cp={"enable":1,"type":0,"ratio":1,"thresh":-24,"attack":3,"release":5,"knee":3,"makeup":4},   # 2026-07-14: Philosopher's Tone is an OPTICAL comp -> type 0
    md={"enable":1,"type":"Lush-2","rate":0.25,"depth":0.3,"mix":0.3,"width":0.5},
    dl={"enable":1,"type":"Tape","time":380,"feedback":0.25,"mix":0.16,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"predelay":15,"decay":1.6,"damping":0.6,"mix":0.2},
    cab_ir="@factory",   # 2026-07-14: Ghost's Orange PPC412s are V30-loaded — @factory V30 4x12, not Greenbacks (whole Opus bank)
    cab={"micpos":0.20,"micdist":0.10,"lowcut":85,"highcut":8500}),
  preset(2, 1, "Sermon Crunch", out_level=OUT,           # Ghost — Opus Eponymous rhythm
    # SG -> Orange Thunderverb 50: warm, mid-forward, gain backed off, '70s.
    # RESEARCHED: Thunderverb dirty low-gain + Pigtronix Fat Drive (TS-style) light boost; cranked-70s
    # mid-pushed Orange crunch, NOT high gain. Greenback cab.
    # revoiced 2026-07-11: TS was ATTENUATING (level 0.65 = -3 dB) -> weak push; raised to 0.78 (unity boost)
    # to restore the mid-forward Orange crunch; treble/high-cut eased a touch for the now-brighter front-end.
    amp={"model":"Tangerang","gain":0.42,"bass":0.38,"mid":0.72,"treble":0.5,"presence":0.46,"master":0.7,"sag":0.65},
    dr={"enable":1,"model":"Green Man","drive":0.3,"tone":0.55,"level":0.78,"mix":1.0},
    gt={"enable":1,"thresh":-52,"attack":3,"hold":120,"release":260,"hyst":8},
    cab_ir="@factory",   # 2026-07-14: PPC412 = V30s
    cab={"micpos":0.20,"micdist":0.10,"lowcut":90,"highcut":7900}),
  preset(2, 2, "Sermon Rhythm", out_level=OUT,           # Ghost — Opus rhythm, higher gain than Sermon Crunch
    # Same Orange Thunderverb voicing as Sermon Crunch, pushed: more gain + TS tighten + tighter gate.
    # RESEARCHED: same Orange rig pushed harder (live JCM900 also used for grind); mid-forward.
    # revoiced 2026-07-11: TS level 0.6->0.78 (was attenuating -> weak) to restore the pushed Orange grind;
    # treble/presence + high-cut eased for the brighter front-end.
    amp={"model":"Tangerang","gain":0.56,"bass":0.4,"mid":0.68,"treble":0.51,"presence":0.46,"master":0.72,"sag":0.6},
    dr={"enable":1,"model":"Green Man","drive":0.4,"tone":0.5,"level":0.78,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":130,"release":280,"hyst":8},
    cab_ir="@factory",   # 2026-07-14: PPC412 = V30s
    cab={"micpos":0.20,"micdist":0.10,"lowcut":90,"highcut":7900}),
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
    cab_ir="@factory",   # 2026-07-14: PPC412 = V30s
    cab={"micpos":0.20,"micdist":0.10,"lowcut":88,"highcut":8800}),
)

# ── Bank 4 — GHOST · Impera & Skeleta (Friedman BE era) ──────────────────────
add(
  preset(3, 0, "Imperial Rhythm", out_level=OUT,         # Ghost — Impera rhythm (Friedman BE, Akesson's BE-100)
    # RESEARCHED (Akesson): Friedman BE-100 rhythm (BE channel) + TS/Sugar-Drive tightener, V30 4x12,
    # tight+punchy dry, layered.
    # revoiced 2026-07-11: Beardo BE was re-voiced brighter (+2 kHz bite) AND the input clip is gone -> harsh;
    # eased treble/presence + high-cut + a touch less gain. TS 0.6->0.74 (was attenuating) to keep the tight push.
    amp={"model":"Beardo BE","fr_channel":"BE","fr_fat":1,"gain":0.58,"bass":0.45,"mid":0.55,"treble":0.52,"presence":0.55,"master":0.68,"sag":0.45},
    # 2026-07-14 research: Akesson's much-used Impera drive was the MXR Sugar Drive — a KLON-style circuit
    # (Gilded Horse), not a TS808. Same knob values: still a light tightening boost.
    dr={"enable":1,"model":"Gilded Horse","drive":0.25,"tone":0.65,"level":0.74,"mix":1.0},
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
    # 2026-07-14 research: Akesson cut the Impera SOLOS with a '70s DOD Preamp 250 (the Sugar Drive was the
    # rhythm pedal); level 0.55 mirrors the user's proven DOD-into-HBE dial (Cardinal Lead) — boost, not fuzz-stack.
    dr={"enable":1,"model":"Preamp 250","drive":0.16,"tone":0.6,"level":0.55,"mix":1.0},
    md={"enable":1,"type":"Lush-2","rate":0.22,"depth":0.32,"mix":0.20,"width":0.35},  # subtle CE-2 chorus — user pref over the researched Phase 95 (phaser read wrong on-rig)
    gt={"enable":1,"thresh":-45,"attack":1,"hold":130,"release":280,"hyst":8},
    dl={"enable":1,"type":"Seraph","time":320,"feedback":0.28,"mix":0.22,"width":0.45,"pattern":"Dotted 8th","ducking":0.2,"moddepth":0.12,"modrate":0.3},
    rv={"enable":1,"predelay":15,"decay":1.4,"damping":0.45,"mix":0.28},
    cab={"lowcut":88,"highcut":8100}),
  preset(3, 2, "Cardinal Rhythm", out_level=OUT,         # Ghost — Skeleta rhythm (Mesa Mark IIC+ per SoS)
    # RE-RESEARCHED 2026-07-14 (Sound on Sound "Inside Track" on Skeleta): the distorted rhythm guitars were
    # cut on James Hetfield's Mesa Boogie IIC+ — the Cali V IIC+ LEAD-channel voicing (mv_mode 6).
    # USER DIAL-IN (2026-07-14, read back from the device .dat) — DO NOT re-tune without the user. Their
    # Mark-doctrine voicing: the amp works like an OVERDRIVE — gain LOW (0.15!), Bass literally 0, Treble
    # backed to 0.38, brightness regained POST-gain at the 6.6 kHz GEQ; boost = DOD Preamp 250 at near-zero
    # drive pushing LEVEL into the front end. Mid 0.6 thickener kept (Ghost is mid-forward, not scooped).
    amp={"model":"Cali V","mv_mode":6,"gain":0.1525,"bass":0.0,"mid":0.6,"treble":0.38,"presence":0.57,"master":0.5,"sag":0.35,
         "mv_geq4":0.62},
    dr={"enable":1,"model":"Preamp 250","drive":0.0175,"tone":0.7,"level":0.74,"mix":1.0},
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
    cab={"micpos":0.25,"micdist":0.15,"lowcut":80,"highcut":10000}),
  preset(4, 1, "Berlin Wall Pulse", out_level=OUT,       # Pink Floyd — Run Like Hell
    # Strat, palm-muted, huge dotted delay soaked in chorus.
    # RESEARCHED (Gilmourish/kitrae): Hiwatt bright clean + mild boost, EHX Electric Mistress FLANGER
    # (not chorus), dotted-8th 380ms (117 BPM) dual delay via Seraph, subtle Dynacomp.
    amp={"model":"Hi-Volt","gain":0.2,"bass":0.5,"mid":0.45,"treble":0.6,"presence":0.6,"master":0.7,"sag":0.25},
    # 2026-07-14 research: Gilmour's Run Like Hell boost was the Colorsound Power Boost (Cornish board) — a
    # TRANSPARENT full-range booster run full-series, not a mid-humped TS at 40% blend.
    dr={"enable":1,"model":"New Dawn","drive":0.15,"tone":0.6,"level":0.55,"mix":1.0},
    cp={"enable":1,"type":0,"ratio":1,"thresh":-20,"makeup":1},
    md={"enable":1,"type":"Flanger","rate":0.12,"depth":0.4,"mix":0.4,"width":0.4},
    dl={"enable":1,"type":"Seraph","time":380,"feedback":0.5,"mix":0.4,"width":0.4,"pattern":"Dotted 8th","ducking":0.1,"moddepth":0.05,"modrate":0.2},
    rv={"enable":1,"decay":1.4,"mix":0.1},
    cab_ir="@hiwatt",
    cab={"micpos":0.25,"micdist":0.15,"lowcut":85,"highcut":11000}),
  preset(4, 2, "Numb Sustain", out_level=OUT,            # Pink Floyd — Gilmour Big Muff + Binson Echorec lead
    # Ram's-Head Muff (Ovis) Vol4/Tone6/Sus6 + Dyna Comp + Hiwatt clean platform + Binson (Echo Wreck).
    # RESEARCHED (Gilmourish/kitrae, 1980 photos): Rams-Head Muff (Ovis) Sus~0.75/Tone~0.38 dark/Vol~0.6
    # into a CRANKED Hiwatt (master 0.9 — the violin sustain is amp-compression, not maxed fuzz). Comp
    # BYPASSED for the solo. MXR Digital Delay 450ms (studio). Restrained treble/mid.
    fz={"enable":1,"pedal":"Italian Hero","mode":"Ovis","sustain":0.75,"tone":0.38,"volume":0.6},
    amp={"model":"Hi-Volt","gain":0.4,"bass":0.55,"mid":0.45,"treble":0.4,"presence":0.55,"master":0.9,"sag":0.4},
    gt={"enable":1,"thresh":-56,"attack":2,"hold":200,"release":400,"hyst":8},
    # 2026-07-14: delay type fixed Echo Wreck->Digital — the Binson was retired by '77; the comment above
    # already said "MXR Digital Delay 450ms (studio)" but the block contradicted it. ALSO added the missing
    # signature: the solo ran the Hiwatt in PARALLEL with a Yamaha RA-200 rotating-speaker cab — a gentle
    # rotary blended under the dry Hiwatt. Cab pinned to @hiwatt (Fane) like the rest of the Gilmour bank.
    md={"enable":1,"type":"Rotary","rate":0.25,"depth":0.35,"mix":0.3,"width":0.6},
    dl={"enable":1,"type":"Digital","time":450,"feedback":0.45,"mix":0.25},
    rv={"enable":1,"predelay":25,"decay":1.8,"damping":0.4,"mix":0.22},
    cab_ir="@hiwatt",
    cab={"micpos":0.25,"micdist":0.15,"lowcut":80,"highcut":9500}),
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
    cab_ir="@american-ob",   # 2026-07-14: Mayer's Two-Rock/Dumble platform speaks through American open-back combos, not a V30 4x12
    cab={"micpos":0.30,"micdist":0.10,"lowcut":80,"highcut":9500}),
)

# ── Bank 6 — JIMI HENDRIX (+ Sleep, doom singleton) ──────────────────────────
add(
  preset(5, 0, "Mauve Haze", out_level=OUT,              # Jimi Hendrix — Purple Haze
    # Germanium Fuzz Face (I Know It) into a Marshall Super Lead PLEXI (Plexiglass — the actual
    # Hendrix amp; JCM800 didn't exist until '81). Amp master up for power-amp grind, treble
    # eased (plexi runs bright), gain moderate — the Fuzz Face is the dirt.
    fz={"enable":1,"pedal":"I Know It","sustain":0.7,"volume":0.55,"bias":0.55,"inputtrim":0.45,"getemp":0.4},
    # 2026-07-14: JUMPERED 1959 (new pl_vol2 = Vol II Normal channel) — Hendrix ran the channels bridged;
    # Vol I carries the bright cut, Vol II adds the fat underneath, master up for power-amp crunch.
    amp={"model":"Plexiglass","gain":0.5,"pl_vol2":0.5,"bass":0.5,"mid":0.6,"treble":0.5,"presence":0.5,"master":0.7,"sag":0.35},
    gt={"enable":1,"thresh":-52,"attack":2,"hold":150,"release":300,"hyst":8},
    cab_ir="@greenback",   # 2026-07-14: '66-67 Marshall Super 100 4x12s ran G12M Greenbacks (V30s didn't exist until '86)
    cab={"micpos":0.30,"micdist":0.25,"lowcut":80,"highcut":8800},
    rv={"enable":1,"decay":1.3,"mix":0.08}),
  preset(5, 1, "Hazy Solo", out_level=OUT,               # Jimi Hendrix — Purple Haze SOLO
    # Octavia octave-up fuzz + a parked (cocked) wah into a plexi — the ring-modulated
    # octave-fuzz lead. (Mauve Haze, two slots up, is the germanium Fuzz Face riff tone.)
    # un-starved 2026-07-11: the gate (in FRONT of the fuzz) was choking the Octavia's sustain, and the amp
    # was too clean behind it. Opened the gate (thresh -52->-64, longer tail), +Octavia sustain 0.62->0.76,
    # +amp gain 0.35->0.46 so the plexi adds body/sustain under the octave-fuzz.
    wh={"enable":1,"pos":2,"type":"Fixed","freq":0.55,"q":0.55,"mix":0.42},  # cocked-wah honk (tamed)
    fz={"enable":1,"pedal":"Octavius","sustain":0.76,"tone":0.5,"volume":0.5},  # Octavius is near-0dBFS; keep it near guitar level ("Octavia" renamed in the de-trademark pass)
    # 2026-07-14 "starved Octavia" fix (user): the real rig was the Octavia into a DIMED, JUMPERED Super
    # Lead — the fat horn sustain is the cranked power amp + Normal channel, not the pedal. Vol I 0.62 +
    # Vol II 0.55 + master 0.75 + sag up = the roaring platform the octave rings over.
    amp={"model":"Plexiglass","gain":0.62,"pl_vol2":0.55,"bass":0.5,"mid":0.6,"treble":0.5,"presence":0.52,"master":0.75,"sag":0.45},
    gt={"enable":1,"thresh":-64,"attack":2,"hold":200,"release":420,"hyst":8},
    dl={"enable":1,"type":"Seraph","time":400,"feedback":0.3,"mix":0.15,"width":0.6,"pattern":"Dotted 8th","ducking":0.2,"moddepth":0.15,"modrate":0.3},
    rv={"enable":1,"decay":1.5,"mix":0.1},
    cab_ir="@greenback",   # 2026-07-14: same '67 Marshall/G12M stack as Mauve Haze
    cab={"micpos":0.30,"micdist":0.25,"lowcut":80,"highcut":9000}),
  preset(5, 2, "Little Feather", out_level=OUT,          # Jimi Hendrix — Little Wing
    # Strat neck, warm edge-of-breakup, rotary-speaker swirl (Little Wing was tracked through a
    # rotating cab). Now on the Marshall PLEXI (Hendrix's amp) at low gain/master for a warm,
    # midrange-forward clean rather than the previous Fender voicing.
    amp={"model":"Plexiglass","gain":0.32,"pl_vol2":0.35,"bass":0.55,"mid":0.55,"treble":0.48,"presence":0.45,"master":0.52,"sag":0.4},   # 2026-07-14: gentle Vol II warmth (jumpered clean)
    cp={"enable":1,"type":1,"ratio":1,"thresh":-22,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Rotary","rate":0.3,"depth":0.5,"mix":0.5,"width":0.7},
    rv={"enable":1,"predelay":15,"decay":2.2,"mix":0.18},
    cab={"micpos":0.30,"micdist":0.20,"lowcut":82,"highcut":10000}),
  preset(5, 3, "Holy Smoke", out_level=OUT,              # Sleep — Dragonaut / Holy Mountain
    # RE-RESEARCHED 2026-07-14 (Matt Pike, Premier Guitar): NO fuzz pedal in the Sleep rig — the Holy
    # Mountain "chainsaw" sustain was a Soldano GTO tube overdrive slamming daisy-chained cranked amps.
    # Muff dropped; TS-style OD (GTO stand-in) with REAL drive into a much hotter, saggier Orange — the
    # squishy compression of chained small amps is part of the documented sound. Gate to the -52 floor
    # now it's a dirty-non-fuzz preset.
    dr={"enable":1,"model":"Green Man","drive":0.35,"tone":0.5,"level":0.7,"mix":1.0},
    cp={"enable":1,"type":0,"ratio":1,"thresh":-22,"makeup":2},
    amp={"model":"Tangerang","gain":0.68,"bass":0.6,"mid":0.55,"treble":0.45,"presence":0.4,"master":0.5,"sag":0.6},
    gt={"enable":1,"thresh":-52,"attack":3,"hold":200,"release":400,"hyst":8},
    rv={"enable":1,"predelay":30,"decay":3.2,"damping":0.5,"mix":0.18},          # cavernous doom space
    cab={"micpos":0.35,"micdist":0.30,"lowcut":72,"highcut":7000}),
)

# ── Bank 7 — MASTODON · Crack the Skye (+ Electric Wizard, doom singleton) ────
add(
  preset(6, 0, "Skye Crusher", out_level=OUT,            # Mastodon — Crack the Skye rhythm (JCM800 + OD808)
    # RESEARCHED (Kelliher): JCM800 2203 — "bass almost all the way up, mids in the middle, treble 3-4
    # o'clock, DO NOT scoop mids" + TS9/OD808 boost (drive~0, level high) into a Greenback 1960B.
    # revoiced 2026-07-11 (post input-ceiling removal + TS808 re-voice): TS level 0.9->0.74 (near-unity boost;
    # the amp no longer needs the extra push now the input clip is gone) + treble/presence + cab high-cut
    # eased to tame the restored top end (was fizzy/too-gainy).
    # 2026-07-14 user feedback: "needs more gain" — amp gain 0.45->0.62 + TS level 0.74->0.8 (still Kelliher's
    # drive~0/level-high OD808 recipe, just pushing the 2203 harder). THEN "way too much bass" at the new gain —
    # bass 0.78->0.48 (user: "4.5 or 5"; Kelliher's bass-nearly-max quote doesn't survive this gain on this rig).
    amp={"model":"Crunchy","gain":0.62,"bass":0.48,"mid":0.55,"treble":0.66,"presence":0.44,"master":0.45},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.55,"level":0.8,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":120,"release":260,"hyst":8},
    md={"enable":1,"type":"Phaser","rate":0.2,"depth":0.45,"mix":0.2},   # Phase 90 subtle, behind the mix
    rv={"enable":1,"decay":1.6,"mix":0.1},
    cab_ir="@factory",   # 2026-07-14: Kelliher's Crack the Skye cabs = Mills 4x12s with V30s, not Greenbacks
    cab={"lowcut":85,"highcut":7800}),
  preset(6, 1, "Skye (No Mod)", out_level=OUT,           # Mastodon — Crack the Skye rhythm, phaser removed
    # = Skye Crusher (JCM800 + OD808) with the MXR Phase 90 dropped. Revoiced 2026-07-11 as Skye Crusher.
    amp={"model":"Crunchy","gain":0.62,"bass":0.48,"mid":0.55,"treble":0.66,"presence":0.44,"master":0.45},   # 2026-07-14: +gain, then bass 0.78->0.48 (user), as Skye Crusher
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.55,"level":0.8,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":120,"release":260,"hyst":8},
    rv={"enable":1,"decay":1.6,"mix":0.1},
    cab_ir="@factory",   # 2026-07-14: same Mills/V30 studio cabs as Skye Crusher
    cab={"lowcut":85,"highcut":7800}),
  preset(6, 2, "Skye Soar", out_level=OUT,               # Mastodon — Crack the Skye lead (JCM800 + boost + octave + delay)
    # RESEARCHED (Kelliher EQ + Hinds POG2 on "The Czar"): JCM800 bass-up/mids-mid/treble-up + TS boost,
    # octave-up shimmer in front, RE-20/DD-6 dual delay. Greenback 1960B.
    # revoiced 2026-07-11: TS level 0.85->0.74, treble/presence + high-cut eased (restored top end).
    amp={"model":"Crunchy","gain":0.7,"bass":0.75,"mid":0.55,"treble":0.66,"presence":0.48,"master":0.46},   # 2026-07-14: +gain (user), lead pushes past the rhythm
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.55,"level":0.8,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":120,"release":260,"hyst":8},
    oc={"enable":1,"pos":2,"up":0.35,"down":0.0,"dry":1.0},   # "The Czar" POG octave-up, in front of the amp
    dl={"enable":1,"type":"Seraph","time":420,"feedback":0.32,"mix":0.16,"width":0.6,"pattern":"Dotted 8th","ducking":0.25,"moddepth":0.15,"modrate":0.3},
    rv={"enable":1,"decay":1.7,"mix":0.1},
    cab_ir="@factory",   # 2026-07-14: shared Crack the Skye V30 platform
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
    cab={"micpos":0.40,"micdist":0.35,"lowcut":66,"highcut":6500},
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
    cab={"micpos":0.10,"micdist":0.00,"lowcut":86,"highcut":8800}),
  preset(7, 1, "Dreamlit Shimmer", out_level=OUT,        # A Perfect Circle — dreamy chorusy clean
    # Howerdel the "effects guy": lush chorus + ambient delay + big reverb on a clean amp.
    # RESEARCHED: Howerdel is "an effects guy" — clean Gibson GA-15RV/Vox, warm (presence rolled off),
    # wide CE-1/Lexicon chorus, tape/DL4 delay, huge Strymon/Lexicon hall (The Hollow tails).
    amp={"model":"Clean Meanie","gain":0.2,"bass":0.5,"mid":0.45,"treble":0.55,"presence":0.4,"master":0.75,"sag":0.55},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-24,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Lush-2","rate":0.22,"depth":0.55,"mix":0.5,"width":0.9},
    dl={"enable":1,"type":"Tape","time":480,"feedback":0.38,"mix":0.28,"width":0.8,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"predelay":35,"decay":3.2,"damping":0.4,"mix":0.38},
    cab={"micpos":0.30,"micdist":0.25,"lowcut":80,"highcut":12000}),
  preset(7, 2, "Flatliner", out_level=OUT,               # Periphery — Flatline
    # RESEARCHED (Misha Mansoors own Fractal Tone Tour): PVH 6160 Block = 5150; Drive 6.5/Bass 5/Mid 5/
    # Treble 6/Pres 5/Master 4, minimal sag. TS808 as a CLEAN boost (drive 0, level max). Drop Ab 7-str.
    # revoiced 2026-07-11: TS was a UNITY-level boost (1.0) slamming an untamed front-end (input-clip removed)
    # -> too gainy/fizzy. TS level 1.0->0.72 (still a clean tightening boost, not a gain wall) + treble/presence
    # + high-cut eased. Amp gain kept (the 5150 grind IS the tone; the fix was the boost + brightness).
    # DJENT RETUNE 2026-07-14 (user: "too much bass, not enough mid/treble-driven clean gain" + research —
    # Misha's Precision Drive exists to CUT BASS INTO THE AMP; his rhythm voicing runs "less deep bass than
    # expected", saturation-not-mush, definition from mids/treble/presence): bass 0.5->0.25, mid 0.5->0.62,
    # treble 0.54->0.62, presence 0.46->0.55, gain 0.62->0.58 (clarity carries the perceived gain), TS tone
    # up 0.5->0.7 (the PD-style bright tightener), cab low-cut 85->100 (7-string sub-flub is bass-guitar
    # territory on the records).
    amp={"model":"Gainzilla","gain":0.58,"bass":0.25,"mid":0.62,"treble":0.62,"presence":0.55,"master":0.4,"channel":1,"resonance":0.5,"sag":0.2},
    dr={"enable":1,"model":"Green Man","drive":0.0,"tone":0.7,"level":0.72,"mix":1.0},
    gt={"enable":1,"thresh":-42,"attack":0.5,"hold":90,"release":200,"hyst":8},
    # EQ intent 2026-07-24 (djent end-chain polish — Misha's own mix moves): -2 @100 / -1 @200 =
    # the post-cab tightness the Precision-Drive philosophy demands (the amp already got the
    # pre-cut), +0.5 @800 / +1 @1.6k note definition, +1 @3.2k pick articulation. 400 -1 only —
    # the amp scoop already lives there.
    eq={"enable":1,"pos":13,"100":-2,"200":-1,"400":-1,"800":0.5,"1k6":1,"3k2":1},
    cab={"lowcut":100,"highcut":7300}),
  preset(7, 3, "Prayer Djent", out_level=OUT,            # Periphery — Prayer Position
    # RESEARCHED: same PVH 6160 core as Flatliner, slightly more bass/mid for chug weight, EVEN
    # tighter (3 gates live), G# standard 7-str.
    # revoiced 2026-07-11: as Flatliner — TS level 1.0->0.72, gain eased, treble/presence + high-cut tamed.
    # DJENT RETUNE 2026-07-14 (as Flatliner; this one EVEN tighter — G# 7-string chug): bass 0.55->0.22,
    # mid 0.52->0.65, treble 0.52->0.6, presence 0.44->0.52, gain 0.62->0.58, TS tone 0.48->0.72,
    # low-cut 90->105.
    amp={"model":"Gainzilla","gain":0.58,"bass":0.22,"mid":0.65,"treble":0.6,"presence":0.52,"master":0.42,"channel":1,"resonance":0.5,"sag":0.18},
    dr={"enable":1,"model":"Green Man","drive":0.0,"tone":0.72,"level":0.72,"mix":1.0},
    gt={"enable":1,"thresh":-40,"attack":0.5,"hold":80,"release":180,"hyst":8},
    # EQ intent 2026-07-24 (as Flatliner but chuggier): -2 @100 / -1 @200 post-cab tightness,
    # +1 @800 = the chug KNOCK this preset leans on harder than Flatliner, +1 @1.6k definition,
    # +0.5 @3.2k only (even tighter top than Flatliner — 3 gates live, zero fizz budget).
    eq={"enable":1,"pos":13,"100":-2,"200":-1,"400":-1,"800":1,"1k6":1,"3k2":0.5},
    cab={"lowcut":105,"highcut":7200}),
)

# ── Bank 9 (index 8) — VIBE & WAVE (Trower / Police / Cure / Chic) ────────────
# Fun, distinctive tones that lean on the suite's oddball gear (Uni-Vibe, chorus,
# flanger, comp) — deliberately NOT the overused modeler defaults.
add(
  preset(8, 0, "Bridge Vibe", out_level=-9.0,            # Robin Trower — Bridge of Sighs
    # RESEARCHED: cranked Marshall JMP-100 (NOT Hiwatt) + Greenback 4x12, Dan Armstrong treble
    # boost driving the input, SLOW liquid Uni-Vibe. Neve studio comp = the thick sustain.
    dr={"enable":1,"model":"Gilded Horse","drive":0.22,"tone":0.6,"level":0.7,"mix":1.0},
    # 2026-07-14 research (documented Trower JMP settings): presence 0, treble 2-3, bass 2-3 — thickness
    # comes from mids + the Vibe, NOT the tone stack. Greenback 1960B pinned (was falling back to V30).
    amp={"model":"Crunchy","gain":0.6,"bass":0.3,"mid":0.55,"treble":0.35,"presence":0.1,"master":0.8,"sag":0.7},
    cp={"enable":1,"type":0,"ratio":1,"thresh":-20,"makeup":1},
    md={"enable":1,"pos":3,"type":"Uni-Verse","rate":0.28,"depth":0.72,"mix":0.62,"width":0.5},   # Trower ran the Vibe FIRST — pre-boost, pre-amp (pos 3 < drive 4 < amp 5); chorus-blend mix
    gt={"enable":1,"thresh":-50,"attack":2,"hold":150,"release":320,"hyst":8},   # was the -60 default: never closed against the measured rig floor (cranked-JMP preset hummed at rest)
    rv={"enable":1,"predelay":15,"decay":1.8,"damping":0.6,"mix":0.22},
    cab_ir="@greenback",
    cab={"micpos":0.30,"micdist":0.20,"lowcut":80,"highcut":9000}),
  preset(8, 1, "Bottle Jangle", out_level=OUT,           # The Police / Andy Summers — Message in a Bottle
    # RE-RESEARCHED 2026-07-14: by Reggatta de Blanc Summers' platform was a Marshall 1959 Super Lead
    # (clean-to-edge, documented settings pres 3 / bass 4 / mid 3 / treble 6 / vol 5) into 1960A Greenbacks,
    # not the earlier Twin. Echoplex documented at ~330 ms single repeat, roughly 50/50 with the dry —
    # not a 120 ms slapback. Dyna Comp + Electric Mistress kept.
    amp={"model":"Plexiglass","gain":0.45,"pl_vol2":0.3,"bass":0.4,"mid":0.3,"treble":0.65,"presence":0.55,"master":0.6},   # 2026-07-14: light jumper body under the bright chime
    cp={"enable":1,"type":1,"ratio":1,"thresh":-22,"attack":3,"release":5,"knee":3,"makeup":4},
    md={"enable":1,"type":"Flanger","rate":0.2,"depth":0.35,"mix":0.7,"width":0.5},
    dl={"enable":1,"type":"Tape","time":330,"feedback":0.18,"mix":0.38,"width":0.6,"wow":0.003,"flutter":0.001},
    rv={"enable":1,"predelay":8,"decay":1.4,"damping":0.45,"mix":0.2},
    cab_ir="@greenback",
    cab={"micpos":0.25,"micdist":0.10,"lowcut":85,"highcut":11000}),
  preset(8, 2, "Forest Wash", out_level=OUT,             # The Cure — A Forest
    # RESEARCHED (Mike Hedges): Roland JC-120 dead clean (no sag) + Jazzmaster, SLOW deep flanger
    # (7 env-flangers, approximated static), tape-machine delay building across the song, cavernous verb.
    amp={"model":"Clean Meanie","gain":0.2,"bass":0.5,"mid":0.6,"treble":0.5,"presence":0.4,"master":0.55,"sag":0.1},
    md={"enable":1,"type":"Flanger","rate":0.08,"depth":0.65,"mix":0.55,"width":0.9},
    dl={"enable":1,"type":"Tape","time":480,"feedback":0.45,"mix":0.32,"width":0.7,"wow":0.004,"flutter":0.002},
    rv={"enable":1,"predelay":20,"decay":3.2,"damping":0.55,"mix":0.32},
    cab_ir="@american-ob",   # 2026-07-14: A Forest = close-miked Roland JC-120 2x12 — open-back, not a V30 4x12
    cab={"micpos":0.25,"micdist":0.15,"lowcut":90,"highcut":9500}),
  preset(8, 3, "Disco Chuck", out_level=OUT,             # Nile Rodgers / Chic — funk chucking
    # RESEARCHED (Le Freak, Bob Clearmountain): Strat NECK single-coil + DI/Deluxe blend, console
    # EQ = HPF 160Hz + 12k air + 4.8k mid. Bone-dry, BRIGHT, hard comp glue, gate for tight chucks.
    amp={"model":"Clean Meanie","gain":0.25,"bass":0.35,"mid":0.6,"treble":0.75,"presence":0.65,"master":0.55},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-26,"attack":1,"release":4,"knee":2,"makeup":5},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":60,"release":120,"hyst":8},
    cab_ir="@american-ob",   # 2026-07-14: the miked half of Le Freak was a Deluxe Reverb 1x12 open-back
    cab={"micpos":0.20,"micdist":0.15,"lowcut":150,"highcut":12000}),   # lowcut 110->150: Clearmountain's Neve HPF ~160 Hz (bone-dry chuck)
)

# ── Bank 10 (index 9) — TWANG & FUZZ (Dick Dale / Shadows / QOTSA / Ronson) ───
add(
  preset(9, 0, "Surf Splash", out_level=OUT,             # Dick Dale — Misirlou (out locked via MANUAL_OUT)
    # USER DIAL-IN BAKED VERBATIM (2026-07-24, read from the device .dat — USER-PRESERVED, do not
    # retune): after the v61 recipe retune the user dialed their own surf in on the device. Theirs is
    # CLEANER (gain .165) and THICKER (bass .615, mid .495 — no scoop after all), with a slow DEEP
    # TREMOLO (rate .13, depth .71, mix .46 — Misirlou has none, but their surf does) and a LONGER,
    # darker spring (decay 4.3, damping .37, mix .42, predelay 11.5). Comp OFF (bypassed + parked).
    # Chain REORDERED: Gate -> Amp -> Cab -> Trem -> Spring (fz/dr/eq/dl parked disabled at 7-10).
    cp={"enable":0,"bypass":1,"pos":6,"type":1,"ratio":1,"thresh":-42,"attack":0.7,"release":2.2,"knee":3,"makeup":3},
    fz={"pos":7}, dr={"pos":8}, eq={"pos":9}, dl={"pos":10}, wh={"pos":11}, oc={"pos":12}, nail={"pos":13},
    amp={"pos":2,"model":"Clean Meanie","gain":0.165,"bass":0.615,"mid":0.495,"treble":0.6325,"presence":0.5075,"master":0.8575,"sag":0.2},
    cab={"pos":3,"micpos":0.15,"micdist":0.35,"lowcut":85,"highcut":10800},
    md={"pos":4,"enable":1,"type":"Tremolo","rate":0.1325,"depth":0.7075,"mix":0.4625},
    rv={"pos":5,"enable":1,"predelay":11.5,"decay":4.30675,"damping":0.37125,"mix":0.4175},
    gt={"enable":1,"thresh":-45,"attack":0.5,"hold":40,"release":90,"hyst":8},
    cab_ir="@american-ob"),
  preset(9, 1, "Apache Echo", out_level=OUT,             # The Shadows / Hank Marvin — Apache
    # RESEARCHED: Vox AC15 (Chime Thirty) + Meazzi/Binson multi-head echo ~130ms (3 taps swelling).
    # NO tremolo — Marvins "vibrato" is the Strat vibrato ARM. Clean bell-like twang, alnico cab.
    amp={"model":"Chime Thirty","gain":0.2,"bass":0.4,"mid":0.55,"treble":0.7,"presence":0.6,"master":0.65,"sag":0.55},
    dl={"enable":1,"type":"Echo Wreck","time":130,"feedback":0.35,"mix":0.4,"width":0.0,"heads":10,"wow":0.002,"flutter":0.001},
    rv={"enable":1,"predelay":8,"decay":1.4,"damping":0.55,"mix":0.16},
    cab={"micpos":0.20,"micdist":0.50,"lowcut":82,"highcut":9500}),
  preset(9, 2, "Desert Robot", out_level=-15.5,          # QOTSA — No One Knows
    # RESEARCHED (Eric Valentine): Hommes "No One Knows" dirt was a tiny SOLID-STATE Peavey Decade
    # driven into saturation — NOT a Big Muff. Backline Plus (Peavey Backstage+) pushed harder now +
    # a BOSS SD-1 (Super Nova) overdrive out front for the extra gain/grit the tone was missing (user
    # request). SD-1 drive up, amp gain up = mid-forward, tight, saturated desert grind.
    dr={"enable":1,"model":"Super Nova","drive":0.65,"tone":0.5,"level":0.72,"mix":1.0},
    amp={"model":"Backline Plus","gain":0.62,"bass":0.58,"mid":0.82,"treble":0.68,"presence":0.5,"master":0.7},
    gt={"enable":1,"thresh":-52,"attack":1.5,"hold":100,"release":220,"hyst":8},
    rv={"enable":1,"decay":0.9,"damping":0.7,"mix":0.07},
    cab={"micpos":0.20,"micdist":0.10,"lowcut":82,"highcut":8500}),
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
    # RE-RESEARCHED 2026-07-14: Parker's Innerspeaker fuzz was a vintage 70s GERMANIUM FUZZ FACE — the
    # germanium Tone Bender MkII (I Know It, same circuit family, mid-forward) is the right stand-in, not a
    # mid-scooped Muff. Sustain eased (the MkII has an extra gain stage over a Fuzz Face) + healthy bias 0.6
    # for the warm non-sputtery response. Mid-forward AC30 + slow Small Stone + baked-in verb kept. Strat.
    fz={"enable":1,"pedal":"I Know It","sustain":0.6,"tone":0.45,"volume":0.42,"bias":0.6},
    amp={"model":"Chime Thirty","gain":0.6,"bass":0.45,"mid":0.7,"treble":0.55,"presence":0.5,"master":0.75,"sag":0.6},
    md={"enable":1,"type":"Phaser","rate":0.25,"depth":0.6,"mix":0.5},
    gt={"enable":1,"thresh":-52,"attack":2,"hold":150,"release":300,"hyst":8},
    rv={"enable":1,"predelay":15,"decay":1.6,"damping":0.4,"mix":0.28},
    cab={"micpos":0.30,"micdist":0.35,"lowcut":80,"highcut":9000}),
  preset(10, 1, "Glide Wall", out_level=-16.0,           # My Bloody Valentine — shoegaze glide
    # RESEARCHED (Kevin Shields/Loveless): raw open fuzz (Muff Delta, dark) into a CRANKED Vox AC30,
    # tremolo (dual-amp trem character), huge near-reverse reverb wall (0 predelay, long, very wet). Jazzmaster glide.
    # 2026-07-14: reverb predelay 15->0 — the SPX90 reverse-reverb wall arrives WITH the dry; any gap is inauthentic.
    fz={"enable":1,"pedal":"Italian Hero","mode":"Delta","sustain":0.82,"tone":0.4,"volume":0.5},
    amp={"model":"Chime Thirty","gain":0.72,"bass":0.5,"mid":0.55,"treble":0.5,"presence":0.4,"master":0.85,"sag":0.7},
    md={"enable":1,"type":"Tremolo","rate":0.52,"depth":0.7,"mix":0.9},
    gt={"enable":1,"thresh":-54,"attack":2,"hold":180,"release":350,"hyst":8},
    rv={"enable":1,"predelay":0,"decay":3.4,"damping":0.35,"mix":0.32},
    cab={"micpos":0.30,"micdist":0.20,"lowcut":82,"highcut":9000}),
  preset(10, 2, "Streets Chime", out_level=OUT,          # The Edge / U2 — dotted-eighth chime (Vox)
    # RESEARCHED: SDD-3000 was a DUAL delay (dotted-8th ~350ms + secondary tap, panned L/R) — the
    # Seraph dual-delay nails this. Pattern "Dotted 8th" @129BPM, 2-3 clean repeats, wide stereo,
    # light ducking so the attack stays clear + a touch of mod for shimmer. Vox AC30 edge-of-breakup.
    amp={"model":"Chime Thirty","gain":0.55,"bass":0.4,"mid":0.55,"treble":0.7,"presence":0.6,"master":0.8,"sag":0.5},
    cp={"enable":1,"type":1,"ratio":1,"thresh":-22,"attack":3,"release":5,"knee":3,"makeup":3},
    gt={"enable":1,"thresh":-52,"attack":2,"hold":150,"release":320,"hyst":8},   # was the -60 default (open vs the measured floor); the SDD delay is post-gate so repeats still ring after it closes
    dl={"enable":1,"type":"Seraph","time":359,"feedback":0.25,"mix":0.28,"width":0.9,"pattern":"Dotted 8th","ducking":0.15,"moddepth":0.1,"modrate":0.25},   # 359 ms = the measured 3/16 echo at the song's actual 125.5 BPM (amnesta.net isolated-track study)
    rv={"enable":1,"predelay":20,"decay":1.2,"damping":0.5,"mix":0.14},
    cab={"micpos":0.15,"micdist":0.10,"lowcut":85,"highcut":11000}),
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
    # 2026-07-14: master 0.8->0.55 — Reznor tracked TDS guitars DIRECT via a JMP-1 preamp into the Zoom
    # 9030 speaker sim (Guitar World 4/94): no cranked power amp on the record, the Nail IS the voice.
    amp={"model":"Crunchy McCrunchFace","gain":0.35,"bass":0.5,"mid":0.5,"treble":0.6,"presence":0.6,"master":0.55,"sag":0.3},
    gt={"enable":1,"thresh":-52,"attack":1.2,"hold":90,"release":180,"hyst":8},
    # 2026-07-24 user's END-OF-CHAIN TDS mix-EQ (eq block, pos 13 = after everything): enforce the
    # band-passed mechanical midrange — deep low/low-mid cuts + a 1.6k push. The user's 7-band table
    # also had -4 dB @6.4k; the 6-band eq has no 6.4k band, so that's folded into cab highcut 7800->6800.
    eq={"enable":1,"pos":13,"100":-6,"200":-4,"400":-6,"800":-2,"1k6":4,"3k2":1},
    cab_ir="@greenback",
    cab={"lowcut":95,"highcut":6800}),
  preset(11, 1, "World Went Away", out_level=OUT,       # The Day the World Went Away — The Fragile (1999)
    # The Fragile's crushing wall: fat, saturated, Muff-leaning distortion. Nail "Delicate"
    # (Swollen-Pickle-ish: fat lows, gentle scoop) into a thick amp + the big dark Doom cab with
    # atmospheric reverb. The loud album guitar (not the acoustic "quiet" intro).
    nail={"enable":1,"pos":4,"mode":"Delicate","drive":0.7,"tone":0.42,"texture":0.5,"level":0.5},
    amp={"model":"Crunchy McCrunchFace","gain":0.3,"bass":0.6,"mid":0.4,"treble":0.5,"presence":0.45,"master":0.7,"sag":0.4},
    rv={"enable":1,"predelay":14,"decay":2.2,"damping":0.45,"mix":0.22},
    gt={"enable":1,"thresh":-58,"attack":2,"hold":160,"release":320,"hyst":8},
    # 2026-07-24 user's END-OF-CHAIN Fragile mix-EQ (eq pos 13): clean the mud, keep the wall wide —
    # 200 warmth up, 400/800 mud out, gentle air. +2 dB @6.4k (no band) -> cab highcut 7000->7800.
    eq={"enable":1,"pos":13,"100":-1,"200":2,"400":-4,"800":-2,"1k6":1,"3k2":2},
    cab_ir="@doom",
    cab={"lowcut":80,"highcut":7800}),
  preset(11, 2, "Broken Crush", out_level=OUT,          # Broken EP — Wish / Happiness in Slavery (1992)
    # The angriest NIN: harsh, lo-fi DIGITAL distortion. Nail "Broke" mode (hard-clip -> sample-rate
    # decimation + bit-crush) into a clean/tight solid-state platform so the digital grit stays raw;
    # brutal fast gate for the machine-gun rhythm.
    nail={"enable":1,"pos":4,"mode":"Broke","drive":0.75,"tone":0.55,"texture":0.5,"level":0.32},
    amp={"model":"Backline Plus","gain":0.25,"bass":0.5,"mid":0.6,"treble":0.6,"presence":0.55,"master":0.55,"sag":0.3},
    gt={"enable":1,"thresh":-50,"attack":1.0,"hold":70,"release":150,"hyst":8},
    # 2026-07-24 user's END-OF-CHAIN Broken mix-EQ (eq pos 13): reduce mud + fizz, push the 1.6-3.2k
    # snarl — the grit is already in the Nail. +1 dB @6.4k (no band) -> cab highcut 8500->9000.
    eq={"enable":1,"pos":13,"100":-4,"200":-1,"400":-5,"800":-3,"1k6":2,"3k2":3},
    cab_ir="@american-ob",
    cab={"lowcut":90,"highcut":9000}),
  preset(11, 3, "Con Molars", out_level=OUT,            # With Teeth — The Hand That Feeds / Only (2005)
    # The 2005 return to a raw, direct live-band rock tone. Nail "Con Molars" (bright aggressive
    # clip -> speaker/cab voicing: low-cut + mid push) into a real cranked Marshall for the
    # in-the-room grind. Tighter, less scooped than the '90s eras.
    nail={"enable":1,"pos":4,"mode":"Con Molars","drive":0.7,"tone":0.55,"texture":0.5,"level":0.58},
    amp={"model":"Crunchy McCrunchFace","gain":0.5,"bass":0.5,"mid":0.58,"treble":0.58,"presence":0.55,"master":0.82,"sag":0.35},
    gt={"enable":1,"thresh":-48,"attack":1.5,"hold":120,"release":220,"hyst":8},   # raised -54->-48: couldn't close vs the measured floor, and NIN staccato wants a tight gate anyway
    # 2026-07-24 user's END-OF-CHAIN With-Teeth mix-EQ (eq pos 13): tighten lows, push 800 Hz forward
    # (modern-rock mid punch) without adding harshness. 6.4k = 0 in the user's table -> highcut untouched.
    eq={"enable":1,"pos":13,"100":-3,"200":1,"400":-3,"800":3,"1k6":2,"3k2":2},
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
    cab={"micpos":0.20,"micdist":0.15,"lowcut":85,"highcut":9500}),
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
    cab={"micpos":0.15,"micdist":0.10,"lowcut":80,"highcut":10500}),
  preset(12, 3, "Castaway Groove", out_level=-17.0,   # Gojira — "Stranded"-style groovy main-riff RHYTHM
    # EVH 5150 III (Gainzilla), red channel — gain for the groovy main riff; tight gate.
    # 2026-07-14 user feedback: "too much bass" — bass 0.55->0.35 + low-cut 84->92 (tight modern Gojira chug).
    amp={"model":"Gainzilla","gain":0.6,"bass":0.35,"mid":0.48,"treble":0.54,"presence":0.48,"master":0.42,"channel":1,"resonance":0.5,"sag":0.18},
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.48,"level":0.74,"mix":1.0},
    gt={"enable":1,"thresh":-44,"attack":0.5,"hold":80,"release":170,"hyst":8},
    rv={"enable":1,"decay":1.4,"mix":0.07},
    cab_ir="@factory",
    cab={"lowcut":92,"highcut":7300}),
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
    # CALI V DOCTRINE PASS 2026-07-14 (user's confirmed recipe, from their Cardinal Rhythm dial-in): the Mark
    # works like an OVERDRIVE — much less gain, near-zero Bass, Treble backed off, brightness regained in the
    # post-gain GEQ. No boost here (Hetfield ran the IIC+ straight in), so gain sits higher than Cardinal's 0.15.
    amp={"model":"Cali V","mv_mode":"IIC+","gain":0.5,"bass":0.05,"mid":0.35,"treble":0.40,"presence":0.6,"master":0.6,"sag":0.32,
         "mv_geq0":0.542,"mv_geq1":0.5,"mv_geq2":0.167,"mv_geq3":0.583,"mv_geq4":0.78},
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
    # CALI V DOCTRINE PASS 2026-07-14 (see Cardinal Rhythm): less gain (TS level-boost carries the push),
    # near-zero Bass, Treble back, brightness up at the 6.6 kHz GEQ.
    amp={"model":"Cali V","mv_mode":"Mk IV","gain":0.35,"bass":0.08,"mid":0.5,"treble":0.40,"presence":0.55,"master":0.55,"sag":0.3,
         "mv_geq0":0.583,"mv_geq1":0.5,"mv_geq2":0.375,"mv_geq3":0.583,"mv_geq4":0.72},
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
    # CALI V DOCTRINE PASS 2026-07-14 (see Cardinal Rhythm): less gain (the Klon boost + mode gain carries the
    # singing sustain), near-zero Bass, Treble back, brightness regained at the GEQ. Lead keeps a bit more gain.
    amp={"model":"Cali V","mv_mode":"Mk IV","gain":0.42,"bass":0.10,"mid":0.6,"treble":0.45,"presence":0.6,"master":0.6,"sag":0.35,
         "mv_geq0":0.458,"mv_geq1":0.542,"mv_geq2":0.625,"mv_geq3":0.583,"mv_geq4":0.66},
    dr={"enable":1,"model":"Gilded Horse","drive":0.2,"tone":0.6,"level":0.7,"mix":1.0},
    # gate -52->-48 (2026-07-14): the user's measured idle floor peaks at -45 dBFS hands-off / -58 hands-on;
    # -52 (close -56) could never close hands-off even with the hum comb. -48 closes reliably, tails keep 12+ s.
    gt={"enable":1,"thresh":-48,"attack":1,"hold":130,"release":280,"hyst":8},
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
    cab={"micpos":0.30,"micdist":0.20,"lowcut":85,"highcut":9500}),
  preset(10, 3, "Regal Solo", out_level=-13.5,   # Brian May / Queen — the singing harmonised LEAD [packed into bank 11/D to fill the gap]
    # As Regal Sustain + a tape delay for the layered-solo feel. Input-Trim clean boost (it_boost) ON.
    it={"boost":1,"boostamt":6},
    dr={"enable":1,"model":"Green Man","drive":0.2,"tone":0.82,"level":0.95,"mix":1.0},
    amp={"model":"Chime Thirty","gain":0.88,"bass":0.5,"mid":0.62,"treble":0.8,"presence":0.6,"master":0.92,"sag":0.8},
    gt={"enable":1,"thresh":-50,"attack":2,"hold":160,"release":320,"hyst":8},   # -54 couldn't close vs the measured floor; -50 still keeps May's long sustain (close -54, tails ring to it)
    dl={"enable":1,"type":"Tape","time":800,"feedback":0.34,"mix":0.2,"width":0.4,"wow":0.003,"flutter":0.001},   # 2026-07-14: May's layered-lead Echoplex canon is ~800 ms (Brighton Rock), not a 310 ms slapback
    rv={"enable":1,"predelay":15,"decay":1.4,"damping":0.45,"mix":0.18},
    cab_ir="@vox2x12",
    cab={"micpos":0.30,"micdist":0.20,"lowcut":85,"highcut":9500}),
  preset(13, 3, "Grunge Drop", out_level=-15.0,   # Soundgarden / Alice in Chains — thick detuned mid-gain + cocked wah [packed into bank 14/D to fill the gap]
    # Detuned, thick mid-gain (cranked Marshall) with a parked/cocked wah for the nasal honk; heavy, not scooped.
    amp={"model":"Crunchy","gain":0.52,"bass":0.6,"mid":0.6,"treble":0.5,"presence":0.45,"master":0.55},
    wh={"enable":1,"pos":2,"type":"Fixed","freq":0.5,"q":0.55,"mix":0.35},
    dr={"enable":1,"model":"Green Man","drive":0.12,"tone":0.5,"level":0.7,"mix":1.0},
    gt={"enable":1,"thresh":-50,"attack":1,"hold":120,"release":250,"hyst":8},
    rv={"enable":1,"decay":1.4,"mix":0.1},
    cab_ir="@greenback",
    cab={"micpos":0.20,"micdist":0.10,"lowcut":76,"highcut":8000}),
)

# ── Bank 15 (index 14) slot C — MAROMARO · Retro Poland (2026-07-23, user request) ──
# Polish guitarist maromaro (Ibanez artist — hence the Iceman; ex-Czerwone Gitary lead):
# the viral "Retro Poland" covers of 90s hits — post-punk 80s/90s nostalgia. The sound:
# a compressed clean at the EDGE of break-up with a deep SEASICK chorus. Packed into the
# Muse bank's free C slot per the no-blanks rule ("fill the banks with what I have").
add(
  preset(14, 2, "Retro Poland", out_level=-9.9,   # user dialed -9.6; Dense tank measured -0.6 (-> -9.0), Dense cab room +0.9 (-> -9.9) keeps the user-dialed loudness
    # USER-PRESERVED (2026-07-23): the user's on-device dial-in, read back from the
    # saved store and baked verbatim — do not retune. Their changes vs the shipped
    # cut: drive = Preamp 250 (DOD) at a whisker of drive with mix at half (grit
    # seasoning, not a push), the chain reordered to gate->comp->drive->amp->cab->
    # chorus->delay->reverb (fuzz parked at the back), and a 106 ms tape slap
    # dialed in but left BYPASSED for live A/B.
    it={"gain":-4},
    gt={"thresh":-52,"release":300},
    cp={"enable":1,"pos":2,"type":1,"ratio":1,"thresh":-32,"attack":4,"release":4,"knee":4,"makeup":4},
    dr={"enable":1,"pos":3,"model":"Preamp 250","drive":0.135,"tone":0.55,"level":0.7},   # mix stays at the default 1.0 (full series)
    amp={"pos":4,"model":"Clean Meanie","gain":0.42,"bass":0.4,"mid":0.5,"treble":0.65,"presence":0.4,"master":0.55,"sag":0.4},
    cab_ir="@american-ob",
    cab={"pos":5,"micpos":0.2,"micdist":0.15,"lowcut":85,"highcut":8000},
    md={"enable":1,"pos":6,"type":"Seasick Vibe","rate":0.15,"depth":0.75,"mix":0.68,"width":0.5,"offset":10},
    dl={"enable":1,"pos":7,"type":"Tape","time":105.95,"feedback":0.4655,"mix":0.3,"bypass":1},   # in the chain, BYPASSED (their live A/B)
    rv={"enable":1,"pos":8,"decay":1.1,"damping":0.55,"mix":0.16},
    fz={"pos":9}, eq={"pos":10}, wh={"pos":11}, oc={"pos":12}, nail={"pos":13})
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
    # RESEARCHED 2026-07-14 (thepedallab/musewiki/guitarchalk — Bellamy's documented FF recipe): Drive MAX,
    # Stab MAX, Comp ~9 o'clock (low), Gate down until the noise is acceptable. Probe-verified on the v3
    # model: at Comp 0.3 / Gate 0.42, Stab 1.0 is riff-safe (no standalone squeal) while backing Stab to
    # ~0.55-0.85 gives the continuous splatty scream — the authentic "manipulate Gate+Stab for squeals".
    fz={"enable":1,"pedal":"Fuzz Zachary","sustain":1.0,"bias":0.3,"inputtrim":0.42,"getemp":1.0,"volume":0.55},
    amp={"model":"Crunchy","gain":0.45,"bass":0.45,"mid":0.6,"treble":0.62,"presence":0.55,"master":0.62,"sag":0.3},
    # chain gate OPENED -50->-58 + longer tail (2026-07-14): keyed on raw guitar in FRONT of the fuzz, it
    # closed mid-sustain while the fuzz was still roaring = the "cuts out" complaint (same as the old Octavia
    # choke). The FF's own Gate knob (Tr3 dead-band) is the authentic noise-killer for this chain.
    gt={"enable":1,"thresh":-58,"attack":1,"hold":200,"release":400,"hyst":8},
    rv={"enable":1,"predelay":10,"decay":1.3,"damping":0.5,"mix":0.1},
    cab_ir="@greenback",
    cab={"micpos":0.10,"micdist":0.05,"lowcut":85,"highcut":8500}),
  preset(14, 1, "Cavalier Charge", out_level=-16.5,   # Muse — "Knights of Cydonia"-style galloping fuzz riff + epic lead
    # Bellamy's spaghetti-western epic: the Fizz Factory driving a bright cranked Marshall, dotted-8th delay for
    # the galloping ride, and enough Stab edge for the unhinged solo. RESEARCHED: Knights = Fuzz Factory + big
    # delay (+ a Whammy octave on the solo harmonies, approximated here by the delay/room width). Bright, cutting.
    # Same-era Bellamy FF recipe as Plug-In Junior (Drive high / Comp low / Stab high), a touch smoother
    # for the gallop: Stab 0.8 keeps it quiet at Gate 0.4 (probe-verified), less octave-rasp than PIB's 1.0.
    fz={"enable":1,"pedal":"Fuzz Zachary","sustain":0.95,"bias":0.35,"inputtrim":0.4,"getemp":0.8,"volume":0.55},
    amp={"model":"Crunchy","gain":0.5,"bass":0.45,"mid":0.6,"treble":0.62,"presence":0.55,"master":0.65,"sag":0.3},
    gt={"enable":1,"thresh":-58,"attack":1,"hold":200,"release":400,"hyst":8},   # opened -48->-58 (fuzz-choke fix, see Plug-In Junior)
    # 2026-07-14: delay 400->330 ms (Knights is ~137 BPM; dotted-8th = 328 ms — 400 implied ~112 BPM) and
    # added the missing MXR Phase 90 that's wired into Bellamy's Manson (cited for the KOC lead sweep):
    # subtle, pre-fuzz (pos 3, in-the-guitar), slow-medium.
    md={"enable":1,"pos":3,"type":"Phaser","rate":0.35,"depth":0.5,"mix":0.2},
    dl={"enable":1,"type":"Seraph","time":330,"feedback":0.4,"mix":0.22,"width":0.6,"pattern":"Dotted 8th","ducking":0.2,"moddepth":0.12,"modrate":0.3},
    rv={"enable":1,"predelay":15,"decay":1.6,"damping":0.45,"mix":0.2},
    cab_ir="@greenback",
    cab={"micpos":0.10,"micdist":0.05,"lowcut":85,"highcut":8800}),
)

# ── Bank 15/D + Bank 16 (index 15) — NU METAL (2026-07-24, user request; first Diamond
# Plate presets). User plays DROP D: low D fundamental = 73.4 Hz, so chug presets run
# lowcut 76-82 (keep the fundamental, cut the sub-mud) and hard fast gates for the
# genre's staccato (all thresholds vs the measured -45 dBFS hands-off rig floor).
# Modern tight metal keeps mic 0/0 (close cap edge) per the mic-placement doctrine.
# "Duality Crush" rides in the Muse bank's free D slot (the no-blanks packing rule);
# the four picks fill Bank 16.
add(
  preset(14, 3, "Duality Crush", out_level=OUT,   # Slipknot — Iowa-era wall (bonus 5th nu tone)
    # Mick/Jim's Iowa rhythm: Recto CH3 Modern + silicon (tightest, most brutal), TS out front at
    # drive~0/level-high to clamp the low end for the machine chugs, DEEP amp scoop (mid 0.3),
    # hard fast gate. V30 4x12 close.
    dr={"enable":1,"model":"Green Man","drive":0.0,"tone":0.5,"level":0.76,"mix":1.0},
    amp={"model":"Diamond Plate","rc_mode":"CH3 Modern","rc_variac":"Bold","rc_rect":"Silicon",
         "gain":0.72,"bass":0.58,"mid":0.3,"treble":0.58,"presence":0.55,"master":0.6},
    gt={"enable":1,"thresh":-47,"attack":0.8,"hold":70,"release":140,"hyst":8},
    # EQ intent (Iowa wall): deepen the scoop the RIGHT way — -2.5 @400 (the Iowa hole) with only
    # -1.5 @100 / -1 @200 so the drop-D chug keeps its chest, -1 @800 out of the way, +1 @1.6k /
    # +1.5 @3.2k = the serrated attack that keeps the wall aggressive instead of woolly.
    eq={"enable":1,"pos":13,"100":-1.5,"200":-1,"400":-2.5,"800":-1,"1k6":1,"3k2":1.5},
    cab_ir="@factory",
    cab={"lowcut":78,"highcut":7800}),
)
add(
  preset(15, 0, "Blind Squall", out_level=OUT,   # Korn — Blind / self-titled+Life Is Peachy era
    # RESEARCHED: early Korn = 2-channel Mesa Rectos, LOOSE and clanky (Ross Robinson era) — no
    # tightening pedal. Recto CH2 Vintage + TUBE rectifier (the sag/bounce IS the Korn feel),
    # scooped mids, gate riding the staccato bounce but with enough hold for the loose lows.
    amp={"model":"Diamond Plate","rc_mode":"CH2 Vintage","rc_variac":"Bold","rc_rect":"Tube",
         "gain":0.68,"bass":0.62,"mid":0.32,"treble":0.55,"presence":0.5,"master":0.6},
    gt={"enable":1,"thresh":-48,"attack":1,"hold":100,"release":200,"hyst":8},
    # EQ intent (Korn CLANK): keep the low-D weight (only -1 @100) but pull the 90s-Recto box
    # (-1.5 @200) and mud (-2 @400), dip the honk (-1 @800), then +1.5 @1.6k / +1 @3.2k = the
    # clanky pick attack that defines Blind without brightening the amp itself.
    eq={"enable":1,"pos":13,"100":-1,"200":-1.5,"400":-2,"800":-1,"1k6":1.5,"3k2":1},
    cab_ir="@factory",
    cab={"lowcut":76,"highcut":7600}),
  preset(15, 1, "Quiet Drive", out_level=OUT,   # Deftones — Be Quiet and Drive (Around the Fur); out locked via MANUAL_OUT
    # Carpenter's washy shimmer: compressed 4x12, wide chorus, dreamy digital delay + big plate.
    # Optical comp (smooth), gate deep + long (clean sustain > noise, per the clean-gate doctrine).
    # USER AMP DIAL-IN BAKED VERBATIM (2026-07-24, from the device .dat — do not retune the amp):
    # they moved the platform Clean Meanie -> Diamond Plate CH2 MODERN with the Variac on SPONGY
    # (browned-out sag), gain .4225 / treble .54 / presence .485 / master .67 — a driven saggy
    # edge under the wash instead of a pristine clean (closer to the record, honestly). FX chain
    # left exactly as shipped; out stays -21.3 (the level they dialed at).
    cp={"enable":1,"type":0,"ratio":1,"thresh":-26,"attack":5,"release":5,"knee":3,"makeup":4},
    amp={"model":"Diamond Plate","rc_mode":"CH2 Modern","rc_variac":"Spongy",
         "gain":0.4225,"bass":0.5,"mid":0.5,"treble":0.54,"presence":0.485,"master":0.67,"sag":0.3},
    md={"enable":1,"type":"Lush-2","rate":0.3,"depth":0.55,"mix":0.45,"width":0.8},
    dl={"enable":1,"type":"Digital","time":420,"feedback":0.3,"mix":0.18,"width":0.7},
    rv={"enable":1,"predelay":20,"decay":2.6,"damping":0.4,"mix":0.3},
    gt={"enable":1,"thresh":-56,"attack":2,"hold":160,"release":320,"hyst":8},
    # EQ intent (Deftones shimmer-clean): the chorus+plate wash stacks low-mid energy, so -1.5
    # @100 and -1.5 @400 clean the wash without thinning the guitar; +0.5 @1.6k presence and
    # +1.5 @3.2k AIR = the glassy top the intro strums float on. 200/800 untouched (the body).
    eq={"enable":1,"pos":13,"100":-1.5,"200":0,"400":-1.5,"800":0,"1k6":0.5,"3k2":1.5},
    cab_ir="@factory",
    cab={"lowcut":80,"highcut":10000}),
  preset(15, 2, "One Step Deeper", out_level=OUT,   # Linkin Park — One Step Closer (Hybrid Theory)
    # Delson's Hybrid Theory rhythm: Dual Rec CH3 Modern but TIGHTER and more mid-present than the
    # Korn scoop — TS tightener (drive~0/level high), mids 0.45, silicon/Bold, short hard gate.
    dr={"enable":1,"model":"Green Man","drive":0.05,"tone":0.55,"level":0.78,"mix":1.0},
    amp={"model":"Diamond Plate","rc_mode":"CH3 Modern","rc_variac":"Bold","rc_rect":"Silicon",
         "gain":0.55,"bass":0.5,"mid":0.45,"treble":0.58,"presence":0.55,"master":0.6},
    gt={"enable":1,"thresh":-48,"attack":1,"hold":80,"release":160,"hyst":8},
    # EQ intent (Hybrid Theory tight): -2 @100 / -1 @200 = studio-tight drop-D chug (the record's
    # low end is drums+bass, not guitar), -1.5 @400 de-mud, +1 @800 the produced mid punch,
    # +1 @1.6k definition, +0.5 @3.2k only (the album top is smooth, not fizzy).
    eq={"enable":1,"pos":13,"100":-2,"200":-1,"400":-1.5,"800":1,"1k6":1,"3k2":0.5},
    cab_ir="@factory",
    cab={"lowcut":82,"highcut":8200}),
  preset(15, 3, "Sweet Soy Stabs", out_level=OUT,   # System of a Down — Chop Suey! / Toxicity
    # Daron's hyper-staccato Recto: CH3 Vintage + TUBE rectifier (the bouncy attack), mids FORWARD
    # (0.58 — the anti-scoop of the genre), no pedals, the tightest gate in the bank for the stabs.
    # 2026-07-24 user: "boomy and muffled" — the tube-rect CH3 Vintage runs dark+saggy, so: bass
    # .55->.45 + lowcut 80->84 (boom), treble .6->.66 + presence .5->.58 + highcut 8500->9200
    # (muffle), and the end-chain EQ (below) carves what the amp knobs can't.
    amp={"model":"Diamond Plate","rc_mode":"CH3 Vintage","rc_variac":"Bold","rc_rect":"Tube",
         "gain":0.6,"bass":0.45,"mid":0.58,"treble":0.66,"presence":0.58,"master":0.62},
    gt={"enable":1,"thresh":-47,"attack":0.7,"hold":70,"release":130,"hyst":8},
    # EQ intent (SOAD): kill the residual tube-rect BOOM (-3 @100, -2 @200), clear the mud shelf
    # (-1.5 @400), then push the STAB itself: +1 @800 (punch), +2 @1.6k (pick attack), +2 @3.2k
    # (the choppy top that "Chop Suey" lives on).
    eq={"enable":1,"pos":13,"100":-3,"200":-2,"400":-1.5,"800":1,"1k6":2,"3k2":2},
    cab_ir="@factory",
    cab={"lowcut":84,"highcut":9200}),
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
# FULL RE-MEASURE 2026-07-14 (all 54 presets, indices 4-57) after: Fuzz Zachary v3 re-model
# (new starve/compression law + Comp level makeup shifted every FF preset's level), Cali V hiss
# retune (rev 28 out_levels were estimates), high-gain DNR additions, and the Muse preset retune
# to Bellamy's documented FF settings (measured AFTER that retune). The 9 previously-unmeasured
# presets (Gojira/Mesa/Muse banks, Regal Solo, Grunge Drop) are now included.
MEAS_RMS_AT_M20 = {   # re-measured 2026-07-14 AFTER the rev-32 authenticity pass (28 presets re-voiced)
    "Regal Sustain":-14.33, "Nevermind Verse":-24.54, "Nevermind Wall":-9.09, "Come As Water":-17.6,
    "Candlelit Clean":-15.71, "Sermon Crunch":-14.65, "Sermon Rhythm":-14.02, "Sermon Solo":-13.66,
    "Imperial Rhythm":-15.93, "Imperial Lead":-14.88, "Cardinal Rhythm":-14.64, "Cardinal Lead":-15.36,
    "Dark Side Air":-25.72, "Berlin Wall Pulse":-11.68, "Numb Sustain":-11.46, "Gravity Lead":-30.73,
    "Mauve Haze":-12.63, "Hazy Solo":-15.15, "Little Feather":-19.47, "Holy Smoke":-12.86,
    "Skye Crusher":-14.46, "Skye (No Mod)":-13.91, "Skye Soar":-14.15, "Wizard's Doom":-8.58,
    "Vanishing Drive":-16.87, "Dreamlit Shimmer":-28.61, "Flatliner":-12.51, "Prayer Djent":-12.58,
    "Bridge Vibe":-14.6, "Bottle Jangle":-14.47, "Forest Wash":-28.82, "Disco Chuck":-33.37,
    "Surf Splash":-26.49, "Apache Echo":-21.48, "Desert Robot":-9.14, "Moondust Glam":-7.42,
    "Innerspeaker Swirl":-15.5, "Glide Wall":-16.03, "Streets Chime":-20.21, "Regal Solo":-14.33,
    "March Stabs":-15.3, "World Went Away":-11.55, "Broken Crush":-13.88, "Con Molars":-14.65,
    "Quarter-Tone Lead":-12.6, "Anyone Can Play Guitar":-17.64, "Winterborn":-29.82,
    "Castaway Groove":-13.2, "Marionette Master":-15.25, "Spectrum Rhythm":-15.29,
    "Spectrum Lead":-15.04, "Grunge Drop":-15.29, "Plug-In Junior":-14.95, "Cavalier Charge":-15.08,
    # NU METAL bank (2026-07-24, rev-63 build w/ end-chain EQ, race-fixed tool — true @ out=-20):
    "Duality Crush":-10.45, "Blind Squall":-8.95, "Quiet Drive":-11.68,
    "One Step Deeper":-11.83, "Sweet Soy Stabs":-11.19,
}
# Peak dBFS at out_level=-20 (same run) — CAP out_level so high-crest clean/lead transients stay
# below the limiter ceiling (prevents pumping/crush on peaky presets).
MEAS_PEAK_AT_M20 = {
    "Regal Sustain":-4.49, "Nevermind Verse":-12.15, "Nevermind Wall":-0.42, "Come As Water":-4.07,
    "Candlelit Clean":-7.1, "Sermon Crunch":-5.61, "Sermon Rhythm":-5.2, "Sermon Solo":-3.13,
    "Imperial Rhythm":-5.8, "Imperial Lead":-4.51, "Cardinal Rhythm":-5.23, "Cardinal Lead":-4.88,
    "Dark Side Air":-13.49, "Berlin Wall Pulse":0.77, "Numb Sustain":-2.67, "Gravity Lead":-18.4,
    "Mauve Haze":-3.62, "Hazy Solo":-4.18, "Little Feather":-8.59, "Holy Smoke":-3.53,
    "Skye Crusher":-3.83, "Skye (No Mod)":-4.34, "Skye Soar":-4.69, "Wizard's Doom":-1.34,
    "Vanishing Drive":-6.14, "Dreamlit Shimmer":-15.36, "Flatliner":-3.7, "Prayer Djent":-3.7,
    "Bridge Vibe":-4.64, "Bottle Jangle":-1.96, "Forest Wash":-14.65, "Disco Chuck":-22.0,
    "Surf Splash":-15.38, "Apache Echo":-9.41, "Desert Robot":0.45, "Moondust Glam":2.09,
    "Innerspeaker Swirl":-2.5, "Glide Wall":-3.99, "Streets Chime":-8.51, "Regal Solo":-3.25,
    "March Stabs":-5.21, "World Went Away":-2.86, "Broken Crush":-3.33, "Con Molars":-5.13,
    "Quarter-Tone Lead":-1.93, "Anyone Can Play Guitar":-6.61, "Winterborn":-17.24,
    "Castaway Groove":-4.27, "Marionette Master":-5.44, "Spectrum Rhythm":-5.35,
    "Spectrum Lead":-4.51, "Grunge Drop":-5.03, "Plug-In Junior":-4.52, "Cavalier Charge":-4.22,
    "Duality Crush":-1.08, "Blind Squall":-1.28, "Quiet Drive":-2.19,
    "One Step Deeper":-1.83, "Sweet Soy Stabs":-2.52,
}
CLEAN_NAMES = {"Candlelit Clean", "Dreamlit Shimmer", "Berlin Wall Pulse", "Dark Side Air", "Little Feather",
               "Nevermind Verse", "Come As Water", "Winterborn",
               "Bottle Jangle", "Forest Wash", "Disco Chuck", "Surf Splash", "Apache Echo", "Streets Chime",
               "Quiet Drive"}
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
# ── Fidelity polish (2026-07-23): per-preset Speaker Coupling + Pickup Load ──
# Keyed by NAME (rearrange-proof). Reasoning per rig:
#   coupling — how hard the real power section works into its speakers:
#     dimed vintage/non-master (May AC30s, Hendrix Plexi, doom stacks, MBV) .45-.55
#     pushed vintage/rock .25-.40 | cleans with a working PA .15-.30
#     tight modern metal (EVH djent, Mesa Mark) .10 or 0 — high damping IS that sound
#     0 for: direct rigs (JMP-1/NIN), solid-state (Backline), Sunn (PA bypassed).
#   load — pickup/cable/input impedance: straight-in vintage .15-.25 (Hendrix coily
#     cable .35), sparkle on cleans .10-.20, buffered/modern boards 0.
# USER-PRESERVED presets (Cardinal Rhythm, Retro Poland) are NOT touched.
POLISH = {  # name: (coupling, pickup_load)
    "Nevermind Verse": (0.20, 0.10), "Nevermind Wall": (0.35, 0.10), "Come As Water": (0.25, 0.12),
    "Candlelit Clean": (0.30, 0.12), "Sermon Crunch": (0.40, 0.12), "Sermon Rhythm": (0.40, 0.12),
    "Sermon Solo": (0.40, 0.12), "Imperial Rhythm": (0.20, 0.05), "Imperial Lead": (0.20, 0.05),
    "Cardinal Lead": (0.20, 0.05), "Dark Side Air": (0.25, 0.15), "Berlin Wall Pulse": (0.25, 0.10),
    "Numb Sustain": (0.40, 0.15), "Gravity Lead": (0.30, 0.10), "Mauve Haze": (0.50, 0.35),
    "Hazy Solo": (0.55, 0.35), "Little Feather": (0.35, 0.25), "Holy Smoke": (0.50, 0.15),
    "Skye Crusher": (0.25, 0.05), "Skye (No Mod)": (0.25, 0.05), "Skye Soar": (0.25, 0.05),
    "Wizard's Doom": (0.0, 0.20), "Vanishing Drive": (0.25, 0.05), "Dreamlit Shimmer": (0.25, 0.15),
    "Flatliner": (0.0, 0.0), "Prayer Djent": (0.0, 0.0), "Bridge Vibe": (0.40, 0.10),
    "Bottle Jangle": (0.30, 0.20), "Forest Wash": (0.15, 0.10), "Disco Chuck": (0.10, 0.15),
    "Surf Splash": (0.30, 0.20), "Apache Echo": (0.30, 0.20), "Desert Robot": (0.0, 0.05),
    "Moondust Glam": (0.45, 0.25), "Innerspeaker Swirl": (0.45, 0.15), "Glide Wall": (0.50, 0.15),
    "Streets Chime": (0.35, 0.10), "March Stabs": (0.0, 0.0), "World Went Away": (0.10, 0.0),
    "Broken Crush": (0.0, 0.0), "Con Molars": (0.30, 0.0), "Quarter-Tone Lead": (0.20, 0.05),
    "Anyone Can Play Guitar": (0.25, 0.10), "Winterborn": (0.15, 0.05), "Castaway Groove": (0.0, 0.0),
    "Marionette Master": (0.10, 0.0), "Spectrum Rhythm": (0.10, 0.0), "Spectrum Lead": (0.10, 0.0),
    "Regal Sustain": (0.55, 0.25), "Regal Solo": (0.55, 0.25), "Grunge Drop": (0.25, 0.05),
    "Plug-In Junior": (0.35, 0.05), "Cavalier Charge": (0.35, 0.05),
}
for _p in PRESETS:
    if _p["name"] in POLISH:
        _c, _l = POLISH[_p["name"]]
        _p["vals"][SYM_IDX["amp_pamp_coupl"]] = _c
        _p["vals"][SYM_IDX["it_load"]] = _l

# Dense reverb tank on EVERY preset (user 2026-07-23 after A/B: "it sounds great —
# turn Dense on for all of the presets"). Level-matched by design; the Classic
# port default stays 0 so old user boards/blobs are untouched until they recall.
for _p in PRESETS:
    _p["vals"][SYM_IDX["rv_density"]] = 1.0

# Dense cab room on EVERY preset too (user 2026-07-23, after the same A/B). As with
# the tank: level-matched at the design point, per-preset residuals absorbed via
# ROOMDENSE_MEAS_DELTA below; port default stays Classic for old boards.
for _p in PRESETS:
    _p["vals"][SYM_IDX["cab_roomdense"]] = 1.0

# Spring reverb where the rig calls for it (user 2026-07-23): the two genuinely
# surf / early-60s instrumental presets. Everything else references plate/studio
# reverb rigs and stays on the plate tank.
for _p in PRESETS:
    if _p["name"] in ("Surf Splash", "Apache Echo"):
        _p["vals"][SYM_IDX["rv_type"]] = 1.0

# Lead Cut on every lead preset (2026-07-24 user request): EQ block at end-of-chain with
# the stock "Lead Cut" curve. The DSP reads the SLIDERS only, so the curve is baked into
# the band ports; eq_preset=5 is the retained UI selection (dropdown shows "Lead Cut").
# Curve mirrors EQ_PRE[5] in script-hexforge.js: -1/0/+1/+3/+4/+5 — low trim + rising
# upper-mid/presence push so the lead steps out front.
# EXCLUDED: Cardinal Lead (user-preserved dial-in) + the stock Bank-1 "Lead" (user's own
# reference set, inline seeds).
LEAD_CUT = {"eq_enable": 1.0, "eq_pos": 13.0, "eq_preset": 5.0,
            "eq_100": -1.0, "eq_200": 0.0, "eq_400": 1.0,
            "eq_800": 3.0, "eq_1k6": 4.0, "eq_3k2": 5.0}
LEAD_NAMES = ("Regal Sustain", "Sermon Solo", "Imperial Lead", "Numb Sustain", "Gravity Lead",
              "Hazy Solo", "Skye Soar", "Regal Solo", "Quarter-Tone Lead", "Spectrum Lead")
for _p in PRESETS:
    if _p["name"] in LEAD_NAMES:
        for _s, _v in LEAD_CUT.items():
            _p["vals"][SYM_IDX[_s]] = _v

# Loudness deltas measured on-device AFTER the polish (coupling adds level on the
# pushed presets) — applied to the MEAS table so the parity pipeline re-levels.
POLISH_MEAS_DELTA = {   # measured on-device 2026-07-23 (pdfast before/after, real rev-53 build)
    "Regal Sustain": 0.5, "Nevermind Wall": 0.5, "Come As Water": 0.4, "Candlelit Clean": 0.3,
    "Numb Sustain": 0.6, "Hazy Solo": 0.4, "Bridge Vibe": 0.3, "Bottle Jangle": 0.3,
    "Surf Splash": 0.3, "Apache Echo": 0.5, "Innerspeaker Swirl": 0.7, "Glide Wall": 1.0,
    "Streets Chime": 0.5, "Regal Solo": 0.7, "Anyone Can Play Guitar": 0.3,
}
for _nm, _d in POLISH_MEAS_DELTA.items():
    if _nm in MEAS_RMS_AT_M20:  MEAS_RMS_AT_M20[_nm]  += _d
    if _nm in MEAS_PEAK_AT_M20: MEAS_PEAK_AT_M20[_nm] += _d   # coupling lifts peaks alike;
                                                              # peak-capped presets re-level too

# Dense-tank loudness deltas (measured on-device 2026-07-23 after turning Dense on
# everywhere): each preset's decay/damping interacts with the 8-comb tank, so the
# design-point level match holds only at defaults. Same pipeline treatment.
DENSE_MEAS_DELTA = {
    "Nevermind Verse": 0.4, "Candlelit Clean": -0.3, "Sermon Solo": 1.1, "Cardinal Lead": -0.5,
    "Dark Side Air": 1.1, "Berlin Wall Pulse": 0.4, "Numb Sustain": 0.9, "Gravity Lead": 0.3,
    "Skye (No Mod)": -0.3, "Vanishing Drive": 0.6, "Dreamlit Shimmer": -2.8, "Bottle Jangle": 0.5,
    "Forest Wash": 1.3, "Surf Splash": -4.0, "Apache Echo": 0.4, "Glide Wall": -0.3,
    "Streets Chime": 1.0, "World Went Away": 1.4, "Quarter-Tone Lead": 0.3, "Winterborn": 2.3,
}
for _nm, _d in DENSE_MEAS_DELTA.items():
    if _nm in MEAS_RMS_AT_M20:  MEAS_RMS_AT_M20[_nm]  += _d
    if _nm in MEAS_PEAK_AT_M20: MEAS_PEAK_AT_M20[_nm] += _d

# Loudness deltas measured on-device 2026-07-23 AFTER (a) Dense cab room on every
# preset (extra comb energy lifts most rigs +0.3..+1.0 dB at the stock 0.12 room
# mix) and (b) the Plate->Spring swap on the two surf presets (the spring tank
# runs quieter at the same mix: Surf Splash -2.4, Apache Echo -1.2). Same pipeline
# treatment: fold into BOTH MEAS tables so out_level re-levels to parity.
ROOMDENSE_MEAS_DELTA = {
    "Regal Sustain": +0.7, "Nevermind Verse": +0.7, "Nevermind Wall": +0.8, "Come As Water": +0.7,
    "Candlelit Clean": +0.5, "Sermon Crunch": +0.4, "Sermon Rhythm": +0.4, "Sermon Solo": +0.4,
    "Imperial Rhythm": +0.5, "Imperial Lead": +0.5, "Cardinal Rhythm": +0.7, "Cardinal Lead": +0.4,
    "Dark Side Air": +0.5, "Numb Sustain": +0.8, "Gravity Lead": +0.9, "Mauve Haze": +0.6,
    "Little Feather": +0.7, "Skye Crusher": +0.7, "Skye (No Mod)": +0.7, "Skye Soar": +0.6,
    "Vanishing Drive": +0.4, "Dreamlit Shimmer": +0.8, "Flatliner": +0.7, "Prayer Djent": +0.7,
    "Bridge Vibe": +0.7, "Bottle Jangle": +0.5, "Forest Wash": +0.6, "Disco Chuck": +0.8,
    "Surf Splash": -2.4, "Apache Echo": -1.2, "Desert Robot": +0.6, "Moondust Glam": +0.6,
    "Innerspeaker Swirl": +0.7, "Glide Wall": +0.7, "Streets Chime": +0.9, "Regal Solo": +0.6,
    "March Stabs": +0.6, "World Went Away": +0.8, "Broken Crush": +0.9, "Con Molars": +0.9,
    "Quarter-Tone Lead": -0.4, "Anyone Can Play Guitar": +0.7, "Winterborn": +0.6, "Castaway Groove": +0.5,
    "Marionette Master": +0.6, "Spectrum Rhythm": +0.6, "Spectrum Lead": +0.5, "Grunge Drop": +0.6,
    "Plug-In Junior": +0.5, "Cavalier Charge": +0.3,
}
for _nm, _d in ROOMDENSE_MEAS_DELTA.items():
    if _nm in MEAS_RMS_AT_M20:  MEAS_RMS_AT_M20[_nm]  += _d
    if _nm in MEAS_PEAK_AT_M20: MEAS_PEAK_AT_M20[_nm] += _d

# ABSOLUTE re-measurements (2026-07-24, rev 63) — supersede every delta above for these
# names. History: TWO hfmeas flaws fixed today. (1) The seamless deferred recall broke the
# out_level=-20 force (readings came out at baked out). (2) Worse: the amp-model swap
# response landed during the recall's fade-IN and was never applied until the NEXT preset
# switch — every preset measured through the PREVIOUS preset's amp model (also a real
# plugin race, fixed in work_response). Values below are from the fully-fixed tool +
# race-fixed plugin, verified ORDER-INDEPENDENT (two shuffled batches agree ≤0.02 dB).
# (Surf Splash needs no entry — MANUAL_OUT locks its level.)
REDO_MEAS = {   # name: (rms@-20, peak@-20)
    "March Stabs": (-16.90, -5.53),
    "World Went Away": (-8.99, -1.58), "Broken Crush": (-16.64, -4.26),
    "Con Molars": (-13.82, -3.25),
    "Flatliner": (-13.52, -4.04), "Prayer Djent": (-13.64, -4.21),   # rev-63 end-chain EQ added
    # rev-64 Lead Cut pass (2026-07-24): all 10 leads re-measured after the EQ curve landed
    # (two shuffled batches, ≤0.04 dB; peak = the higher of the two runs, conservative cap):
    "Regal Sustain": (-10.28, 0.05), "Sermon Solo": (-10.58, 0.05),
    "Imperial Lead": (-11.70, -0.85), "Numb Sustain": (-9.03, 1.53),
    "Gravity Lead": (-28.35, -13.08), "Hazy Solo": (-10.97, 0.99),
    "Skye Soar": (-12.35, -0.90), "Regal Solo": (-9.89, 0.95),
    "Quarter-Tone Lead": (-11.59, 0.95), "Spectrum Lead": (-11.96, -0.94),
}
for _nm, (_r, _pk) in REDO_MEAS.items():
    MEAS_RMS_AT_M20[_nm] = _r; MEAS_PEAK_AT_M20[_nm] = _pk

MANUAL_OUT = {   # hand-locks: presets the user tuned BY EAR at a specific level — re-measuring would change what they heard
    "Surf Splash": 2.4,    # user's 2026-07-24 device dial-in was made at the rev-61 out (+2.4); baked verbatim with it
    "Quiet Drive": -21.3,  # user's 2026-07-24 amp dial-in (Recto CH2 Modern/Spongy) made at the rev-63 out; level is what they heard
}
for _p in PRESETS:
    _nm = _p["name"]
    if _nm in MANUAL_OUT:
        _p["vals"][SYM_IDX["out_level"]] = MANUAL_OUT[_nm]
    elif _nm in MEAS_RMS_AT_M20:
        _tgt = TARGET_CLEAN if _nm in CLEAN_NAMES else TARGET_SUNN if _nm in SUNN_NAMES else TARGET_DIRTY
        _out_rms  = -20.0 + (_tgt - MEAS_RMS_AT_M20[_nm])
        _out_peak = PEAK_CEIL - 20.0 - MEAS_PEAK_AT_M20.get(_nm, -6.0)   # cap: peak stays under the ceiling
        _p["vals"][SYM_IDX["out_level"]] = round(max(-40.0, min(6.0, min(_out_rms, _out_peak))), 1)

# Perceptual loudness trims (2026-07-24, user: "March Stabs and Con Molars are louder
# compared to the other presets"): their end-of-chain EQ pushes the 1.6-3.2k presence
# region (+800 Hz on Con Molars) where the ear is most sensitive, so they read louder
# than the rest AT measured RMS parity (March Stabs even sits ~2 dB under target from
# its peak cap and still reads loud). Ear-offset applied AFTER the parity computation
# so the MEAS pipeline stays live for future re-measures.
PERC_TRIM_DB = {"March Stabs": -2.5, "Con Molars": -2.5,
                "One Step Deeper": -1.5}   # 2026-07-24 user: "a little loud" (tight+mid-punchy reads hot at RMS parity)
for _p in PRESETS:
    _t = PERC_TRIM_DB.get(_p["name"])
    if _t:
        _i = SYM_IDX["out_level"]
        _p["vals"][_i] = round(max(-40.0, _p["vals"][_i] + _t), 1)

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
    assert N_PORTS == 223, "port count drift: got %d" % N_PORTS   # 223: + rv_type/cab_roomdense (v26); 221: + rv_density (v25)
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
