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
    "P_DRUM_COMP": "drum_comp", "P_DRUM_ROOM": "drum_room",
    "P_DRUM_ROOM_SIZE": "drum_room_size", "P_DRUM_BODY": "drum_body",
    "P_BYPASS": "bypass", "P_ENABLED": "enabled",
    "P_CONTROL": "control", "P_NOTIFY": "notify",
    "P_TEMPO_SYNC": "tempo_sync", "P_HOST_BPM": "host_bpm",
    "P_COUNT_IN": "count_in", "P_OUT_COUNTIN": "out_countin",
    "P_LOOP_BARS": "loop_bars",
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
    # The modgui:gui block repeats lv2:index/lv2:symbol to map ports onto the
    # template's controls.N slots. Those are not port declarations -- scraping
    # them here would double-count every port. Cut the block off before
    # matching; check_modgui() validates that block on its own terms.
    gui = src.find("modgui:gui")
    if gui >= 0:
        src = src[:gui]
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

    # `enabled` no longer has to be last. It was kept last for UI ordering,
    # but this plugin has been renumbered three times and every renumber
    # invalidates saved pedalboards and MIDI bindings, so new ports now append
    # at the END. What actually matters is that the DESIGNATION exists (checked
    # in the Turtle pass) and that nothing before it ever moves again.
    if "P_ENABLED" not in names:
        errors.append("the lv2:enabled port is missing")

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
    # Its INDEX is no longer pinned (see above); only its existence is.

    print("  Turtle parses; %d ports, indices contiguous, defaults in range" % len(seen))


def check_modgui():
    """Gate the modgui port mapping against how mod-ui ACTUALLY resolves it.

    Verified against mod-ui on the device 2026-09-30. Its native parser assigns
    each modgui:port entry AT ITS lv2:index into a vector whose length is the
    NUMBER OF ENTRIES, and the icon template's {{#controls.N}} indexes that
    vector. Two consequences, both of which silently produced a wrong panel
    here before they were understood:
      * N is the port's lv2:index, not its position in the list;
      * an entry whose lv2:index >= the entry count is DROPPED without a word.
    So this checks that every {{#controls.N}} the template uses names a
    declared port at lv2:index N, and that N is inside the vector.
    """
    src = TTL.read_text(encoding="utf-8")
    gui = src.find("modgui:gui")
    if gui < 0:
        return
    real = dict((sym, idx) for idx, sym in ttl_ports())
    entries = re.findall(
        r'lv2:index\s+(\d+)\s*;\s*lv2:symbol\s+"([a-z0-9_]+)"', src[gui:])
    if not entries:
        errors.append("modgui:gui present but no modgui:port entries were parsed")
        return
    count = len(entries)

    by_index = {}
    for idx, sym in entries:
        idx = int(idx)
        if real.get(sym) != idx:
            errors.append("modgui:port says %s is index %d; the port list says %s"
                          % (sym, idx, real.get(sym)))
        if idx in by_index:
            errors.append("modgui:port declares index %d twice (%s, %s)"
                          % (idx, by_index[idx], sym))
        by_index[idx] = sym

    html = ROOT / "lv2" / "modgui-practice" / "icon-practice.html"
    if not html.exists():
        return count
    used = sorted(set(int(n) for n in
                      re.findall(r"\{\{#controls\.(\d+)\}\}", html.read_text(encoding="utf-8"))))
    for n in used:
        if n >= count:
            errors.append("icon-practice.html uses controls.%d but only %d entries are "
                          "declared, so mod-ui drops it" % (n, count))
        elif n not in by_index:
            errors.append("icon-practice.html uses controls.%d but no modgui:port entry "
                          "has lv2:index %d" % (n, n))
    if used:
        print("  modgui: %d entries; template uses controls.%s -> %s"
              % (count, ",".join(str(u) for u in used),
                 ",".join(by_index.get(u, "?") for u in used)))
    return count


