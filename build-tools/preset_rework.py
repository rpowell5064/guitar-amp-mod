# ─────────────────────────────────────────────────────────────────────────────
# The 2026-09-24 factory preset rework.
#
# Every factory preset is defined here through gen_hexforge_presets.preset():
#   preset(bank, slot, name, cls, chain=[...], rig=(ir, variant), **blocks)
# Values not mentioned keep the 2026-09-24 on-device capture (preset_base.json),
# so a preset can be re-voiced without restating every knob; base=False starts
# from port defaults instead. Knob values are 0..1 = the real amp's dial / 10
# (the component twins are schematic-exact, so documented dial positions carry).
# Global context the whole set is voiced for: Component Model ON, Dynamic Load
# ON (both default ON since this rework), humbucker player.
# Loudness classes: clean / dirty / sunn / bass (see TARGET in the generator).
# Research (private lab, per-claim evidence grades) is summarised in the
# comments; "doc" = documented dial, "inf" = inferred from the record.
# ─────────────────────────────────────────────────────────────────────────────
from gen_hexforge_presets import preset, BASE

# Rig shorthands (the Cab panel's factory rows; each sets cab IR + mics + room +
# Speaker Drive "Physical"). High cuts are set per preset AFTER the rig.
TIGHT57  = ("@factory",     "Tight 57")
PAIR57R  = ("@factory",     "57 + Ribbon")
STUDIO   = ("@factory",     "Studio Pair")
LIVEROOM = ("@factory",     "Live Room Pair")
CHIME    = ("@vox2x12",     "Chime Pair")
OPENBACK = ("@american-ob", "Open-Back Air")
GBROOM   = ("@greenback",   "Room")
WALL     = ("@hiwatt",      "Wall")
CAVE     = ("@doom",        "Cave")
B810     = ("@bass810",     "Close")
B115     = ("@bass115",     "Room")

# Gate thresholds are checked against the measured rig floor: hands-off idle
# peaks -45 dBFS (89 % mains hum, the comb takes ~15 dB off) → dirty presets
# close at -48..-55, clean presets sit open at -58/-60, fuzz/doom go deeper on
# purpose (sustain beats hiss there).
def gate(thresh, attack=1.5, hold=120, release=250, hyst=8):
    return {"thresh": thresh, "attack": attack, "hold": hold, "release": release, "hyst": hyst}
FAST, MED, SLOW = 8, 5, 2           # the compressor's 0..10 attack / release scales (10 = FASTEST)
def comp(type_, thresh, ratio, attack=MED, release=MED, knee=3, makeup=None):
    # CompressorBlock semantics (measured 2026-09-24: textbook thresholds cost 20-30 dB):
    #   Once76 (1176) — "threshold" is INPUT DRIVE (0 dBFS = barely, -60 = full); no auto makeup.
    #   5 Creature Amp (VCA) — threshold in dBFS on the guitar-level signal; makeup 0..10 = 0..+20 dB.
    # Pedal-style levelling = light drive / moderate threshold with the gain reduction made up.
    # The 1176 path has NO makeup stage at all (setMakeupGain only reaches the VCA), so it
    # leaves 15-20 dB on the floor at any drive — every preset compressor runs the VCA.
    type_ = "5 Creature Amp"
    thresh = max(thresh, -22.0); mk = 5 if makeup is None else makeup
    return {"type": type_, "thresh": thresh, "ratio": ratio, "attack": attack, "release": release, "knee": knee, "makeup": mk}
def plate(mix, decay=1.5, pre=20, damp=0.5):  return {"type": "Plate",   "predelay": pre, "decay": decay, "damping": damp, "mix": mix}
def spring(mix, decay=2.0, pre=10, damp=0.5): return {"type": "Spring",  "predelay": pre, "decay": decay, "damping": damp, "mix": mix}
def ambient(mix, decay=3.0, pre=40, damp=0.5, bloom=0.5):
    return {"type": "Ambient", "predelay": pre, "decay": decay, "damping": damp, "mix": mix, "bloom": bloom}
def dig(ms, fb, mix, width=0.4): return {"type": "Digital", "time": ms, "feedback": fb, "mix": mix, "width": width}
def tape(ms, fb, mix, width=0.4): return {"type": "Tape", "time": ms, "feedback": fb, "mix": mix, "width": width}
def eqm(**bands):                # eq={"preset":"Manual", "100":..}
    d = {"preset": "Manual"}; d.update(bands); return d
def dotted8(bpm):  return round(60000.0 / bpm * 0.75, 1)
def quarter(bpm):  return round(60000.0 / bpm, 1)
def eighth(bpm):   return round(60000.0 / bpm * 0.5, 1)

def passthrough(bank, slots=(0, 1, 2, 3), cls="dirty"):
    """Slots not yet reworked pass through the capture unchanged."""
    for s in slots:
        p = BASE.get((bank, s))
        if p: preset(bank, s, p["name"], cls=cls)

# ═══ Bank 1 (index 0) — STOCK: one clean, one crunch, one rhythm, one lead ═══
def bank_stock():
    preset(0, 0, "Clean", cls="clean", chain=["gt", "cp", "amp", "cab", "rv"], rig=OPENBACK,
           amp={"model": "Clean Meanie", "gain": 0.5, "bass": 0.55, "mid": 0.5, "treble": 0.6, "presence": 0.5, "master": 0.85, "sag": 0.35},
           cp=comp("Once76", -24, "4:1", MED, MED), cab={"highcut": 9000}, rv=spring(0.18, 2.0), gt=gate(-60))
    preset(0, 1, "Crunch", cls="dirty", chain=["gt", "dr", "amp", "cab", "rv"], rig=PAIR57R,
           dr={"model": "Green Man", "drive": 0.1, "tone": 0.5, "level": 0.6, "mix": 1.0},
           amp={"model": "Crunchy McCrunchFace", "gain": 0.55, "bass": 0.5, "mid": 0.6, "treble": 0.6, "presence": 0.5, "master": 0.55, "sag": 0.35, "sir34": 0},
           cab={"highcut": 8000}, rv=plate(0.1, 1.2), gt=gate(-52))
    preset(0, 2, "Rhythm", cls="dirty", chain=["gt", "dr", "amp", "cab"], rig=STUDIO,
           dr={"model": "Green Man", "drive": 0.0, "tone": 0.55, "level": 0.6, "mix": 1.0},
           amp={"model": "Gainzilla", "channel": 1, "gain": 0.5, "bass": 0.5, "mid": 0.55, "treble": 0.55, "presence": 0.55, "resonance": 0.5, "master": 0.45, "sag": 0.3},
           cab={"highcut": 7500}, gt=gate(-48))
    preset(0, 3, "Lead", cls="dirty", chain=["gt", "dr", "amp", "cab", "dl", "rv"], rig=PAIR57R,
           dr={"model": "Green Man", "drive": 0.15, "tone": 0.5, "level": 0.65, "mix": 1.0},
           amp={"model": "Tangerang", "channel": 0, "gain": 0.6, "bass": 0.5, "mid": 0.7, "treble": 0.6, "presence": 0.55, "master": 0.5, "sag": 0.4},
           cab={"highcut": 8000}, dl=dig(380, 0.3, 0.18), rv=plate(0.15, 1.8, 30), gt=gate(-50))

# ═══ Bank 2 (index 1) — NIRVANA · Nevermind (+ Brian May's rhythm sound in A) ═══
def bank_nirvana():
    # A Regal Sustain — May: AC30 Normal channel "about 9.5" (A), Cut 0 (B), Rangemaster full (A) → Chime Pair, Physical.
    preset(1, 0, "Regal Sustain", cls="dirty", chain=["gt", "dr", "amp", "cab", "rv"], rig=CHIME,
           dr={"model": "Treble Ranger", "drive": 1.0, "tone": 0.5, "level": 1.0, "mix": 1.0},
           amp={"model": "Chime Thirty", "gain": 0.95, "bass": 0.5, "mid": 0.5, "treble": 0.4, "presence": 0.05, "master": 1.0, "sag": 0.65},
           cab={"lowcut": 90, "highcut": 9000}, rv=plate(0.1, 1.4, 25), gt=gate(-55))
    # B Nevermind Verse — Bassman clean (A: Vig) + Small Clone rate noon / depth deep (A/B), one close mic, no room.
    preset(1, 1, "Nevermind Verse", cls="clean", chain=["gt", "md", "amp", "cab", "rv"], rig=OPENBACK,
           md={"type": "Nevermind Chorus", "rate": 0.5, "depth": 1.0, "mix": 0.5, "width": 0.0},
           amp={"model": "Clean Meanie", "gain": 0.45, "bass": 0.6, "mid": 0.5, "treble": 0.55, "presence": 0.5, "master": 0.9, "sag": 0.4},
           cab={"highcut": 8000, "spkdrive": "Off", "roommix": 0.08}, rv=plate(0.08, 1.2), gt=gate(-60))
    # C Nevermind Wall — DS-1 (tone ~10:00, dist ~4, level max — B) into the pushed Bassman; no chorus on the wall (A).
    preset(1, 2, "Nevermind Wall", cls="dirty", chain=["gt", "dr", "amp", "cab"], rig=OPENBACK,
           dr={"model": "Grunge DS", "drive": 0.45, "tone": 0.35, "level": 1.0, "mix": 1.0},
           amp={"model": "Clean Meanie", "gain": 0.6, "bass": 0.6, "mid": 0.5, "treble": 0.5, "presence": 0.5, "master": 0.85, "sag": 0.5},
           cab={"lowcut": 70, "highcut": 7500}, gt=gate(-52))
    # D Come As Water — "all the clean strummy stuff was the AC30" (A: Vig) + deep Small Clone; dark, no air.
    preset(1, 3, "Come As Water", cls="clean", chain=["gt", "md", "amp", "cab", "rv"], rig=CHIME,
           md={"type": "Nevermind Chorus", "rate": 0.5, "depth": 1.0, "mix": 0.5, "width": 0.0},
           amp={"model": "Chime Thirty", "gain": 0.45, "bass": 0.5, "mid": 0.5, "treble": 0.45, "presence": 0.4, "master": 0.9, "sag": 0.5},
           cab={"highcut": 7000, "spkdrive": "Off"}, rv=plate(0.06, 1.0, 15), gt=gate(-60))

