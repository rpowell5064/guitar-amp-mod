#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# Darkglass Anagram (KosmOS) bundle generator — M4 pilot: drive + fuzz.
#
# Emits anagram/<name>.lv2/{manifest.ttl, plugin.ttl} by TRANSFORMING the
# existing pi-Stomp TTLs (single source of truth for knobs/scalePoints):
#   * strips the MOD-specific bits (modgui:gui, mod:label/brand)
#   * adds the KosmOS requirements from the Darkglass reference plugin
#     (dark-plugins/dark-tremolo): dg:abbreviation, dg:blockImageOff/On,
#     an `enabled` port (lv2:enabled designation, default 1) and a `reset`
#     port (kx:Reset designation, pp:trigger), pg:MonoGroup audio grouping
#   * for drive, enabled/reset are inserted BEFORE the atom ports (mod-host —
#     which KosmOS also runs — breaks when control ports follow atoms), and
#     every port index is renumbered sequentially afterwards.
#
# The matching .so is the normal plugin built with -DHEXCHAIN_ANAGRAM=ON
# (cross-compiled via mod-plugin-builder for the device).
#
# The transform is ANCHORED text surgery with hard assertions — if a source
# TTL's formatting changes, this fails loudly instead of emitting garbage.
# ─────────────────────────────────────────────────────────────────────────────
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EXTRA_PREFIXES = """@prefix dg:    <http://www.darkglass.com/lv2/ns#> .
@prefix kx:    <http://kxstudio.sf.net/ns/lv2ext/props#> .
@prefix pg:    <http://lv2plug.in/ns/ext/port-groups#> .
@prefix pprops:<http://lv2plug.in/ns/ext/port-props#> .
"""

def port_block(body):
    return "        " + body.strip()

def enabled_reset_blocks():
    # Shapes copied from the Darkglass reference plugin (dark-tremolo.lv2).
    return [
        ("a lv2:InputPort, lv2:ControlPort ;\n"
         "        lv2:index @IDX@ ; lv2:symbol \"enabled\" ; lv2:name \"Enabled\" ;\n"
         "        lv2:designation lv2:enabled ;\n"
         "        lv2:portProperty lv2:toggled ;\n"
         "        lv2:default 1.0 ; lv2:minimum 0.0 ; lv2:maximum 1.0"),
        ("a lv2:InputPort, lv2:ControlPort ;\n"
         "        lv2:index @IDX@ ; lv2:symbol \"reset\" ; lv2:name \"Reset\" ;\n"
         "        lv2:designation kx:Reset ;\n"
         "        lv2:portProperty lv2:toggled, pprops:trigger ;\n"
         "        lv2:default 0.0 ; lv2:minimum 0.0 ; lv2:maximum 1.0"),
    ]

