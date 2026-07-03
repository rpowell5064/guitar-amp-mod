// Quick offline check for the built-in Hex Forge cabs: prints each cab's L2 energy
// (should be EQUAL across all cabs = loudness-matched), peak (clipping sanity), and
// a coarse magnitude response in dB relative to 1 kHz (voicing sanity).
#include "CabModels.h"
#include <cstdio>
#include <cmath>
#include <vector>

static double mag(const std::vector<float>& ir, double f, double sr) {
    double re = 0, im = 0, w = 2.0 * M_PI * f / sr;
    for (size_t n = 0; n < ir.size(); ++n) { re += ir[n] * std::cos(w * n); im -= ir[n] * std::sin(w * n); }
    return std::sqrt(re * re + im * im);
}

int main() {
    const double sr = 48000.0;
    const char* ids[] = {"@factory", "@vox2x12", "@american-ob", "@greenback", "@hiwatt", "@doom"};
    const double freqs[] = {80, 160, 320, 640, 1000, 2000, 4000, 6000, 8000};
    printf("cab            L2      pk   | dB re 1kHz @ 80/160/320/640/1k/2k/4k/6k/8k\n");
    for (auto id : ids) {
        auto ir = CabModels::generate(id, sr);
        double l2 = 0, pk = 0;
        for (float v : ir) { l2 += double(v) * v; pk = std::max(pk, (double)std::fabs(v)); }
        l2 = std::sqrt(l2);
        double ref = mag(ir, 1000, sr);
        printf("%-13s %.4f  %.3f | ", id, l2, pk);
        for (double f : freqs) printf("%+5.1f ", 20.0 * std::log10(mag(ir, f, sr) / ref));
        printf("\n");
    }
    return 0;
}
