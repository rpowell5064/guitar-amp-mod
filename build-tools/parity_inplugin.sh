#!/bin/bash
# parity_inplugin.sh — loudness-parity rows measured INSIDE the Hex Forge plugin (2026-10-06).
#
# The rows in hf_types.inc / amp_plugin.cpp (kCompMkDb / kCompGainDb / kCompMasterDb) used to be
# derived from the bare models on a quiet DI (parity_measure.sh). A component twin's loudness-vs-gain
# law depends on input level far more than the shipped model's does (the Friedman twin swung 31 dB
# across the gain knob at the lab level but ~9 dB inside a preset with a pedal in front), so rows
# derived at the quiet level over-corrected and INVERTED the knob in use. This measures shipped vs
# twin through the real plugin on a preset whose chain has no drive pedal, at two input levels, and
# prints the rows for each level plus their mean.
#
#   bash build-tools/parity_inplugin.sh <preset idx> [wavgain dB ...]
# e.g. bash build-tools/parity_inplugin.sh 28 0 10
set -u
cd "$(dirname "$0")/.." || exit 1
P=$1; shift
LEVELS=("$@"); [ ${#LEVELS[@]} -eq 0 ] && LEVELS=(0 10)
BA=./build-wsl/hexforge_bassab
DI=di_ref/di_take_1.wav
GAINS=(0.2 0.3 0.4 0.5 0.6 0.7 0.8)
MASTERS=(0.2 0.4 0.7 0.9)
rms() {   # rms <mode flags...> -- mean of the bass .1 / .9 readings (dBFS)
  $BA "$P" "$@" --wav $DI 2>/dev/null | tail -1 | awk '{printf "%.2f", ($(NF-1)+$NF)/2}'
}
echo "== preset $P  in-plugin parity  $(date +%H:%M)"
declare -A GD MD
for lvl in "${LEVELS[@]}"; do
  echo "-- input wavgain ${lvl} dB"
  G=(); M=()
  for g in "${GAINS[@]}"; do
    a=$(rms --nocomp --wavgain "$lvl" --set amp_gain=$g --set amp_master=0.7)
    b=$(rms          --wavgain "$lvl" --set amp_gain=$g --set amp_master=0.7)
    d=$(python3 -c "print(round($a-($b),2))"); G+=("$d")
    echo "  gain $g master .7  shipped $a twin $b delta $d"
  done
  for m in "${MASTERS[@]}"; do
    a=$(rms --nocomp --wavgain "$lvl" --set amp_gain=0.5 --set amp_master=$m)
    b=$(rms          --wavgain "$lvl" --set amp_gain=0.5 --set amp_master=$m)
    d=$(python3 -c "print(round($a-($b),2))"); M+=("$d")
    echo "  gain .5 master $m  shipped $a twin $b delta $d"
  done
  GD[$lvl]="${G[*]}"; MD[$lvl]="${M[*]}"
done
python3 - "${LEVELS[@]}" -- "${GD[@]}" -- "${MD[@]}" <<'PY'
import sys
a=sys.argv[1:]; i=a.index('--'); j=a.index('--', i+1)
levels=a[:i]; G=[[float(x) for x in s.split()] for s in a[i+1:j]]; M=[[float(x) for x in s.split()] for s in a[j+1:]]
def rows(Gr, Mr):
    mk=Gr[3]
    return mk, [g-mk for g in Gr], [m-mk for m in Mr]
for k,l in enumerate(levels):
    mk,gr,mr=rows(G[k],M[k])
    print("  @%s dB: kCompMkDb %.2f  gain { %s }  master { %s }" % (l, mk, ', '.join('%.2f'%x for x in gr), ', '.join('%.2f'%x for x in mr)))
n=len(levels)
Gm=[sum(G[k][i] for k in range(n))/n for i in range(len(G[0]))]
Mm=[sum(M[k][i] for k in range(n))/n for i in range(len(M[0]))]
mk,gr,mr=rows(Gm,Mm)
print("  MEAN:  kCompMkDb      = %.2ff" % mk)
print("         kCompGainDb    = { %s }" % ', '.join('%.2ff'%x for x in gr))
print("         kCompMasterDb  = { %s }" % ', '.join('%.2ff'%x for x in mr))
PY