# ═══ Bank 3 (index 2) — GHOST · Opus Eponymous / Infestissumam (Orange Thunderverb era) ═══
def bank_ghost_opus():
    # A Candlelit Clean — the Opus "clean" is the Orange turned down; Once76 tightens the arpeggios.
    preset(2, 0, "Candlelit Clean", cls="clean", chain=["gt", "cp", "amp", "cab", "rv"], rig=PAIR57R,
           cp=comp("Once76", -22, "4:1", MED, MED),
           amp={"model": "Tangerang", "channel": 1, "gain": 0.55, "bass": 0.55, "mid": 0.5, "treble": 0.55, "presence": 0.4, "master": 0.8, "sag": 0.4},
           cab={"highcut": 8000}, rv=spring(0.15, 1.6, 20), gt=gate(-58))
    # B Sermon Crunch — "we backed up the gain as much as possible without losing the tone or the sustain … the
    #   midrange was really important" (A, Guitar World 2013). Amp only.
    preset(2, 1, "Sermon Crunch", cls="dirty", chain=["gt", "amp", "cab"], rig=PAIR57R,
           amp={"model": "Tangerang", "channel": 0, "gain": 0.4, "bass": 0.5, "mid": 0.6, "treble": 0.55, "presence": 0.45, "master": 0.65, "sag": 0.5},
           cab={"lowcut": 70, "highcut": 7500}, gt=gate(-55))
    # C Sermon Rhythm — Infestissumam: the Oranges "dialed in with vintage flavors" (A, Paul Fig); a notch more gain.
    preset(2, 2, "Sermon Rhythm", cls="dirty", chain=["gt", "amp", "cab", "eq"], rig=PAIR57R,
           amp={"model": "Tangerang", "channel": 0, "gain": 0.55, "bass": 0.55, "mid": 0.6, "treble": 0.55, "presence": 0.5, "master": 0.65, "sag": 0.45},
           cab={"lowcut": 75, "highcut": 7500}, eq=eqm(**{"100": 1, "200": 0, "400": -1, "800": 0, "1k6": 0, "3k2": 1, "level": 0}), gt=gate(-50))
    # D Sermon Solo — the same head with the neck pickup; TS push for sustain, quarter-note echo at 135 BPM (Genesis).
    preset(2, 3, "Sermon Solo", cls="dirty", chain=["gt", "dr", "amp", "cab", "dl", "rv"], rig=PAIR57R,
           dr={"model": "Green Man", "drive": 0.2, "tone": 0.5, "level": 0.75, "mix": 1.0},
           amp={"model": "Tangerang", "channel": 0, "gain": 0.6, "bass": 0.5, "mid": 0.65, "treble": 0.5, "presence": 0.45, "master": 0.65, "sag": 0.5},
           cab={"lowcut": 90, "highcut": 7000}, dl=dig(quarter(135), 0.25, 0.18, 0.3), rv=plate(0.18, 1.8, 30, 0.55), gt=gate(-52))

# ═══ Bank 4 (index 3) — GHOST · Impera / Skeletá ═══
def bank_ghost_impera():
    # A Imperial Rhythm — Åkesson: Friedman BE-100 + MXR Sugar Drive (Klon-style) "a lot" (A); rhythm packs of 8, dry.
    preset(3, 0, "Imperial Rhythm", cls="dirty", chain=["gt", "dr", "amp", "cab", "eq"], rig=STUDIO,
           dr={"model": "Gilded Horse", "drive": 0.25, "tone": 0.5, "level": 0.65, "mix": 1.0},
           amp={"model": "Beardo BE", "fr_channel": "BE", "fr_fat": 0, "fr_c45": 0, "fr_sat": 0,
                "gain": 0.55, "bass": 0.5, "mid": 0.55, "treble": 0.6, "presence": 0.55, "master": 0.5, "sag": 0.3},
           cab={"highcut": 8000}, eq=eqm(**{"100": 1, "200": -1, "400": 0, "800": 0, "1k6": 0, "3k2": 1.5, "level": 0}), gt=gate(-48))
    # B Imperial Lead — "for most of the solos I borrowed an old DOD Preamp 250" into "late-'50s Marshall Plexis" (A).
    preset(3, 1, "Imperial Lead", cls="dirty", chain=["gt", "dr", "amp", "cab", "dl", "rv"], rig=GBROOM,
           dr={"model": "Preamp 250", "drive": 0.6, "tone": 0.5, "level": 0.7, "mix": 1.0},
           amp={"model": "Plexiglass", "gain": 0.7, "pl_vol2": 0.5, "pl_variac": 0, "bass": 0.45, "mid": 0.65, "treble": 0.6, "presence": 0.5, "master": 1.0, "sag": 0.6},
           cab={"lowcut": 100, "highcut": 7000}, dl=dig(dotted8(128), 0.3, 0.2, 0.5), rv=plate(0.2, 2.0, 40), gt=gate(-50))
    # C Cardinal Rhythm — Skeletá: "a Mesa Boogie IIC+ … the same one James Hetfield used", MD421s (A, SoS); shallow V.
    preset(3, 2, "Cardinal Rhythm", cls="dirty", chain=["gt", "amp", "cab", "eq"], rig=TIGHT57,
           amp={"model": "Cali V", "mv_mode": "IIC+", "mv_eqpreset": "Custom", "mv_geq0": 0.62, "mv_geq1": 0.45, "mv_geq2": 0.40, "mv_geq3": 0.60, "mv_geq4": 0.55,
                "gain": 0.7, "bass": 0.25, "mid": 0.35, "treble": 0.7, "presence": 0.4, "master": 0.5, "sag": 0.25},
           cab={"lowcut": 90, "highcut": 8000}, eq=eqm(**{"100": -1, "200": 1, "400": 0, "800": 1, "1k6": 0, "3k2": 1, "level": 0}), gt=gate(-45))
    # D Cardinal Lead — a late-70s Boss OD-1 on the solos (A) → Super Nova; IIC+ under the rhythm gain so the pedal shows.
    preset(3, 3, "Cardinal Lead", cls="dirty", chain=["gt", "dr", "amp", "cab", "dl", "rv"], rig=TIGHT57,
           dr={"model": "Super Nova", "drive": 0.55, "tone": 0.5, "level": 0.7, "mix": 1.0},
           amp={"model": "Cali V", "mv_mode": "IIC+", "mv_eqpreset": "Custom", "mv_geq0": 0.55, "mv_geq1": 0.5, "mv_geq2": 0.5, "mv_geq3": 0.6, "mv_geq4": 0.5,
                "gain": 0.65, "bass": 0.3, "mid": 0.45, "treble": 0.65, "presence": 0.45, "master": 0.5, "sag": 0.3},
           cab={"lowcut": 100, "highcut": 7500}, dl=dig(dotted8(125), 0.3, 0.22, 0.6), rv=plate(0.22, 2.2, 40, 0.45), gt=gate(-48))

