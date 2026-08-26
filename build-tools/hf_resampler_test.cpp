// Verification for desktop/src/HfResampler.h (M3): rate pairs, signal
// integrity, imaging/aliasing floor, and produced-count bookkeeping.
//
//  1. 997 Hz sine through host→48k→host round trips (44.1/88.2/96/192):
//     residual vs an ideally delayed copy of the input → SNR (expect > 80 dB).
//  2. 15 kHz sine 44.1→48: energy off the 15 kHz bin (images/aliases) < −70 dB.
//  3. Streaming equivalence: chopping the input into ragged block sizes
//     produces the identical output stream as one big call.
#include "../desktop/src/HfResampler.h"
#include <cstdio>
#include <cmath>
#include <vector>

static double sineSnrDb(double hostRate) {
    const double f = 997.0;
    const int n = (int) hostRate;            // 1 s
    const int cap48 = (int) (n * 48000.0 / hostRate) + 64;
    std::vector<float> in((size_t) n);
    std::vector<float> midL((size_t) cap48), midR((size_t) cap48);
    std::vector<float> outL((size_t) n + 64), outR((size_t) n + 64);
    for (int i = 0; i < n; ++i) in[(size_t) i] = (float) std::sin(2.0 * 3.141592653589793 * f * i / hostRate);

    HfPolyResampler up, dn;
    up.prepare(hostRate, 48000.0, n);
    dn.prepare(48000.0, hostRate, cap48);
    const int m = up.process(in.data(), in.data(), n, midL.data(), midR.data());
    const int o = dn.process(midL.data(), midR.data(), m, outL.data(), outR.data());

    // Total group delay in host samples: up-stage at host rate + down-stage
    // (input rate 48k) converted to host samples.
    const double delay = up.groupDelayInputSamples()
                       + dn.groupDelayInputSamples() * hostRate / 48000.0;
    double sig = 0.0, err = 0.0;
    const int a = (int) (delay + 512), b = o - 512;   // skip edges
    for (int i = a; i < b; ++i) {
        const double ref = std::sin(2.0 * 3.141592653589793 * f * (i - delay) / hostRate);
        sig += ref * ref;
        const double e = outL[(size_t) i] - ref;
        err += e * e;
    }
    return 10.0 * std::log10(sig / (err + 1e-30));
}

static double imageRejectionDb() {
    const double f = 15000.0, hostRate = 44100.0;
    const int n = 44100;
    const int cap = (int) (n * 48000.0 / hostRate) + 64;
    std::vector<float> in((size_t) n), outL((size_t) cap), outR((size_t) cap);
    for (int i = 0; i < n; ++i) in[(size_t) i] = (float) std::sin(2.0 * 3.141592653589793 * f * i / hostRate);
    HfPolyResampler up;
    up.prepare(hostRate, 48000.0, n);
    const int m = up.process(in.data(), in.data(), n, outL.data(), outR.data());
    // Goertzel power at 15 kHz vs total power (minus the tone) over the middle.
    const int a = 2048, len = m - 4096;
    const double w = 2.0 * 3.141592653589793 * f / 48000.0;
    double c = 2.0 * std::cos(w), s0 = 0, s1 = 0, total = 0;
    for (int i = 0; i < len; ++i) {
        const double x = outL[(size_t) (a + i)];
        const double s = x + c * s1 - s0;
        s0 = s1; s1 = s;
        total += x * x;
    }
    const double tonePow = (s1 * s1 + s0 * s0 - c * s0 * s1) / ((double) len * len / 4.0);
    const double meanPow = total / len;                       // ≈ tone/2 + junk
    const double junk = meanPow - tonePow / 2.0;
    return 10.0 * std::log10((tonePow / 2.0) / (std::abs(junk) + 1e-30));
}

static bool streamingEquivalent() {
    const int n = 48000;
    const int cap = (int) (n * 160.0 / 147.0) + 64;
    std::vector<float> in((size_t) n);
    unsigned lcg = 1234567u;
    for (int i = 0; i < n; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        in[(size_t) i] = (float) ((double) (lcg >> 8) / 8388608.0 - 1.0) * 0.5f;
    }
    std::vector<float> aL((size_t) cap), aR((size_t) cap), bL((size_t) cap), bR((size_t) cap);
    HfPolyResampler r1, r2;
    r1.prepare(44100.0, 48000.0, n);
    r2.prepare(44100.0, 48000.0, n);
    const int ma = r1.process(in.data(), in.data(), n, aL.data(), aR.data());
    int mb = 0;
    static const int chops[] = { 1, 7, 480, 3, 137, 512, 64, 1000 };
    int pos = 0, ci = 0;
    while (pos < n) {
        int c = chops[ci++ % 8];
        if (c > n - pos) c = n - pos;
        mb += r2.process(in.data() + pos, in.data() + pos, c, bL.data() + mb, bR.data() + mb);
        pos += c;
    }
    if (ma != mb) { printf("  streaming: count mismatch %d vs %d\n", ma, mb); return false; }
    for (int i = 0; i < ma; ++i)
        if (aL[(size_t) i] != bL[(size_t) i]) { printf("  streaming: sample %d differs\n", i); return false; }
    return true;
}

int main() {
    bool ok = true;
    for (double r : { 44100.0, 88200.0, 96000.0, 192000.0 }) {
        const double snr = sineSnrDb(r);
        printf("round-trip %6.0f Hz: SNR %.1f dB %s\n", r, snr, snr > 80.0 ? "OK" : "FAIL");
        ok = ok && snr > 80.0;
    }
    const double rej = imageRejectionDb();
    printf("15 kHz 44.1->48 image rejection: %.1f dB %s\n", rej, rej > 70.0 ? "OK" : "FAIL");
    ok = ok && rej > 70.0;
    const bool se = streamingEquivalent();
    printf("ragged-block streaming equivalence: %s\n", se ? "OK" : "FAIL");
    ok = ok && se;
    printf(ok ? "ALL OK\n" : "FAILURES\n");
    return ok ? 0 : 1;
}
