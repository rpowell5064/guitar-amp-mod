#!/usr/bin/env python3
"""Render presentable screenshots of the Practice panel, for sharing.

Not a gate -- practice_gui_check.py is the gate. This exists to show the plugin
doing something, so the fixtures are a plausible session (two takes recorded, a
third trimmed, a groove loaded) rather than the minimal state a test needs.

It drives the REAL script against the REAL stylesheet, so what comes out is
what the panel actually looks like, not a mock-up.

Run:  python build-tools/practice_screens.py [--out DIR]
"""
import base64
import importlib.util
import json
import math
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LV2 = os.path.join(REPO, "lv2")
BASE = os.path.join(LV2, "modgui-practice")
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

_spec = importlib.util.spec_from_file_location(
    "gc", os.path.join(REPO, "build-tools", "practice_gui_check.py"))
gc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gc)
rm = gc.rm


def peaks(seed, shape):
    """A guitar-ish envelope: pick attacks decaying into sustain."""
    b = bytearray()
    for i in range(256):
        t = (i % shape) / float(shape)
        env = math.exp(-t * 2.2) * (0.45 + 0.55 * abs(math.sin(i * seed)))
        swell = 0.55 + 0.45 * math.sin(i * 0.024 + seed)
        b.append(max(0, min(255, int(238 * env * swell))))
    return base64.b64encode(bytes(b)).decode()


WAVE = json.dumps({"v": 7, "bars": 4, "len": 768000,
                   "t": [peaks(0.31, 64), peaks(0.17, 32), peaks(0.44, 96), ""]})

GROOVE = json.dumps({
    "builtin": 1, "idx": 21, "name": "Modern Half-Time", "spb": 16, "bars": 2,
    "lanes": [
        {"i": 0,  "s": "X.....x.--..X...X...-...x..X-..."},
        {"i": 1,  "s": "........X.......-.......X......o"},
        {"i": 6,  "s": "x.o.x.o.x.o.x.o.x.o.x.o.x.o.x.o."},
        {"i": 9,  "s": "X...............X..............."},
        {"i": 10, "s": "....o.......o.......o.......o..."},
        {"i": 5,  "s": "..............-.............x..o"},
    ]})

# symbol, value -- the state a real session would be in
PORTS = [
    ("beats_per_bar", 4), ("tempo", 96), ("pattern", 21), ("run", 1),
    ("count_in", 1), ("loop_bars", 4), ("drums_level", -3), ("drum_space", 40),
    ("out_trk1_state", 3), ("out_trk2_state", 3), ("out_trk3_state", 4),
    ("out_progress", 0.38), ("out_step", 11), ("out_undo_avail", 1),
    ("trk3_trim_in", 0.25), ("trk3_trim_out", 0.75),
    ("trk1_level", 0), ("trk2_level", -2.5), ("trk3_level", -5),
    ("lvl_kick", 1.5), ("lvl_snare", 0), ("lvl_toms", -1), ("lvl_hats", -3),
    ("lvl_cymbals", -2), ("drum_comp", 35), ("drum_room", 22), ("drum_body", 30),
]

SHOTS = {
    # name:          (tab,     extra JS before capture)
    "01-loops":      ("loops", ""),
    "02-count-in":   ("loops", "gui({type:'change', icon:icon, symbol:'out_countin', value:3}, funcs);"),
    "03-drums":      ("drums", ""),
    "04-mix":        ("mix",   ""),
}


def build_page(tab, extra):
    css = open(os.path.join(BASE, "stylesheet-practice.css"), encoding="utf-8").read()
    css = css.replace("{{{cns}}}", "").replace("{{{ns}}}", "")
    fileurl = "file:///" + BASE.replace("\\", "/") + "/"
    css = css.replace("url(/resources/", "url(" + fileurl).replace('url("/resources/', 'url("' + fileurl)

    controls = rm.parse_controls(os.path.join(LV2, "practice.ttl"))
    html = rm.fill_mustache(open(os.path.join(BASE, "icon-practice.html"), encoding="utf-8").read(), controls)
    html = html.replace('class="mod-powerswitch-image"', 'class="mod-powerswitch-image on"')

    ports = [{"symbol": c["symbol"], "value": 0,
              "ranges": {"minimum": -60, "maximum": 12, "default": 0}} for c in controls]

    drive = "    " + "".join(
        "gui({type:'change', icon:icon, symbol:%s, value:%s}, funcs);\n    "
        % (json.dumps(sym), json.dumps(val)) for sym, val in PORTS) + extra

    shim = (gc.SHIM
            .replace("__SCRIPT__", open(os.path.join(BASE, "script-practice.js"), encoding="utf-8").read())
            .replace("__WAVE__", json.dumps(WAVE))
            .replace("__PAT__", json.dumps(GROOVE))
            .replace("__PORTS__", json.dumps(ports))
            .replace("__DRIVE__", drive)
            # These pictures show the panel at rest, not the gate poking it --
            # except for lighting the correct TAB, which the gate never needed
            # (it reads the view directly) but which looks plainly wrong in a
            # screenshot someone else is going to look at.
            .replace("__INTERACT__",
                     "[].slice.call(document.querySelectorAll('.px-tab')).forEach("
                     "function(t){ t.classList.toggle('on',"
                     " t.getAttribute('data-pxtab') === window.__TAB__); });"))

    return ('<!DOCTYPE html><html><head><meta charset="utf-8"><style>'
            'html,body{margin:0;padding:0;background:#0b0d12;'
            'font-family:"Helvetica Neue",Helvetica,Arial,sans-serif;}' + css + '</style></head><body>'
            + html + '<script>window.__TAB__=' + json.dumps(tab) + ';</script>' + shim
            + '</body></html>')


def main():
    outdir = os.path.join(REPO, "out_analyze", "screenshots")
    if "--out" in sys.argv:
        outdir = sys.argv[sys.argv.index("--out") + 1]
    os.makedirs(outdir, exist_ok=True)
    if not os.path.exists(CHROME):
        print("Chrome not found at %s" % CHROME)
        return 1

    for name, (tab, extra) in SHOTS.items():
        hp = os.path.join(outdir, name + ".html")
        open(hp, "w", encoding="utf-8").write(build_page(tab, extra))
        png = os.path.join(outdir, name + ".png")
        subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                        "--window-size=1320,900", "--virtual-time-budget=2500",
                        "--default-background-color=00000000",
                        "--screenshot=" + png,
                        "file:///" + hp.replace("\\", "/")],
                       capture_output=True, timeout=120)
        os.remove(hp)
        print("  %s" % png)
    print("\n%d screenshots in %s" % (len(SHOTS), outdir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