# ═══ Bank 5 (index 4) — PINK FLOYD (+ Gravity) ═══
def bank_floyd():
    # a DR103 at 5 is a QUIET clean amp on this DI; 6 keeps Gilmour's "very clean" with humbucker headroom
    hiwatt = {"model": "Hi-Volt", "gain": 0.6, "bass": 0.5, "mid": 0.6, "treble": 0.7, "presence": 0.6, "master": 0.8, "sag": 0.25}
    # A Dark Side Air — Breathe: Hiwatt/WEM (B), Uni-Vibe vol 100 / intensity 60 / rate 40-50 (B), Echorec, long cable (6.5 k).
    preset(4, 0, "Dark Side Air", cls="clean", chain=["gt", "md", "amp", "cab", "dl", "rv"], rig=WALL,
           md={"type": "Uni-Verse", "rate": 0.45, "depth": 0.6, "mix": 1.0, "width": 0.0},
           amp=hiwatt, cab={"highcut": 6500},
           dl={"type": "Echo Wreck", "time": quarter(64), "feedback": 0.45, "mix": 0.3, "width": 0.3},
           rv=plate(0.18, 2.2, 40), gt=gate(-60), rb={"enable": 0, "cab2on": 0})
    # B Berlin Wall Pulse — Run Like Hell: Dyna Comp > ST-2 boost (Tube Chauffeur stands in) > Mistress > Hiwatt;
    #   two MXR DDLs: 380 ms 7-8 repeats at unity + 507 ms one repeat (B) = dotted 8th + quarter at 117 BPM.
    preset(4, 1, "Berlin Wall Pulse", cls="clean", chain=["gt", "cp", "dr", "md", "amp", "cab", "dl", "dl2"], rig=WALL,
           cp=comp("5 Creature Amp", -28, "4:1", FAST, MED),
           dr={"model": "Tube Chauffeur", "drive": 0.25, "tone": 0.45, "level": 0.6, "mix": 1.0},
           md={"type": "Flanger", "rate": 0.2, "depth": 0.5, "mix": 0.5, "width": 0.3},
           amp=dict(hiwatt, treble=0.7), cab={"lowcut": 90, "highcut": 7000},
           dl=dig(dotted8(117), 0.72, 0.5, 0.2), dl2=dig(quarter(117), 0.15, 0.3, 0.6), gt=gate(-58))
    # C Numb Sustain — Comfortably Numb: ram's head Muff "through a Hiwatt and a Yamaha RA-200" (B), 450 ms 4-5 repeats.
    preset(4, 2, "Numb Sustain", cls="dirty", chain=["gt", "fz", "amp", "cab", "md", "dl", "rv"], rig=WALL,
           fz={"pedal": "Italian Hero", "mode": "Ovis", "sustain": 0.7, "tone": 0.35, "volume": 0.55, "bias": 0.5, "inputtrim": 0.4, "gvol": 1.0},
           amp=dict(hiwatt, treble=0.65, presence=0.55), cab={"lowcut": 90, "highcut": 7000},
           md={"type": "Rotary", "rate": 0.15, "depth": 0.5, "mix": 0.4, "width": 0.6},
           dl=dig(450, 0.45, 0.22, 0.3), rv=plate(0.18, 2.4, 40), gt=gate(-52, release=400), rb={"enable": 0, "cab2on": 0})
    # D Gravity Lead — Mayer: Klon on the edge into an open-back 2x12 Fender-family clean; spring, short slap.
    preset(4, 3, "Gravity Lead", cls="clean", chain=["gt", "cp", "dr", "amp", "cab", "dl", "rv"], rig=OPENBACK,
           cp=comp("5 Creature Amp", -25, "2:1", MED, MED),
           dr={"model": "Gilded Horse", "drive": 0.3, "tone": 0.5, "level": 0.6, "mix": 1.0},
           amp={"model": "Clean Meanie", "gain": 0.55, "bass": 0.45, "mid": 0.6, "treble": 0.6, "presence": 0.6, "master": 0.9, "sag": 0.5},
           cab={"highcut": 8000}, dl=tape(320, 0.15, 0.1), rv=spring(0.2, 2.0, 10), gt=gate(-60))

# ═══ Bank 10 (index 9) — TWANG & FUZZ: Dick Dale / Shadows / QOTSA / Ronson ═══
def bank_twang():
    # A Surf Splash — Misirlou: Showman + 6G15 tank (A for gear); loud-clean-with-hair, drip after the cab.
    preset(9, 0, "Surf Splash", cls="clean", chain=["amp", "cab", "eq", "rv"], rig=OPENBACK,
           amp={"model": "Clean Meanie", "gain": 0.65, "bass": 0.45, "mid": 0.6, "treble": 0.8, "presence": 0.7, "master": 0.95, "sag": 0.25},
           cab={"lowcut": 70, "highcut": 8000}, eq=eqm(**{"100": 0, "200": -2, "400": 0, "800": 0, "1k6": 0, "3k2": 3, "level": 0}),
           rv=spring(0.6, 2.8, 0, 0.25))
    # B Apache Echo — AC15 + Meazzi (B): 430 ms + 165 ms echoes, feedback 0, in front of the amp; light Dyna Comp.
    preset(9, 1, "Apache Echo", cls="clean", chain=["gt", "cp", "dl", "dl2", "amp", "cab", "rv"], rig=CHIME,
           cp=comp("5 Creature Amp", -25, "2:1", MED, FAST),
           dl={"type": "Vintage Echo", "time": 430, "feedback": 0.12, "mix": 0.45, "width": 0.3, "age": 0.5},
           dl2=dig(165, 0.0, 0.3, 0.3),
           amp={"model": "Chime Thirty", "gain": 0.4, "bass": 0.45, "mid": 0.55, "treble": 0.6, "presence": 0.35, "master": 0.7, "sag": 0.45},
           cab={"lowcut": 90, "highcut": 9000}, rv=spring(0.12, 1.6, 0), gt=gate(-60))
    # C Desert Robot — No One Knows: solid-state heads, no pedal (A); wiry mid-push, dark, dry, SHO-style lift in front.
    preset(9, 2, "Desert Robot", cls="dirty", chain=["gt", "amp", "cab", "eq"], rig=STUDIO,
           it={"boost": 1, "boostamt": 4},
           amp={"model": "Backline Plus", "gain": 0.6, "bass": 0.4, "mid": 0.7, "treble": 0.5, "presence": 0.45, "master": 0.75, "sag": 0.05},
           cab={"lowcut": 90, "highcut": 6500, "spkdrive": "Off"},
           eq=eqm(**{"100": 0, "200": 0, "400": 3, "800": 2, "1k6": 0, "3k2": -2, "level": 0}), gt=gate(-48), rb={"enable": 0, "cab2on": 0})
    # D Moondust Glam — Ronson: "Les Paul, my Marshall with the midrange all the way up" (A); Tone Bender > cocked wah > Major.
    preset(9, 3, "Moondust Glam", cls="dirty", chain=["gt", "fz", "wh", "amp", "cab", "rv"], rig=GBROOM,
           fz={"pedal": "I Know It", "sustain": 0.8, "tone": 0.6, "volume": 0.6, "bias": 0.42, "inputtrim": 0.7, "gvol": 1.0},
           wh={"type": "Fixed", "freq": 0.62, "q": 0.55, "mix": 1.0},
           amp={"model": "Plexiglass", "gain": 0.75, "pl_vol2": 0.5, "pl_variac": 0, "bass": 0.5, "mid": 1.0, "treble": 0.6, "presence": 0.6, "master": 1.0, "sag": 0.3},
           cab={"highcut": 7500}, rv=plate(0.15, 1.4, 20), gt=gate(-50))

# ═══ Bank 11 (index 10) — PSYCH & CHIME: Tame Impala / MBV / U2 / Brian May lead ═══
def bank_psych():
    # A Innerspeaker Swirl — Parker: AC30 "roll back some of the treble and emphasise the mid-range" (B), Fuzz Face,
    #   Small Stone, Holy Grail, drive AFTER the modulation, everything to the ceiling.
    preset(10, 0, "Innerspeaker Swirl", cls="dirty", chain=["gt", "cp", "md", "fz", "dr", "amp", "cab", "dl", "rv", "eq"], rig=CHIME,
           cp=comp("5 Creature Amp", -22, "4:1", FAST, MED),
           md={"type": "Script Phaser", "rate": 0.3, "depth": 0.8, "mix": 0.5, "width": 0.6},
           fz={"pedal": "I Know It", "sustain": 0.7, "tone": 0.45, "volume": 0.6, "bias": 0.5, "inputtrim": 0.6, "gvol": 1.0},
           dr={"model": "Super Nova", "drive": 0.3, "tone": 0.45, "level": 0.6, "mix": 1.0},
           amp={"model": "Chime Thirty", "gain": 0.55, "bass": 0.5, "mid": 0.65, "treble": 0.35, "presence": 0.55, "master": 0.7, "sag": 0.5},
           cab={"highcut": 6000}, dl=tape(380, 0.3, 0.18, 0.5), rv=spring(0.3, 2.2, 10, 0.4),
           eq=eqm(**{"100": 0, "200": 0, "400": 0, "800": 0, "1k6": 0, "3k2": -2, "level": 0}), gt=gate(-52))
    # B Glide Wall — Loveless: Jazzmaster glide > fuzz > JCM800 (doc), one mic between two tremolo amps, SPX90 reverse.
    preset(10, 1, "Glide Wall", cls="dirty", chain=["fz", "amp", "cab", "md", "rv", "eq"], rig=GBROOM,
           fz={"pedal": "Italian Hero", "mode": "Ovis", "sustain": 0.75, "tone": 0.35, "volume": 0.55, "bias": 0.5, "inputtrim": 0.7, "gvol": 1.0},
           amp={"model": "Crunchy McCrunchFace", "gain": 0.6, "bass": 0.6, "mid": 0.6, "treble": 0.45, "presence": 0.35, "master": 0.55, "sag": 0.45, "sir34": 0},
           cab={"lowcut": 70, "highcut": 5500},
           md={"type": "Tremolo", "shape": "Opto", "rate": 0.45, "depth": 0.6, "mix": 1.0, "width": 0.7},
           rv=ambient(0.55, 1.8, 75, 0.6, 0.6), eq=eqm(**{"100": 0, "200": 2, "400": 0, "800": 0, "1k6": 0, "3k2": -3, "level": 0}))
    # C Streets Chime — the Edge: AC30 Normal 12:30 / Brilliant 10:30 / Treble 11 / Bass 1:00 / Cut 8:00 (B);
    #   SDD-3000s: dotted 8th (2 repeats, ~70 % wet) + quarter (1 repeat) at 125.5 BPM; Dyna Comp; the invisible TS lift.
    preset(10, 2, "Streets Chime", cls="clean", chain=["gt", "cp", "dr", "dl", "dl2", "amp", "cab", "rv"], rig=CHIME,
           cp=comp("5 Creature Amp", -24, "4:1", MED, MED),
           dr={"model": "Green Man", "drive": 0.15, "tone": 0.7, "level": 0.6, "mix": 1.0},
           dl=dig(dotted8(125.5), 0.35, 0.45, 0.3), dl2=dig(quarter(125.5), 0.2, 0.35, 0.7),
           amp={"model": "Chime Thirty", "gain": 0.45, "bass": 0.62, "mid": 0.5, "treble": 0.45, "presence": 0.1, "master": 0.75, "sag": 0.45},
           cab={"lowcut": 90, "highcut": 9000}, rv=plate(0.12, 1.6, 20), gt=gate(-55), rb={"enable": 0, "cab2on": 0})
    # D Regal Solo — May's lead: same cranked AC30 + booster; the Brighton Rock canon = 800 ms + 1600 ms Echoplexes.
    preset(10, 3, "Regal Solo", cls="dirty", chain=["gt", "dr", "amp", "cab", "dl", "dl2", "rv"], rig=CHIME,
           dr={"model": "Treble Ranger", "drive": 1.0, "tone": 0.5, "level": 1.0, "mix": 1.0},
           amp={"model": "Chime Thirty", "gain": 0.95, "bass": 0.5, "mid": 0.55, "treble": 0.4, "presence": 0.05, "master": 1.0, "sag": 0.65},
           cab={"lowcut": 100, "highcut": 9000},
           dl=tape(800, 0.4, 0.5, 1.0), dl2=tape(1600, 0.3, 0.45, 1.0), rv=plate(0.15, 1.8, 30), gt=gate(-55))

