// Hard-pick dropout probe: recall a preset, synthesize a HARD pick transient
// (loud attack decaying to a sustained note) and print the output envelope in
// 20 ms hops — quantifies the "cuts out when I pick hard" sag collapse
// (PowerAmpProcessor sagEnv is input-proportional; see the 2026-07-25 clamp).
// Build (Pi):  g++ -O2 -std=c++17 -I lv2/hexforge $(pkg-config --cflags lv2)
//              build-tools/hexforge_pick.cpp -ldl -o /tmp/hfpick
// Run:         /tmp/hfpick <flat_idx> [...]
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/atom/atom.h>
#include "hexforge_ports.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <map>

static std::map<std::string,uint32_t> g_uris;
static LV2_URID map_uri(LV2_URID_Map_Handle,const char* u){
    auto it=g_uris.find(u); if(it!=g_uris.end())return it->second;
    uint32_t id=(uint32_t)g_uris.size()+1; g_uris[u]=id; return id; }
static const LV2_Worker_Interface* g_wi=nullptr; static LV2_Handle g_inst=nullptr;
static LV2_Worker_Status respond_fn(LV2_Worker_Respond_Handle,uint32_t size,const void* data){
    if(g_wi&&g_wi->work_response) g_wi->work_response(g_inst,size,data); return LV2_WORKER_SUCCESS; }
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle,uint32_t size,const void* data){
    if(g_wi&&g_wi->work) g_wi->work(g_inst,respond_fn,nullptr,size,data); return LV2_WORKER_SUCCESS; }

static bool g_pureSine=false;

