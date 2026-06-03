"""
TS808 Tube Screamer frequency response plot.

Models the analog RC network via the bilinear transform, then computes and plots
the magnitude response from 20 Hz to 20 kHz.  Three tone settings are overlaid
so the mid-hump shift is clearly visible.

Run:  python ts808_response.py
Requires:  numpy, scipy, matplotlib
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from scipy.signal import freqz, lfilter_zi


# ---------------------------------------------------------------------------
# Analog component values (original Ibanez TS808 schematic)
# ---------------------------------------------------------------------------
R_HP = 4700.0      # Input HP resistor (Ω)   — R4 in schematic
C_HP = 47e-9       # Input HP capacitor (F)  — C4
R_LP = 1000.0      # Output LP resistor (Ω)  — R9
C_LP = 47e-9       # Output LP capacitor (F) — C8

# Tone network: 20 kΩ pot + 22 nF cap (approximate)
R_TONE_TOTAL = 20000.0
C_TONE       = 22e-9


FS = 44100.0   # sample rate for digital model


# ---------------------------------------------------------------------------
# Bilinear transform for a 1-pole RC filter
# Returns (b, a) arrays for scipy.signal
# ---------------------------------------------------------------------------
def hp1pole_bilinear(fc: float, fs: float):
    """1-pole high-pass: H(s) = s / (s + 2π·fc)"""
    k  = 2.0 * fs
    wc = 2.0 * np.pi * fc
    n  = 1.0 / (k + wc)
    b0 = k  * n
    a1 = (wc - k) * n          # stored coeff (minus sign absorbed into diff eq)
    # scipy convention: y[n] = b0 x[n] + b1 x[n-1] - a1_stored y[n-1]
    # freqz uses H(z) = B(z)/A(z) where A = [1, a1_scipy]
    # Our a1_stored means: y + a1_stored*y[n-1] = ... → a1_scipy = a1_stored
    return np.array([b0, -b0]), np.array([1.0, a1])


def lp1pole_bilinear(fc: float, fs: float):
    """1-pole low-pass: H(s) = 2π·fc / (s + 2π·fc)"""
    k  = 2.0 * fs
    wc = 2.0 * np.pi * fc
    n  = 1.0 / (k + wc)
    b0 = wc * n
    a1 = (wc - k) * n
    return np.array([b0, b0]), np.array([1.0, a1])


# ---------------------------------------------------------------------------
# Compute combined magnitude response
# ---------------------------------------------------------------------------
NFFT = 8192
freqs = np.linspace(0, FS / 2, NFFT // 2 + 1)
freqs_nz = freqs.copy(); freqs_nz[0] = 1e-6   # avoid log(0) in label placement


def combined_response_db(tone: float):
    """
    Returns magnitude in dB for the combined HPF × LPF × toneLP chain
    at the given tone setting [0=dark, 1=bright].
    """
    fc_hp   = 1.0 / (2.0 * np.pi * R_HP  * C_HP)    # ≈ 720 Hz
    fc_lp   = 1.0 / (2.0 * np.pi * R_LP  * C_LP)    # ≈ 3.4 kHz
    # Tone: log sweep 1 kHz (tone=0) → 10 kHz (tone=1)
    fc_tone = 1000.0 * 10.0**tone

    b_hp, a_hp     = hp1pole_bilinear(fc_hp,   FS)
    b_lp, a_lp     = lp1pole_bilinear(fc_lp,   FS)
    b_tone, a_tone = lp1pole_bilinear(fc_tone, FS)

    _, H_hp   = freqz(b_hp,   a_hp,   worN=NFFT // 2 + 1, fs=FS)
    _, H_lp   = freqz(b_lp,   a_lp,   worN=NFFT // 2 + 1, fs=FS)
    _, H_tone = freqz(b_tone, a_tone, worN=NFFT // 2 + 1, fs=FS)

    H_total = H_hp * H_lp * H_tone
    return 20.0 * np.log10(np.abs(H_total) + 1e-12)


# Individual stage responses for annotation
fc_hp_hz = 1.0 / (2.0 * np.pi * R_HP * C_HP)
fc_lp_hz = 1.0 / (2.0 * np.pi * R_LP * C_LP)

b_hp, a_hp = hp1pole_bilinear(fc_hp_hz, FS)
b_lp, a_lp = lp1pole_bilinear(fc_lp_hz, FS)
_, H_hp_solo = freqz(b_hp, a_hp, worN=NFFT // 2 + 1, fs=FS)
_, H_lp_solo = freqz(b_lp, a_lp, worN=NFFT // 2 + 1, fs=FS)
hp_db = 20.0 * np.log10(np.abs(H_hp_solo) + 1e-12)
lp_db = 20.0 * np.log10(np.abs(H_lp_solo) + 1e-12)


# ---------------------------------------------------------------------------
# Plot
# ---------------------------------------------------------------------------
fig, ax = plt.subplots(figsize=(11, 6))
fig.patch.set_facecolor('#1a1a2e')
ax.set_facecolor('#16213e')

TONE_SETTINGS = [
    (0.0,  '#e94560', 'Tone = 0  (dark,   LP ≈ 1 kHz)'),
    (0.5,  '#f5a623', 'Tone = 0.5 (mid,   LP ≈ 3.2 kHz)'),
    (1.0,  '#4ecdc4', 'Tone = 1  (bright, LP ≈ 10 kHz)'),
]

for tone, color, label in TONE_SETTINGS:
    db = combined_response_db(tone)
    ax.semilogx(freqs_nz, db, color=color, linewidth=2.0, label=label)

# Individual stage overlays (dashed)
ax.semilogx(freqs_nz, hp_db, color='#aaaaaa', linewidth=1.0,
            linestyle='--', alpha=0.6, label=f'Input HP alone  (fc ≈ {fc_hp_hz:.0f} Hz)')
ax.semilogx(freqs_nz, lp_db, color='#888888', linewidth=1.0,
            linestyle=':', alpha=0.6, label=f'Output LP alone (fc ≈ {fc_lp_hz:.0f} Hz)')

# -3 dB reference line
ax.axhline(-3.0, color='#ffffff', linewidth=0.5, linestyle='-', alpha=0.2)
ax.text(22, -2.0, '−3 dB', color='#aaaaaa', fontsize=8, va='bottom')

# Cutoff frequency markers
for fc, label_txt in [(fc_hp_hz, f'HP fc\n{fc_hp_hz:.0f} Hz'), (fc_lp_hz, f'LP fc\n{fc_lp_hz:.0f} Hz')]:
    ax.axvline(fc, color='#ffffff', linewidth=0.8, linestyle='--', alpha=0.35)
    ax.text(fc * 1.05, -38, label_txt, color='#cccccc', fontsize=8, va='bottom')

# Mid-hump centre annotation (geometric mean of HP/LP cutoffs)
hump_center = np.sqrt(fc_hp_hz * fc_lp_hz)
ax.axvline(hump_center, color='#f5a623', linewidth=0.8, linestyle=':', alpha=0.5)
ax.text(hump_center * 1.05, -4, f'Hump centre\n≈{hump_center:.0f} Hz', color='#f5a623',
        fontsize=8, va='top')

ax.set_xlim(20, 20000)
ax.set_ylim(-45, 6)
ax.xaxis.set_major_formatter(ticker.FuncFormatter(
    lambda x, _: f'{int(x/1000)}k' if x >= 1000 else str(int(x))))
ax.set_xticks([20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000])
ax.grid(True, which='both', color='#ffffff', alpha=0.08)
ax.grid(True, which='major', color='#ffffff', alpha=0.15)

ax.set_xlabel('Frequency (Hz)', color='#cccccc', fontsize=11)
ax.set_ylabel('Magnitude (dB)', color='#cccccc', fontsize=11)
ax.set_title('TS808 Tube Screamer — Analog RC Network Frequency Response\n'
             '(bilinear-transform model, 44.1 kHz, no drive nonlinearity)',
             color='#ffffff', fontsize=12, pad=12)

ax.tick_params(colors='#aaaaaa')
for spine in ax.spines.values():
    spine.set_edgecolor('#444466')

legend = ax.legend(loc='lower left', fontsize=9, framealpha=0.4,
                   facecolor='#0f0f1a', edgecolor='#444466', labelcolor='#dddddd')

plt.tight_layout()
plt.savefig('ts808_response.png', dpi=150, bbox_inches='tight',
            facecolor=fig.get_facecolor())
print('Saved ts808_response.png')
plt.show()


# ---------------------------------------------------------------------------
# Print key frequency-response facts to console
# ---------------------------------------------------------------------------
print(f'\nAnalog component values:')
print(f'  Input  HPF: R={R_HP:.0f} Ω, C={C_HP*1e9:.0f} nF  →  fc ≈ {fc_hp_hz:.1f} Hz  (6 dB/oct)')
print(f'  Output LPF: R={R_LP:.0f} Ω, C={C_LP*1e9:.0f} nF  →  fc ≈ {fc_lp_hz:.1f} Hz  (6 dB/oct)')
print(f'  Mid-hump centre (√(f_HP × f_LP)) ≈ {hump_center:.1f} Hz')
print(f'\nTone LP sweep:')
for tone, _, label in TONE_SETTINGS:
    fc_t = 1000.0 * 10.0**tone
    print(f'  {label}   →  tone LP fc = {fc_t:.0f} Hz')