def transform(src_path, plugin_uri, abbrev, so_name, group_frag, strip_nam=False,
              strip_b7k=False):
    with open(src_path, "r", encoding="utf-8") as f:
        ttl = f.read()

    if strip_b7k:
        # Marketplace courtesy (2026-08-28): the Helsinki Grind (Microtubes B7K
        # parody) stays OFF the Anagram bundle while the Darkglass vendor
        # application is live — TTL-level hiding, same mechanism as the 32-bit
        # NAM strip (the .so still contains the model; the UI can't select past
        # lv2:maximum).
        # 2026-09-04: Helsinki is no longer the LAST scalePoint (Treble Ranger
        # follows it at 11), so strip the line WITH ITS OWN trailing " ;" first;
        # the old form (Helsinki last, terminator on the previous line) stays as
        # the fallback so this keeps working whichever way the list ends.
        ttl, n1 = re.subn(r"\n\s*lv2:scalePoint \[ rdfs:label \"Helsinki Grind\"[^\n]*\] ;", "", ttl)
        if n1 == 0:
            ttl, n1 = re.subn(r" ;\n\s*lv2:scalePoint \[ rdfs:label \"Helsinki Grind\"[^\n]*", "", ttl)
        # Only Helsinki is hidden, and it now sits MID-list: the surviving top
        # model is Treble Ranger at 11, so the ceiling stays 11 (it used to drop
        # to 9 because Helsinki was the top entry).
        ttl, n2 = re.subn(r"(lv2:symbol \"model\"[^\[]*?lv2:maximum )1[01]", r"\g<1>11", ttl, flags=re.S)
        ttl, n3 = re.subn(r"\n- Helsinki Grind[^\n]*", "", ttl)              # listing bullet
        # changelog note: the Helsinki entry is no longer the newest one on the
        # microVersion line, so cut from its "NN: Helsinki Grind" marker up to
        # whatever version marker follows it (or end of line).
        ttl, n4 = re.subn(r"\s*4[0-9]: Helsinki Grind.*?(?=\s\d\d: |\n)", "", ttl)
        ttl, n5 = re.subn(r"Eleven original", "Ten original", ttl)             # counts (2 sites)
        ttl, n6 = re.subn(r"the eleven algorithmic voicings", "the ten algorithmic voicings", ttl)
        assert (n1, n2, n3, n4, n5, n6) == (1, 1, 1, 1, 2, 1), \
            f"{src_path}: B7K strip anchors moved {n1},{n2},{n3},{n4},{n5},{n6}"

    if strip_nam:
        # KosmOS target ships without NAM (GCC 9.4 toolchain can't build the
        # C++20 wavenet; the .so stubs every load): drop the file parameter,
        # its patch:writable, and the Neural scalePoint. The nam_gain/nam_vol
        # ports stay (harmless, keeps indices aligned with the enum).
        ttl, n1 = re.subn(r"# NAM neural-model file parameter[^\n]*\n"
                          r"<[^>]*#nammodel>\n(?:[^\n]*\n)*?[^\n]*mod:fileTypes[^\n]*\n",
                          "", ttl)
        ttl, n2 = re.subn(r"\n\s*patch:writable <[^>]*#nammodel> ;", "", ttl)
        ttl, n3 = re.subn(r"\n\s*lv2:scalePoint \[ rdfs:label \"Neural \(NAM\)\"[^\n]*", "", ttl)
        assert (n1, n2, n3) == (1, 1, 1), f"{src_path}: NAM strip anchors moved {n1},{n2},{n3}"

    # 1. Cut the modgui section: from "modgui:gui [" through the final "] ."
    #    and close the plugin statement with " ." instead.
    m = re.search(r"\n\s*(#[^\n]*\n\s*)*modgui:gui \[", ttl)
    assert m, f"{src_path}: no modgui:gui anchor"
    assert ttl.rstrip().endswith("] ."), f"{src_path}: unexpected file tail"
    ttl = ttl[: m.start()].rstrip()
    assert ttl.endswith("] ;"), f"{src_path}: port list should end '] ;' before modgui"
    ttl = ttl[: -len("] ;")] + "] ."

    # 2. Drop MOD-only plugin-level lines (keep the file parameter's
    #    mod:fileTypes — it is harmless and mod-host-family hosts read it).
    ttl = re.sub(r"\n\s*mod:label\s+[^\n]*", "", ttl)
    ttl = re.sub(r"\n\s*mod:brand\s+[^\n]*", "", ttl)
    ttl = re.sub(r"\n@prefix modgui:[^\n]*", "", ttl)

    # 3. Add the Darkglass prefixes after the existing prefix block.
    lastPrefix = ttl.rfind("\n@prefix")
    endOfPrefixes = ttl.index("\n", ttl.index(".", lastPrefix)) + 1
    ttl = ttl[:endOfPrefixes] + EXTRA_PREFIXES + ttl[endOfPrefixes:]

    # 4. Audio port group node(s) + plugin-level dg: metadata. Mono plugins
    #    (1-in/1-out) get the dark-tremolo MonoGroup shape; stereo plugins
    #    (2-in/2-out — the only other shape KosmOS allows) get standard
    #    port-groups Stereo in/out groups with left/right designations.
    stereo = ttl.count("lv2:AudioPort") == 4
    assert ttl.count("lv2:AudioPort") in (2, 4), \
        f"{src_path}: unexpected audio port count (KosmOS allows 1/1 or 2/2 only)"
    if stereo:
        in_group_uri  = f"<{plugin_uri}#ingroup>"
        out_group_uri = f"<{plugin_uri}#outgroup>"
        group_node = (f"{in_group_uri}\n"
                      "    a pg:StereoGroup, pg:InputGroup ;\n"
                      "    lv2:symbol \"audioin\" ;\n"
                      "    lv2:name \"Audio In\" .\n\n"
                      f"{out_group_uri}\n"
                      "    a pg:StereoGroup, pg:OutputGroup ;\n"
                      "    lv2:symbol \"audioout\" ;\n"
                      "    lv2:name \"Audio Out\" .\n\n")
    else:
        group_uri = f"<{plugin_uri}#audiogroup>"
        group_node = (f"{group_uri}\n"
                      "    a pg:MonoGroup, pg:Group ;\n"
                      "    lv2:symbol \"audio\" ;\n"
                      "    lv2:name \"Audio\" .\n\n")
    plug_anchor = f"<{plugin_uri}>\n"
    assert plug_anchor in ttl, f"{src_path}: plugin URI block not found"
    ttl = ttl.replace(plug_anchor, group_node + plug_anchor, 1)
    dg_lines = (f"    dg:abbreviation \"{abbrev}\" ;\n"
                f"    dg:blockImageOff <block-off.png> ;\n"
                f"    dg:blockImageOn <block-on.png> ;\n")
    m = re.search(re.escape(plug_anchor) + r"(\s*a lv2:Plugin[^\n]*\n)", ttl)
    assert m, f"{src_path}: plugin type line not found"
    ttl = ttl[: m.end()] + dg_lines + ttl[m.end():]

    # 5. Port surgery: split the port list into blocks, tag the audio ports
    #    with the group, insert enabled/reset, renumber sequentially.
    ps = ttl.index("lv2:port [")
    pe = ttl.rindex("] .")
    head, ports_text, tail = ttl[:ps], ttl[ps + len("lv2:port ["): pe], ttl[pe:]
    blocks = re.split(r"\n    \] , \[\n", ports_text)
    assert len(blocks) >= 4, f"{src_path}: too few port blocks ({len(blocks)})"

    # Sort by the ORIGINAL lv2:index — some source TTLs declare late-appended
    # ports out of textual order (amp.ttl: mv_* 36-42 sit after the atoms in
    # the file). Renumbering in textual order would both scramble the enum
    # mapping and put control ports after the atoms (the mod-host breaker).
    # For in-order TTLs this sort is a no-op.
    def orig_index(b):
        m = re.search(r"lv2:index (\d+)", b)
        assert m, f"{src_path}: port block has no index"
        return int(m.group(1))
    blocks.sort(key=orig_index)

    out_blocks = []
    for b in blocks:
        if "lv2:AudioPort" in b:
            b = b.rstrip()
            if not b.endswith(";"):
                b += " ;"
            if stereo:
                sym = re.search(r'lv2:symbol "([^"]+)"', b)
                assert sym and sym.group(1).endswith(("_l", "_r")), \
                    f"{src_path}: stereo audio port symbol {sym and sym.group(1)!r} lacks _l/_r"
                side = "pg:left" if sym.group(1).endswith("_l") else "pg:right"
                grp = in_group_uri if "lv2:InputPort" in b else out_group_uri
                b += (f"\n        lv2:designation {side} ;"
                      f"\n        pg:group {grp}")
            else:
                b += (f"\n        lv2:designation pg:center ;"
                      f"\n        pg:group {group_uri}")
        out_blocks.append(b)

    new_blocks = [port_block(x) for x in enabled_reset_blocks()]
    atom_at = next((i for i, b in enumerate(out_blocks) if "atom:AtomPort" in b), None)
    if atom_at is not None:
        out_blocks[atom_at:atom_at] = new_blocks   # BEFORE the atoms (mod-host rule)
    else:
        out_blocks.extend(new_blocks)

    for i, b in enumerate(out_blocks):
        b2, nsub = re.subn(r"lv2:index (?:\d+|@IDX@)", f"lv2:index {i}", b, count=1)
        assert nsub == 1, f"{src_path}: port block {i} has no index"
        out_blocks[i] = b2

    ttl = head + "lv2:port [" + "\n    ] , [\n".join(out_blocks) + tail

    manifest = ("@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n"
                "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n\n"
                f"<{plugin_uri}>\n"
                "    a lv2:Plugin ;\n"
                f"    lv2:binary <{so_name}> ;\n"
                "    rdfs:seeAlso <plugin.ttl> .\n")
    return ttl + "\n", manifest

