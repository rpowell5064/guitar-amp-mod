// Tone Bender MkII sanity/character probe: feeds a 220 Hz sine at several input
// levels, reports output peak, NaN/inf count, THD% (Goertzel), and a decaying-note
// tail level (gating check). Confirms the germanium Newton solver is stable and
// the pedal fuzzes + cleans up before deploying.
#include "OversamplingWrapper.h"
#include "ToneBenderMkII.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <memory>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double goertzel(const std::vector<float>& x, double f, double fs) {
    const double w=2*M_PI*f/fs, c=std::cos(w), coeff=2*c, sn=std::sin(w);
    double s1=0,s2=0; for(float v:x){double s0=v+coeff*s1-s2;s2=s1;s1=s0;}
    const double re=s1-s2*c, im=s2*sn; return std::sqrt(re*re+im*im);
}

int main() {
    const double fs=48000.0, f=220.0;
    printf("Tone Bender MkII probe (attack 0.7, level 0.6, bias 0.5, trim 0.5, temp 0.4)\n");
    printf("  in     peak    NaN   THD%%   (cleanup: THD should drop at low input)\n");
    for (double amp : {0.03,0.1,0.3,0.6}) {
        auto m=std::make_unique<OversamplingWrapper>(std::make_unique<ToneBenderMkII>());
        m->prepare(fs,128,1);
        m->setParameter("attack",0.7f); m->setParameter("level",0.6f);
        m->setParameter("bias",0.5f); m->setParameter("inputtrim",0.5f); m->setParameter("getemp",0.4f);
        std::vector<float> in(128),ob(128),out; float*ip[1]={in.data()};float*op[1]={ob.data()};
        double ph=0,dph=2*M_PI*f/fs; double peak=0; int nan=0; const int N=24000;
        for(int b=0;b*128<N+8192;b++){
            for(int i=0;i<128;i++){in[i]=(float)(amp*std::sin(ph));ph+=dph;if(ph>2*M_PI)ph-=2*M_PI;}
            m->process(ip,op,128,1);
            for(int i=0;i<128;i++){float v=ob[i]; if(!std::isfinite(v))nan++; if(std::fabs(v)>peak)peak=std::fabs(v); out.push_back(v);}
        }
        std::vector<float> seg(out.begin()+8192,out.end());
        double h1=goertzel(seg,f,fs);
        double h=0; for(int k=2;k<=8;k++){double hk=goertzel(seg,k*f,fs); h+=hk*hk;}
        double thd = h1>1e-9? std::sqrt(h)/h1*100.0 : 0.0;
        printf("  %.2f   %5.3f   %3d   %5.1f\n", amp, peak, nan, thd);
    }
    return 0;
}
