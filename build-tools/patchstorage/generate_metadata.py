#!/usr/bin/env python3
"""
Render the single-source-of-truth plugins_meta.json into the artifacts the
PatchStorage upload needs, for one target:

  * plugins.json  — the uploader's bundle-keyed override file
                    (source_code_url / donate_url / license / categories),
                    one entry per <plugin>.lv2 bundle present in the target dir.
  * LISTINGS.md   — ready-to-paste title + description per plugin, with the
                    correct NAM availability note for THIS target.

Usage:
    python generate_metadata.py --target <target> --out dist/patchstorage/<target>

The bundles must already exist in --out (produced by split_bundle.py); only
plugins actually present are emitted, so the 32-bit kit and the 64-bit kit stay
consistent with what was built.
"""
import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
NAM_TARGETS = {"linux-amd64", "rpi-aarch64"}   # targets that ship NAM
# The uploader's licenses.json matches this string to a PatchStorage license id
# (gpl-3-0 -> id 4178). The SPDX "GPL-3.0-or-later" is NOT a recognized key, so we
# emit the canonical token here while keeping the human label for LISTINGS.md.
UPLOADER_LICENSE = "gpl-3-0"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    meta = json.loads((HERE / "plugins_meta.json").read_text(encoding="utf-8"))
    plugins = meta["plugins"]
    nam_on = args.target in NAM_TARGETS

    present = sorted(p.stem for p in args.out.glob("*.lv2"))
    if not present:
        raise SystemExit(f"!! no *.lv2 bundles in {args.out} — run split_bundle.py first")

    # 1) uploader plugins.json (bundle-keyed)
    uploader = {}
    for name in present:
        m = plugins.get(name)
        if not m:
            raise SystemExit(f"!! {name}.lv2 present but missing from plugins_meta.json")
        uploader[f"{name}.lv2"] = {
            "source_code_url": meta["source_code_url"],
            "donate_url": meta["donate_url"],
            "license": UPLOADER_LICENSE,
            "categories": m["categories"],
            # The uploader's get_tags() does `tags.extend(overwrites['tags'])`, so
            # these reach the listing (merged with its lv2-plugin/modgui defaults).
            "tags": m["tags"],
        }
    (args.out / "plugins.json").write_text(
        json.dumps(uploader, indent=2) + "\n", encoding="utf-8")

    # 2) human-facing LISTINGS.md
    lines = [f"# PatchStorage listings — `{args.target}`",
             "",
             f"NAM (Neural Amp Modeler): **{'included' if nam_on else 'not included on this target'}**.",
             ""]
    for name in present:
        m = plugins[name]
        desc = m["long"]
        tags = ", ".join(dict.fromkeys(m["tags"] + ["lv2-plugin"]))
        nam_note = ""
        if m["nam"] and not nam_on:
            nam_note = ("\n\n_Note: this 32-bit build omits the Neural (NAM) option; "
                        "the algorithmic models are included._")
        lines += [f"## {m['title']}  (`{name}.lv2`)",
                  f"*{m['short']}*",
                  "",
                  f"- **Categories:** {', '.join(m['categories'])}",
                  f"- **Tags:** {tags}",
                  f"- **License:** {meta['license']}",
                  "",
                  desc + nam_note,
                  ""]
    (args.out / "LISTINGS.md").write_text("\n".join(lines), encoding="utf-8")

    print(f"== [{args.target}] wrote plugins.json ({len(uploader)} bundles) + LISTINGS.md "
          f"(NAM {'on' if nam_on else 'off'})")


if __name__ == "__main__":
    main()
