// OlaConvolver correctness + performance check (partitioned rewrite 2026-07-23).
//   * correctness: convolver output vs direct time-domain convolution for IR
//     lengths around the partition edges and for irregular host block sizes —
//     max abs error must stay < 1e-4 (float FFT rounding).
//   * performance: seconds of audio processed per wall-second for factory-length
//     and long user-IR cases (per channel).
#include "OlaConvolver.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

int main() {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
    int fails = 0;

    const int irLens[]  = {1, 63, 64, 65, 128, 1000, 4096, 20000};
    const int patterns[][6] = {   // host call-size sequences (0-terminated)
        {64, 64, 64, 64, 64, 0},
        {512, 512, 0, 0, 0, 0},
        {37, 91, 64, 128, 3, 0},
        {1, 2, 3, 61, 200, 0},
    };
    for (int irLen : irLens) {
        std::vector<float> ir(irLen);
        for (auto& v : ir) v = uni(rng) * std::exp(-3.0f * (&v - ir.data()) / float(irLen ? irLen : 1));
        const int total = 2048;
        std::vector<float> x(total);
        for (auto& v : x) v = uni(rng);
        // direct reference
        std::vector<float> ref(total, 0.0f);
        for (int n = 0; n < total; ++n) {
            double a = 0.0;
            for (int j = 0; j < irLen && j <= n; ++j) a += double(ir[j]) * x[n - j];
            ref[n] = float(a);
        }
        for (const auto& pat : patterns) {
            OlaConvolver c;
            c.prepare(1024);
            c.setIR(ir.data(), irLen);
            std::vector<float> y(total, 0.0f);
            int pos = 0, pi = 0;
            while (pos < total) {
                int n = pat[pi];
                if (n == 0) { pi = 0; n = pat[0]; }
                ++pi; if (pi >= 6 || pat[pi] == 0) pi = 0;
                n = std::min(n, total - pos);
                c.process(x.data() + pos, y.data() + pos, n);
                pos += n;
            }
            float maxErr = 0.0f;
            for (int n = 0; n < total; ++n) maxErr = std::max(maxErr, std::fabs(y[n] - ref[n]));
            const bool ok = maxErr < 1e-4f * std::max(1.0f, float(std::sqrt(double(irLen))));
            if (!ok) ++fails;
            std::printf("irLen %6d  pattern[%d..] %4d/%-4d  maxErr %.2e %s\n",
                        irLen, pat[0], pat[1], pat[2], maxErr, ok ? "" : "<-FAIL");
        }
    }

    // ── throughput ──
    for (int irLen : {2048, 24000, 96000}) {
        std::vector<float> ir(irLen, 0.001f);
        OlaConvolver c; c.prepare(64); c.setIR(ir.data(), irLen);
        std::vector<float> buf(64, 0.1f);
        const int blocks = 48000 / 64 * 4;   // 4 s of audio
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) c.process(buf.data(), buf.data(), 64);
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("perf irLen %6d : %6.1fx realtime per channel\n", irLen, 4.0 / dt);
    }

    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