# ═══ Bank 12 (index 11) — NINE INCH NAILS (the Nail block carries each era) ═══
def bank_nin():
    # A March Stabs — "most of the time I recorded his guitars direct through the Zoom 9030" (A, Beavan): no cab, no room.
    preset(11, 0, "March Stabs", cls="dirty", chain=["gt", "nail", "amp", "cab", "eq"], rig=TIGHT57,
           nail={"mode": "Dahnward", "drive": 0.8, "tone": 0.55, "texture": 0.55, "level": 0.5},
           amp={"model": "Crunchy McCrunchFace", "gain": 0.8, "bass": 0.55, "mid": 0.3, "treble": 0.7, "presence": 0.6, "master": 0.5, "sag": 0.0, "sir34": 0},
           cab={"lowcut": 110, "highcut": 5500, "spkdrive": "Off", "roomon": 0},
           eq=eqm(**{"100": -3, "200": 0, "400": 0, "800": -4, "1k6": 0, "3k2": 2, "level": 0}), gt=gate(-40, attack=0.5, hold=40, release=60), rb={"enable": 0, "cab2on": 0})
    # B World Went Away — The Fragile: DigiTech 2112 → Boogie power amp → two Boogie 4x12s, Fuzz Factory in the chain (A, Moulder).
    preset(11, 1, "World Went Away", cls="dirty", chain=["gt", "nail", "fz", "amp", "cab", "dl", "rv"], rig=LIVEROOM,
           nail={"mode": "Delicate", "drive": 0.5, "tone": 0.5, "texture": 0.4, "level": 0.5},
           fz={"pedal": "Fuzz Zachary", "sustain": 0.6, "tone": 0.45, "volume": 0.5, "bias": 0.5, "inputtrim": 0.7, "gvol": 1.0},
           amp={"model": "Diamond Plate", "rc_mode": "CH3 Modern", "rc_variac": "Bold", "rc_rect": "Tube", "gain": 0.65, "bass": 0.6, "mid": 0.45, "treble": 0.55, "presence": 0.45, "master": 0.6, "sag": 0.5},
           cab={"lowcut": 70, "highcut": 6500}, dl=dig(quarter(129), 0.4, 0.25, 0.8), rv=ambient(0.35, 4.5, 60, 0.55),
           gt=gate(-55, release=500), rb={"enable": 0, "cab2on": 0})
    # C Broken Crush — Broken: hard, tight, cab-sim band-limited; the Red 5150 lineage of Reznor's Peavey.
    preset(11, 2, "Broken Crush", cls="dirty", chain=["gt", "nail", "amp", "cab", "eq"], rig=TIGHT57,
           nail={"mode": "Broke", "drive": 0.8, "tone": 0.5, "texture": 0.5, "level": 0.5},
           amp={"model": "Gainzilla", "channel": 1, "gain": 0.7, "bass": 0.55, "mid": 0.5, "treble": 0.65, "presence": 0.6, "resonance": 0.4, "master": 0.5, "sag": 0.0},
           cab={"lowcut": 100, "highcut": 5500, "spkdrive": "Off", "roomon": 0},
           eq=eqm(**{"100": 0, "200": 2, "400": 0, "800": -2, "1k6": 0, "3k2": 2, "level": 0}), gt=gate(-42, attack=0.5, hold=60, release=80), rb={"enable": 0, "cab2on": 0})
    # D Con Molars — With Teeth: Reaktor "as the ultimate distortion box" INTO real mic'd heads (A) — the one NIN preset with a real cab.
    preset(11, 3, "Con Molars", cls="dirty", chain=["gt", "nail", "amp", "cab", "eq"], rig=PAIR57R,
           nail={"mode": "Con Molars", "drive": 0.65, "tone": 0.5, "texture": 0.5, "level": 0.45},
           amp={"model": "Crunchy McCrunchFace", "gain": 0.6, "bass": 0.5, "mid": 0.6, "treble": 0.55, "presence": 0.5, "master": 0.6, "sag": 0.35, "sir34": 0},
           cab={"lowcut": 90, "highcut": 7000}, eq=eqm(**{"100": -2, "200": 0, "400": 0, "800": 0, "1k6": 3, "3k2": 0, "level": 0}), gt=gate(-45))

# ═══ Bank 16 (index 15) — MODERN ROCK (replaces the Nu Metal bank) ═══
def bank_modern_rock():
    # A Blackwing Rise — Alter Bridge, Blackbird: Tremonti's documented Recto numbers (gain 6, mids 4, treble 6, presence 10:30 — B) on the MT15 Lead.
    preset(15, 0, "Blackwing Rise", cls="dirty", chain=["gt", "dr", "amp", "cab", "dl"], rig=STUDIO, base=False,
           dr={"model": "Green Man", "drive": 0.15, "tone": 0.5, "level": 0.8, "mix": 1.0},
           amp={"model": "Tremont 15", "mt_mode": "Lead", "mt_bright": "Off", "gain": 0.6, "bass": 0.75, "mid": 0.4, "treble": 0.6, "presence": 0.35, "master": 0.5, "sag": 0.25},
           cab={"lowcut": 70, "highcut": 9000}, dl=dig(quarter(150), 0.3, 0.12, 0.3), gt=gate(-52))
    # B Spiral Out — Tool: VH4 gain noon / mids 5 o'clock (photo, B) → Recto CH3 Modern with the mids pushed and an un-scoop EQ before the cab.
    preset(15, 1, "Spiral Out", cls="dirty", chain=["gt", "amp", "eq", "cab", "dl"], rig=PAIR57R, base=False,
           amp={"model": "Diamond Plate", "rc_mode": "CH3 Modern", "rc_variac": "Bold", "rc_rect": "Silicon", "gain": 0.5, "bass": 0.55, "mid": 0.85, "treble": 0.55, "presence": 0.45, "master": 0.55, "sag": 0.35},
           eq=eqm(**{"100": -2, "200": 0, "400": 2, "800": 3, "1k6": 2, "3k2": -1, "level": 0}),
           cab={"lowcut": 60, "highcut": 8500}, dl=dig(quarter(158), 0.25, 0.1, 0.5), gt=gate(-52))
    # C Freedom Scratch — RATM: JCM800 2205 boost channel, bass 10 / mid 10 / treble 7 / presence 7 / master 6 (doc, Neural DSP row);
    #   2205 gain 9 ≈ 2203 gain 7; Tele → humbucker: -4 dB at the input; G12K-85 cab = Tight 57, bone dry.
    preset(15, 2, "Freedom Scratch", cls="dirty", chain=["gt", "wh", "amp", "cab", "dl"], rig=TIGHT57, base=False,
           it={"gain": -4},
           wh={"type": "Fixed", "freq": 0.58, "q": 0.6, "mix": 1.0},
           amp={"model": "Crunchy McCrunchFace", "sir34": 0, "gain": 0.7, "bass": 1.0, "mid": 1.0, "treble": 0.7, "presence": 0.7, "master": 0.6, "sag": 0.4},
           cab={"lowcut": 80, "highcut": 7500}, dl=dig(eighth(88), 0.35, 0.0, 0.3), gt=gate(-50))
    # D Boxed Bones — Alice in Chains: Bogner-modded JCM800 4010 (Facelift) / Fish-VHT (Dirt) → Beardo BE, Greenbacks off-cap.
    preset(15, 3, "Boxed Bones", cls="dirty", chain=["gt", "amp", "cab", "rv"], rig=GBROOM, base=False,
           amp={"model": "Beardo BE", "fr_channel": "BE", "fr_fat": 0, "fr_c45": 0, "fr_sat": 0, "gain": 0.6, "bass": 0.55, "mid": 0.65, "treble": 0.6, "presence": 0.55, "master": 0.5, "sag": 0.35},
           cab={"lowcut": 70, "highcut": 8000}, rv=plate(0.08, 1.2, 20), gt=gate(-55))

