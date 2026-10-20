#!/usr/bin/env python3
"""Generate the native X11 UI's port tables and its TTL from the plugin TTLs.

The X11 UI (ui/, built only with -DHEXCHAIN_X11UI=ON) needs every control port's
index, range, default, flags, unit, enum labels and port group. Hand-typing them would
let the UI drift from the DSP (port indices can never move), so this script reads the
plugins lv2/manifest.ttl ships and writes:

  ui/generated/hc_ports.inc        port tables + panel metadata for every plugin
  ui/generated/x11ui.ttl           the ui:X11UI declarations (installed in the bundle)
  ui/generated/manifest-x11ui.ttl  lines CMake appends to the bundle's manifest.ttl
  ui/generated/res/logo.png        the .hx-logo badge (cairo cannot load the JPEG)

Every shipped plugin gets a UI: a hand-made panel when ui/plugins/<key>_ui.cpp
registers one (<key> = TTL basename), otherwise the generic paged panel built from its
port groups (ui/core/hc_generic.cpp). The theme, title and tagline for the generic
panel come from the plugin's modgui.

Why the manifest: Zynthian decides whether a plugin has a GUI by text-searching the
files in lilv's get_data_uris() for "X11UI". Those are only the files the plugin's
OWN bundle manifest points at, so a separate add-on bundle is never detected
(measured 2026-10-11 with lilv 0.24.22). The default build installs lv2/manifest.ttl
untouched; only the X11 UI flavour installs the extended one.

Run after any TTL or modgui edit:  python build-tools/gen_ui_ports.py
"""
import html
import os
import re
import sys

import rdflib
from rdflib.namespace import RDF, RDFS

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LV2DIR = os.path.join(ROOT, "lv2")
UIDIR = os.path.join(ROOT, "ui")
OUTDIR = os.path.join(UIDIR, "generated")

LV2 = rdflib.Namespace("http://lv2plug.in/ns/lv2core#")
UNITS = rdflib.Namespace("http://lv2plug.in/ns/extensions/units#")
ATOM = rdflib.Namespace("http://lv2plug.in/ns/ext/atom#")
PATCH = rdflib.Namespace("http://lv2plug.in/ns/ext/patch#")
PPROPS = rdflib.Namespace("http://lv2plug.in/ns/ext/port-props#")
PG = rdflib.Namespace("http://lv2plug.in/ns/ext/port-groups#")
MOD = rdflib.Namespace("http://moddevices.com/ns/mod#")
DOAP = rdflib.Namespace("http://usefulinc.com/ns/doap#")

# units:<name> -> the suffix mod-ui prints after the value.
UNIT_SYMBOL = {
    "db": "dB", "ms": "ms", "s": "s", "hz": "Hz", "khz": "kHz", "pc": "%",
    "bpm": "BPM", "cent": "ct", "semitone12TET": "st", "degree": "°",
    "m": "m", "coef": "", "frame": "", "midiNote": "", "beat": "beats", "bar": "bars",
}

# mod:fileTypes tokens -> the extensions the X11 file browser lists.
FILE_TYPES = {
    "nammodel": [".nam"], "nam": [".nam"],
    "cabsim": [".wav"], "ir": [".wav"], "wav": [".wav"], "audio": [".wav"],
    "midisong": [".mid", ".midi"], "midi": [".mid", ".midi"],
}

# Flags mirrored in ui/core/hc_types.h (PortFlag).
F_OUTPUT, F_TOGGLED, F_INTEGER, F_ENUM, F_LOG, F_HIDDEN = 1, 2, 4, 8, 16, 32

# Generic panels for plugins whose modgui has no --acc theme (Hex Forge, Amp):
# the suite's cyan on a neutral enclosure.
DEFAULT_THEME = ("19e0ff", "2a3340", "161b22")


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_float(v):
    r = repr(float(v))
    return (r if ("." in r or "e" in r) else r + ".0") + "f"


