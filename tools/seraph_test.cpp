// Direct SeraphDelay impulse test: feed a short burst, process 2 s as a stereo pair with a MONO
// input (L==R, as the amp feeds it), and report the mono-sum (0.5*(L+R)) peak per 50 ms window
// plus L/R separately. If the 300 ms repeats show up and decay in the mono-sum column, mono works.
// Run at stereoWidth 0 (what mono-out forces) vs 1 to compare.
#include "SeraphDelay.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

int main(int argc, char** argv) {
    const float width = (argc > 1) ? (float)atof(argv[1]) : 0.0f;
    SeraphDelay d;
    d.prepare(48000.0, 64, 2);
    d.setParameter("timeMs",   300.0f);
    d.setParameter("feedback", 0.6f);
    d.setParameter("mix",      0.6f);
    d.setParameter("ducking",  0.0f);
    // pattern + filters left at DEFAULT (pattern 1 = Dotted8th, lowCut 120, highCut 4000) — real config.
    d.setParameter("stereoWidth", width);
    for (int i = 0; i < 4000; ++i) { d.advanceSmoothing(); d.processSample(0.0f, 0); d.processSample(0.0f, 1); }
    d.reset();

    const int N = 144000, win = 2400;   // 3 s, 50 ms windows
    printf("stereoWidth=%.2f  (realistic 120 ms decaying 150 Hz pluck)\n  time   mono   L      R\n", width);
    double pm = 0, pl = 0, pr = 0; int wc = 0;
    for (int i = 0; i < N; ++i) {
        // A realistic pluck: 150 Hz tone that decays over ~40 ms, gone by 120 ms.
        const float x = (i < 5760) ? std::sin(2.0f*3.14159265f*150.0f*i/48000.0f)
                                     * std::exp(-i/(0.04f*48000.0f)) : 0.0f;
        d.advanceSmoothing();
        const float l = d.processSample(x, 0);
        const float r = d.processSample(x, 1);
        const float mono = 0.5f * (l + r);
        if (std::fabs(mono) > pm) pm = std::fabs(mono);
        if (std::fabs(l)    > pl) pl = std::fabs(l);
        if (std::fabs(r)    > pr) pr = std::fabs(r);
        if (++wc >= win) { printf("%5.0fms %.4f %.4f %.4f\n", (i/48000.0)*1000, pm, pl, pr); pm=pl=pr=0; wc=0; }
    }
    return 0;
}
