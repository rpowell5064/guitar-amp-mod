// Preset fuzz diagnostic: instantiate the deployed Hex Forge (auto-restores the user's
// preset store), recall the first N presets via ps_goto, run a real DI through each and
// print RMS/peak/crest. For any preset whose crest collapses (clipping), re-measure with
// each block force-bypassed (host knob-move) to isolate the clipping stage.
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
    const int NPRESETS = argc>1 ? atoi(argv[1]) : 8;

    FILE* fd=fopen("/home/pistomp/di.f32","rb"); if(!fd){fprintf(stderr,"no di\n");return 2;}
    fseek(fd,0,SEEK_END); long sz=ftell(fd); fseek(fd,0,SEEK_SET);
    std::vector<float> di(sz/4); size_t rd=fread(di.data(),4,di.size(),fd); (void)rd; fclose(fd);
    { double ss=0,pk=0; for(float v:di){ss+=v*(double)v; if(std::fabs(v)>pk)pk=std::fabs(v);}
      double r=std::sqrt(ss/di.size());
      printf("DI: rms %.1f dBFS, peak %.1f dBFS, crest %.1f dB\n", 20*log10(r),20*log10(pk),20*log10(pk/r)); }

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
    for(int w=0;w<8;++w){ outSeq(notify); d->run(inst,NF); }

    auto setP=[&](int idx,float v){ val[idx]=v; outSeq(notify); d->run(inst,NF); };
    auto runDi=[&](double& rmsDb,double& peakDb,double& crest){
        double ss=0,pk=0; uint64_t cnt=0; size_t pos=0; uint64_t gi=0;
        const uint64_t skip=(uint64_t)(0.3*RATE);
        while(pos<di.size()){
            uint32_t n=(uint32_t)std::min((size_t)NF,di.size()-pos);
            for(uint32_t k=0;k<NF;++k){ float s=(k<n)?di[pos+k]:0.0f; ain_l[k]=s; ain_r[k]=s; }
            outSeq(notify); d->run(inst,NF);
            for(uint32_t k=0;k<n;++k){ if(gi>=skip){ double v=aout_l[k]; ss+=v*v; double a=std::fabs(v); if(a>pk)pk=a; cnt++; } gi++; }
            pos+=n;
        }
        double r=cnt?std::sqrt(ss/cnt):0;
        rmsDb=r>1e-9?20*log10(r):-120; peakDb=pk>1e-9?20*log10(pk):-120; crest=peakDb-rmsDb;
    };
    auto recall=[&](int idx){
        setP(HF_PS_GOTO,(float)idx);
        for(int w=0;w<40;++w){ outSeq(notify); d->run(inst,NF); }  // let recall + worker loads settle
        setP(HF_PS_GOTO,-1.0f);
    };

    struct Byp { const char* name; int port; };
    const Byp byps[] = {
        {"fuzz", HF_FZ_BYPASS}, {"drive", HF_DR_BYPASS}, {"amp", HF_AMP_BYPASS},
        {"nail", HF_NAIL_BYPASS}, {"comp", HF_CP_BYPASS}, {"cab", HF_CAB_BYPASS},
    };

    auto runSilence=[&](double& rmsDb,double& peakDb){
        double ss=0,pk=0; uint64_t cnt=0;
        const int blocks=(int)(4.0*RATE/NF);
        for(int b=0;b<blocks;++b){
            for(uint32_t k=0;k<NF;++k){ ain_l[k]=0.0f; ain_r[k]=0.0f; }
            outSeq(notify); d->run(inst,NF);
            if(b>=(int)(0.5*RATE/NF)) for(uint32_t k=0;k<NF;++k){ double v=aout_l[k]; ss+=v*v; double a=std::fabs(v); if(a>pk)pk=a; cnt++; }
        }
        double r=cnt?std::sqrt(ss/cnt):0;
        rmsDb=r>1e-9?20*log10(r):-120; peakDb=pk>1e-9?20*log10(pk):-120;
    };
    printf("\npreset   rms_dBFS  peak_dBFS  crest_dB   SILENCE:rms  peak\n");
    for(int pidx=0; pidx<NPRESETS; ++pidx){
        recall(pidx);
        double sr,sp; runSilence(sr,sp);
        recall(pidx);
        double r,pk,c; runDi(r,pk,c);
        printf("%-8d %8.1f %9.1f %9.1f %11.1f %6.1f %s%s\n", pidx, r, pk, c, sr, sp,
               c<6.0?"<-- CLIPPING ":"", sr>-50.0?"<-- NOISE ROAR":"");
        if(c<6.0){
            for(const Byp& b: byps){
                recall(pidx);                       // reset overrides
                setP(b.port, 1.0f);                 // force this block bypassed (knob-move)
                double r2,pk2,c2; runDi(r2,pk2,c2);
                setP(b.port, 0.0f);
                printf("    bypass %-6s -> rms %7.1f  crest %5.1f %s\n",
                       b.name, r2, c2, (c2>c+3.0)?"<-- RESTORES":"");
            }
        }
    }
    if(d->deactivate)d->deactivate(inst); if(d->cleanup)d->cleanup(inst); dlclose(h);
    return 0;
}