def unit_of(g, port):
    u = g.value(port, UNITS.unit)
    if u is None:
        return ""
    if isinstance(u, rdflib.URIRef) and str(u).startswith(str(UNITS)):
        name = str(u)[len(str(UNITS)):]
        if name not in UNIT_SYMBOL:
            sys.exit(f"gen_ui_ports: unknown unit units:{name} - add it to UNIT_SYMBOL")
        return UNIT_SYMBOL[name]
    sym = g.value(u, UNITS.symbol)          # custom unit blank node
    return str(sym) if sym is not None else ""


def shipped_keys():
    """TTL basenames the bundle's manifest points at (the shelved Claw is not in it)."""
    g = rdflib.Graph()
    g.parse(os.path.join(LV2DIR, "manifest.ttl"), format="turtle")
    keys = []
    for p in g.subjects(RDF.type, LV2.Plugin):
        for see in g.objects(p, RDFS.seeAlso):
            name = str(see).rsplit("/", 1)[-1]
            if name.endswith(".ttl"):
                keys.append(name[:-4])
    return sorted(keys)


def modgui_meta(key, fallback_title):
    """Theme, title and tagline from the plugin's modgui (for the generic panel)."""
    theme, title, tag = DEFAULT_THEME, fallback_title, ""
    css = os.path.join(LV2DIR, f"modgui-{key}", f"stylesheet-{key}.css")
    if os.path.exists(css):
        m = re.search(r"--acc:\s*#(\w{6});\s*--encl1:\s*#(\w{6});\s*--encl2:\s*#(\w{6})",
                      open(css, encoding="utf-8").read())
        if m:
            theme = m.groups()
    icon = os.path.join(LV2DIR, f"modgui-{key}", f"icon-{key}.html")
    if os.path.exists(icon):
        src = open(icon, encoding="utf-8").read()
        m = re.search(r'class="hx-title">(.*?)</div>', src, re.S)
        if m:
            title = html.unescape(re.sub(r"<[^>]+>", "", m.group(1))).strip()
        m = re.search(r'class="hx-tag">(.*?)</div>', src, re.S)
        if m:
            tag = html.unescape(re.sub(r"<[^>]+>", "", m.group(1))).strip()
    return theme, title, tag


