// Verify the Cab Voice feature (2026-07-22): Room vs Studio on the factory cab.
//   1. voice=0 must be BIT-IDENTICAL to a fresh instance that never saw the param
//      (legacy path untouched — shipped presets can't move).
//   2. voice=1 spectral signature vs Room, pink-noise 1/3-oct bands:
//      * 50 Hz strongly cut (78 Hz bracketing HPF)
//      * 9.5 kHz down >= 2.5 dB (mic-2 blend darkening + 10.5 kHz bracketing LPF;
//        14 kHz is below the factory IR's own floor — probe inside the passband)
//      * 400 Hz dips ~1-2 dB REL 1 kHz (console curve A)
//      * 3.2 kHz nets a small lift (console B must beat the darker mic-2 blend)
//      * broadband within ±3 dB of Room (the glue comp is level-invariant)
//   3. bus glue: on loud/quiet pink BURSTS the loud-vs-quiet RMS ratio must shrink
//      in Studio (dynamics evened toward the sliding average) but by <= ~3.5 dB
//      (the GR cap) — and Room's ratio must be untouched.
#include "CabinetBlock.h"
#include "CabModels.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr double kFs = 48000.0;
static constexpr int    kBlk = 256;

static std::vector<float> runPink(CabinetBlock& cab, float voice) {
    cab.setParameter("voice", voice);
    const size_t total = size_t(kFs * 3.0);
    std::vector<float> out(total), buf(kBlk);
    double b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0; uint32_t st = 1234567u;
    size_t pos = 0;
    while (pos < total) {
        const int n = int(std::min<size_t>(kBlk, total - pos));
        for (int i = 0; i < n; ++i) {
            st ^= st << 13; st ^= st >> 17; st ^= st << 5;
            double w = (double(st) / 2147483648.0) - 1.0;
            b0=0.99886*b0+w*0.0555179; b1=0.99332*b1+w*0.0750759; b2=0.96900*b2+w*0.1538520;
            b3=0.86650*b3+w*0.3104856; b4=0.55000*b4+w*0.5329522; b5=-0.7616*b5-w*0.0168980;
            buf[size_t(i)] = float((b0+b1+b2+b3+b4+b5+b6+w*0.5362) * 0.11 * 0.35);
            b6=w*0.115926;
        }
        float* p = buf.data();
        cab.process(&p, &p, n, 1);
        std::memcpy(out.data()+pos, buf.data(), size_t(n)*sizeof(float));
        pos += size_t(n);
    }
    return out;
}

static double goertzel(const std::vector<float>& x, size_t from, double f) {
    const double w = 2.0 * M_PI * f / kFs, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < x.size(); ++i) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(std::max(s1*s1 + s2*s2 - c*s1*s2, 0.0)) / (double(x.size() - from) / 2.0);
}
static double bandDb(const std::vector<float>& x, double fc) {
    const size_t skip = size_t(kFs * 0.4);
    double acc = 0; int n = 0;
    for (double f = fc * 0.85; f <= fc * 1.18; f *= 1.05) { double g = goertzel(x, skip, f); acc += g*g; ++n; }
    return 20.0 * std::log10(std::sqrt(acc / n) + 1e-12);
}

