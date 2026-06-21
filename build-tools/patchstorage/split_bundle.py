#!/usr/bin/env python3
"""
Split the single guitaramp-suite.lv2 bundle into per-plugin .lv2 bundles laid out
for the PatchStorage LV2 uploader, optionally stripping NAM from the four
NAM-capable plugins (amp / drive / cab / hexforge) for NAM-disabled targets.

PatchStorage lists each plugin separately, so we ship one bundle per plugin (11
effects + the Hex Forge flagship) instead of the combined suite bundle. The
uploader expects:  <out>/<plugin>.lv2/   each with its own manifest.ttl.

Usage:
    python split_bundle.py --bundle <built>/guitaramp-suite.lv2 \
                           --out dist/<target> \
                           [--strip-nam]

--strip-nam is used for patchbox-os-arm32 (NAM compiled out). It removes, from
amp/drive/cab/hexforge TTL:
  * the NAM file Parameter blocks (#nammodel/#namfile/#ampnam/#drnam/#cabnam),
  * those URIs from any patch:writable statement (keeping #irfile on cab/hexforge),
  * the "Neural (NAM)" model scalePoints,
so mod-ui never renders a NAM file browser or offers the Neural model.
"""
import argparse
import re
import shutil
import sys
from pathlib import Path

# URI fragments of the NAM file parameters (NOT #irfile, which stays on cab/hexforge).
NAM_FRAGMENTS = {"nammodel", "namfile", "ampnam", "drnam", "cabnam"}
NAM_PLUGINS = {"amp", "drive", "cab", "hexforge"}

PARAM_SUBJECT_RE = re.compile(r"^<https://[^>]*#(" + "|".join(NAM_FRAGMENTS) + r")>\s*$")
URI_RE = re.compile(r"<[^>]+>")


def parse_manifest(manifest_path: Path):
    """Return list of (shortname, so_file, ttl_file) from the suite manifest.ttl."""
    text = manifest_path.read_text(encoding="utf-8")
    stanza = re.compile(
        r"<https://[^>]*/guitaramp-suite/(\w+)>\s+a\s+lv2:Plugin\s*;\s*"
        r"lv2:binary\s+<([^>]+)>\s*;\s*"
        r"rdfs:seeAlso\s+<([^>]+)>",
        re.MULTILINE,
    )
    out = [(m.group(1), m.group(2), m.group(3)) for m in stanza.finditer(text)]
    if not out:
        sys.exit(f"!! no plugin stanzas found in {manifest_path}")
    return out


def _is_nam_uri(uri: str) -> bool:
    return any(uri.rstrip(">").endswith("#" + f) for f in NAM_FRAGMENTS)


def strip_nam_from_ttl(text: str) -> str:
    lines = text.splitlines()
    out = []
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        stripped = line.strip()

        # 1) Drop a NAM file Parameter block: subject line .. up to the '.' terminator,
        #    plus any comment lines directly above it (stop at a blank/non-comment).
        if PARAM_SUBJECT_RE.match(stripped):
            while out and out[-1].strip().startswith("#"):
                out.pop()
            while i < n and not lines[i].rstrip().endswith("."):
                i += 1
            i += 1  # skip the terminator line too
            continue

        # 2) Filter NAM URIs out of a patch:writable statement (may span lines).
        if stripped.startswith("patch:writable"):
            indent = line[: len(line) - len(line.lstrip())]
            buf = [line]
            while not lines[i].rstrip().endswith(";"):
                i += 1
                buf.append(lines[i])
            i += 1  # consume the line ending in ';'
            joined = "\n".join(buf)
            kept = [u for u in URI_RE.findall(joined) if not _is_nam_uri(u)]
            if kept:
                out.append(f"{indent}patch:writable " + " , ".join(kept) + " ;")
            # else: drop the whole (now empty) statement
            continue

        # 3) Drop the "Neural (NAM)" model scalePoint line. A trailing ';' left on
        #    the preceding scalePoint before a ']' is valid Turtle, so no fixup.
        if 'rdfs:label "Neural (NAM)"' in line:
            i += 1
            continue

        out.append(line)
        i += 1

    return "\n".join(out) + ("\n" if text.endswith("\n") else "")


MANIFEST_TMPL = """@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .

<https://rpowell5064.github.io/guitaramp-suite/{name}>
    a lv2:Plugin ;
    lv2:binary <{so}> ;
    rdfs:seeAlso <{ttl}> .
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bundle", required=True, type=Path,
                    help="built guitaramp-suite.lv2 directory")
    ap.add_argument("--out", required=True, type=Path,
                    help="output dir; per-plugin <name>.lv2 bundles are written here")
    ap.add_argument("--strip-nam", action="store_true",
                    help="remove NAM from amp/drive/cab/hexforge (NAM-disabled targets)")
    args = ap.parse_args()

    bundle = args.bundle
    if not (bundle / "manifest.ttl").is_file():
        sys.exit(f"!! {bundle}/manifest.ttl not found")

    plugins = parse_manifest(bundle / "manifest.ttl")
    args.out.mkdir(parents=True, exist_ok=True)

    for name, so, ttl in plugins:
        src_so, src_ttl, src_gui = bundle / so, bundle / ttl, bundle / f"modgui-{name}"
        for p in (src_so, src_ttl):
            if not p.is_file():
                sys.exit(f"!! missing {p}")

        dest = args.out / f"{name}.lv2"
        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir(parents=True)

        shutil.copy2(src_so, dest / so)

        ttl_text = src_ttl.read_text(encoding="utf-8")
        if args.strip_nam and name in NAM_PLUGINS:
            ttl_text = strip_nam_from_ttl(ttl_text)
            if 'rdfs:range atom:Path' in ttl_text and any(
                    f in ttl_text for f in ("#nammodel", "#namfile", "#ampnam",
                                            "#drnam", "#cabnam")):
                sys.exit(f"!! NAM strip incomplete for {name}.ttl")
        (dest / ttl).write_text(ttl_text, encoding="utf-8")

        if src_gui.is_dir():
            shutil.copytree(src_gui, dest / f"modgui-{name}")

        (dest / "manifest.ttl").write_text(
            MANIFEST_TMPL.format(name=name, so=so, ttl=ttl), encoding="utf-8")

        print(f"   {name}.lv2  (so={so}, ttl={ttl}, "
              f"modgui={'yes' if src_gui.is_dir() else 'NO'}"
              f"{', NAM-stripped' if args.strip_nam and name in NAM_PLUGINS else ''})")

    print(f"== wrote {len(plugins)} per-plugin bundles to {args.out}")


if __name__ == "__main__":
    main()