def plugin_data(key):
    path = os.path.join(LV2DIR, key + ".ttl")
    g = rdflib.Graph()
    g.parse(path, format="turtle")
    plugins = list(g.subjects(RDF.type, LV2.Plugin))
    if len(plugins) != 1:
        sys.exit(f"gen_ui_ports: {path} declares {len(plugins)} plugins, expected 1")
    uri = plugins[0]
    ports = []
    atom_in = atom_out = -1
    for p in g.objects(uri, LV2.port):
        types = set(g.objects(p, RDF.type))
        if ATOM.AtomPort in types:      # patch:Set / patch:Get travel here (file pickers)
            if LV2.InputPort in types:
                atom_in = int(g.value(p, LV2["index"]))
            else:
                atom_out = int(g.value(p, LV2["index"]))
            continue
        if LV2.ControlPort not in types:
            continue
        props = set(g.objects(p, LV2.portProperty))
        flags = 0
        if LV2.OutputPort in types:
            flags |= F_OUTPUT
        if LV2.toggled in props:
            flags |= F_TOGGLED
        if LV2.integer in props:
            flags |= F_INTEGER
        if LV2.enumeration in props:
            flags |= F_ENUM
        if PPROPS.logarithmic in props:
            flags |= F_LOG
        if PPROPS.notOnGUI in props:
            flags |= F_HIDDEN
        idx = int(g.value(p, LV2["index"]))   # LV2.index would be str.index
        lo = g.value(p, LV2.minimum)
        hi = g.value(p, LV2.maximum)
        df = g.value(p, LV2.default)
        lo = float(lo) if lo is not None else 0.0
        hi = float(hi) if hi is not None else 1.0
        df = float(df) if df is not None else lo
        enums = sorted((float(g.value(sp, RDF.value)), str(g.value(sp, RDFS.label)))
                       for sp in g.objects(p, LV2.scalePoint))
        grp = g.value(p, PG.group)
        ports.append(dict(idx=idx, sym=str(g.value(p, LV2.symbol)),
                          name=str(g.value(p, LV2.name)), lo=lo, hi=hi, df=df, flags=flags,
                          unit=unit_of(g, p), enums=enums,
                          group=str(g.value(grp, LV2.name)) if grp is not None else ""))
    ports.sort(key=lambda d: d["idx"])
    syms = [d["sym"] for d in ports]
    if len(set(syms)) != len(syms):
        sys.exit(f"gen_ui_ports: duplicate port symbol in {path}")
    # Port groups, in the order their first port appears (how Zynthian pages them).
    groups = []
    for d in ports:
        if d["group"] and d["group"] not in groups:
            groups.append(d["group"])
    # Parameters a panel can set over patch:Set: patch:writable with an atom:Path range
    # (files the panel picks) or atom:String (text, e.g. Hex Forge's preset JSON).
    files = []
    # (patch:readable too: values only the plugin sends, e.g. Scratch Pad's #status)
    params = set(g.objects(uri, PATCH.writable)) | set(g.objects(uri, PATCH.readable))
    for prm in params:
        rng = g.value(prm, RDFS.range)
        if rng not in (ATOM.Path, ATOM.String):
            continue
        exts = []
        for tok in str(g.value(prm, MOD.fileTypes) or "").split(","):
            for e in FILE_TYPES.get(tok.strip(), []):
                if e not in exts:
                    exts.append(e)
        pkey = str(prm).rsplit("#", 1)[-1]
        if not exts and "midi" in pkey:   # Scratch Pad's MIDI groove declares no fileTypes
            exts = [".mid", ".midi"]
        if rng == ATOM.String:
            exts = []
        files.append((str(prm), pkey, str(g.value(prm, RDFS.label)), ",".join(exts),
                      rng == ATOM.String))
    files.sort()
    name = str(g.value(uri, DOAP.name) or key)
    return str(uri), ports, groups, atom_in, atom_out, files, name


def write_logo(keys):
    """The .hx-logo badge is a JPEG and cairo only loads PNG, so ship a PNG copy
    (installed to <bundle>/x11ui-res/ by the X11 build only). Every plugin carries
    the same logo; one with its own would need a per-key file, so fail loudly."""
    from PIL import Image
    import hashlib
    digests = {}
    for key in keys:
        path = os.path.join(LV2DIR, f"modgui-{key}", "logo.jpg")
        if os.path.exists(path):
            with open(path, "rb") as f:
                digests.setdefault(hashlib.md5(f.read()).hexdigest(), []).append(key)
    if len(digests) != 1:
        sys.exit(f"gen_ui_ports: modgui logos differ {list(digests.values())} - "
                 "make the logo per-key in hc_panel.cpp and here")
    src = os.path.join(LV2DIR, f"modgui-{next(iter(digests.values()))[0]}", "logo.jpg")
    os.makedirs(os.path.join(OUTDIR, "res"), exist_ok=True)
    # 4x the 58 px badge, so it stays crisp when the host scales the panel up.
    im = Image.open(src).convert("RGB").resize((232, 232), Image.LANCZOS)
    im.save(os.path.join(OUTDIR, "res", "logo.png"), optimize=True)