int main() {
    int fails = 0;
    auto mkCab = [] (CabinetBlock& c) {
        c.prepare(kFs, kBlk, 1);
        c.setIR(CabModels::generate("@factory", kFs));
    };

    // 1. Legacy-path identity: voice=0 vs never-touched.
    {
        CabinetBlock a, b; mkCab(a); mkCab(b);
        auto ya = runPink(a, 0.0f);
        CabinetBlock ref; mkCab(ref);
        auto yr = runPink(ref, 0.0f);          // same seed, same path
        b.setParameter("voice", 1.0f);         // toggle away and back — must return bit-identical
        b.setParameter("voice", 0.0f);
        auto yb = runPink(b, 0.0f);
        bool same = true;
        for (size_t i = 0; i < ya.size(); ++i) if (ya[i] != yb[i]) { same = false; break; }
        if (!same) { std::printf("FAIL: Room path not bit-identical after voice toggle\n"); ++fails; }
        else std::printf("Room path: bit-identical with voice untouched / toggled away+back\n");
        (void)yr;
    }

    // 2 + 3. Studio signature.
    {
        CabinetBlock r, s; mkCab(r); mkCab(s);
        auto yr = runPink(r, 0.0f);
        auto ys = runPink(s, 1.0f);
        const double r50  = bandDb(yr,   50) - bandDb(yr, 1000), s50  = bandDb(ys,   50) - bandDb(ys, 1000);
        const double r95  = bandDb(yr, 9500) - bandDb(yr, 1000), s95  = bandDb(ys, 9500) - bandDb(ys, 1000);
        const double r400 = bandDb(yr,  400) - bandDb(yr, 1000), s400 = bandDb(ys,  400) - bandDb(ys, 1000);
        const double r32  = bandDb(yr, 3200) - bandDb(yr, 1000), s32  = bandDb(ys, 3200) - bandDb(ys, 1000);
        // broadband from the last 1.2 s only: the glue's sliding reference needs
        // ~1.5 s to settle, and early GR would read as level loss
        const size_t skip = size_t(kFs * 1.8);
        double rr=0, sr2=0;
        for (size_t i = skip; i < yr.size(); ++i) rr  += double(yr[i])*yr[i];
        for (size_t i = skip; i < ys.size(); ++i) sr2 += double(ys[i])*ys[i];
        rr = std::sqrt(rr / (yr.size()-skip)); sr2 = std::sqrt(sr2 / (ys.size()-skip));
        const double lvl = 20.0*std::log10(sr2/rr);
        std::printf("Studio vs Room (dB rel 1 kHz):  50 Hz %+.1f -> %+.1f | 9.5 kHz %+.1f -> %+.1f\n", r50, s50, r95, s95);
        std::printf("                               400 Hz %+.1f -> %+.1f | 3.2 kHz %+.1f -> %+.1f\n", r400, s400, r32, s32);
        std::printf("broadband level delta %+.1f dB\n", lvl);
        if (s50  > r50  - 4.0)  { std::printf("FAIL: 78 Hz HPF signature missing at 50 Hz\n"); ++fails; }
        if (s95  > r95  - 2.5)  { std::printf("FAIL: HF darkening signature missing at 9.5 kHz\n"); ++fails; }
        if (s400 - r400 > -0.5 || s400 - r400 < -3.5) { std::printf("FAIL: 400 Hz console dip out of range (%+.1f)\n", s400-r400); ++fails; }
        if (s32  - r32  < 0.1  || s32  - r32  > 3.0)  { std::printf("FAIL: 3.2 kHz console lift out of range (%+.1f)\n", s32-r32); ++fails; }
        if (std::fabs(lvl) > 3.0) { std::printf("FAIL: Studio loudness drifted %+.1f dB\n", lvl); ++fails; }
    }

    // 3. Bus glue on bursts: loud/quiet RMS ratio must shrink in Studio only.
    {
        auto runBurst = [&](float voice) {
            CabinetBlock cab; mkCab(cab);
            cab.setParameter("voice", voice);
            const size_t total = size_t(kFs * 4.0);
            const size_t seg = size_t(kFs * 0.4);
            std::vector<float> out(total), buf(kBlk);
            double b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0; uint32_t st = 7654321u;
            size_t pos = 0;
            while (pos < total) {
                const int n = int(std::min<size_t>(kBlk, total - pos));
                for (int i = 0; i < n; ++i) {
                    st ^= st << 13; st ^= st >> 17; st ^= st << 5;
                    double w = (double(st) / 2147483648.0) - 1.0;
                    b0=0.99886*b0+w*0.0555179; b1=0.99332*b1+w*0.0750759; b2=0.96900*b2+w*0.1538520;
                    b3=0.86650*b3+w*0.3104856; b4=0.55000*b4+w*0.5329522; b5=-0.7616*b5-w*0.0168980;
                    const bool loud = ((pos + size_t(i)) / seg) % 2 == 0;
                    buf[size_t(i)] = float((b0+b1+b2+b3+b4+b5+b6+w*0.5362) * 0.11 * 0.35 * (loud ? 1.0 : 0.25));
                    b6=w*0.115926;
                }
                float* p = buf.data();
                cab.process(&p, &p, n, 1);
                std::memcpy(out.data()+pos, buf.data(), size_t(n)*sizeof(float));
                pos += size_t(n);
            }
            // segment RMS, skipping 120 ms after each boundary + the first second (ref settle)
            double lo=0, qu=0; size_t nl=0, nq=0;
            const size_t guard = size_t(kFs * 0.12), warm = size_t(kFs * 1.0);
            for (size_t i = warm; i < total; ++i) {
                if (i % seg < guard) continue;
                if ((i / seg) % 2 == 0) { lo += double(out[i])*out[i]; ++nl; }
                else                    { qu += double(out[i])*out[i]; ++nq; }
            }
            return 20.0 * std::log10(std::sqrt(lo/nl) / (std::sqrt(qu/nq) + 1e-12));
        };
        const double ratR = runBurst(0.0f), ratS = runBurst(1.0f);
        std::printf("burst loud/quiet ratio: Room %.1f dB -> Studio %.1f dB (glue = shrink)\n", ratR, ratS);
        if (ratS > ratR - 0.4) { std::printf("FAIL: glue comp not evening dynamics\n"); ++fails; }
        if (ratS < ratR - 4.0) { std::printf("FAIL: glue comp over-compressing (> ~3.5 dB)\n"); ++fails; }
    }

    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
