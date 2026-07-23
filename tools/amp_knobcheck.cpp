// Suite-wide knob-range audit: for every amp model (and its channels/modes), measure
//   * GAIN sweep: 1 kHz THD% at knob 0.05 / 0.3 / 0.5 / 1.0 (-12 dBFS in) — does the
//     knob actually sweep clean->saturated like the real amp?
//   * TONE authority: output level swing (dB) of a band-centered sine when each tone
//     knob goes 0 -> 1 (others at noon, gain 0.2 to stay out of heavy compression):
//     bass @ 90 Hz, mid @ 550 Hz, treble @ 4.2 kHz, presence @ 5 kHz.
// Reference expectations (real TMB stacks): bass/treble ~10-20 dB, mid ~6-14 dB,
// presence ~5-12 dB. A swing < 4 dB = dead knob; THD flat across the gain sweep =
// the railed-cascade defect fixed on Diamond Plate/Tremont 15 (commit e9079ec).
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr double kFs = 48000.0;
static constexpr int    kBlk = 512;

struct AmpCfg {
    const char* label;
    AmpModel model;
    int paIdx, tube;
    bool sunn = false;
    const char* chParam = nullptr; float chVal = 0.0f;   // channel/mode selector
};

static double goertzel(const std::vector<float>& x, size_t from, double freq) {
    const double w = 2.0 * M_PI * freq / kFs, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < x.size(); ++i) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    const double n = double(x.size() - from);
    return std::sqrt(std::max(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0)) / (n / 2.0);
}
static double db(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

// Run a sine through amp+PA with the given knobs; return output buffer.
static std::vector<float> run(const AmpCfg& a, double freq, double inDb,
                              float gain, float bass, float mid, float treble, float pres,
                              float master = 0.65f) {
    AmpBlockExtended amp; amp.prepare(kFs, kBlk, 1);
    amp.setAmpModel(a.model); amp.setBypass(false);
    if (a.chParam) amp.setParameter(a.chParam, a.chVal);
    if (a.sunn) { amp.setParameter("vol1", gain); amp.setParameter("bass1", bass);
                  amp.setParameter("mid1", mid);  amp.setParameter("treble1", treble); }
    else { amp.setParameter("gain", gain); amp.setParameter("bass", bass);
           amp.setParameter("mid", mid);   amp.setParameter("treble", treble); }
    amp.setParameter("presence", pres);
    amp.setParameter("master", master); amp.setParameter("sag", 0.2f);
    PowerAmpProcessor pa; pa.prepare(kFs, kBlk, 1);
    const auto d = PowerAmpProcessor::getDefaultsForModel(a.paIdx);
    pa.setParameter("master", d.master); pa.setParameter("presence", d.presence);
    pa.setParameter("depth", d.depth);   pa.setParameter("nfb", d.nfb);
    pa.setParameter("sag", d.sag);       pa.setParameter("bloomvca", d.bloomVca);
    pa.setTubeType(static_cast<TubeType>(a.tube));
    pa.setBypass(a.sunn);

    const double amp0 = std::pow(10.0, inDb / 20.0) * std::sqrt(2.0);
    const size_t total = size_t(kFs * 1.2);
    std::vector<float> out(total);
    std::vector<float> buf(kBlk);
    size_t pos = 0;
    while (pos < total) {
        const int n = int(std::min<size_t>(kBlk, total - pos));
        for (int i = 0; i < n; ++i) buf[size_t(i)] = float(amp0 * std::sin(2.0 * M_PI * freq * double(pos + size_t(i)) / kFs));
        float* p = buf.data();
        amp.process(&p, &p, n, 1);
        pa.process(&p, &p, n, 1);
        std::memcpy(out.data() + pos, buf.data(), size_t(n) * sizeof(float));
        pos += size_t(n);
    }
    return out;
}

static double thdAt(const AmpCfg& a, float gain, double* outDb = nullptr, float master = 0.65f) {
    auto out = run(a, 999.0, -12.0, gain, 0.5f, 0.5f, 0.5f, 0.5f, master);
    const size_t skip = size_t(kFs * 0.2);
    const double f = goertzel(out, skip, 999.0);
    double h = 0.0, ss = 0.0;
    for (int k = 2; k <= 8; ++k) { const double hk = goertzel(out, skip, 999.0 * k); h += hk * hk; }
    for (size_t i = skip; i < out.size(); ++i) ss += double(out[i]) * out[i];
    if (outDb) *outDb = db(std::sqrt(ss / double(out.size() - skip)));
    return 100.0 * std::sqrt(h) / std::max(f, 1e-12);
}

// Pink noise through the amp; band level RELATIVE to a 1 kHz reference band, so
// compression/limiting cancels and only the spectral authority of the knob remains.
// paPres/paDepth >= 0 override the POWER-AMP presence/depth (for PA-knob authority).
static std::vector<float> runPink(const AmpCfg& a, float gain, float b, float m, float t, float p, unsigned seed,
                                  float paPres = -1.0f, float paDepth = -1.0f) {
    AmpBlockExtended amp; amp.prepare(kFs, kBlk, 1);
    amp.setAmpModel(a.model); amp.setBypass(false);
    if (a.chParam) amp.setParameter(a.chParam, a.chVal);
    if (a.sunn) { amp.setParameter("vol1", gain); amp.setParameter("bass1", b);
                  amp.setParameter("mid1", m);  amp.setParameter("treble1", t); }
    else { amp.setParameter("gain", gain); amp.setParameter("bass", b);
           amp.setParameter("mid", m);   amp.setParameter("treble", t); }
    amp.setParameter("presence", p);
    amp.setParameter("master", 0.65f); amp.setParameter("sag", 0.2f);
    PowerAmpProcessor pa; pa.prepare(kFs, kBlk, 1);
    const auto d = PowerAmpProcessor::getDefaultsForModel(a.paIdx);
    pa.setParameter("master", d.master); pa.setParameter("presence", paPres >= 0.0f ? paPres : d.presence);
    pa.setParameter("depth",  paDepth >= 0.0f ? paDepth : d.depth);   pa.setParameter("nfb", d.nfb);
    pa.setParameter("sag", d.sag);       pa.setParameter("bloomvca", d.bloomVca);
    pa.setTubeType(static_cast<TubeType>(a.tube));
    pa.setBypass(a.sunn);
    const size_t total = size_t(kFs * 1.5);
    std::vector<float> out(total), buf(kBlk);
    double b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0; uint32_t st = seed ? seed : 1u;
    size_t pos = 0;
    while (pos < total) {
        const int n = int(std::min<size_t>(kBlk, total - pos));
        for (int i = 0; i < n; ++i) {
            st ^= st << 13; st ^= st >> 17; st ^= st << 5;
            double w = (double(st) / 2147483648.0) - 1.0;
            b0=0.99886*b0+w*0.0555179; b1=0.99332*b1+w*0.0750759; b2=0.96900*b2+w*0.1538520;
            b3=0.86650*b3+w*0.3104856; b4=0.55000*b4+w*0.5329522; b5=-0.7616*b5-w*0.0168980;
            buf[size_t(i)] = float((b0+b1+b2+b3+b4+b5+b6+w*0.5362) * 0.11 * 0.35);   // ~ -18 dBFS rms
            b6=w*0.115926;
        }
        float* pp = buf.data();
        amp.process(&pp, &pp, n, 1);
        pa.process(&pp, &pp, n, 1);
        std::memcpy(out.data()+pos, buf.data(), size_t(n)*sizeof(float));
        pos += size_t(n);
    }
    return out;
}
static double bandDb(const std::vector<float>& x, double fc) {
    const size_t skip = size_t(kFs * 0.3);
    double acc = 0.0; int cnt = 0;
    for (double f = fc * 0.85; f <= fc * 1.18; f *= 1.06) { double g = goertzel(x, skip, f); acc += g * g; ++cnt; }
    return db(std::sqrt(acc / std::max(cnt,1)));
}

static double toneSwing(const AmpCfg& a, int knob /*0 b,1 m,2 t,3 p*/, double freq) {
    auto rel = [&](float v) {
        float b = 0.5f, m = 0.5f, t = 0.5f, p = 0.5f;
        if (knob == 0) b = v; else if (knob == 1) m = v; else if (knob == 2) t = v; else p = v;
        auto out = runPink(a, 0.5f, b, m, t, p, 12345);
        return bandDb(out, freq) - bandDb(out, 1000.0);   // ref-normalized: compression cancels
    };
    return rel(1.0f) - rel(0.0f);
}

int main() {
    const AmpCfg amps[] = {
        {"Fender",        AmpModel::FenderDeluxe,       0, 0},
        {"JCM800",        AmpModel::MarshallJCM800,     1, 1},
        {"EVH blue",      AmpModel::EVH5150III,         2, 1, false, "channel", 0.0f},
        {"EVH red",       AmpModel::EVH5150III,         2, 1, false, "channel", 1.0f},
        {"Sunn",          AmpModel::SunnModelT,         4, 0, true},
        {"Rockerverb cl", AmpModel::OrangeRockerverb50, 5, 1, false, "channel", 0.0f},
        {"Rockerverb dr", AmpModel::OrangeRockerverb50, 5, 1, false, "channel", 1.0f},
        {"Friedman BE",   AmpModel::FriedmanBEDeluxe,   6, 1, false, "channel", 1.0f},
        {"Friedman HBE",  AmpModel::FriedmanBEDeluxe,   6, 1, false, "channel", 2.0f},
        {"Hiwatt",        AmpModel::HiwattDR103,        0, 1},
        {"Vox",           AmpModel::VoxAC30,            0, 2},
        {"Backline",      AmpModel::PeaveyBackstage,    0, 0},
        {"Plexi",         AmpModel::MarshallPlexi,      1, 1},
        {"CaliV Clean",   AmpModel::MesaMarkV,          1, 1, false, "mode", 0.0f},
        {"CaliV Crunch",  AmpModel::MesaMarkV,          1, 1, false, "mode", 4.0f},
        {"CaliV IIC+",    AmpModel::MesaMarkV,          1, 1, false, "mode", 6.0f},
        {"Recto CH1",     AmpModel::MesaDualRectifier,  7, 0, false, "mode", 0.0f},
        {"Recto CH2 Vin", AmpModel::MesaDualRectifier,  7, 0, false, "mode", 3.0f},
        {"Recto CH3 Mod", AmpModel::MesaDualRectifier,  7, 0, false, "mode", 7.0f},
        {"MT15 Clean",    AmpModel::PRSMT15,            8, 0, false, "mode", 0.0f},
        {"MT15 Lead",     AmpModel::PRSMT15,            8, 0, false, "mode", 2.0f},
    };
    printf("%-14s | GAIN thd%% @ .05 / .3 / .5 / 1.0        | TONE swing dB (0->1): bass mid treb pres\n", "amp");
    printf("%-14s | ------------------------------------- | ----------------------------------------\n", "");
    for (const auto& a : amps) {
        double l05=0, l10=0;
        const double t05 = thdAt(a, 0.05f, &l05), t3 = thdAt(a, 0.3f), t5 = thdAt(a, 0.5f), t10 = thdAt(a, 1.0f, &l10);
        const double sb = toneSwing(a, 0, 90.0),  sm = toneSwing(a, 1, 550.0);
        const double st = toneSwing(a, 2, 4200.0), sp = toneSwing(a, 3, 5000.0);
        const bool quiet05 = l05 < -55.0;   // near-silent: THD number not meaningful
        const bool flat = !quiet05 && (t10 > 15.0) && (t05 > 0.6 * t10);
        const bool dead = std::fabs(sb) < 4.0 || std::fabs(st) < 4.0;
        printf("%-14s | %6.1f %6.1f %6.1f %6.1f (o%3.0f/%3.0fdB)%s| %5.1f %5.1f %5.1f %5.1f %s\n",
               a.label, t05, t3, t5, t10, l05, l10, flat ? "<-FLAT " : "       ",
               sb, sm, st, sp, dead ? "<-DEAD KNOB" : "");
    }

    // ── Fine gain sweep: 10 steps — flag THD dips (bias-choke/blocking pathologies
    // like the Cali V Crunch spike) and output-level jumps between adjacent steps.
    printf("\n%-14s | fine GAIN thd%% at .1 .2 .3 .4 .5 .6 .7 .8 .9 1.0\n", "amp");
    for (const auto& a : amps) {
        double thd[10], lv[10]; char flags[64] = "";
        for (int i = 0; i < 10; ++i) thd[i] = thdAt(a, 0.1f * float(i + 1), &lv[i]);
        for (int i = 1; i < 10; ++i) {
            if (thd[i] < thd[i-1] * 0.72 && thd[i-1] > 12.0 && lv[i] > -50.0)
                std::snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " DIP@.%d", i + 1);
            if (std::fabs(lv[i] - lv[i-1]) > 4.0)
                std::snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " JUMP@.%d", i + 1);
        }
        printf("%-14s |", a.label);
        for (int i = 0; i < 10; ++i) printf(" %5.1f", thd[i]);
        printf("%s\n", flags);
    }

    // ── Amp master taper: output dB at master .1/.35/.65/1.0 (gain 0.3) — must rise
    // monotonically with real authority (>6 dB total swing); flag inversions/plateaus.
    printf("\n%-14s | MASTER out dB @ .1 / .35 / .65 / 1.0\n", "amp");
    for (const auto& a : amps) {
        double lv[4]; const float ms[4] = {0.1f, 0.35f, 0.65f, 1.0f};
        for (int i = 0; i < 4; ++i) thdAt(a, 0.3f, &lv[i], ms[i]);
        const bool inv  = lv[1] < lv[0] - 0.3 || lv[2] < lv[1] - 0.3 || lv[3] < lv[2] - 0.3;
        const bool weak = (lv[3] - lv[0]) < 6.0;
        printf("%-14s | %6.1f %6.1f %6.1f %6.1f %s%s\n", a.label, lv[0], lv[1], lv[2], lv[3],
               inv ? "<-NON-MONOTONIC " : "", weak ? "<-WEAK MASTER" : "");
    }

    // ── Power-amp presence/depth authority (PA knobs 0 -> 1, pink-tilt vs 1 kHz;
    // amps that bypass the PA (Sunn) are skipped).
    printf("\n%-14s | PA presence swing @4.5k | PA depth swing @100\n", "amp");
    for (const auto& a : amps) {
        if (a.sunn) { printf("%-14s |   (PA bypassed)\n", a.label); continue; }
        auto rel = [&](float pp, float pd, double fc) {
            auto out = runPink(a, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 777, pp, pd);
            return bandDb(out, fc) - bandDb(out, 1000.0);
        };
        const double sp = rel(1.0f, -1.0f, 4500.0) - rel(0.0f, -1.0f, 4500.0);
        const double sd = rel(-1.0f, 1.0f, 100.0)  - rel(-1.0f, 0.0f, 100.0);
        printf("%-14s | %6.1f dB %s | %6.1f dB %s\n", a.label,
               sp, std::fabs(sp) < 1.5 ? "<-DEAD" : "      ",
               sd, std::fabs(sd) < 1.5 ? "<-DEAD" : "      ");
    }
    return 0;
}