# ═══ Bank 6 (index 5) — JIMI HENDRIX (+ Sleep) ═══
def bank_hendrix():
    plexi67 = {"model": "Plexiglass", "gain": 0.75, "pl_vol2": 0.5, "pl_variac": 0, "bass": 0.45, "mid": 0.6, "treble": 0.7, "presence": 0.5, "master": 1.0, "sag": 0.5}
    # A Mauve Haze — Purple Haze rhythm: germanium Fuzz Face on max (B) into the Super 100, no wah in Feb 1967 (B).
    preset(5, 0, "Mauve Haze", cls="dirty", chain=["gt", "fz", "amp", "cab", "rv"], rig=GBROOM,
           fz={"pedal": "I Know It", "sustain": 1.0, "tone": 0.5, "volume": 0.65, "bias": 0.5, "getemp": 0.5, "inputtrim": 0.35, "gvol": 1.0},
           amp=plexi67, cab={"highcut": 8000}, rv=plate(0.15, 1.8, 20), gt=gate(-60))
    # B Hazy Solo — Fuzz Face → Octavia (A, Mayer: "creates an upper octave double … in the solo"); neck pickup above the 12th fret.
    preset(5, 1, "Hazy Solo", cls="dirty", chain=["gt", "fz", "fz2", "amp", "cab", "rv"], rig=GBROOM,
           fz={"pedal": "I Know It", "sustain": 1.0, "tone": 0.5, "volume": 0.6, "bias": 0.5, "getemp": 0.5, "inputtrim": 0.35, "gvol": 1.0},
           fz2={"pedal": "Octavius", "sustain": 0.6, "tone": 0.4, "volume": 0.6, "bias": 0.5, "inputtrim": 0.5, "gvol": 1.0},
           amp=plexi67, cab={"highcut": 8000}, rv=plate(0.18, 2.0, 20), gt=gate(-60))
    # C Little Feather — Little Wing: Marshall on the edge, the lead through a makeshift organ Leslie (A), no delay (doc).
    preset(5, 2, "Little Feather", cls="clean", chain=["gt", "cp", "amp", "cab", "md", "rv"], rig=GBROOM,
           cp=comp("Once76", -24, "4:1", MED, FAST),
           amp={"model": "Plexiglass", "gain": 0.45, "pl_vol2": 0.35, "pl_variac": 0, "bass": 0.5, "mid": 0.6, "treble": 0.6, "presence": 0.4, "master": 1.0, "sag": 0.4},
           cab={"lowcut": 90, "highcut": 9000}, md={"type": "Rotary", "rate": 0.15, "depth": 0.7, "mix": 0.5, "width": 0.8},
           rv=plate(0.22, 2.2, 30, 0.4), gt=gate(-60))
    # D Holy Smoke — Sleep: "Green Matamp, MXR distortion, and power amp" (A, Pike); compressor set hot in front is his sustain engine (A).
    preset(5, 3, "Holy Smoke", cls="dirty", chain=["gt", "cp", "dr", "amp", "cab", "rv"], rig=GBROOM,
           cp=comp("5 Creature Amp", -30, "8:1", FAST, MED),
           dr={"model": "Green Man", "drive": 0.35, "tone": 0.45, "level": 0.8, "mix": 1.0},
           amp={"model": "Tangerang", "channel": 0, "gain": 0.65, "bass": 0.7, "mid": 0.6, "treble": 0.5, "presence": 0.5, "master": 0.6, "sag": 0.6},
           cab={"lowcut": 60, "highcut": 7000}, rv=spring(0.06, 1.4), gt=gate(-55))

# ═══ Bank 7 (index 6) — MASTODON · Crack the Skye (+ Sunn O)))) ═══
def bank_mastodon():
    # Crack the Skye was cut on JCM800 2203s into Mills V30 4x12s (A, Premier Guitar); the Oranges are the 2010+ live rig.
    # A Skye Crusher — Kelliher's articulate chug with the Oblivion-intro rotary swirl at low mix.
    preset(6, 0, "Skye Crusher", cls="dirty", chain=["gt", "dr", "amp", "cab", "md", "rv"], rig=PAIR57R,
           dr={"model": "Green Man", "drive": 0.2, "tone": 0.5, "level": 0.85, "mix": 1.0},
           amp={"model": "Crunchy McCrunchFace", "sir34": 0, "gain": 0.75, "bass": 0.6, "mid": 0.65, "treble": 0.6, "presence": 0.6, "master": 0.7, "sag": 0.35},
           cab={"lowcut": 70, "highcut": 9000}, md={"type": "Rotary", "rate": 0.2, "depth": 0.5, "mix": 0.3, "width": 0.6},
           rv=plate(0.06, 1.2), gt=gate(-52), rb={"enable": 0, "cab2on": 0})
    # B Skye (No Mod) — the riff layer: the 2203KK's extra front-end drive ≈ the SIR mod, dry close 57, no second amp.
    preset(6, 1, "Skye (No Mod)", cls="dirty", chain=["gt", "dr", "amp", "cab"], rig=TIGHT57,
           dr={"model": "Green Man", "drive": 0.15, "tone": 0.55, "level": 0.9, "mix": 1.0},
           amp={"model": "Crunchy McCrunchFace", "sir34": 1, "gain": 0.7, "bass": 0.65, "mid": 0.65, "treble": 0.55, "presence": 0.65, "master": 0.75, "sag": 0.3},
           cab={"lowcut": 75, "highcut": 9000}, gt=gate(-50), rb={"enable": 0, "cab2on": 0})
    # C Skye Soar — Hinds: JMP 2203 + TS-9 (A) + subtle Phase 90 (B) + DD-6 quarter at 132 BPM (Oblivion).
    preset(6, 2, "Skye Soar", cls="dirty", chain=["gt", "dr", "amp", "cab", "md", "dl", "rv"], rig=PAIR57R,
           dr={"model": "Green Man", "drive": 0.35, "tone": 0.55, "level": 0.8, "mix": 1.0},
           amp={"model": "Crunchy McCrunchFace", "sir34": 0, "gain": 0.7, "bass": 0.55, "mid": 0.7, "treble": 0.6, "presence": 0.55, "master": 0.7, "sag": 0.45},
           cab={"lowcut": 90, "highcut": 9000}, md={"type": "Script Phaser", "rate": 0.25, "depth": 0.5, "mix": 0.25, "width": 0.5},
           dl=dig(quarter(132), 0.3, 0.2, 0.6), rv=plate(0.12, 1.6), gt=gate(-55))
    # D Solar Monolith — Sunn O))): Model Ts are "a loud, powerful, clean amp that takes pedals really well" (A); the grind is the
    #   Life Pedal + sheer volume; feedback is compositional, so no gate; octave kept low for chord drones (A, manual).
    preset(6, 3, "Solar Monolith", cls="sunn", chain=["dr", "amp", "cab", "dl", "rv"], rig=CAVE,
           dr={"model": "New Dawn", "drive": 0.7, "tone": 0.4, "level": 0.65, "mix": 1.0, "octave": 0.25},
           amp={"model": "Doom Daddy", "sunn_link": "Parallel", "sunn_bright1": 0, "sunn_bright2": 0, "gain": 0.85, "sunn_vol2": 0.85,
                "bass": 0.8, "mid": 0.5, "treble": 0.5, "presence": 0.4, "master": 0.9, "sag": 0.7},
           cab={"lowcut": 40, "highcut": 6000}, dl=tape(420, 0.5, 0.2, 0.7), rv=ambient(0.25, 6.0, 40, 0.6))

