"""nam_compare forensics: where does the specESR live?  usage: namcmp_forensics.py <dump-prefix>
Reads <prefix>_{in,ref,mod}.wav written by `nam_compare --dump <prefix>` and prints the error split by DI take
(the di_ref/di_all.wav takes: finger / strum / palm / lead), by 1/3-octave band (with the LS-matched level delta),
and a 1 s time track — this is what separated 'voicing' from 'clipper character' in the B7K fit (2026-08-28)."""
import sys, numpy as np
from scipy.io import wavfile
from scipy.signal import stft

prefix = sys.argv[1]
sr, x = wavfile.read(prefix + '_in.wav')
_, r = wavfile.read(prefix + '_ref.wav')
_, m = wavfile.read(prefix + '_mod.wav')
n = min(len(r), len(m), len(x)); x, r, m = x[:n].astype(np.float64), r[:n].astype(np.float64), m[:n].astype(np.float64)

# align (lag search +-256 by cross-correlation on a mid chunk)
seg = slice(sr * 20, sr * 40)
best, bestc = 0, -1
for lag in range(-256, 257):
    a = r[seg]; b = np.roll(m, lag)[seg]
    c = abs(np.dot(a, b))
    if c > bestc: bestc, best = c, lag
m = np.roll(m, best)

NW = 2048
f, t, R = stft(r, sr, window='hann', nperseg=NW, noverlap=NW // 2, boundary=None, padded=False)
_, _, M = stft(m, sr, window='hann', nperseg=NW, noverlap=NW // 2, boundary=None, padded=False)
R, M = np.abs(R[1:]), np.abs(M[1:]); f = f[1:]
g = (R * M).sum() / (M * M).sum()
D = (R - g * M) ** 2
tot = (R ** 2).sum()
print(f'{prefix}: lag {best}, LS mag gain {g:.3f} ({20*np.log10(g):+.1f} dB), specESR {100*D.sum()/tot:.2f}%')

# per-take contributions (takes: 0-19.75 | 20.25-39.75 | 40.25-57.95 | 58.45-78.45, minus the 0.5 s skip)
takes = [('finger', 0, 19.25), ('strum', 19.75, 39.25), ('palm', 39.75, 57.45), ('lead', 57.95, 78.0)]
print('  take     err/tot%  ESR-within%  refRMSdB  modRMSdB(g)')
for name, a, b in takes:
    sel = (t >= a) & (t < b)
    e = D[:, sel].sum(); w = (R[:, sel] ** 2).sum()
    rr = 20 * np.log10(np.sqrt(np.mean(r[int(a*sr):int(b*sr)] ** 2)) + 1e-9)
    mm = 20 * np.log10(g * np.sqrt(np.mean(m[int(a*sr):int(b*sr)] ** 2)) + 1e-9)
    print(f'  {name:8s} {100*e/tot:7.2f}   {100*e/w:8.2f}    {rr:7.1f}   {mm:7.1f}')

# per-band contributions (1/3-octave-ish)
edges = [20, 45, 70, 100, 140, 180, 250, 350, 500, 700, 1000, 1400, 2000, 2800, 4000, 5600, 8000, 12000, 20000]
print('  band(Hz)     err/tot%   ref%energy   mod-ref dB (LS-matched)')
for lo, hi in zip(edges[:-1], edges[1:]):
    sel = (f >= lo) & (f < hi)
    e = D[sel].sum(); wr = (R[sel] ** 2).sum(); wm = (g * M[sel]) ** 2
    print(f'  {lo:5d}-{hi:5d}   {100*e/tot:7.2f}    {100*wr/tot:7.2f}     {10*np.log10(wm.sum()/wr+1e-12):+6.1f}')

# 1-s time track: level and within-window ESR
print('  t(s)  refdB  moddB  inpk   ESR%')
for s0 in range(0, int(n / sr)):
    sel = (t >= s0) & (t < s0 + 1)
    w = (R[:, sel] ** 2).sum()
    if w <= 0: continue
    e = D[:, sel].sum()
    rr = 20 * np.log10(np.sqrt(np.mean(r[s0*sr:(s0+1)*sr] ** 2)) + 1e-9)
    mm = 20 * np.log10(g * np.sqrt(np.mean(m[s0*sr:(s0+1)*sr] ** 2)) + 1e-9)
    pk = 20 * np.log10(np.max(np.abs(x[s0*sr:(s0+1)*sr])) + 1e-9)
    if rr < -60: continue
    print(f'  {s0:3d}  {rr:6.1f} {mm:6.1f} {pk:6.1f} {100*e/w:6.1f}')
