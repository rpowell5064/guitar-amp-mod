// Fidelity-pass verification (2026-07-23): speaker-impedance coupling (PowerAmp)
// + pickup-loading sim (PickupLoadSim).
//   1. coupling = 0 must be BIT-IDENTICAL to a never-touched instance.
//   2. coupling = 1: the cone-resonance lift and HF sheen must GROW with drive
//      (the damping-collapse behavior) — measured as band-vs-1kHz pink tilt at
//      quiet vs pushed input levels.
//   3. PickupLoadSim: amount 0 = exact passthrough; light load = resonant peak
//      near 3 kHz + top rolloff; heavy load = darker, lower resonance.
#include "PowerAmpProcessor.h"
#include "PickupLoadSim.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr double kFs = 48000.0;
static constexpr int kBlk = 256;

static double goertzel(const std::vector<float>& x, size_t from, double f) {
    const double w = 2.0*M_PI*f/kFs, c = 2.0*std::cos(w);
    double s1=0,s2=0;
    for (size_t i=from;i<x.size();++i){double s0=x[i]+c*s1-s2;s2=s1;s1=s0;}
    return std::sqrt(std::max(s1*s1+s2*s2-c*s1*s2,0.0))/(double(x.size()-from)/2.0);
}
static double bandDb(const std::vector<float>& x, double fc) {
    const size_t skip = size_t(kFs*0.3);
    double acc=0; int n=0;
    for (double f=fc*0.85; f<=fc*1.18; f*=1.06){ double g=goertzel(x,skip,f); acc+=g*g; ++n; }
    return 20.0*std::log10(std::sqrt(acc/n)+1e-12);
}
static std::vector<float> pinkThroughPA(PowerAmpProcessor& pa, float lvl, unsigned seed) {
    const size_t total = size_t(kFs*1.5);
    std::vector<float> out(total), buf(kBlk);
    double b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0; uint32_t st=seed;
    size_t pos=0;
    while (pos<total) {
        const int n = int(std::min<size_t>(kBlk,total-pos));
        for (int i=0;i<n;++i){
            st^=st<<13; st^=st>>17; st^=st<<5;
            double w=(double(st)/2147483648.0)-1.0;
            b0=0.99886*b0+w*0.0555179; b1=0.99332*b1+w*0.0750759; b2=0.96900*b2+w*0.1538520;
            b3=0.86650*b3+w*0.3104856; b4=0.55000*b4+w*0.5329522; b5=-0.7616*b5-w*0.0168980;
            buf[size_t(i)]=float((b0+b1+b2+b3+b4+b5+b6+w*0.5362)*0.11)*lvl;
            b6=w*0.115926;
        }
        float* p=buf.data();
        pa.process(&p,&p,n,1);
        std::memcpy(out.data()+pos,buf.data(),size_t(n)*4);
        pos+=size_t(n);
    }
    return out;
}
static void setupPA(PowerAmpProcessor& pa) {
    pa.prepare(kFs,kBlk,1);
    pa.setParameter("master",0.6f); pa.setParameter("presence",0.5f);
    pa.setParameter("depth",0.5f);  pa.setParameter("nfb",0.4f);
    pa.setParameter("sag",0.2f);    pa.setParameter("bloomvca",0.1f);
    pa.setTubeType(TubeType::Tube_EL34);
}

int main() {
    int fails = 0;

    // 1. coupling = 0 bit-identity
    {
        PowerAmpProcessor a, b; setupPA(a); setupPA(b);
        b.setParameter("coupling", 0.0f);   // touched but zero
        auto ya = pinkThroughPA(a, 0.3f, 99);
        auto yb = pinkThroughPA(b, 0.3f, 99);
        bool same = true;
        for (size_t i=0;i<ya.size();++i) if (ya[i]!=yb[i]) { same=false; break; }
        if (!same) { std::printf("FAIL: coupling=0 not bit-identical\n"); ++fails; }
        else std::printf("coupling 0: bit-identical to untouched PA\n");
    }

    // 2. coupling response grows with drive
    {
        auto tilt=[&](float coupling, float lvl, double fc){
            PowerAmpProcessor pa; setupPA(pa);
            pa.setParameter("coupling", coupling);
            auto y = pinkThroughPA(pa, lvl, 1234);
            return bandDb(y, fc) - bandDb(y, 1000.0);
        };
        const double resQ = tilt(1,0.05f,95.0)-tilt(0,0.05f,95.0);   // quiet: partial coupling
        const double resH = tilt(1,0.8f, 95.0)-tilt(0,0.8f, 95.0);   // pushed: full coupling
        const double shQ  = tilt(1,0.05f,3000.0)-tilt(0,0.05f,3000.0);
        const double shH  = tilt(1,0.8f, 3000.0)-tilt(0,0.8f, 3000.0);
        std::printf("coupling lift @95 Hz: quiet %+.1f dB -> pushed %+.1f dB | @3 kHz: %+.1f -> %+.1f\n",
                    resQ, resH, shQ, shH);
        if (resH < 1.5)      { std::printf("FAIL: no cone-resonance lift when pushed\n"); ++fails; }
        if (resH < resQ+0.7) { std::printf("FAIL: resonance lift does not grow with drive\n"); ++fails; }
        if (shH  < 0.8)      { std::printf("FAIL: no HF sheen when pushed\n"); ++fails; }
    }

    // 3. PickupLoadSim
    {
        PickupLoadSim ps; ps.prepare(kFs);
        // amount 0: exact passthrough
        ps.set(0.0f);
        bool same = true;
        for (int i=0;i<1000;++i){ float x=std::sin(0.1f*i)*0.5f; if (ps.process(x)!=x){same=false;break;} }
        if (!same){ std::printf("FAIL: load=0 not exact passthrough\n"); ++fails; }
        auto resp=[&](float amount, double f){
            PickupLoadSim p2; p2.prepare(kFs); p2.set(amount);
            const size_t total=size_t(kFs*0.5); std::vector<float> y(total);
            for (size_t i=0;i<total;++i) y[i]=p2.process(float(0.3*std::sin(2.0*M_PI*f*double(i)/kFs)));
            double ss=0; const size_t skip=size_t(kFs*0.1);
            for (size_t i=skip;i<total;++i) ss+=double(y[i])*y[i];
            return 20.0*std::log10(std::sqrt(ss/double(total-skip))+1e-12);
        };
        const double pk15 = resp(0.15f,3100.0)-resp(0.0f,3100.0);   // light load: resonant sparkle
        const double hf15 = resp(0.15f,8000.0)-resp(0.0f,8000.0);   // ...with top rolloff
        const double hf1  = resp(1.0f, 5000.0)-resp(0.0f,5000.0);   // heavy load: dark
        std::printf("pickup load: peak@3.1k %+.1f dB (light), 8k %+.1f (light), 5k %+.1f (heavy)\n",
                    pk15, hf15, hf1);
        if (pk15 < 0.5)  { std::printf("FAIL: light load has no resonant peak\n"); ++fails; }
        if (hf15 > -0.5) { std::printf("FAIL: light load has no top rolloff\n"); ++fails; }
        if (hf1  > -6.0) { std::printf("FAIL: heavy load not dark enough\n"); ++fails; }
    }

    std::printf("%s (%d failure%s)\n", fails?"FAILED":"PASSED", fails, fails==1?"":"s");
    return fails?1:0;
}
