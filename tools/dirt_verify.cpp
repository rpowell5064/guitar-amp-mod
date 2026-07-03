// Harmonic analysis of the softened dirt models: feed a 1 kHz guitar-level sine and
// print h2..h9 (% of fundamental) + a THD-ish figure. A SOFT clip rolls the high
// harmonics off quickly (h3>h5>h7>h9); a hard/buzzy clip keeps h5..h9 strong.
#include "DS1Distortion.h"
#include "Octavia.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

static double mag(const std::vector<float>& y, size_t start, double f, double sr) {
    double re = 0, im = 0, w = 2.0 * M_PI * f / sr;
    for (size_t n = start; n < y.size(); ++n) { re += y[n] * std::cos(w * n); im -= y[n] * std::sin(w * n); }
    return std::sqrt(re * re + im * im);
}

template <class M>
static void test(const char* name, M& m, double sr, double drive, double tone, double amp) {
    m.prepare(sr, 512);
    m.setParameter("drive", drive); m.setParameter("tone", tone); m.setParameter("level", 0.7f);
    const int N = (int)(sr * 0.25);
    const size_t warm = (size_t)(sr * 0.05);           // let the param smoothers settle
    std::vector<float> y((size_t)N);
    const double f0 = 1000.0;
    for (int i = 0; i < N; ++i) {
        m.advanceSmoothing();
        float x = (float)(amp * std::sin(2.0 * M_PI * f0 * i / sr));
        y[(size_t)i] = m.processSample(x, 0);
    }
    const double h1 = mag(y, warm, f0, sr);
    double sumsq = 0;
    printf("%-9s drive=%.2f | ", name, drive);
    for (int k = 2; k <= 9; ++k) { double hk = (h1 > 1e-9) ? mag(y, warm, k * f0, sr) / h1 : 0; sumsq += hk * hk; printf("h%d=%4.1f%% ", k, 100 * hk); }
    printf("| THD=%.1f%%\n", 100 * std::sqrt(sumsq));
}

int main() {
    const double sr = 192000.0;   // 4x-oversampled rate the models run at in the plugin
    DS1Distortion ds; test("DS-1", ds, sr, 0.55, 0.35, 0.15);
    Octavia oc;       test("Octavia", oc, sr, 0.40, 0.50, 0.15);
    return 0;
}
