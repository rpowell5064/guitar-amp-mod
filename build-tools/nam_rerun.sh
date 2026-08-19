#!/bin/bash
# Re-measure every NAM-checked model with the user's DI takes (2026-08-19).
# Runs build/tools/nam_compare (ESR-enabled) once per historical tuning point,
# logs to /tmp/namrerun/, prints a summary table at the end.
# Knob flags marked [doc] are quoted from AMP-REVOICE-NOTES; [est] are estimated.
set -u
cd ~/guitar-amp-mod
NC=build/tools/nam_compare
OUT=/tmp/namrerun
mkdir -p "$OUT"

# Concatenate the takes (0.5 s gaps) into the batch probe.
python3 - <<'PY'
import wave, array
out = []
sr = 48000
for i in range(1, 5):
    w = wave.open(f'/tmp/di_take_{i}.wav')
    sr = w.getframerate()
    out.append(w.readframes(w.getnframes()))
    w.close()
gap = b'\x00' * (int(0.5 * sr) * 4)
o = wave.open('/tmp/di_all.wav', 'w')
o.setnchannels(1); o.setsampwidth(4); o.setframerate(sr)
o.writeframes(gap.join(out))
o.close()
print('di_all.wav written')
PY

run() {  # label | capture path | model + flags...
    local label="$1"; local cap="$2"; shift 2
    if [ ! -f "$cap" ]; then echo "SKIP $label (missing: $cap)"; return; fi
    echo "== $label =="
    "$NC" --ref "$cap" "$@" --in /tmp/di_all.wav > "$OUT/$label.log" 2>&1
    grep -E "^  (waveESR|specESR):" "$OUT/$label.log"
}

D=~/dl_caps
R=nam_refs

# ── Amps ──
run fender-clean      "$D/amps/CLEAN - Fender Deluxe Reverb 1965 [Hyper Accuracy].nam" --model fender --gain 0.4                                    # [doc]
run fender-hot        "$D/amps/HOT - Fender Deluxe Reverb 1965 [Hyper Accuracy].nam"   --model fender --gain 0.7                                    # [est]
run jcm800-high       "$D/amps/JCM 800 High channel - P5 B5 M5 T5 M10 PA10.nam"        --model marshall --gain 0.5 --bass 0.5 --mid 0.5 --treble 0.5 --master 1.0   # [doc]
run plexi-jumped      "$D/amps2/plexi/MARSHALL Plexi Super Lead 1959 EL34 CH I High Jumped.nam" --model plexi --gain 1.0 --master 1.0               # [est]
run markv-iic-hgbal   "$D/amps2/markv/Mesa Boogie - Mark V (Ch 3 MK-IIC+) HG bal [18dBu].nam" --model markv --mode 6 --gain 0.8                     # [est gain]
run recto-ch3-modern  "$D/amps2/recto/Mesa Dual Rectifier Solo Head CH3 Modern.nam"    --model recto --mode 7 --gain 0.7                            # [est gain]
run mt15-sweetspot    "$D/amps2/mt15/PRS_MT_15_Metal_Sweet_Spot.nam"                   --model mt15 --gain 0.7                                      # [doc]
run evh-red-sixes     "$D/evh_headonly/EVH 5150 III Red All Sixes.nam"                 --model evh --channel 2 --gain 0.6 --bass 0.6 --mid 0.6 --treble 0.6   # [doc-ish]
run evh-blue-sixes    "$D/evh_headonly/EVH 5150 III Blue All Sixes.nam"                --model evh --channel 1 --gain 0.6 --bass 0.6 --mid 0.6 --treble 0.6   # [doc-ish]
run friedman-be-g6    "$D/amps2/friedman/BE100 - BE - GAIN 6.nam"                      --model friedman --channel 1 --gain 0.6                      # [doc]
run vox-b5t5cut5m8    "$D/amps2/vox/AC30 (TOP BOOST) V3 - EQ Standard B5 T5 - Cut 5 - M 8.nam" --model vox --gain 0.6 --bass 0.5 --treble 0.5 --mid 0.8   # [doc]
run peavey-sat3       "$D/amps2/backstage/Peavey Backstage+ (Pre10 Sat3 Pos7 L9 M6 H5).nam" --model peavey --gain 0.3                               # [doc]
run hiwatt-all12      "$D/amps2/hiwatt/HIWATT DI ALL 12 MASTER 10 REVyHI.nam"          --model hiwatt --gain 0.5 --bass 0.5 --mid 0.5 --treble 0.5 --master 1.0   # [doc]
run rockerverb-dirty  "$D/amps2/rockerverb/Rockerverb Dirty - Gain 10, Bass 10, Mid 10, Treble 10.nam" --model rockerverb --channel 0 --gain 1.0 --bass 1.0 --mid 1.0 --treble 1.0   # [doc]
run rockerverb-clean  "$D/amps2/rockerverb/Rockerverb Clean - Bass 5, Treble 5.nam"    --model rockerverb --channel 1 --bass 0.5 --treble 0.5       # [doc]

