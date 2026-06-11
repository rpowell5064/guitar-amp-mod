// Offline verification: generate the synthetic default cab IR and print its
// magnitude response (DFT) at log-spaced frequencies, so we can confirm the
// voicing matches the intended V30+SM57 target before shipping.
//
//   g++ -O2 -I deps/guitar-amp-simulator/include tools/cab_ir_response.cpp -o /tmp/cabresp
//   /tmp/cabresp
#include "DefaultCabIR.h"
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    const double sr = 48000.0;
    std::vector<float> ir = DefaultCabIR::generate(sr);

    // Log-spaced probe frequencies spanning the guitar-cab band.
    const double freqs[] = {50, 80, 100, 150, 200, 300, 440, 600, 750, 1000,
                            1500, 1900, 2500, 3000, 4000, 5000, 6000, 8000, 10000, 12000};
    const int nf = sizeof(freqs) / sizeof(freqs[0]);

    // DFT magnitude at each probe frequency.
    std::vector<double> mag(nf, 0.0);
    double peak = 0.0;
    for (int k = 0; k < nf; ++k) {
        const double w = 2.0 * M_PI * freqs[k] / sr;
        double re = 0.0, im = 0.0;
        for (size_t n = 0; n < ir.size(); ++n) {
            re += ir[n] * std::cos(w * n);
            im -= ir[n] * std::sin(w * n);
        }
        mag[k] = std::sqrt(re * re + im * im);
        if (mag[k] > peak) peak = mag[k];
    }

    std::printf("IR length: %zu samples (%.1f ms @ %.0f Hz)\n\n",
                ir.size(), 1000.0 * ir.size() / sr, sr);
    std::printf("  Freq(Hz)   dB(rel peak)   bar\n");
    for (int k = 0; k < nf; ++k) {
        const double db = 20.0 * std::log10(mag[k] / peak + 1e-12);
        // bar: one '#' per dB above -40
        int bars = (int)(db + 40.0); if (bars < 0) bars = 0;
        char bar[64]; int b = bars < 60 ? bars : 60;
        for (int i = 0; i < b; ++i) bar[i] = '#';
        bar[b] = '\0';
        std::printf("  %7.0f   %8.1f      %s\n", freqs[k], db, bar);
    }
    return 0;
}
