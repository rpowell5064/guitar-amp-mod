// Decay-response probe for the Fizz Factory: feed a decaying pluck then silence,
// print the output-envelope over time to SEE the gate chop the sustain + whether
// Stab sustains/whines. Build on the Pi:
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include ff_decay_probe.cpp \
//       build/deps/guitar-amp-simulator/libGuitarAmpSim.a -o /tmp/ffprobe
#include "OversamplingWrapper.h"
#include "ZVexFuzzFactory.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    const double fs = 48000.0;
    // args: gate stab   (defaults)
    float gate = argc > 1 ? std::stof(argv[1]) : 0.1f;
    float stab = argc > 2 ? std::stof(argv[2]) : 0.0f;

    OversamplingWrapper w(std::make_unique<ZVexFuzzFactory>(), 4);
    w.prepare(fs, 512, 1);
    w.setParameter("sustain", 0.8f);   // Drive
    w.setParameter("bias",    0.66f);  // Comp
    w.setParameter("inputtrim", gate); // Gate
    w.setParameter("getemp",  stab);   // Stab
    w.setParameter("level",   0.5f);   // Volume

    const double f0 = 165.0, tau = 1.2;   // pluck: 165 Hz, decay tau 1.2 s
    const double noteDur = 2.5, silence = 1.0, total = noteDur + silence;
    const int N = (int)(total * fs);
    const int win = (int)(0.02 * fs);     // 20 ms RMS windows

    printf("# gate=%.2f stab=%.2f  | t(s)  inRMS(dB)  outRMS(dB)\n", gate, stab);
    double accIn = 0, accOut = 0; int wc = 0; double t0 = 0;
    for (int i = 0; i < N; ++i) {
        double t = i / fs;
        double amp = (t < noteDur) ? std::exp(-t / tau) : 0.0;   // pluck then hard silence
        float x = (float)(0.35 * amp * std::sin(2.0 * M_PI * f0 * t));
        float* ins[1] = { &x }; float y = 0; float* outs[1] = { &y };
        w.process(ins, outs, 1, 1);
        accIn  += (double)x * x;
        accOut += (double)y * y;
        if (++wc >= win) {
            double inR  = std::sqrt(accIn / wc), outR = std::sqrt(accOut / wc);
            double indB = inR  > 1e-9 ? 20*std::log10(inR)  : -120;
            double odB  = outR > 1e-9 ? 20*std::log10(outR) : -120;
            printf("%.2f  %7.1f  %7.1f  %s\n", t0, indB, odB,
                   (t0 >= noteDur ? "<-silence" : ""));
            accIn = accOut = 0; wc = 0; t0 = t;
        }
    }
    return 0;
}
