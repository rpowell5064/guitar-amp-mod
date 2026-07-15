// Fuzz Zachary (ZVex FF) diagnostic probe — reproduces the 3 user complaints offline:
//   1. "oscillates too easily"  -> steady-tone Stab sweep: inharmonic (non-f0-harmonic) energy ratio
//   2. "won't scream enough"    -> same sweep at Stab=1: squeal strength vs note
//   3. "cuts out / sputters"    -> decaying-pluck grid over Gate x Stab: cut-out time + sputter flaps
// Build on the Pi:
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include build-tools/ff_diag.cpp \
//       build/deps/guitar-amp-simulator/libGuitarAmpSim.a -o /tmp/ffdiag
// Run: ffdiag osc   -> Stab sweep (gate 0.5, comp 0.5, drive 0.85)
//      ffdiag decay -> Gate x Stab pluck grid
//      ffdiag drive -> Drive sweep THD at S0 (base saturation)
#include "OversamplingWrapper.h"
#include "ZVexFuzzFactory.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <complex>

static const double kFs = 48000.0;

static std::unique_ptr<OversamplingWrapper> makeFF(float drive, float comp, float gate, float stab) {
    auto w = std::make_unique<OversamplingWrapper>(std::make_unique<ZVexFuzzFactory>(), 4);
    w->prepare(kFs, 512, 1);
    w->setParameter("sustain",   drive);
    w->setParameter("bias",      comp);
    w->setParameter("inputtrim", gate);
    w->setParameter("getemp",    stab);
    w->setParameter("level",     0.5f);
    return w;
}

// Goertzel magnitude at frequency f over x[n0..n0+len)
static double goertzel(const std::vector<float>& x, int n0, int len, double f) {
    const double w = 2.0 * M_PI * f / kFs, c = 2.0 * std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < len; ++i) { s0 = x[n0+i] + c*s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(std::max(0.0, s1*s1 + s2*s2 - c*s1*s2)) * 2.0 / len;
}

static double rmsOf(const std::vector<float>& x, int n0, int len) {
    double a = 0; for (int i = 0; i < len; ++i) a += (double)x[n0+i]*x[n0+i];
    return std::sqrt(a / len);
}
static double dB(double v) { return v > 1e-9 ? 20.0*std::log10(v) : -120.0; }

// ── 1+2: steady tone, sweep Stab. Measure harmonic vs total energy in the last stretch,
//         plus post-note tail (does the squeal die on silence?).
static void oscSweep(float drive, float comp, float gate) {
    printf("# OSC sweep: drive=%.2f comp=%.2f gate=%.2f | f0=165Hz @-20dBFS steady 1.5s + 0.5s silence\n",
           drive, comp, gate);
    printf("# stab | outRMS(dB) harmRMS(dB) inharmRMS(dB) inharm%%  tail@+0.2s(dB) tail@+0.45s(dB)\n");
    const double f0 = 165.0;
    const int Nnote = (int)(1.5*kFs), Nsil = (int)(0.5*kFs), N = Nnote + Nsil;
    for (int s = 0; s <= 10; ++s) {
        float stab = s / 10.0f;
        auto w = makeFF(drive, comp, gate, stab);
        std::vector<float> out(N);
        for (int i = 0; i < N; ++i) {
            float x = (i < Nnote) ? (float)(0.1 * std::sin(2.0*M_PI*f0*i/kFs)) : 0.0f;
            float* ins[1] = { &x }; float y = 0; float* outs[1] = { &y };
            w->process(ins, outs, 1, 1);
            out[i] = y;
        }
        // analyse the last 0.5 s of the note (steady state)
        const int a0 = Nnote - (int)(0.5*kFs), alen = (int)(0.5*kFs);
        double tot = rmsOf(out, a0, alen);
        double harm2 = 0;
        for (int h = 1; h <= 40; ++h) {
            double f = f0*h; if (f > 20000.0) break;
            double m = goertzel(out, a0, alen, f) / std::sqrt(2.0);  // peak->rms
            harm2 += m*m;
        }
        double harm = std::sqrt(harm2);
        double inh  = std::sqrt(std::max(0.0, tot*tot - harm2));
        double t1 = rmsOf(out, Nnote + (int)(0.2*kFs),  (int)(0.05*kFs));
        double t2 = rmsOf(out, Nnote + (int)(0.45*kFs), (int)(0.05*kFs));
        printf("%.1f  %7.1f  %7.1f  %7.1f  %5.1f%%  %7.1f  %7.1f\n",
               stab, dB(tot), dB(harm), dB(inh), tot > 1e-9 ? 100.0*inh/tot : 0.0, dB(t1), dB(t2));
    }
}

