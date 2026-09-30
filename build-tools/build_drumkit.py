#!/usr/bin/env python3
"""Build the Practice plugin's resynthesised drum kit from source recordings.

Runs the analyser over a handful of representative hits per instrument and
packs the resulting parametric models into one HXKIT1 container. No sample
audio ends up in the output — the kit ships as numbers.

Source: Big Rusty Drums (CC0 1.0, Karoryfer Samples). CC0 means no attribution
is legally required and nothing constrains relicensing, which is why it was
chosen over the CC-BY and CC-BY-SA alternatives; the share-alike kits would
have been incompatible with the commercial half of the dual licence.

The sources are FLAC, so `flac` must be installed, and they live outside the
repo (drum_src/ is git-ignored) because they are a development input, not a
shipped asset.

Run (WSL, from the repo root):
    python3 build-tools/build_drumkit.py \\
        --src drum_src/Samples --analyzer /tmp/drum_analyze \\
        --out lv2/practice/drumkit.dat
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile

# Instrument indices must match hexdrums::Instrument in DrumPatterns.h.
INSTRUMENTS = [
    ("KICK",       0), ("SNARE",     1), ("SIDESTICK", 2),
    ("TOM_HI",     3), ("TOM_MID",   4), ("TOM_FLOOR", 5),
    ("HAT_CLOSED", 6), ("HAT_PEDAL", 7), ("HAT_OPEN",  8),
    ("CRASH",      9), ("RIDE",     10), ("RIDE_BELL", 11),
    ("CHINA",     12), ("STACK",    13), ("SNARE_RIM", 14),
]

# Where each instrument's close-mic hits live inside the source kit, and how
# long a model to keep. Close mics are used deliberately: overheads carry
# bleed from the rest of the kit, which would put a crash's partials into the
# snare's model. Room and overhead character belongs in the engine, applied to
# the whole kit, not baked into each voice.
#
# Cymbals are capped because their tails dominate the model size (an 18 s ride
# is mostly inaudible decay) and a practice plugin retriggers long before then.
# (path, model seconds, velocity layers, partial budget, dense modes, window)
#
# ANALYSIS WINDOW matters enormously for the low drums and was set far too
# short. At 1024 points (23 ms) a 47 Hz kick fundamental gets barely one cycle
# and a ~170 Hz mainlobe, so its lowest partials were smeared together and
# largely missed — the energy then fell into the residual and was resynthesised
# as NOISE, i.e. the kick came out as rumble rather than as a note. Measured
# band-distance from the real recording, by window:
#
#            1024     2048     4096
#   kick    10.82     4.83     7.22
#   tom     14.57    10.08     5.29     <- "tom groove sounds really bad"
#   snare    5.95     5.62     5.13
#
# Cymbals and hats keep the short window: their partials are high and dense, a
# long window smears their attack, and their modes come from a separate
# 65536-point analysis anyway.
#
# Two model types, chosen per instrument:
#
#   DRUMS get tracked partials + a noise residual. Their fundamentals glide (a
#   kick falls 112 -> 47 Hz) and their decays are not single exponentials, so
#   the partials have to be followed frame by frame.
#
#   CYMBALS AND HATS get a dense bank of FIXED modes instead. Nothing about
#   them sweeps, so tracking bought nothing and cost warble — which is why
#   their partial budget had been cut to 4-8, leaving them as 24-band shaped
#   noise that measured a spectral crest of 7.8 dB against a real crash's 17.7.
#   That is the "sounds like static" the user heard. 400 static modes measure
#   17.3 dB, cost 1.6 kB instead of ~140 kB, and cannot warble because no
#   frequency ever moves. Their noise model is trimmed to the attack only —
#   the modes carry the whole ringing body, and keeping seconds of broadband
#   residual would put the hiss straight back.
#
# The partial budget matters more than it looks. Resynthesised sinusoids are
# the source of warble, so each instrument gets only as many as it genuinely
# has. Measured on real hits, cymbals barely use theirs at all: a crash scores
# 12.01 dB with 24 partials and 12.90 dB with NONE, because its sound is
# essentially all noise. Spending 24 gliding tones to buy 0.9 dB is a bad
# trade when those tones are what makes it sound electronic. Drums are the
# opposite — their shell modes are the sound.
SOURCES = {
    #                path                      secs  vel  parts  modes
    "KICK":       ("kick_24/kick/kick",        3.0, 4, 32,   0, 2048),
    "SNARE":      ("snare_14/center/top",      3.0, 4, 28,   0, 4096),
    "SIDESTICK":  ("snare_14/sidestick/top",   2.0, 3, 16,   0, 2048),
    "TOM_HI":     ("tom_14/center/cl",         3.5, 4, 28,   0, 4096),
    "TOM_MID":    ("tom_18/center/cl",         4.0, 4, 28,   0, 4096),
    "TOM_FLOOR":  ("tom_22/center/cl",         4.5, 4, 28,   0, 4096),
    "HAT_CLOSED": ("hihat_14/cl/cl",           1.5, 4,  0, 500, 1024),
    "HAT_PEDAL":  ("hihat_14/chik/cl",         1.5, 3,  0, 400, 1024),
    "HAT_OPEN":   ("hihat_14/open/cl",         4.0, 4,  0, 700, 1024),
    "CRASH":      ("crash_17/cr/cl",           8.0, 3,  0, 900, 1024),
    "RIDE":       ("ride_22/rd/cl",            8.0, 3,  0,1000, 1024),
    "RIDE_BELL":  ("ride_22/bl/cl",            8.0, 3,  0, 800, 1024),
    # Added for modern metal. China and stack are cymbals, so they take
    # the dense modal model; the rimshot is a drum and takes tracked
    # partials with the snare's 4096-point window.
    "CHINA":      ("china_18/cn/cl",           6.0, 3,  0,1000, 1024),
    "STACK":      ("stack_3_layer/mid/cl",     2.5, 3,  0, 500, 1024),
    "SNARE_RIM":  ("snare_14/rimshot/top",     3.0, 3, 28,   0, 4096),
}

# How much of the noise residual to keep, in seconds. For a modal instrument
# this only needs to cover the stick attack.
NOISE_SECONDS_MODAL = 0.5

VL_RE = re.compile(r"_vl(\d+)_rr(\d+)\.flac$", re.I)


def source_hits(directory):
    """Available (velocity_layer, path) for round-robin 1, sorted quietest first."""
    if not os.path.isdir(directory):
        return []
    out = []
    for name in os.listdir(directory):
        m = VL_RE.search(name)
        if m and int(m.group(2)) == 1:
            out.append((int(m.group(1)), os.path.join(directory, name)))
    out.sort()
    return out


def pick_layers(hits, count):
    """Spread `count` picks across the available dynamic range.

    The source kits have 10-14 layers; we keep a handful. Always include the
    hardest hit — it is the one a metal pattern spends most of its time on.
    """
    if not hits:
        return []
    if len(hits) <= count:
        return hits
    idx = [round(i * (len(hits) - 1) / (count - 1)) for i in range(count)]
    return [hits[i] for i in sorted(set(idx))]


def velocity_windows(n):
    """Split 1..127 into n contiguous windows, quietest first."""
    edges = [round(1 + i * 126 / n) for i in range(n + 1)]
    return [(edges[i], edges[i + 1] - 1 if i + 1 < n else 127) for i in range(n)]


def analyse(analyzer, wav_path, out_path, max_seconds, partials, modes, win):
    cmd = [analyzer, wav_path, "--emit", out_path, "--quiet"]
    if max_seconds:
        cmd += ["--max-seconds", str(max_seconds)]
    cmd += ["--win", str(win)]
    if modes:
        cmd += ["--dense-modes", str(modes),
                "--noise-seconds", str(NOISE_SECONDS_MODAL)]
    else:
        cmd += ["--partials", str(partials)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        return None
    for line in res.stdout.splitlines():
        if line.startswith("STAT "):
            f = line.split()
            return {"dB": float(f[2]), "partials": int(f[3]),
                    "frames": int(f[4]), "bytes": int(f[5]),
                    "peak": float(f[6]) if len(f) > 6 else 1.0}
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="the kit's Samples/ directory")
    ap.add_argument("--analyzer", default="/tmp/drum_analyze")
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="Big Rusty (CC0)")
    args = ap.parse_args()

    if not os.path.isfile(args.analyzer):
        sys.exit("analyzer not found: %s" % args.analyzer)

    entries = []          # (inst_index, loVel, hiVel, gain, blob)
    total_dB, total_n = 0.0, 0
    tmp = tempfile.mkdtemp(prefix="hexdrums_")

    print("%-12s %-5s %-6s %8s %8s %6s %7s" %
          ("instrument", "layer", "vel", "dB", "bytes", "parts", "gain"))
    print("-" * 64)

    for name, index in INSTRUMENTS:
        rel, max_sec, want, budget, nmodes, win = SOURCES[name]
        hits = source_hits(os.path.join(args.src, rel))
        if not hits:
            print("%-12s  !! no sources under %s" % (name, rel))
            continue

        chosen = pick_layers(hits, want)
        windows = velocity_windows(len(chosen))
        pending = []      # collected first, so the whole instrument can be scaled together

        for (vl, flac), (lo, hi) in zip(chosen, windows):
            wav = os.path.join(tmp, "hit.wav")
            hit = os.path.join(tmp, "hit.bin")
            dec = subprocess.run(["flac", "-s", "-d", "--force", "-o", wav, flac],
                                 capture_output=True)
            if dec.returncode != 0:
                print("%-12s  !! flac failed on %s" % (name, os.path.basename(flac)))
                continue

            stat = analyse(args.analyzer, wav, hit, max_sec, budget, nmodes, win)
            if stat is None:
                print("%-12s  !! analysis failed on %s" % (name, os.path.basename(flac)))
                continue

            with open(hit, "rb") as f:
                blob = f.read()
            pending.append((vl, lo, hi, blob, stat))

        if not pending:
            continue

        # Normalise per INSTRUMENT, not per layer. The source kit's close mics
        # were recorded at wildly different gains (the raw kick came out 10x
        # the crash), so without this the mix balance is set by whatever the
        # engineer did rather than by the kit's balance table. Scaling every
        # layer of one instrument by the SAME factor preserves the dynamics
        # between velocity layers, which is the whole point of having them.
        loudest = max(st["peak"] for (_, _, _, _, st) in pending)
        gain = (0.80 / loudest) if loudest > 1e-6 else 1.0

        for vl, lo, hi, blob, stat in pending:
            entries.append((index, lo, hi, gain, blob))
            total_dB += stat["dB"]; total_n += 1
            print("%-12s vl%-3d %3d-%-3d %7.2f %8d %6d %7.2f"
                  % (name, vl, lo, hi, stat["dB"], len(blob), stat["partials"], gain))

    if not entries:
        sys.exit("no models produced")

    # ── Pack HXKIT1 ──────────────────────────────────────────────────────────
    header = b"HXKIT1" + bytes([2, len(entries)])   # version 2 adds the gain field
    table_size = 16 * len(entries)
    offset = len(header) + table_size

    table, body = b"", b""
    for inst, lo, hi, gain, blob in entries:
        table += struct.pack("<BBBBIIf", inst, lo, hi, 0, offset, len(blob), gain)
        body += blob
        offset += len(blob)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(header + table + body)

    size = len(header) + table_size + len(body)
    print("-" * 64)
    print("%d models, %d instruments, mean %.2f dB" %
          (len(entries), len({e[0] for e in entries}), total_dB / total_n))
    print("wrote %s  (%.2f MB)" % (args.out, size / 1048576.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