URI_BASE = "https://rpowell5064.github.io/guitaramp-suite/"

PLUGINS = [
    # (source ttl, plugin uri, abbrev, .so name, bundle dir, strip_nam)
    # strip_nam stays available for toolchains that can't build NAM; the
    # Anagram keeps NAM (KosmOS supports the Neural Loader atom:Path file
    # pattern, and the submodule's nam_atomic_fallback patch makes NamCore
    # build on its GCC 9.4 toolchain).
    # M4 pilots (mono). Drive: strip_b7k hides the Helsinki Grind on the
    # Anagram (vendor-application courtesy; see transform()).
    ("lv2/drive.ttl",   URI_BASE + "drive",   "DRV", "guitaramp_drive.so",   "hexchain-drive.lv2",   False),
    ("lv2/fuzz.ttl",    URI_BASE + "fuzz",    "FZZ", "guitaramp_fuzz.so",    "hexchain-fuzz.lv2",    False),
    # M5 full-suite pass — mono (1-in/1-out, dual-mono rules apply):
    ("lv2/comp.ttl",    URI_BASE + "comp",    "CMP", "guitaramp_comp.so",    "hexchain-comp.lv2",    False),
    ("lv2/gate.ttl",    URI_BASE + "gate",    "GTE", "guitaramp_gate.so",    "hexchain-gate.lv2",    False),
    ("lv2/nail.ttl",    URI_BASE + "nail",    "NL",  "guitaramp_nail.so",    "hexchain-nail.lv2",    False),
    ("lv2/utility.ttl", URI_BASE + "utility", "TRM", "guitaramp_utility.so", "hexchain-utility.lv2", False),
    # M5 — stereo (2-in/2-out, single-instance):
    ("lv2/amp.ttl",     URI_BASE + "amp",     "AMP", "guitaramp_amp.so",     "hexchain-amp.lv2",     False),
    ("lv2/cab.ttl",     URI_BASE + "cab",     "CAB", "guitaramp_cab.so",     "hexchain-cab.lv2",     False),
    ("lv2/delay.ttl",   URI_BASE + "delay",   "DLY", "guitaramp_delay.so",   "hexchain-delay.lv2",   False),
    ("lv2/modfx.ttl",   URI_BASE + "modfx",   "MOD", "guitaramp_modfx.so",   "hexchain-modfx.lv2",   False),
    ("lv2/octave.ttl",  URI_BASE + "octave",  "OCT", "guitaramp_octave.so",  "hexchain-octave.lv2",  False),
    ("lv2/reverb.ttl",  URI_BASE + "reverb",  "RVB", "guitaramp_reverb.so",  "hexchain-reverb.lv2",  False),
    ("lv2/wah.ttl",     URI_BASE + "wah",     "WAH", "guitaramp_wah.so",     "hexchain-wah.lv2",     False),
]