// ── 3: decaying pluck across Gate x Stab. Report when the output collapses (>35 dB below its
//       own peak) vs the input level at that moment, plus a sputter count (output flapping
//       up/down >12 dB between adjacent 20 ms windows during the decay).
static void decayGrid(float drive, float comp, float pk0, double noteDur) {
    printf("# DECAY grid: drive=%.2f comp=%.2f | pluck 165Hz %.2fpk tau=1.2s, %.1fs + 1s silence\n",
           drive, comp, pk0, noteDur);
    printf("# gate stab | peakOut(dB) cutT(s) in@cut(dB) sputters  tailSil(dB)\n");
    const double f0 = 165.0, tau = 1.2;
    const int N = (int)((noteDur + 1.0)*kFs), win = (int)(0.02*kFs);
    for (float gate : {0.2f, 0.42f, 0.6f, 0.8f}) {
        for (float stab : {0.0f, 0.3f, 0.5f, 0.72f, 1.0f}) {
            auto w = makeFF(drive, comp, gate, stab);
            std::vector<double> inW, outW;   // 20 ms RMS ladders
            double accI = 0, accO = 0; int wc = 0;
            for (int i = 0; i < N; ++i) {
                double t = i / kFs;
                double amp = (t < noteDur) ? std::exp(-t / tau) : 0.0;
                float x = (float)(pk0 * amp * std::sin(2.0*M_PI*f0*t));
                float* ins[1] = { &x }; float y = 0; float* outs[1] = { &y };
                w->process(ins, outs, 1, 1);
                accI += (double)x*x; accO += (double)y*y;
                if (++wc >= win) {
                    inW.push_back(std::sqrt(accI/wc)); outW.push_back(std::sqrt(accO/wc));
                    accI = accO = 0; wc = 0;
                }
            }
            double pk = 0; for (double v : outW) pk = std::max(pk, v);
            // cut-out: first window (during the note) where out < pk-35dB while in > -45 dB
            double cutT = -1, inAtCut = 0;
            int nNote = (int)(noteDur / 0.02);
            for (int k = 2; k < nNote && k < (int)outW.size(); ++k) {
                if (dB(outW[k]) < dB(pk) - 35.0 && dB(inW[k]) > -45.0) { cutT = k*0.02; inAtCut = dB(inW[k]); break; }
            }
            int sput = 0;
            for (int k = 3; k < nNote && k < (int)outW.size(); ++k) {
                double d = dB(outW[k]) - dB(outW[k-1]);
                if (std::fabs(d) > 12.0 && dB(inW[k]) > -50.0) ++sput;
            }
            int silStart = (int)((noteDur + 0.4) / 0.02);
            double ts = (silStart < (int)outW.size()) ? outW[silStart] : 0.0;
            printf("%.2f  %.2f  %7.1f  %s  %s  %3d  %7.1f\n",
                   gate, stab, dB(pk),
                   cutT < 0 ? "  none " : (std::string("  ")+std::to_string(cutT).substr(0,4)).c_str(),
                   cutT < 0 ? "   -  " : (std::string(" ")+std::to_string(inAtCut).substr(0,5)).c_str(),
                   sput, dB(ts));
        }
    }
}

