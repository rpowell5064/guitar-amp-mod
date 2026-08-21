// Woosh source isolation round 2 (2026-08-21): the monoRoom fix removed the
// dual-room comb; residual "whoosh, partly from the end EQ" remains on the
// Periphery presets. The chain EQ is STATIC (amplifier, not source), so this
// probe measures per-band envelope FLUTTER (mean |Δ dB| between 20 ms frames,
// active frames only) in a low band (350 Hz) and a high band (5 kHz) across
// suspect bypasses. The config that collapses the HF flutter is the source.
// Suspects: EVH-red DNR decay-darkener (no port — inferred by band), gate,
// speaker-drive compression, rig B, end EQ.
// Usage: woosh2 <presetIdx>
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

int main(int argc,char** argv){
    const char* SO="/home/pistomp/.lv2/guitaramp-suite.lv2/guitaramp_hexforge.so";
    const char* BUNDLE="/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* URI="https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const double RATE=48000.0; const uint32_t NF=64;
    const int PRESET=argc>1?atoi(argv[1]):30;

    FILE* fd=fopen("/home/pistomp/di.f32","rb"); if(!fd){fprintf(stderr,"no di\n");return 2;}
    fseek(fd,0,SEEK_END); long sz=ftell(fd); fseek(fd,0,SEEK_SET);
    std::vector<float> di(sz/4); size_t rd=fread(di.data(),4,di.size(),fd); (void)rd; fclose(fd);

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
    val[HF_PS_GOTO]=-1.0f;
    for(int i=0;i<HF_N_PORTS;++i){ void* p;
        if(i==HF_IN_L)p=ain_l.data(); else if(i==HF_IN_R)p=ain_r.data();
        else if(i==HF_OUT_L)p=aout_l.data(); else if(i==HF_OUT_R)p=aout_r.data();
        else if(i==HF_CONTROL)p=ctl.data(); else if(i==HF_NOTIFY)p=notify.data();
        else if(i==HF_MIDI_IN)p=midi.data(); else p=&val[i];
        d->connect_port(inst,i,p); }
    if(d->activate)d->activate(inst);
    for(int w=0;w<20;++w){ outSeq(notify); d->run(inst,NF); }

    auto setP=[&](int idx,float v){ val[idx]=v; outSeq(notify); d->run(inst,NF); };
    auto settle=[&](int blocks){ for(int w=0;w<blocks;++w){ outSeq(notify); d->run(inst,NF); } };
    auto recall=[&](int idx){ setP(HF_PS_GOTO,(float)idx); settle(60); setP(HF_PS_GOTO,-1.0f); settle(20); };

    struct BP { double b0,b2,a1,a2,z1=0,z2=0;
        void design(double fc,double Q,double fs){ double w=2*M_PI*fc/fs, al=sin(w)/(2*Q), c=cos(w), a0=1+al;
            b0=al/a0; b2=-al/a0; a1=-2*c/a0; a2=(1-al)/a0; }
        float run(float x){ double y=b0*x+z1; z1=-a1*y+z2; z2=b2*x-a2*y; return (float)y; } };

    auto measure=[&](const char* tag){
        BP lo, hi; lo.design(350.0,0.5,RATE); hi.design(5000.0,0.5,RATE);
        const int HOP=(int)(0.020*RATE);
        double ssL=0, ssH=0; int cnt=0; size_t pos=0;
        std::vector<double> envL, envH;
        while(pos<di.size()){
            uint32_t n=(uint32_t)((di.size()-pos)<(size_t)NF?(di.size()-pos):NF);
            for(uint32_t k=0;k<NF;++k){ float s=(k<n)?di[pos+k]:0.0f; ain_l[k]=s; ain_r[k]=s; }
            outSeq(notify); d->run(inst,NF);
            for(uint32_t k=0;k<n;++k){ float m=0.5f*(aout_l[k]+aout_r[k]);
                float bl=lo.run(m), bh=hi.run(m); ssL+=(double)bl*bl; ssH+=(double)bh*bh;
                if(++cnt==HOP){ envL.push_back(ssL/HOP); envH.push_back(ssH/HOP); ssL=ssH=0; cnt=0; } }
            pos+=n;
        }
        auto flutter=[&](std::vector<double>& e){
            double f=0; int n2=0; double prev=0; bool have=false;
            for(double v:e){ double db = v>1e-18?10*log10(v):-180;
                if(db>-60){ if(have){ f+=fabs(db-prev); n2++; } prev=db; have=true; } else have=false; }
            return n2? f/n2 : 0.0; };
        printf("%-14s  loFlutter %.3f dB/frame   hiFlutter %.3f dB/frame\n", tag, flutter(envL), flutter(envH));
    };

    recall(PRESET); measure("stock");
    recall(PRESET); setP(HF_RB_BLEND,0.001f);     settle(100); measure("rigB-off");
    recall(PRESET); setP(HF_RB_LOCUT,150.0f);     settle(100); measure("locut-150");
    recall(PRESET); setP(HF_RB_LOCUT,220.0f);     settle(100); measure("locut-220");
    recall(PRESET); setP(HF_RB_LOCUT,300.0f);     settle(100); measure("locut-300");
    recall(PRESET); setP(HF_RB_LOCUT,400.0f);     settle(100); measure("locut-400");
    if(d->deactivate)d->deactivate(inst);
    d->cleanup(inst);
    return 0;
}