def main():
    keys = shipped_keys()
    pdir = os.path.join(UIDIR, "plugins")
    custom = sorted(f[:-len("_ui.cpp")] for f in os.listdir(pdir) if f.endswith("_ui.cpp"))
    unknown = [k for k in custom if k not in keys]
    if unknown:
        sys.exit(f"gen_ui_ports: panels for plugins the manifest doesn't ship: {unknown}")
    os.makedirs(OUTDIR, exist_ok=True)

    inc = ["// GENERATED by build-tools/gen_ui_ports.py from lv2/*.ttl - do not edit.",
           "// Included once, by ui/core/hc_registry.cpp.", ""]
    ttl = ["# GENERATED by build-tools/gen_ui_ports.py - do not edit.",
           "# Native X11 UIs (Zynthian VNC, desktop Linux hosts). Installed only by",
           "# the -DHEXCHAIN_X11UI=ON build; the default/Pi bundle never carries it.",
           "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .",
           "@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .",
           "@prefix urid: <http://lv2plug.in/ns/ext/urid#> .",
           ""]
    man = ["", "# ── Native X11 UI (appended by the -DHEXCHAIN_X11UI=ON build only) ──",
           "# Zynthian finds a plugin GUI by text-searching the files this manifest",
           "# points the plugin at, so the UI TTL has to be linked from HERE.", ""]
    table = []
    for key in keys:
        uri, ports, groups, atom_in, atom_out, files, name = plugin_data(key)
        theme, title, tag = modgui_meta(key, re.sub(r"^Hex Chain\s+", "", name).upper())
        cid = "k" + "".join(w.capitalize() for w in re.split(r"[^A-Za-z0-9]", key))
        if files:
            inc.append(f"static const hc::FileParam {cid}Files[] = {{ "
                       + ", ".join(f"{{{c_str(u)}, {c_str(k)}, {c_str(l)}, {c_str(e)}, "
                                   f"{'true' if t else 'false'}}}"
                                   for u, k, l, e, t in files) + " };")
        if groups:
            inc.append(f"static const char* const {cid}Groups[] = {{ "
                       + ", ".join(c_str(gn) for gn in groups) + " };")
        for d in ports:
            if d["enums"]:
                inc.append(f"static const hc::EnumPt {cid}_e{d['idx']}[] = {{ "
                           + ", ".join(f"{{{c_float(v)}, {c_str(l)}}}" for v, l in d["enums"])
                           + " };")
        inc.append(f"static const hc::PortInfo {cid}Ports[] = {{")
        for d in ports:
            en = f"{cid}_e{d['idx']}, {len(d['enums'])}" if d["enums"] else "nullptr, 0"
            gi = groups.index(d["group"]) if d["group"] else -1
            inc.append(f"    {{ {d['idx']}, {c_str(d['sym'])}, {c_str(d['name'])}, "
                       f"{c_float(d['lo'])}, {c_float(d['hi'])}, {c_float(d['df'])}, "
                       f"{d['flags']}u, {c_str(d['unit'])}, {en}, {gi} }},")
        inc.append("};")
        inc.append("")
        fl = f"{cid}Files, {len(files)}" if files else "nullptr, 0"
        gl = f"{cid}Groups, {len(groups)}" if groups else "nullptr, 0"
        th = ", ".join(f"hc::Rgb::hex(0x{c})" for c in theme)
        table.append(f"    {{ {c_str(key)}, {c_str(uri)}, {c_str(uri + '#x11ui')}, "
                     f"{cid}Ports, {len(ports)}, {atom_in}, {atom_out}, {fl}, {gl},\n"
                     f"      {c_str(title)}, {c_str(tag)}, {{ {th} }} }},")
        ttl += [f"<{uri}>", f"    ui:ui <{uri}#x11ui> .",
                f"<{uri}#x11ui>",
                "    a ui:X11UI ;",
                "    ui:binary <hexchain_ui.so> ;",
                "    lv2:optionalFeature ui:parent , ui:resize , urid:map , urid:unmap ;",
                "    lv2:extensionData ui:idleInterface .", ""]
        man.append(f"<{uri}> rdfs:seeAlso <x11ui.ttl> .")
    inc.append("static const hc::PluginInfo kPlugins[] = {")
    inc += table
    inc.append("};")

    def write(fname, lines):
        with open(os.path.join(OUTDIR, fname), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")

    write("hc_ports.inc", inc)
    write("x11ui.ttl", ttl)
    write("manifest-x11ui.ttl", man)
    write_logo(keys)
    generic = [k for k in keys if k not in custom]
    print(f"gen_ui_ports: {len(keys)} plugin UIs - hand-made: {', '.join(custom) or '-'}; "
          f"generic: {', '.join(generic) or '-'}")


if __name__ == "__main__":
    main()