// ── chord: two-tone (165+220 Hz) steady, sweep Stab. Any energy NOT on the f1/f2 harmonic/IMD
//    grid = the feedback resonance beating against the notes (the "unruly warble"). Report the
//    residual after removing all n*f1+m*f2 products (|n|,|m|<=8) — pure nonlinearity leaves ~0;
//    a non-locked feedback resonance leaves audible residual.
static void chordSweep(float drive, float comp, float gate) {
    printf("# CHORD sweep: drive=%.2f comp=%.2f gate=%.2f | 165+220Hz @-20dBFS each\n", drive, comp, gate);
    printf("# stab | outRMS(dB) gridRMS(dB) residRMS(dB) resid%%\n");
    const double f1 = 165.0, f2 = 220.0;
    const int Nnote = (int)(2.0*kFs);
    for (int s = 0; s <= 10; ++s) {
        float stab = s / 10.0f;
        auto w = makeFF(drive, comp, gate, stab);
        std::vector<float> out(Nnote);
        for (int i = 0; i < Nnote; ++i) {
            double t = i / kFs;
            float x = (float)(0.07*std::sin(2.0*M_PI*f1*t) + 0.07*std::sin(2.0*M_PI*f2*t));
            float* ins[1] = { &x }; float y = 0; float* outs[1] = { &y };
            w->process(ins, outs, 1, 1);
            out[i] = y;
        }
        const int a0 = Nnote - (int)(1.0*kFs), alen = (int)(1.0*kFs);
        double tot = rmsOf(out, a0, alen);
        std::vector<double> freqs;
        for (int n = -8; n <= 8; ++n) for (int m = -8; m <= 8; ++m) {
            double f = n*f1 + m*f2;
            if (f < 25.0 || f > 20000.0) continue;
            bool dup = false; for (double g : freqs) if (std::fabs(g - f) < 2.0) { dup = true; break; }
            if (!dup) freqs.push_back(f);
        }
        double grid2 = 0;
        for (double f : freqs) { double m = goertzel(out, a0, alen, f)/std::sqrt(2.0); grid2 += m*m; }
        double grid = std::sqrt(std::min(grid2, tot*tot));
        double res  = std::sqrt(std::max(0.0, tot*tot - grid2));
        printf("%.1f  %7.1f  %7.1f  %7.1f  %5.1f%%\n", stab, dB(tot), dB(grid), dB(res),
               tot > 1e-9 ? 100.0*res/tot : 0.0);
    }
}

// ── squeal: burst then silence, grid over Gate x Stab. Does the squeal RUN AWAY into silence
//    (authentic, Gate-open + Stab cranked) and does the Gate knob kill it? Reports the silent-tail
//    RMS at +0.5/+1.5/+2.4 s and the dominant squeal frequency.
static void squealGrid(float drive, float comp) {
    printf("# SQUEAL grid: drive=%.2f comp=%.2f | 0.4s burst @-15dBFS then 2.5s silence\n", drive, comp);
    printf("# gate stab | tail+0.5s(dB) +1.5s(dB) +2.4s(dB)  domFreq(Hz)\n");
    const double f0 = 165.0, burst = 0.4, sil = 2.5;
    const int N = (int)((burst + sil)*kFs);
    for (float gate : {0.2f, 0.42f, 0.6f, 0.8f, 1.0f}) {
        for (float stab : {0.0f, 0.4f, 0.55f, 0.7f, 0.85f, 1.0f}) {
            auto w = makeFF(drive, comp, gate, stab);
            std::vector<float> out(N);
            for (int i = 0; i < N; ++i) {
                double t = i / kFs;
                float x = (t < burst) ? (float)(0.18 * std::sin(2.0*M_PI*f0*t)) : 0.0f;
                float* ins[1] = { &x }; float y = 0; float* outs[1] = { &y };
                w->process(ins, outs, 1, 1);
                out[i] = y;
            }
            auto tail = [&](double dt){ return rmsOf(out, (int)((burst+dt)*kFs), (int)(0.08*kFs)); };
            // dominant frequency in the last 0.5 s (coarse Goertzel scan 60..4000 Hz)
            double bf = 0, bm = 0;
            const int a0 = N - (int)(0.5*kFs), alen = (int)(0.5*kFs);
            for (double f = 60; f <= 4000; f *= 1.06) {
                double m = goertzel(out, a0, alen, f);
                if (m > bm) { bm = m; bf = f; }
            }
            bool alive = dB(tail(2.4)) > -60.0;
            printf("%.2f  %.2f  %7.1f  %7.1f  %7.1f   %s\n", gate, stab,
                   dB(tail(0.5)), dB(tail(1.5)), dB(tail(2.4)),
                   alive ? (std::to_string((int)bf) + " Hz SQUEALING").c_str() : "-");
        }
    }
}

