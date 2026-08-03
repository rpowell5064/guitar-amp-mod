// evh_hiss — measure the inter-harmonic "hiss" floor of the EVH 5150 III Red channel
// on a POWER CHORD (two tones), where signal-dependent gain modulation (e.g. a fast sag
// node driven by the rippling distorted envelope) shows up as broadband sidebands.
// Renders through AmpBlockExtended + PowerAmpProcessor exactly like the plugin (no cab).
// Metric: median bin magnitude in 3-16 kHz EXCLUDING bins near any m*f1 + n*f2 product
// (harmonics + intermod) -> that residual "grass" IS the hiss. Higher = hissier.
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include <complex>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

static constexpr double kFs = 48000.0;
static constexpr int    kBlk = 512;
static constexpr int    kN = 32768;   // ~0.68 s, bin 1.46 Hz

static void fft(std::vector<std::complex<double>>& a){
    const int n=(int)a.size();
    for(int i=1,j=0;i<n;++i){int bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(int len=2;len<=n;len<<=1){double ang=-2.0*M_PI/len;std::complex<double> wl(cos(ang),sin(ang));
        for(int i=0;i<n;i+=len){std::complex<double> w(1,0);for(int k=0;k<len/2;++k){
            auto u=a[i+k],v=a[i+k+len/2]*w;a[i+k]=u+v;a[i+k+len/2]=u-v;w*=wl;}}}
}

int main(){
    const double f1=110.0, f2=164.81;   // A2 + E3 (power chord, perfect fifth)
    AmpBlockExtended amp; amp.prepare(kFs,kBlk,1);
    amp.setAmpModel(AmpModel::EVH5150III); amp.setBypass(false);
    amp.setParameter("channel",1.0f);          // RED
    amp.setParameter("gain",0.63f); amp.setParameter("bass",0.26f);
    amp.setParameter("mid",0.63f);  amp.setParameter("treble",0.60f);
    amp.setParameter("presence",0.55f); amp.setParameter("master",0.5f);
    amp.setParameter("sag",0.4f);   amp.setParameter("resonance",0.5f);
    PowerAmpProcessor pa; pa.prepare(kFs,kBlk,1);
    auto d=PowerAmpProcessor::getDefaultsForModel(2);
    pa.setParameter("master",d.master); pa.setParameter("presence",d.presence);
    pa.setParameter("depth",d.depth); pa.setParameter("nfb",d.nfb);
    pa.setParameter("sag",d.sag); pa.setParameter("bloomvca",d.bloomVca);
    pa.setTubeType(static_cast<TubeType>(1));

    const double amp0 = std::pow(10.0,-14.0/20.0);   // hot into the front (djent slam)
    const double noiseAmp = std::pow(10.0,-45.0/20.0);  // rig input floor ~-45 dBFS (see rig-noise-floor)
    const size_t total=size_t(kFs*1.5);
    std::vector<float> out(total), buf(kBlk); size_t pos=0; uint32_t st=12345u;
    while(pos<total){
        int n=(int)std::min<size_t>(kBlk,total-pos);
        for(int i=0;i<n;++i){double t=double(pos+i)/kFs;
            st^=st<<13; st^=st>>17; st^=st<<5; double wn=(double(int32_t(st))/2147483648.0);
            buf[i]=float(amp0*(std::sin(2*M_PI*f1*t)+std::sin(2*M_PI*f2*t)) + noiseAmp*wn);}
        float* p=buf.data(); amp.process(&p,&p,n,1); pa.process(&p,&p,n,1);
        std::copy(buf.begin(),buf.begin()+n,out.begin()+pos); pos+=n;
    }
    // HF hiss band (6 kHz 1-pole HPF) -> per-20ms-frame RMS -> mean + pumping range.
    {
        double a1=std::exp(-2.0*M_PI*6000.0/kFs), yp=0, xp=0; std::vector<float> hp(total);
        for(size_t i=0;i<total;++i){double y=a1*(yp+out[i]-xp); hp[i]=float(y); yp=y; xp=out[i];}
        int fr=int(kFs*0.020); std::vector<double> rms;
        for(size_t i=kN; i+fr<total; i+=fr){double s=0; for(int k=0;k<fr;++k) s+=double(hp[i+k])*hp[i+k]; rms.push_back(std::sqrt(s/fr));}
        std::sort(rms.begin(),rms.end());
        double med=rms[rms.size()/2], p05=rms[rms.size()/20], p95=rms[rms.size()*19/20];
        printf("HF hiss band (>6 kHz) w/ -45 dBFS input noise:\n");
        printf("  median level : %.1f dBFS\n", 20*std::log10(std::max(med,1e-12)));
        printf("  PUMPING p95/p05 : %.1f dB (modulation of the hiss = the audible artifact)\n", 20*std::log10(std::max(p95/std::max(p05,1e-12),1e-12)));
    }
    // FFT the steady tail
    std::vector<std::complex<double>> a(kN);
    size_t start=total-kN;
    for(int i=0;i<kN;++i){double w=0.5-0.5*std::cos(2*M_PI*i/(kN-1)); a[i]=out[start+i]*w;}
    fft(a);
    std::vector<double> mag(kN/2);
    for(int i=0;i<kN/2;++i) mag[i]=std::abs(a[i]);
    double binHz=kFs/kN;
    // mark bins within +-25 Hz of any m*f1+n*f2 (|m|,|n|<=12) as "peak"
    std::vector<char> peak(kN/2,0);
    for(int m=-12;m<=12;++m)for(int nn=-12;nn<=12;++nn){
        double fr=m*f1+nn*f2; if(fr<=0)continue; int c=int(fr/binHz+0.5);
        int w=int(25.0/binHz)+1; for(int b=c-w;b<=c+w;++b) if(b>=0&&b<kN/2) peak[b]=1;
    }
    // reference: fundamental magnitude
    double fund=0; for(int b=int(f1/binHz)-3;b<=int(f1/binHz)+3;++b) fund=std::max(fund,mag[b]);
    auto floorDb=[&](double lo,double hi){
        std::vector<double> v; for(int b=int(lo/binHz);b<int(hi/binHz)&&b<kN/2;++b) if(!peak[b]) v.push_back(mag[b]);
        std::sort(v.begin(),v.end()); double med=v.empty()?0:v[v.size()/2];
        return 20.0*std::log10(std::max(med/std::max(fund,1e-12),1e-12));
    };
    printf("EVH Red power-chord inter-harmonic floor (rel fundamental):\n");
    printf("  3-8 kHz : %.1f dB\n", floorDb(3000,8000));
    printf("  8-16 kHz: %.1f dB\n", floorDb(8000,16000));
    return 0;
}
