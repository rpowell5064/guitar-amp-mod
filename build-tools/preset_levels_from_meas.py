# Turn hexforge_meas output (idx rms_dBFS peak_dBFS, measured at out_level = -20)
# into build-tools/preset_levels.json keyed by preset NAME (via preset_manifest.txt),
# which gen_hexforge_presets.py folds into each preset's out_level.
#   python build-tools/preset_levels_from_meas.py <hfmeas output file> [more files...]
# Later files override earlier ones (re-measure a subset after re-voicing knobs).
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
names = {}
for line in open(os.path.join(HERE, "preset_manifest.txt"), encoding="utf-8"):
    if line.startswith("#") or not line.strip(): continue
    b, s, idx, rest = line.rstrip("\n").split(" ", 3)
    names[int(idx)] = rest.split("|")[0]
path = os.path.join(HERE, "preset_levels.json")
levels = json.load(open(path)) if os.path.exists(path) else {}
n = 0
for f in sys.argv[1:]:
    for line in open(f, encoding="utf-8", errors="replace"):
        parts = line.split()
        if len(parts) != 3 or not parts[0].isdigit(): continue
        idx, rms, peak = int(parts[0]), float(parts[1]), float(parts[2])
        if idx not in names: continue
        levels[names[idx]] = {"rms": rms, "peak": peak, "idx": idx}
        n += 1
json.dump(levels, open(path, "w", encoding="utf-8"), indent=1, sort_keys=True)
print("levels: %d measured rows folded, %d presets in %s" % (n, len(levels), path))