# ═══ Bank 8 (index 7) — A PERFECT CIRCLE + PERIPHERY ═══
def bank_apc_periphery():
    # A Vanishing Drive — Howerdel's Friedman-modded '78 Super Lead (A) — a BE-100 is the closer twin; V30 4x12 (A, live).
    preset(7, 0, "Vanishing Drive", cls="dirty", chain=["gt", "amp", "cab", "rv"], rig=STUDIO,
           amp={"model": "Beardo BE", "fr_channel": "BE", "fr_fat": 0, "fr_c45": 0, "fr_sat": 0, "gain": 0.55, "bass": 0.5, "mid": 0.65, "treble": 0.55, "presence": 0.45, "master": 0.6, "sag": 0.4},
           cab={"lowcut": 90, "highcut": 8000}, rv=plate(0.08, 1.4, 20), gt=gate(-52))
    # B Dreamlit Shimmer — Orestes / Rose: dry-tracked clean with CE-1-style chorus, quarter-note echo and a bloom added after.
    preset(7, 1, "Dreamlit Shimmer", cls="clean", chain=["cp", "amp", "cab", "md", "dl", "rv"], rig=OPENBACK,
           cp=comp("5 Creature Amp", -25, "4:1", MED, FAST),
           amp={"model": "Clean Meanie", "gain": 0.35, "bass": 0.5, "mid": 0.5, "treble": 0.6, "presence": 0.5, "master": 0.8, "sag": 0.3},
           cab={"lowcut": 70, "highcut": 10000, "spkdrive": "Off"}, md={"type": "Lush-2", "rate": 0.3, "depth": 0.45, "mix": 0.5, "width": 1.0},
           dl=dig(quarter(144), 0.35, 0.25, 1.0), rv=ambient(0.3, 3.5, 40, 0.45))
    # C Flatliner — Periphery II: Axe-Fx as preamp into a real 5150 power amp + mic'd Mesa 4x12 (A); Misha's documented
    #   6160 patch: drive 6.5 / bass 5 / mid 5 / treble 6 / presence 5 / depth 5.5 / master 4, TS drive 0 level 10, 7.5 k LPF.
    preset(7, 2, "Flatliner", cls="dirty", chain=["gt", "dr", "amp", "cab", "eq"], rig=TIGHT57,
           dr={"model": "Green Man", "drive": 0.0, "tone": 0.5, "level": 1.0, "mix": 1.0},
           amp={"model": "Gainzilla", "channel": 1, "gain": 0.62, "bass": 0.5, "mid": 0.5, "treble": 0.6, "presence": 0.5, "resonance": 0.55, "master": 0.4, "sag": 0.2},
           cab={"lowcut": 100, "highcut": 7500, "spkdrive": "Off"}, eq=eqm(**{"100": -2, "200": 0, "400": 0, "800": 0, "1k6": 0, "3k2": -3, "level": 0}),
           gt=gate(-48, attack=0.5, hold=60, release=120), rb={"enable": 0, "cab2on": 0})
    # D Prayer Djent — Periphery III: fully direct, tone-matched Zilla 2x12; "the Friedman model" (B) → HBE; Sentry gate (A).
    preset(7, 3, "Prayer Djent", cls="dirty", chain=["gt", "dr", "amp", "cab", "eq"], rig=TIGHT57,
           dr={"model": "Green Man", "drive": 0.0, "tone": 0.5, "level": 1.0, "mix": 1.0},
           amp={"model": "Beardo BE", "fr_channel": "HBE", "fr_fat": 0, "fr_c45": 0, "fr_sat": 0, "gain": 0.6, "bass": 0.45, "mid": 0.55, "treble": 0.6, "presence": 0.5, "master": 0.4, "sag": 0.15},
           cab={"lowcut": 100, "highcut": 7500, "spkdrive": "Off"}, eq=eqm(**{"100": -2, "200": 0, "400": 0, "800": 0, "1k6": 0, "3k2": -3, "level": 0}),
           gt=gate(-48, attack=0.5, hold=60, release=120), rb={"enable": 0, "cab2on": 0})

# ═══ Bank 9 (index 8) — VIBE & WAVE: Trower / Police / Cure / Chic ═══
def bank_vibe():
    # A Bridge Vibe — Trower's documented 1959: Presence 0 / Bass 2 / Middle 8 / Treble 2 / Vol I 7, Input I only (B, Guitar World);
    #   Uni-Vibe Vol 10 / Intensity 7 / Speed 3 (B); Neve compression (A). Treble up a notch for humbuckers.
    preset(8, 0, "Bridge Vibe", cls="dirty", chain=["gt", "cp", "md", "amp", "cab", "rv"], rig=GBROOM,
           it={"boost": 1, "boostamt": 3}, cp=comp("Once76", -22, "4:1", MED, FAST),
           md={"type": "Uni-Verse", "rate": 0.28, "depth": 0.7, "mix": 1.0, "width": 0.0},
           amp={"model": "Plexiglass", "gain": 0.7, "pl_vol2": 0.0, "pl_variac": 0, "bass": 0.2, "mid": 0.8, "treble": 0.3, "presence": 0.0, "master": 1.0, "sag": 0.55},
           cab={"lowcut": 60, "highcut": 7000}, rv=plate(0.18, 1.8, 30, 0.6), gt=gate(-60))
    # B Bottle Jangle — Summers: Dyna Comp > Phase 90 > Electric Mistress (toward chorus) > Echoplex > Cornish-modded 1959 (A/B); 151 BPM → 298 ms.
    preset(8, 1, "Bottle Jangle", cls="clean", chain=["cp", "md", "dl", "amp", "cab"], rig=GBROOM,
           cp=comp("5 Creature Amp", -28, "8:1", FAST, MED),
           md={"type": "Flanger", "rate": 0.12, "depth": 0.45, "mix": 0.4, "width": 0.6},
           dl=tape(dotted8(151), 0.3, 0.35, 0.3),
           amp={"model": "Plexiglass", "gain": 0.45, "pl_vol2": 0.25, "pl_variac": 0, "bass": 0.4, "mid": 0.6, "treble": 0.7, "presence": 0.5, "master": 1.0, "sag": 0.4},
           cab={"highcut": 9000})
    # C Forest Wash — A Forest: Jazzmaster → JC-120 (A, SoS): stereo chorus + studio flangers + two-speed tape delays (162 BPM).
    preset(8, 2, "Forest Wash", cls="clean", chain=["cp", "amp", "cab", "md", "md2", "dl", "dl2", "rv"], rig=OPENBACK,
           cp=comp("Once76", -24, "4:1", MED, MED),
           amp={"model": "Hi-Volt", "gain": 0.5, "bass": 0.4, "mid": 0.5, "treble": 0.65, "presence": 0.5, "master": 0.85, "sag": 0.1},
           cab={"lowcut": 90, "highcut": 9000, "spkdrive": "Off"},
           md={"type": "Flanger", "rate": 0.18, "depth": 0.6, "mix": 0.5, "width": 1.0},
           md2={"type": "Lush-2", "rate": 0.35, "depth": 0.4, "mix": 0.4, "width": 1.0},
           dl=tape(quarter(162), 0.4, 0.3, 1.0), dl2=tape(eighth(162), 0.2, 0.2, 1.0), rv=plate(0.25, 2.6, 20))
    # D Disco Chuck — Le Freak: Deluxe Reverb Normal ch Vol 4 / Treble 7 / Bass 5 under a DI through the Neve strip (B) — dry.
    preset(8, 3, "Disco Chuck", cls="clean", chain=["cp", "amp", "cab", "eq"], rig=OPENBACK,
           cp=comp("Once76", -20, "4:1", FAST, FAST),
           amp={"model": "Clean Meanie", "gain": 0.45, "bass": 0.5, "mid": 0.5, "treble": 0.7, "presence": 0.5, "master": 1.0, "sag": 0.2},
           cab={"lowcut": 100, "highcut": 16000, "spkdrive": "Off", "roomon": 0},
           eq=eqm(**{"100": -6, "200": -5, "400": 0, "800": 0, "1k6": 2, "3k2": 5, "level": 0}))

# ═══ Bank 13 (index 12) — MICROTONAL / RADIOHEAD / GOJIRA ═══
def bank_radiohead_gojira():
    # A Quarter-Tone Lead — the microtonal-shimmer showcase (kept), on Greenbacks in the room.
    preset(12, 0, "Quarter-Tone Lead", cls="dirty", rig=GBROOM, cab={"highcut": 8500})
    # B Anyone Can Play Guitar — Pablo Honey: Shredmaster-style grit into a clean solid-state platform (RAT stands in).
    preset(12, 1, "Anyone Can Play Guitar", cls="dirty", chain=["gt", "dr", "amp", "cab", "rv"], rig=OPENBACK,
           dr={"model": "Dear Rodent Boy", "drive": 0.6, "tone": 0.45, "level": 0.6, "mix": 1.0},
           amp={"model": "Clean Meanie", "gain": 0.35, "bass": 0.5, "mid": 0.5, "treble": 0.6, "presence": 0.5, "master": 0.85, "sag": 0.2},
           cab={"highcut": 8000, "spkdrive": "Off"}, rv=plate(0.12, 1.2, 20), gt=gate(-58))
    # C Winterborn — Born in Winter (L'Enfant Sauvage): the 5150 III's Blue channel clean, one 57, Carbon Copy quarter at 128.
    preset(12, 2, "Winterborn", cls="clean", chain=["cp", "amp", "cab", "dl", "rv"], rig=TIGHT57,
           cp=comp("Once76", -24, "4:1", FAST, MED),
           amp={"model": "Gainzilla", "channel": 0, "gain": 0.35, "bass": 0.45, "mid": 0.5, "treble": 0.6, "presence": 0.5, "resonance": 0.4, "master": 0.75, "sag": 0.3},
           cab={"lowcut": 70, "highcut": 10000, "spkdrive": "Off"}, dl=dig(quarter(128), 0.3, 0.18), rv=plate(0.2, 2.2, 30))
    # D Castaway Groove — Stranded (Magma): 5150IIIS EL34 won a blind A/B, one SM57, "not processed", tuner + gate + KHDK (A).
    preset(12, 3, "Castaway Groove", cls="dirty", chain=["gt", "dr", "amp", "cab", "eq"], rig=TIGHT57,
           dr={"model": "Green Man", "drive": 0.15, "tone": 0.55, "level": 0.8, "mix": 1.0},
           amp={"model": "Gainzilla", "channel": 1, "gain": 0.55, "bass": 0.55, "mid": 0.55, "treble": 0.6, "presence": 0.55, "resonance": 0.6, "master": 0.5, "sag": 0.25},
           cab={"lowcut": 75, "highcut": 9000, "spkdrive": "Off"}, eq=eqm(**{"100": -2, "200": 0, "400": 0, "800": 1, "1k6": 1, "3k2": 0, "level": 0}),
           gt=gate(-45, attack=0.5, hold=60, release=120))