def check_fader_ranges():
    """The modgui hardcodes the fader dB span; assert it against the TTL.

    mod-ui's modgui `start` event passes only {symbol, value} for each port --
    no ranges at all -- so a fader cannot learn its own span from the host and
    the script carries FADER_MIN/FADER_MAX itself. That is a second copy of a
    number the TTL already owns, so it gets checked here: a port drawn as a
    fader whose range is widened in the TTL would otherwise render at the wrong
    position forever, with nothing failing.
    """
    js = ROOT / "lv2" / "modgui-practice" / "script-practice.js"
    html = ROOT / "lv2" / "modgui-practice" / "icon-practice.html"
    if not js.exists() or not html.exists():
        return
    m = re.search(r"var FADER_MIN\s*=\s*(-?[\d.]+)\s*,\s*FADER_MAX\s*=\s*(-?[\d.]+)",
                  js.read_text(encoding="utf-8"))
    if not m:
        errors.append("script-practice.js no longer declares FADER_MIN/FADER_MAX")
        return
    jmin, jmax = float(m.group(1)), float(m.group(2))

    # Every port the template draws as a fader.
    syms = set(re.findall(r'class="px-fader"[^>]*mod-port-symbol="([a-z0-9_]+)"',
                          html.read_text(encoding="utf-8")))
    if not syms:
        errors.append("no .px-fader ports found in icon-practice.html")
        return

    src = TTL.read_text(encoding="utf-8")
    gui = src.find("modgui:gui")
    if gui >= 0:
        src = src[:gui]
    for sym in sorted(syms):
        b = re.search(r'lv2:symbol "%s".*?lv2:minimum\s+(-?[\d.]+)\s*;\s*lv2:maximum\s+(-?[\d.]+)'
                      % re.escape(sym), src, re.S)
        if not b:
            errors.append("fader port %s has no minimum/maximum in the TTL" % sym)
            continue
        tmin, tmax = float(b.group(1)), float(b.group(2))
        if (tmin, tmax) != (jmin, jmax):
            errors.append("fader %s is %g..%g in the TTL but the modgui assumes %g..%g"
                          % (sym, tmin, tmax, jmin, jmax))
    print("  faders: %d ports, all %g..%g dB" % (len(syms), jmin, jmax))

    # Same story for the tempo: the modgui drives that port itself (mod-ui's
    # film widget cannot), so it carries its own span and must agree with the TTL.
    mt = re.search(r"var TEMPO_MIN\s*=\s*(-?[\d.]+)\s*,\s*TEMPO_MAX\s*=\s*(-?[\d.]+)",
                   js.read_text(encoding="utf-8"))
    if not mt:
        errors.append("script-practice.js no longer declares TEMPO_MIN/TEMPO_MAX")
        return
    b = re.search(r'lv2:symbol "tempo" ;.*?lv2:minimum\s+(-?[\d.]+)\s*;\s*lv2:maximum\s+(-?[\d.]+)',
                  src, re.S)
    if not b:
        errors.append("the tempo port has no minimum/maximum in the TTL")
        return
    if (float(b.group(1)), float(b.group(2))) != (float(mt.group(1)), float(mt.group(2))):
        errors.append("tempo is %s..%s in the TTL but the modgui assumes %s..%s"
                      % (b.group(1), b.group(2), mt.group(1), mt.group(2)))
    else:
        print("  tempo:  %s..%s BPM, modgui agrees" % (b.group(1), b.group(2)))