// ── level: THD vs input level (the real FF RISES 113->123->147% over -24->-6 dBFS; the old model
//    was backwards). drive fixed 0.5 (capture D-5), comp arg (capture C-8 = 0.8).
static void levelSweep(float comp, float gate) {
    printf("# LEVEL sweep: drive=0.5 comp=%.2f gate=%.2f stab=0 | 165Hz\n", comp, gate);
    printf("# in(dBFS) | outRMS(dB)  THD%%\n");
    const double f0 = 165.0;
    const int Nn = (int)(1.5*kFs);
    for (double lvl : {-24.0, -18.0, -12.0, -6.0}) {
        auto w = makeFF(0.5f, comp, gate, 0.0f);
        const double a = std::pow(10.0, lvl/20.0);
        std::vector<float> out(Nn);
        for (int i = 0; i < Nn; ++i) {
            float x = (float)(a * std::sin(2.0*M_PI*f0*i/kFs));
            float* ins[1] = { &x }; float y = 0; float* outs[1] = { &y };
            w->process(ins, outs, 1, 1);
            out[i] = y;
        }
        const int a0 = Nn - (int)(0.8*kFs), alen = (int)(0.8*kFs);
        double h1 = goertzel(out, a0, alen, f0)/std::sqrt(2.0), hs = 0;
        for (int h = 2; h <= 40; ++h) {
            double f = f0*h; if (f > 20000.0) break;
            double m = goertzel(out, a0, alen, f)/std::sqrt(2.0); hs += m*m;
        }
        printf("%6.0f  %7.1f  %6.1f%%\n", lvl, dB(rmsOf(out, a0, alen)),
               h1 > 1e-9 ? 100.0*std::sqrt(hs)/h1 : 0.0);
    }
}

// ── drive: Drive sweep at S0, steady tone: THD-ish (inharm+harm>=2 vs h1) + RMS
static void driveSweep(float comp, float gate) {
    printf("# DRIVE sweep: comp=%.2f gate=%.2f stab=0 | 165Hz @-20dBFS\n", comp, gate);
    printf("# drive | outRMS(dB)  h1(dB)  THD%%(h2..h40/h1)\n");
    const double f0 = 165.0;
    const int Nnote = (int)(1.2*kFs);
    for (int d = 2; d <= 10; d += 2) {
        float drive = d / 10.0f;
        auto w = makeFF(drive, comp, gate, 0.0f);
        std::vector<float> out(Nnote);
        for (int i = 0; i < Nnote; ++i) {
            float x = (float)(0.1 * std::sin(2.0*M_PI*f0*i/kFs));
            float* ins[1] = { &x }; float y = 0; float* outs[1] = { &y };
            w->process(ins, outs, 1, 1);
            out[i] = y;
        }
        const int a0 = Nnote - (int)(0.5*kFs), alen = (int)(0.5*kFs);
        double h1 = goertzel(out, a0, alen, f0) / std::sqrt(2.0);
        double hs = 0;
        for (int h = 2; h <= 40; ++h) {
            double f = f0*h; if (f > 20000.0) break;
            double m = goertzel(out, a0, alen, f) / std::sqrt(2.0);
            hs += m*m;
        }
        printf("%.1f  %7.1f  %7.1f  %6.1f%%\n", drive, dB(rmsOf(out, a0, alen)), dB(h1),
               h1 > 1e-9 ? 100.0*std::sqrt(hs)/h1 : 0.0);
    }
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "osc";
    if      (!std::strcmp(mode, "osc"))   oscSweep(argc>2?std::stof(argv[2]):0.85f,
                                                   argc>3?std::stof(argv[3]):0.5f,
                                                   argc>4?std::stof(argv[4]):0.5f);
    else if (!std::strcmp(mode, "decay")) decayGrid(argc>2?std::stof(argv[2]):0.85f,
                                                    argc>3?std::stof(argv[3]):0.5f,
                                                    argc>4?std::stof(argv[4]):0.35f,
                                                    argc>5?std::stod(argv[5]):2.5);
    else if (!std::strcmp(mode, "squeal")) squealGrid(argc>2?std::stof(argv[2]):0.85f,
                                                      argc>3?std::stof(argv[3]):0.5f);
    else if (!std::strcmp(mode, "level"))  levelSweep(argc>2?std::stof(argv[2]):0.8f,
                                                      argc>3?std::stof(argv[3]):0.6f);
    else if (!std::strcmp(mode, "chord")) chordSweep(argc>2?std::stof(argv[2]):0.85f,
                                                     argc>3?std::stof(argv[3]):0.5f,
                                                     argc>4?std::stof(argv[4]):0.5f);
    else if (!std::strcmp(mode, "drive")) driveSweep(argc>2?std::stof(argv[2]):0.5f,
                                                     argc>3?std::stof(argv[3]):0.5f);
    else printf("modes: osc [drive comp gate] | decay [drive comp] | drive [comp gate]\n");
    return 0;
}
