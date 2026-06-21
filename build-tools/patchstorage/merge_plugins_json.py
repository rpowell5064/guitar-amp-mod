#!/usr/bin/env python3
"""
Merge our generated, bundle-keyed plugins.json into the patchstorage-lv2-uploader's
root plugins.json (our entries win). The uploader reads a single plugins.json at its
repo root keyed by "<name>.lv2"; this folds the Hex Chain entries into it without
disturbing the existing community entries.

    python merge_plugins_json.py --uploader <uploader-plugins.json> --ours <ours.json>
"""
import argparse
import json
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--uploader", required=True, type=Path,
                    help="the uploader repo's plugins.json (merged in place)")
    ap.add_argument("--ours", required=True, type=Path,
                    help="our generated bundle-keyed plugins.json")
    args = ap.parse_args()

    base = json.loads(args.uploader.read_text(encoding="utf-8")) if args.uploader.exists() else {}
    ours = json.loads(args.ours.read_text(encoding="utf-8"))
    base.update(ours)  # our entries take precedence
    args.uploader.write_text(json.dumps(base, indent=2) + "\n", encoding="utf-8")
    print(f"== merged {len(ours)} entries into {args.uploader} ({len(base)} total)")


if __name__ == "__main__":
    main()