def check_template_shell():
    """The bits of a MOD pedal template that are easy to forget and invisible offline.

    Both of these shipped broken once: the audio jacks were simply never added
    (the stylesheet had rules for them, the markup did not), so the block had
    no connectors in the pedalboard at all; and without the drag-handle z-index
    override the handle floats above the plate, putting a move cursor over
    every knob and swallowing clicks. Neither shows up in an offline render.
    """
    html_p = ROOT / "lv2" / "modgui-practice" / "icon-practice.html"
    css_p = ROOT / "lv2" / "modgui-practice" / "stylesheet-practice.css"
    if not html_p.exists() or not css_p.exists():
        return
    html = html_p.read_text(encoding="utf-8")
    css = css_p.read_text(encoding="utf-8")

    for role, loop in (("input-audio-port", "effect.ports.audio.input"),
                       ("output-audio-port", "effect.ports.audio.output")):
        if role not in html:
            errors.append("icon-practice.html has no mod-role=%s: the block would show "
                          "no audio connector" % role)
        if loop not in html:
            errors.append("icon-practice.html never iterates {{#%s}}" % loop)

    if not re.search(r"\.mod-drag-handle\s*\{[^}]*z-index\s*:\s*0\s*!important", css):
        errors.append("stylesheet-practice.css does not pin .mod-drag-handle to "
                      "z-index:0 !important, so it covers the controls")
    print("  template: audio jacks present, drag handle pinned behind the plate")

    # mod-ui emits exactly three JS event types: 'start', and 'change' carrying
    # either a patch property (uri) or a control port (symbol). There is no
    # 'port_event'. A handler keyed on one silently drops every live port
    # update -- lamps, playhead, readouts -- while looking correct in any
    # offline harness that invents the event. Both this repo's script and its
    # GUI harness did exactly that, so the name is now banned outright.
    js_src = (ROOT / "lv2" / "modgui-practice" / "script-practice.js").read_text(encoding="utf-8")
    gui_src = (ROOT / "build-tools" / "practice_gui_check.py").read_text(encoding="utf-8")
    for name, src in (("script-practice.js", js_src), ("practice_gui_check.py", gui_src)):
        for line in src.splitlines():
            if "port_event" in line and not line.lstrip().startswith(("//", "#")):
                errors.append("%s references 'port_event', which mod-ui never sends" % name)
                break
    if "event.symbol" not in js_src:
        errors.append("script-practice.js never reads event.symbol, so control-port "
                      "changes cannot reach it")
    print("  events: no 'port_event'; port changes read from change.symbol")

    # mod-ui pushes an OUTPUT port to a custom GUI only if the GUI names it in
    # modgui:monitoredOutputs. An unlisted output is simply never monitored:
    # the JS never hears from it, the readout sits frozen at its load value and
    # absolutely nothing reports an error. Every output the script reads must
    # therefore be declared.
    ttl_src = TTL.read_text(encoding="utf-8")
    gui_at = ttl_src.find("modgui:gui")
    declared = set(re.findall(r'modgui:monitoredOutputs\s+(.*?);', ttl_src[gui_at:], re.S))
    declared = set(re.findall(r'lv2:symbol\s+"([a-z0-9_]+)"', " ".join(declared)))
    used = set(re.findall(r"sym === '(out_[a-z0-9_]+)'", js_src))
    for sym in sorted(used - declared):
        errors.append("the modgui reads %s but it is not in modgui:monitoredOutputs, "
                      "so mod-ui never sends it" % sym)
    print("  outputs: %d monitored, %d read by the script" % (len(declared), len(used)))

    # The Loop Length options are written into the HTML because lv2:index 59 is
    # past the end of the modgui:port vector, so {{#controls.59}} would be
    # dropped. That makes the HTML a second copy of the TTL's scale points, so
    # it gets checked rather than trusted.
    html_src = (ROOT / "lv2" / "modgui-practice" / "icon-practice.html").read_text(encoding="utf-8")
    m = re.search(r'rata-role="sellen".*?</div>\s*</div>', html_src, re.S)
    if not m:
        errors.append("the Loop Length dropdown is missing from icon-practice.html")
    else:
        html_opts = re.findall(r'mod-parameter-value="(\d+)">([^<]+)<', m.group(0))
        blk = re.search(r'lv2:symbol "loop_bars".*?\]\s*,\s*\[\s*a lv2:', ttl_src, re.S)
        blk = blk.group(0) if blk else ttl_src[ttl_src.find('lv2:symbol "loop_bars"'):]
        ttl_opts = re.findall(r'lv2:scalePoint \[ rdfs:label "([^"]+)"\s*;\s*rdf:value\s+(\d+)', blk)
        ttl_pairs = [(v, l) for l, v in ttl_opts]
        if ttl_pairs != html_opts:
            errors.append("Loop Length options differ: TTL %r vs HTML %r" % (ttl_pairs, html_opts))
        else:
            print("  loop length: %d options, HTML matches the TTL" % len(html_opts))


def main():
    count = check_ports()
    check_patterns()
    check_modgui()
    check_fader_ranges()
    check_template_shell()
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