int main(int argc,char** argv){
    const char* SO="/home/pistomp/guitar-amp-mod/build/guitaramp_hexforge.so";
    const char* BUNDLE="/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* URI="https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const double RATE=48000.0; const uint32_t NF=64;
    for(int a=1;a<argc;++a) if(!strcmp(argv[a],"--sine")) g_pureSine=true;

    // Synth DI: 0.2 s silence, HARD pick decaying into a long singing note
    // (tau 1.2 s — raw string reaches the gate-close floor ~7 s in while the
    // boosted/compressed CHAIN output is still clearly audible). Detects the
    // gate chopping a still-singing sustain ("cuts out when I pick hard").
    const uint32_t N=(uint32_t)(9.0*RATE);
    std::vector<float> di(N,0.0f);
    const double f0=196.0;
    for(uint32_t i=(uint32_t)(0.2*RATE);i<N;++i){
        const double t=(i-0.2*RATE)/RATE;
        double env;
        if(t<0.005)      env=0.95*(t/0.005);                    // 5 ms attack — HARD pick
        else if(t<0.125) env=0.55+(0.95-0.55)*std::exp(-(t-0.005)/0.030); // pick decays into a LOUD ringing strum
        else             env=0.55*std::exp(-(t-0.125)/1.2);      // real-string decay
        const double ph=2.0*M_PI*f0*t;
        // Two source flavors (--sine selects the pure one): a pure decaying fundamental
        // isolates real dynamics-stage behavior from partial-cancellation artifacts of
        // the bright multi-partial pick spectrum.
        const double s = g_pureSine ? 0.85*std::sin(ph)
                       : 0.55*std::sin(ph)+0.20*std::sin(2*ph+0.7)+0.10*std::sin(3*ph+1.9)
                        +0.28*std::sin(12*ph+0.3)+0.22*std::sin(17*ph+2.1)+0.16*std::sin(23*ph+1.2);
        double v=1.5*env*s;                            // hot pickup, hard attack
        if(v>0.99)v=0.99; else if(v<-0.99)v=-0.99;     // ADC hard-clip = worst-case real pick
        di[i]=(float)v;
    }

    void* h=dlopen(SO,RTLD_NOW|RTLD_LOCAL); if(!h){fprintf(stderr,"dlopen %s\n",dlerror());return 2;}
    auto descfn=(const LV2_Descriptor*(*)(uint32_t))dlsym(h,"lv2_descriptor");
    const LV2_Descriptor* d=nullptr;
    for(uint32_t i=0;;++i){auto x=descfn(i); if(!x)break; if(!strcmp(x->URI,URI)){d=x;break;}}
    if(!d){fprintf(stderr,"uri not found\n");return 2;}
    LV2_URID_Map map{nullptr,map_uri}; LV2_Worker_Schedule sched{nullptr,sched_work};
    LV2_Feature fmap{LV2_URID__map,&map}, fsched{LV2_WORKER__schedule,&sched};
    const LV2_Feature* feats[]={&fmap,&fsched,nullptr};
    LV2_Handle inst=d->instantiate(d,RATE,BUNDLE,feats);
    if(!inst){fprintf(stderr,"instantiate null\n");return 3;}
    g_inst=inst; g_wi=(const LV2_Worker_Interface*)(d->extension_data?d->extension_data(LV2_WORKER__interface):nullptr);

    std::vector<float> ain_l(NF),ain_r(NF),aout_l(NF),aout_r(NF), val(HF_N_PORTS,0.0f);
    uint32_t seqURID=map_uri(nullptr,LV2_ATOM__Sequence);
    std::vector<uint8_t> ctl(8192,0),midi(8192,0),notify(65536,0);
    auto inSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=sizeof(LV2_Atom_Sequence_Body);s->atom.type=seqURID;s->body.unit=0;s->body.pad=0;};
    auto outSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=(uint32_t)(b.size()-sizeof(LV2_Atom));s->atom.type=seqURID;};
    inSeq(ctl);inSeq(midi);outSeq(notify);
    val[HF_OUT_AUTO]=0.0f; val[HF_PS_GOTO]=-1.0f;
    for(int i=0;i<HF_N_PORTS;++i){ void* p;
        if(i==HF_IN_L)p=ain_l.data(); else if(i==HF_IN_R)p=ain_r.data();
        else if(i==HF_OUT_L)p=aout_l.data(); else if(i==HF_OUT_R)p=aout_r.data();
        else if(i==HF_CONTROL)p=ctl.data(); else if(i==HF_NOTIFY)p=notify.data();
        else if(i==HF_MIDI_IN)p=midi.data(); else p=&val[i];
        d->connect_port(inst,i,p); }
    if(d->activate)d->activate(inst);

    for(int a=1;a<argc;++a){
        if(!strcmp(argv[a],"--sine")) continue;
        int idx=atoi(argv[a]);
        static const char* PN[2]={"as-is","gate-off"};
        for(int pass=0;pass<2;++pass){   // gate chop isolation
        val[HF_OUT_LEVEL]=0.0f; val[HF_PS_GOTO]=(float)idx;
        outSeq(notify); d->run(inst,NF);
        for(int s=0;s<500;++s){ outSeq(notify); d->run(inst,NF); }   // recall + amp swap land
        val[HF_OUT_LEVEL]=-20.0f;
        if(pass==1) val[HF_GT_ENABLE]=0.0f;
        outSeq(notify); d->run(inst,NF);
        for(int s=0;s<4;++s){ outSeq(notify); d->run(inst,NF); }
        // process the pick DI, 20 ms RMS hops
        const uint32_t HOP=(uint32_t)(0.020*RATE);
        std::vector<double> env; double acc=0; uint32_t cnt=0; size_t pos=0;
        while(pos<di.size()){
            uint32_t n=(uint32_t)std::min((size_t)NF,di.size()-pos);
            for(uint32_t k=0;k<NF;++k){ float s=(k<n)?di[pos+k]:0.0f; ain_l[k]=s; ain_r[k]=s; }
            outSeq(notify); d->run(inst,NF);
            for(uint32_t k=0;k<n;++k){ acc+=(double)aout_l[k]*aout_l[k]; if(++cnt==HOP){ env.push_back(std::sqrt(acc/HOP)); acc=0; cnt=0; } }
            pos+=n;
        }
        auto db=[&](double v){ return v>1e-9?20.0*std::log10(v):-120.0; };
        // decay trace + steepest 200 ms drop (a gate chop is a fast cliff; natural decay is slow)
        printf("%-4d %-9s env dB @1..8s:", idx, PN[pass]);
        for(int ssec=1;ssec<=8;++ssec){ size_t k=(size_t)(ssec/0.020); printf(" %7.1f", k<env.size()?db(env[k]):-120.0); }
        printf("\n              fine 3.6-5.4s:");
        for(double tt=3.6;tt<5.5;tt+=0.2){ size_t k=(size_t)(tt/0.020); printf(" %6.1f", k<env.size()?db(env[k]):-120.0); }
        double worst=0; double worstT=0, outAtCliff=-120;
        for(size_t i2=10;i2+10<env.size();++i2){
            const double drop=db(env[i2+10])-db(env[i2]);   // 200 ms window
            if(env[i2]>1e-6 && drop<worst){ worst=drop; worstT=i2*0.020; outAtCliff=db(env[i2]); }
        }
        printf("  | steepest 200ms drop %6.1f dB at t=%.2fs (out was %6.1f dB)\n", worst, worstT, outAtCliff);
        val[HF_PS_GOTO]=-1.0f; outSeq(notify); d->run(inst,NF);
        val[HF_GT_ENABLE]=1.0f;   // neutral between passes (recall re-seeds eff; a CHANGE re-folds)
        }
    }
    if(d->deactivate)d->deactivate(inst);
    if(d->cleanup)d->cleanup(inst);
    dlclose(h);
    return 0;
}