if __name__ == "__main__":
    ok = True
    for src, uri, abbrev, so, bundle, strip_nam in PLUGINS:
        ttl, manifest = transform(os.path.join(REPO, src), uri, abbrev, so, bundle, strip_nam,
                                  strip_b7k=(src == "lv2/drive.ttl"))
        bdir = os.path.join(REPO, "anagram", bundle)
        os.makedirs(bdir, exist_ok=True)
        with open(os.path.join(bdir, "plugin.ttl"), "w", newline="\n", encoding="utf-8") as f:
            f.write(ttl)
        with open(os.path.join(bdir, "manifest.ttl"), "w", newline="\n", encoding="utf-8") as f:
            f.write(manifest)
        print(f"wrote anagram/{bundle}/ (abbrev {abbrev})")
        try:
            import rdflib
            g = rdflib.Graph()
            g.parse(os.path.join(bdir, "plugin.ttl"), format="turtle")
            g.parse(os.path.join(bdir, "manifest.ttl"), format="turtle")
            print(f"  rdflib parse OK ({len(g)} triples)")
        except ImportError:
            print("  (rdflib not installed — TTL parse NOT verified)")
            ok = False
        except Exception as e:
            print(f"  TTL PARSE FAILED: {e}")
            sys.exit(1)
    sys.exit(0 if ok else 2)