# ── Drives ──
run ds1-g5            "$D/ds1/Boss DS-1 T5_V10_G5.nam"          --model ds1 --gain 0.5 --tone 0.5 --level 1.0    # [doc]
run ts808-od5t2       "$D/ts808/17-TS808_Hot_Lvl6_OD5_T2.nam"   --model ts808 --gain 0.5 --tone 0.2 --level 0.6  # [doc]
run rat-d5f3          "$D/rat/ProCo Rat - Distortion 5 Filter 3.nam" --model rat --gain 0.5 --tone 0.3           # [doc]
run sd1-d1            "$R/sd1/sd1_d1.nam"                       --model sd1 --gain 0.1                           # [doc]
run sd1-d7            "$R/sd1/sd1_d7.nam"                       --model sd1 --gain 0.7                           # [doc]

# ── Muff eras (cap→era mapping partly [est]; ambiguous caps run against 2 eras) ──
run muff-bluebeard-e1   "$R/muff/bluebeard.nam"     --model muff --era 1 --gain 0.7 --tone 0.5 --level 0.5
run muff-cherub-e2      "$R/muff/cherub.nam"        --model muff --era 2 --gain 0.7 --tone 0.5 --level 0.5
run muff-cherub-e5      "$R/muff/cherub.nam"        --model muff --era 5 --gain 0.7 --tone 0.5 --level 0.5
run muff-civilwar-e3    "$R/muff/civilwar.nam"      --model muff --era 3 --gain 0.7 --tone 0.5 --level 0.5
run muff-civilwar-e4    "$R/muff/civilwar.nam"      --model muff --era 4 --gain 0.7 --tone 0.5 --level 0.5
run muff-blackrus-e3    "$R/muff/blackrussian.nam"  --model muff --era 3 --gain 0.7 --tone 0.5 --level 0.5
run muff-blackrus-e4    "$R/muff/blackrussian.nam"  --model muff --era 4 --gain 0.7 --tone 0.5 --level 0.5

echo
echo "════ SUMMARY (ESR / worst FR delta / makeup) ════"
for f in "$OUT"/*.log; do
    l=$(basename "$f" .log)
    esr=$(grep -E "^  specESR:" "$f" | head -1 | sed 's/^  //')
    wesr=$(grep -oE "waveESR: [0-9.]+%" "$f" | head -1)
    fr=$(grep -E "^  [0-9]" "$f" | awk '{v=$4; if (v<0) v=-v; if (v>mx) {mx=v; ln=$0}} END {printf "worstFR %.1f dB", mx}')
    mk=$(grep "makeup to match" "$f" | grep -oE '[+-][0-9.]+ dB')
    printf "%-20s %-52s %-18s %-16s makeup %s\n" "$l" "$esr" "$wesr" "$fr" "$mk"
done
echo "logs: $OUT/"
