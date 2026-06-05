// Sunn Model T crunch measurement: THD vs volume across the three channel-link
// modes. Grounds the re-voice — shows where the amp currently breaks up.
#include "AmpModelFactory.h"
#include "OversamplingWrapper.h"
#include <cstdio>
#include <cmath>
#include <vector>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double goertzel(const std::vector<float>& x, double f, double fs) {
    const double w=2*M_PI*f/fs, c=std::cos(w), coeff=2*c, sn=std::sin(w);
    double s1=0,s2=0; for (float v:x){ double s0=v+coeff*s1-s2; s2=s1; s1=s0; }
    const double re=s1-s2*c, im=s2*sn; return std::sqrt(re*re+im*im);
}

static void sweep(const char* mode, float link) {
    const double fs=48000.0, f=110.0; const int N=(int)(fs*0.5);
    printf("\n  %-18s  vol   THD%%    outRMS\n", mode);
    for (double vol : {0.2,0.4,0.6,0.8,1.0}) {
        auto m = AmpModelFactory::createWithOversampling(AmpModelFactory::ModelID::SunnModelT);
        m->prepare(fs,128,1);
        m->setParameter("vol1",(float)vol); m->setParameter("vol2",(float)vol);
        m->setParameter("master",1.0f);     m->setParameter("channel_link",link);
        m->setParameter("bass1",0.5f); m->setParameter("mid1",0.5f); m->setParameter("treble1",0.5f);
        m->setParameter("bass2",0.5f); m->setParameter("mid2",0.5f); m->setParameter("treble2",0.5f);
        std::vector<float> in(128), ob(128), out; out.reserve(N);
        float* ip[1]={in.data()}; float* op[1]={ob.data()};
        double ph=0; const double dph=2*M_PI*f/fs;
        for (int b=0;b<N/128;b++){
            for (int i=0;i<128;i++){ in[i]=0.3f*std::sin(ph); ph+=dph; if(ph>2*M_PI)ph-=2*M_PI; }
            m->process(ip,op,128,1);
            for (int i=0;i<128;i++) out.push_back(ob[i]);
        }
        std::vector<float> seg(out.begin()+8192, out.end());
        const double h1=goertzel(seg,f,fs);
        double hh=0; for (int k=2;k<=6;k++){ double h=goertzel(seg,k*f,fs); hh+=h*h; }
        const double thd = h1>1e-9 ? std::sqrt(hh)/h1*100.0 : 0.0;
        double rms=0; for (float v:seg) rms+=(double)v*v; rms=std::sqrt(rms/seg.size());
        printf("  %-18s  %.1f  %6.2f  %8.4f\n", "", vol, thd, rms);
    }
}

int main() {
    printf("Sunn Model T — THD vs volume (110 Hz tone, input 0.3, master=1.0)\n");
    printf("(a cranked/jumpered Model T should reach well into double-digit THD)\n");
    sweep("Independent", 0.0f);
    sweep("Parallel",    1.0f);
    sweep("Series",      2.0f);
    return 0;
}
