#!/usr/bin/env python3
# Generate a guitar-like DI test signal: a sustained E power chord (E2+B2+E3),
# sawtooth partials (harmonically rich, like a pickup), gentle 5 Hz tremolo so the
# distortion stays lively, 3 ms fades. Raw float32 mono @ 48 kHz. Peak ~ -12 dBFS.
import struct, math, os
SR=48000; DUR=4.0; PEAK=0.24
roots=[82.41,123.47,164.81]          # E2, B2, E3
N=int(SR*DUR)
buf=[0.0]*N
for f in roots:
    nh=int(min(14, (SR*0.45)/f))     # partials up to ~Nyquist-ish
    for n in range(1,nh+1):
        a=1.0/n
        w=2*math.pi*f*n/SR
        for i in range(N):
            buf[i]+=a*math.sin(w*i)
# normalize, tremolo, fades
m=max(abs(x) for x in buf) or 1.0
g=PEAK/m
fade=int(0.003*SR)
for i in range(N):
    trem=0.85+0.15*math.sin(2*math.pi*5.0*i/SR)
    env=1.0
    if i<fade: env=i/fade
    elif i>N-fade: env=(N-i)/fade
    buf[i]*=g*trem*env
out=os.path.join(os.path.dirname(os.path.abspath(__file__)),"di.f32")
with open(out,"wb") as fh:
    fh.write(struct.pack("<%df"%N, *buf))
print("wrote", out, N, "samples", "peak", round(max(abs(x) for x in buf),4))