# ═══ Bank 14 (index 13) — MESA MARK / RECTO: Metallica · BTBAM · Soundgarden ═══
def bank_mark():
    # A Marionette Master — Rasmussen's session notes (B): IIC+ Vol 1 7.5, Treble 7, Bass 2, Mid 4, Master 5, Lead Drive 3.75,
    #   Presence 4.5, GEQ "V" (80 up / 750 deep cut / 2200 + 6600 up); 57 + omni + tube condenser array → Studio Pair.
    preset(13, 0, "Marionette Master", cls="dirty", chain=["gt", "amp", "cab", "eq"], rig=STUDIO,
           amp={"model": "Cali V", "mv_mode": "IIC+", "mv_eqpreset": "Custom", "mv_geq0": 0.7, "mv_geq1": 0.5, "mv_geq2": 0.15, "mv_geq3": 0.7, "mv_geq4": 0.7,
                "gain": 0.45, "bass": 0.2, "mid": 0.4, "treble": 0.7, "presence": 0.45, "master": 0.5, "sag": 0.3},
           cab={"lowcut": 70, "highcut": 9000, "spkdrive": "Off"}, eq=eqm(**{"100": 2, "200": 0, "400": -2, "800": -1, "1k6": 1, "3k2": 2, "level": 0}), gt=gate(-48))
    # B Spectrum Rhythm — Colors: rackmount Dual Rectifier + Recto 4x12 (B), TS808 boost — CH3 Modern, Bold, Silicon.
    preset(13, 1, "Spectrum Rhythm", cls="dirty", chain=["gt", "dr", "amp", "cab"], rig=PAIR57R,
           dr={"model": "Green Man", "drive": 0.15, "tone": 0.55, "level": 0.85, "mix": 1.0},
           amp={"model": "Diamond Plate", "rc_mode": "CH3 Modern", "rc_variac": "Bold", "rc_rect": "Silicon", "gain": 0.6, "bass": 0.5, "mid": 0.45, "treble": 0.65, "presence": 0.55, "master": 0.45, "sag": 0.2},
           cab={"lowcut": 70, "highcut": 9500, "spkdrive": "Off"}, gt=gate(-46))
    # C Spectrum Lead — the singing Boogie lead: Mk IV mode with a vocal mid push, tap delay ~400 ms.
    preset(13, 2, "Spectrum Lead", cls="dirty", chain=["gt", "dr", "amp", "cab", "dl", "rv"], rig=PAIR57R,
           dr={"model": "Green Man", "drive": 0.3, "tone": 0.6, "level": 0.8, "mix": 1.0},
           amp={"model": "Cali V", "mv_mode": "Mk IV", "mv_eqpreset": "Custom", "mv_geq0": 0.55, "mv_geq1": 0.55, "mv_geq2": 0.45, "mv_geq3": 0.6, "mv_geq4": 0.5,
                "gain": 0.65, "bass": 0.4, "mid": 0.6, "treble": 0.6, "presence": 0.5, "master": 0.5, "sag": 0.35},
           cab={"lowcut": 90, "highcut": 8000, "spkdrive": "Off"}, dl=dig(400, 0.35, 0.22, 0.6), rv=plate(0.15, 1.8, 40), gt=gate(-50))
    # D Grunge Drop — Thayil: Peavey VTM-120 (a JCM800-pattern head); "lows up, mids ~11 o'clock, treble ~2 o'clock" (A).
    preset(13, 3, "Grunge Drop", cls="dirty", chain=["gt", "amp", "cab", "rv"], rig=PAIR57R,
           amp={"model": "Crunchy McCrunchFace", "sir34": 0, "gain": 0.65, "bass": 0.7, "mid": 0.45, "treble": 0.65, "presence": 0.5, "master": 0.6, "sag": 0.45},
           cab={"lowcut": 60, "highcut": 8000}, rv=spring(0.08, 1.4, 10), gt=gate(-50))

# ═══ Bank 15 (index 14) — MUSE (+ Retro Poland) ═══
def bank_muse():
    # A Plug-In Junior — Bellamy 2001: "volume on full, with the gain really low" (A), the Fuzz Factory as the distortion.
    preset(14, 0, "Plug-In Junior", cls="dirty", chain=["gt", "fz", "amp", "cab", "dl", "rv"], rig=PAIR57R,
           fz={"pedal": "Fuzz Zachary", "sustain": 0.8, "tone": 0.55, "volume": 0.7, "bias": 0.75, "inputtrim": 0.5, "getemp": 0.5, "gvol": 1.0},
           amp={"model": "Crunchy McCrunchFace", "sir34": 0, "gain": 0.35, "bass": 0.55, "mid": 0.6, "treble": 0.55, "presence": 0.5, "master": 0.9, "sag": 0.5},
           cab={"lowcut": 70, "highcut": 8000}, dl=dig(eighth(136), 0.2, 0.1, 0.3), rv=plate(0.1, 1.5, 20), gt=gate(-50), rb={"enable": 0, "cab2on": 0})
    # B Knights of Fuzz — Knights of Cydonia: Diezel-era gallop (Gainzilla Red stands in) + Phase 90 on the gallop, dotted 8th at 136.
    preset(14, 1, "Knights of Fuzz", cls="dirty", chain=["gt", "fz", "amp", "cab", "md", "dl", "rv"], rig=PAIR57R,
           fz={"pedal": "Fuzz Zachary", "sustain": 0.75, "tone": 0.6, "volume": 0.65, "bias": 0.7, "inputtrim": 0.5, "getemp": 0.5, "gvol": 1.0},
           amp={"model": "Gainzilla", "channel": 1, "gain": 0.5, "bass": 0.5, "mid": 0.6, "treble": 0.55, "presence": 0.5, "resonance": 0.5, "master": 0.5, "sag": 0.3},
           cab={"lowcut": 75, "highcut": 8500, "spkdrive": "Off"}, md={"type": "Phaser", "rate": 0.3, "depth": 0.8, "mix": 1.0, "width": 0.5},
           dl={"type": "Seraph", "pattern": "Dotted 8th", "time": dotted8(136), "feedback": 0.3, "mix": 0.15, "width": 0.5},
           rv=plate(0.15, 2.0, 30), gt=gate(-46), rb={"enable": 0, "cab2on": 0})
    # C Retro Poland — the user's build, on the open-back rig.
    preset(14, 2, "Retro Poland", cls="clean", rig=OPENBACK, cab={"highcut": 8000})
    # D Stockholm Riff — Stockholm Syndrome (Absolution): Fuzz Factory into the Diezel-era high gain, tight eighths at 122.
    preset(14, 3, "Stockholm Riff", cls="dirty", chain=["gt", "fz", "amp", "cab", "dl"], rig=PAIR57R, base=False,
           fz={"pedal": "Fuzz Zachary", "sustain": 0.85, "tone": 0.5, "volume": 0.7, "bias": 0.6, "inputtrim": 0.5, "getemp": 0.5, "gvol": 1.0},
           amp={"model": "Gainzilla", "channel": 1, "gain": 0.6, "bass": 0.55, "mid": 0.6, "treble": 0.55, "presence": 0.5, "resonance": 0.55, "master": 0.5, "sag": 0.3},
           cab={"lowcut": 75, "highcut": 8500, "spkdrive": "Off"}, dl=dig(eighth(122), 0.2, 0.08, 0.3), gt=gate(-48))

# ═══ Bank 17 (index 16) — HEX AMBIENT ═══
def bank_ambient():
    # A Sweet Dispersion — Temper Trap: AC30 on the edge, DD-20 dotted eighth at 129.5, light comp; the repeats nearly as loud as the notes.
    preset(16, 0, "Sweet Dispersion", cls="clean", chain=["gt", "cp", "amp", "cab", "dl", "rv"], rig=CHIME,
           cp=comp("5 Creature Amp", -22, "4:1", MED, MED),
           amp={"model": "Chime Thirty", "gain": 0.55, "bass": 0.4, "mid": 0.5, "treble": 0.6, "presence": 0.55, "master": 0.7, "sag": 0.5},
           cab={"highcut": 10000}, dl={"type": "Seraph", "pattern": "Dotted 8th", "time": dotted8(129.5), "feedback": 0.5, "mix": 0.45, "width": 0.7},
           rv=plate(0.18, 1.8, 20), gt=gate(-58))
    # B Homesick Saucer — Subterranean Homesick Alien: blackface clean, opto tremolo bed, Space Echo chords (395 ms), spring.
    preset(16, 1, "Homesick Saucer", cls="clean", chain=["cp", "amp", "cab", "md", "dl", "rv"], rig=OPENBACK,
           cp=comp("5 Creature Amp", -24, "4:1", MED, MED),
           amp={"model": "Clean Meanie", "gain": 0.4, "bass": 0.5, "mid": 0.45, "treble": 0.6, "presence": 0.5, "master": 0.75, "sag": 0.4},
           cab={"highcut": 10000, "spkdrive": "Off"}, md={"type": "Tremolo", "shape": "Opto", "rate": 0.42, "depth": 0.55, "mix": 1.0, "width": 0.3},
           dl=tape(395, 0.45, 0.3, 0.7), rv=spring(0.25, 2.2, 15))
    # C Hand in Cloud — Your Hand in Mine: Fender clean, DL4/DE7 stacked echoes (391 + 522 ms at 115), RV-3 "cloud".
    preset(16, 2, "Hand in Cloud", cls="clean", chain=["cp", "amp", "cab", "dl", "dl2", "rv"], rig=OPENBACK,
           cp=comp("5 Creature Amp", -22, "4:1", MED, SLOW),
           amp={"model": "Clean Meanie", "gain": 0.35, "bass": 0.5, "mid": 0.45, "treble": 0.65, "presence": 0.5, "master": 0.85, "sag": 0.35},
           cab={"lowcut": 90, "highcut": 11000, "spkdrive": "Off"},
           dl={"type": "Seraph", "pattern": "Dotted 8th", "time": dotted8(115), "feedback": 0.55, "mix": 0.35, "width": 0.8},
           dl2=dig(quarter(115), 0.45, 0.25, 0.8), rv=ambient(0.3, 4.0, 40, 0.4))
    # D I Saw a Deer — the user's showcase build, on the open-back rig.
    preset(16, 3, "I Saw a Deer", cls="clean", rig=OPENBACK, cab={"highcut": 10500})

