#!/usr/bin/env python3
"""Assert practice.ttl and the plugin's port enum describe the same ports.

A port index that drifts between the TTL and the C++ enum does not fail to
build and does not crash: the host simply connects the wrong buffer to the
wrong control, so "Snare Decay" silently drives hat tone. This repo has been
bitten by exactly that class of bug before, so it gets a gate rather than
care.

Also checks the Pattern port's enumeration against the real groove table, so
adding a groove without widening lv2:maximum is caught here instead of as a
dropdown whose last entries do nothing.

The Turtle-level checks (syntax, port defaults within range, the enabled
designation) need rdflib. They are SKIPPED with a notice if it is absent rather
than failing, so the script still works on the Windows-side Python; run it under
WSL to get the full set.

Run:  python build-tools/practice_port_check.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CPP = ROOT / "lv2" / "practice" / "practice_plugin.cpp"
TTL = ROOT / "lv2" / "practice.ttl"
PATTERNS_H = ROOT / "deps" / "guitar-amp-simulator" / "include" / "DrumPatterns.h"

# Enum name -> TTL lv2:symbol. Only the spelling differs; the ORDER is what is
# being verified, so this map is deliberately explicit rather than derived.
SYMBOLS = {
    "P_IN": "in", "P_OUT": "out",
    "P_RUN": "run", "P_TEMPO": "tempo", "P_BEATS_PER_BAR": "beats_per_bar",
    "P_DRUMS_LEVEL": "drums_level", "P_PATTERN": "pattern",
    "P_SWING": "swing", "P_HUMANIZE": "humanize",
    "P_LVL_KICK": "lvl_kick", "P_LVL_SNARE": "lvl_snare", "P_LVL_TOMS": "lvl_toms",
    "P_LVL_HATS": "lvl_hats", "P_LVL_CYMBALS": "lvl_cymbals",
    "P_KICK_TUNE": "kick_tune", "P_KICK_DECAY": "kick_decay", "P_KICK_CLICK": "kick_click",
    "P_SNARE_TUNE": "snare_tune", "P_SNARE_DECAY": "snare_decay",
    "P_SNARE_SNAPPY": "snare_snappy",
    "P_HAT_DECAY": "hat_decay", "P_HAT_TONE": "hat_tone",
    "P_LOOP_LEVEL": "loop_level", "P_LOOP_QUANTIZE": "loop_quantize",
    "P_LOOP_FEEDBACK": "loop_feedback", "P_LOOP_TRACK": "loop_track",
    "P_LOOP_REC": "loop_rec", "P_LOOP_PLAY": "loop_play", "P_LOOP_STOP": "loop_stop",
    "P_LOOP_CLEAR": "loop_clear", "P_LOOP_UNDO": "loop_undo",
    "P_TRK1_LEVEL": "trk1_level", "P_TRK1_MUTE": "trk1_mute",
    "P_TRK2_LEVEL": "trk2_level", "P_TRK2_MUTE": "trk2_mute",
    "P_TRK3_LEVEL": "trk3_level", "P_TRK3_MUTE": "trk3_mute",
    "P_TRK4_LEVEL": "trk4_level", "P_TRK4_MUTE": "trk4_mute",
    "P_OUT_PROGRESS": "out_progress", "P_OUT_BARS": "out_bars",
    "P_OUT_TRK1_STATE": "out_trk1_state", "P_OUT_TRK2_STATE": "out_trk2_state",
    "P_OUT_TRK3_STATE": "out_trk3_state", "P_OUT_TRK4_STATE": "out_trk4_state",
    "P_OUT_STEP": "out_step", "P_OUT_UNDO_AVAIL": "out_undo_avail",
    "P_BYPASS": "bypass", "P_ENABLED": "enabled",
}

errors = []


def enum_order():
    """Port enum names in declaration order, expanding comma-separated lines."""
    src = CPP.read_text(encoding="utf-8")
    m = re.search(r"enum PracticePorts\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit("could not find 'enum PracticePorts' in %s" % CPP)
    names = []
    for line in m.group(1).splitlines():
        line = re.sub(r"//.*", "", line)
        for tok in line.split(","):
            tok = tok.strip()
            if not tok:
                continue
            tok = tok.split("=")[0].strip()
            if re.fullmatch(r"P_[A-Z0-9_]+", tok):
                names.append(tok)
    return [n for n in names if n != "P_N_PORTS"]


def ttl_ports():
    """(index, symbol) pairs from the TTL, in index order."""
    src = TTL.read_text(encoding="utf-8")
    pairs = re.findall(r"lv2:index\s+(\d+)\s*;\s*lv2:symbol\s+\"([a-z0-9_]+)\"", src)
    out = [(int(i), s) for i, s in pairs]
    out.sort(key=lambda t: t[0])
    return out


def check_ports():
    names = enum_order()
    ttl = ttl_ports()

    if len(names) != len(ttl):
        errors.append("port COUNT differs: enum has %d, TTL has %d" % (len(names), len(ttl)))

    for idx, name in enumerate(names):
        want = SYMBOLS.get(name)
        if want is None:
            errors.append("enum %s at index %d is missing from SYMBOLS in this script" % (name, idx))
            continue
        if idx >= len(ttl):
            errors.append("enum %s is index %d but the TTL has no such port" % (name, idx))
            continue
        ttl_idx, ttl_sym = ttl[idx]
        if ttl_idx != idx:
            errors.append("TTL index %d is out of order (expected %d)" % (ttl_idx, idx))
        if ttl_sym != want:
            errors.append("index %d: enum %s expects symbol '%s', TTL says '%s'"
                          % (idx, name, want, ttl_sym))

    # The enabled port must stay last: hosts bind it by designation, and moving
    # it would renumber every port before it and invalidate saved boards.
    if names and names[-1] != "P_ENABLED":
        errors.append("P_ENABLED must be the LAST port, found %s" % names[-1])

    return len(names)


def check_patterns():
    """Pattern dropdown must cover exactly the grooves the table defines."""
    src = PATTERNS_H.read_text(encoding="utf-8")
    table = re.search(r"static const DrumPattern kTable\[\]\s*=\s*\{(.*?)\n    \};", src, re.S)
    if not table:
        errors.append("could not find kTable in DrumPatterns.h")
        return
    groove_names = re.findall(r'HEXDRUMS_PATTERN\("([^"]+)"', table.group(1))

    ttl_src = TTL.read_text(encoding="utf-8")
    block = re.search(r'lv2:symbol "pattern".*?lv2:maximum\s+(\d+)\s*;(.*?)\]\s*,', ttl_src, re.S)
    if not block:
        errors.append("could not find the pattern port's enumeration in practice.ttl")
        return
    maximum = int(block.group(1))
    labels = re.findall(r'lv2:scalePoint \[ rdfs:label "([^"]+)"\s*;\s*rdf:value\s+(\d+)', block.group(2))

    if maximum != len(groove_names) - 1:
        errors.append("pattern lv2:maximum is %d but the table has %d grooves (expected max %d)"
                      % (maximum, len(groove_names), len(groove_names) - 1))
    if len(labels) != len(groove_names):
        errors.append("pattern has %d scalePoints but the table has %d grooves"
                      % (len(labels), len(groove_names)))
    for (label, value), expected in zip(labels, groove_names):
        if label != expected:
            errors.append("pattern value %s is labelled '%s' but the table calls it '%s'"
                          % (value, label, expected))
    for i, (_, value) in enumerate(labels):
        if int(value) != i:
            errors.append("pattern scalePoint values are not 0..n-1 in order (found %s at %d)"
                          % (value, i))


def check_turtle():
    """Parse the TTL properly, not with regexes.

    A Turtle syntax error does not announce itself: the host simply fails to
    discover the plugin, and it looks like a build or install problem. Parsing
    here turns that into a build failure with a line number.
    """
    try:
        import rdflib
    except ImportError:
        print("  (rdflib absent — skipping Turtle checks; run under WSL for these)")
        return

    # NOTE: rdflib's Namespace subclasses str, so LV2.index would resolve to
    # str.index (the method) rather than the lv2:index URI — and every port
    # would silently look like it had no index. Bracket syntax is the only
    # form that is safe for arbitrary predicate names.
    LV2 = rdflib.Namespace("http://lv2plug.in/ns/lv2core#")
    P_INDEX, P_SYMBOL, P_NAME = LV2["index"], LV2["symbol"], LV2["name"]
    P_MIN, P_MAX, P_DEFAULT   = LV2["minimum"], LV2["maximum"], LV2["default"]
    P_PORT, P_DESIG           = LV2["port"], LV2["designation"]
    C_PLUGIN, C_ENABLED       = LV2["Plugin"], LV2["enabled"]
    URI = rdflib.URIRef("https://rpowell5064.github.io/guitaramp-suite/practice")

    g = rdflib.Graph()
    for f in (TTL, ROOT / "lv2" / "manifest.ttl"):
        try:
            g.parse(str(f), format="turtle")
        except Exception as exc:                      # noqa: BLE001 - report any parse error
            errors.append("%s does not parse as Turtle: %s" % (f.name, exc))
            return

    if (URI, rdflib.RDF.type, C_PLUGIN) not in g:
        errors.append("practice URI is not declared an lv2:Plugin")

    seen = {}
    for port in g.objects(URI, P_PORT):
        idx = g.value(port, P_INDEX)
        sym = g.value(port, P_SYMBOL)
        name = g.value(port, P_NAME)
        if idx is None or sym is None or name is None:
            errors.append("a port is missing index, symbol or name")
            continue
        i = int(idx)
        if i in seen:
            errors.append("duplicate lv2:index %d (%s and %s)" % (i, seen[i], sym))
        seen[i] = str(sym)

        lo, hi, dflt = g.value(port, P_MIN), g.value(port, P_MAX), g.value(port, P_DEFAULT)
        if None not in (lo, hi, dflt) and not (float(lo) <= float(dflt) <= float(hi)):
            errors.append("port '%s' default %s is outside [%s, %s]" % (sym, dflt, lo, hi))

    if seen and sorted(seen) != list(range(len(seen))):
        errors.append("port indices are not contiguous from 0: %s" % sorted(seen))

    # The host binds this by designation; losing it silently downgrades the
    # plugin to "no bypass" in generic-UI hosts.
    enabled = [p for p in g.objects(URI, P_PORT)
               if g.value(p, P_DESIG) == C_ENABLED]
    if len(enabled) != 1:
        errors.append("expected exactly one lv2:enabled designated port, found %d" % len(enabled))
    elif int(g.value(enabled[0], P_INDEX)) != len(seen) - 1:
        errors.append("the lv2:enabled port must be last (index %d)" % (len(seen) - 1))

    print("  Turtle parses; %d ports, indices contiguous, defaults in range" % len(seen))


def main():
    count = check_ports()
    check_patterns()
    check_turtle()

    if errors:
        print("practice port check FAILED:")
        for e in errors:
            print("  - %s" % e)
        return 1
    print("practice port check OK — %d ports, TTL and enum agree" % count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