# ═══ Bank 18 (index 17) — CLASSIC ROCK / METAL ═══
def bank_classic():
    # A Frayed Justice — ...And Justice: the borrowed IIC+ with a B&B EQ in the loop, triple-tracked, dry, scooped hard (B); no second amp.
    preset(17, 0, "Frayed Justice", cls="dirty", chain=["gt", "amp", "cab", "eq"], rig=TIGHT57,
           amp={"model": "Cali V", "mv_mode": "IIC+", "mv_eqpreset": "Custom", "mv_geq0": 0.7, "mv_geq1": 0.5, "mv_geq2": 0.08, "mv_geq3": 0.7, "mv_geq4": 0.7,
                "gain": 0.5, "bass": 0.3, "mid": 0.25, "treble": 0.75, "presence": 0.5, "master": 0.5, "sag": 0.2},
           cab={"lowcut": 80, "highcut": 8500, "spkdrive": "Off"}, eq=eqm(**{"100": 2, "200": -1, "400": -3, "800": -2, "1k6": 1, "3k2": 2, "level": 0}),
           gt=gate(-46, attack=0.5, hold=60, release=120), rb={"enable": 0, "cab2on": 0})
    # B Brown Sound '84 — 1968 Super Lead all six knobs on 10, variac ~90 V, Phase 90 in front, Echoplex after, 10 ms plate (A/B).
    preset(17, 1, "Brown Sound '84", cls="dirty", chain=["gt", "cp", "md", "amp", "cab", "dl", "rv"], rig=GBROOM,
           cp=comp("Once76", -18, "4:1", MED, MED),
           md={"type": "Script Phaser", "rate": 0.3, "depth": 0.8, "mix": 1.0, "width": 0.5},
           amp={"model": "Plexiglass", "gain": 1.0, "pl_vol2": 1.0, "pl_variac": 1, "bass": 1.0, "mid": 1.0, "treble": 0.85, "presence": 1.0, "master": 1.0, "sag": 0.65},
           cab={"lowcut": 70, "highcut": 8000}, dl={"type": "Vintage Echo", "time": 280, "feedback": 0.25, "mix": 0.12, "width": 0.5, "age": 0.4},
           rv=plate(0.15, 1.6, 10), gt=gate(-55))
    # C Sister's Singer — the user's cranked non-master Plexi, on Greenbacks in the room.
    preset(17, 2, "Sister's Singer", cls="dirty", rig=GBROOM, cab={"highcut": 8000})
    # D Jungle Sleaze — the Welcome to the Jungle INTRO: the S.I.R. Levi-modded 1959 (A, Clink → SIR mod ON) through the
    #   cascading echo (the SRV-2000's delay mode on the record): a quarter note at 124 BPM, three-four repeats, wet.
    preset(17, 3, "Jungle Sleaze", cls="dirty", chain=["gt", "amp", "cab", "dl", "rv"], rig=TIGHT57,
           amp={"model": "Crunchy McCrunchFace", "sir34": 1, "gain": 0.7, "bass": 0.6, "mid": 0.65, "treble": 0.6, "presence": 0.55, "master": 0.7, "sag": 0.5},
           cab={"lowcut": 75, "highcut": 8500}, dl=dig(quarter(124), 0.5, 0.45, 0.6), rv=plate(0.1, 1.4, 25), gt=gate(-55))

# ═══ Bank 19 (index 18) — LOW END: the Blue Liner (Ampeg SVT twin) ═══════════
def bank_low_end():
    # A Round Trip — SVT clean and round: light comp, 8x10 close-miked, dry. (The Orange twin on clean pick
    #   attacks passes a sub-ms transient 20 dB over its RMS that no block catches — it lives on the presets
    #   whose front end already squashes the pick: Fuzz Wall and the Helsinki B7K pair.)
    preset(18, 0, "Round Trip", cls="bass", chain=["gt", "cp", "amp", "cab"], rig=B810,
           amp={"model": "Blue Liner", "gain": 0.35, "bass": 0.55, "mid": 0.5, "treble": 0.45, "master": 0.62,
                "sv_ultralo": 0, "sv_ultrahi": 0, "sv_midfreq": "800 Hz"},
           cab={"lowcut": 38, "highcut": 9000}, cp=comp("5 Creature Amp", -20, "4:1", MED, MED), gt=gate(-50))
    # B Fridge Grind — driven SVT, Ultra-Hi, 220 Hz mid push; the twin's own power section grinds. (The Orange twin
    #   driven past ~0.4 spikes 20 dB over its RMS on the test DI, so it takes the clean bass presets instead.)
    preset(18, 1, "Fridge Grind", cls="bass", chain=["gt", "amp", "cab"], rig=B810,
           amp={"model": "Blue Liner", "gain": 0.72, "bass": 0.5, "mid": 0.65, "treble": 0.5, "master": 0.78,
                "sv_ultralo": 0, "sv_ultrahi": 1, "sv_midfreq": "220 Hz"},
           cab={"lowcut": 38, "highcut": 8500}, gt=gate(-50))
    preset(18, 2, "Motor City", cls="bass", chain=["gt", "cp", "amp", "cab"], rig=B115,
           amp={"model": "Blue Liner", "gain": 0.30, "bass": 0.6, "mid": 0.45, "treble": 0.28, "master": 0.62,
                "sv_ultralo": 1, "sv_ultrahi": 0, "sv_midfreq": "220 Hz"},
           cab={"lowcut": 40, "highcut": 6500, "roomon": 0}, cp=comp("5 Creature Amp", -18, "8:1", MED, SLOW), gt=gate(-50))
    # D Fuzz Wall — Muff into a CLEAN Citrus 200 (the fuzz is line-hot; the Orange stays a clean platform), 4x10 + horn.
    preset(18, 3, "Fuzz Wall", cls="bass", chain=["gt", "fz", "amp", "cab"], cab_ir="@bass410h",
           amp={"model": "Citrus 200", "gain": 0.35, "bass": 0.5, "mid": 0.5, "treble": 0.45, "master": 0.7, "sag": 0.4},
           cab={"lowcut": 50, "highcut": 9000, "spkdrive": "Physical", "micpos": 0.05, "micdist": 0.05},
           gt=gate(-50))

# ═══ Bank 20 (index 19) — HELSINKI: the Darkglass B7K into a clean Blue Liner ═══
def bank_helsinki():
    common = dict(amp={"model": "Blue Liner", "gain": 0.30, "bass": 0.55, "mid": 0.5, "treble": 0.45, "master": 0.62,
                       "sv_ultralo": 0, "sv_ultrahi": 0, "sv_midfreq": "800 Hz"},
                  gt=gate(-50))
    # Clean + Rock ride a clean Citrus 200 (Orange AD200B twin) — the B7K's own squash tames the pick, the Orange adds the girth.
    citrus = dict(amp={"model": "Citrus 200", "gain": 0.35, "bass": 0.5, "mid": 0.5, "treble": 0.45, "master": 0.7, "sag": 0.4}, gt=gate(-50))
    preset(19, 0, "Helsinki Clean", cls="bass", chain=["gt", "dr", "amp", "cab"], rig=B810,
           dr={"model": "Helsinki Grind", "drive": 0.25, "tone": 0.5, "level": 0.6},
           cab={"lowcut": 45, "highcut": 9000}, **citrus)
    preset(19, 1, "Helsinki Rock", cls="bass", chain=["gt", "dr", "amp", "cab"], rig=B810,
           dr={"model": "Helsinki Grind", "drive": 0.75, "tone": 0.5, "level": 0.6},
           cab={"lowcut": 45, "highcut": 9000}, **citrus)
    preset(19, 2, "Helsinki Heavy", cls="bass", chain=["gt", "dr", "amp", "cab"], cab_ir="@bass410h",
           dr={"model": "Helsinki Grind", "drive": 1.0, "tone": 0.5, "level": 0.6},
           cab={"lowcut": 38, "highcut": 9000, "spkdrive": "Physical", "micpos": 0.05, "micdist": 0.05}, **common)
    preset(19, 3, "Helsinki Djent", cls="bass", chain=["gt", "dr", "amp", "cab"], cab_ir="@bass410h",
           dr={"model": "Helsinki Grind", "drive": 1.0, "tone": 0.5, "level": 0.6},
           cab={"lowcut": 38, "highcut": 9000, "spkdrive": "Physical", "micpos": 0.05, "micdist": 0.05}, **common)

def build():
    bank_stock()
    bank_nirvana()
    bank_ghost_opus()
    bank_ghost_impera()
    bank_floyd()
    bank_hendrix()
    bank_mastodon()
    bank_apc_periphery()
    bank_vibe()
    bank_twang()
    bank_psych()
    bank_nin()
    bank_radiohead_gojira()
    bank_mark()
    bank_muse()
    bank_modern_rock()
    bank_ambient()
    bank_classic()
    bank_low_end()
    bank_helsinki()
