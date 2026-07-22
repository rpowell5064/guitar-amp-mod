// Diamond Plate + Tremont 15 mode-response test (derived from mvtest.cpp): instantiate
// Hex Forge, force amp_model 12 then 13, sweep each amp's mode/toggle ports, run a DI
// through it and print RMS/peak. Differing numbers per mode = the DSP+port path works;
// NaN/silence = wiring bug. Uses ~/di.f32 if present, else a synthetic two-tone burst.
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

int main(){
    const char* SO="/home/pistomp/guitar-amp-mod/build/guitaramp_hexforge.so";
    const char* BUNDLE="/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* URI="https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const double RATE=48000.0; const uint32_t NF=64;

    // DI: real capture if available, else a deterministic 2 s two-tone with a gated attack.
    std::vector<float> di;
    if(FILE* fd=fopen("/home/pistomp/di.f32","rb")){
        fseek(fd,0,SEEK_END); long sz=ftell(fd); fseek(fd,0,SEEK_SET);
        di.resize(sz/4); fread(di.data(),4,di.size(),fd); fclose(fd);
        printf("input: di.f32 (%zu samples)\n", di.size());
    } else {
        di.resize((size_t)(2.0*RATE));
        for(size_t i=0;i<di.size();++i){
            double t=i/RATE;
            double env=(i<di.size()/8)?0.0:1.0;                       // gated attack
            di[i]=(float)(env*0.18*(std::sin(2*M_PI*220.0*t)+0.5*std::sin(2*M_PI*660.0*t)));
        }
        printf("input: synthetic two-tone (no di.f32)\n");
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
    printf("worker iface: %s\n", g_wi?"present":"MISSING");

    std::vector<float> ain_l(NF),ain_r(NF),aout_l(NF),aout_r(NF), val(HF_N_PORTS,0.0f);
    uint32_t seqURID=map_uri(nullptr,LV2_ATOM__Sequence);
    std::vector<uint8_t> ctl(8192,0),midi(8192,0),notify(65536,0);
    auto inSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=sizeof(LV2_Atom_Sequence_Body);s->atom.type=seqURID;s->body.unit=0;s->body.pad=0;};
    auto outSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=(uint32_t)(b.size()-sizeof(LV2_Atom));s->atom.type=seqURID;};
    inSeq(ctl);inSeq(midi);outSeq(notify);

    val[HF_OUT_AUTO]=0.0f; val[HF_OUT_LEVEL]=0.0f; val[HF_PS_GOTO]=-1.0f;
    for(int i=0;i<HF_N_PORTS;++i){ void* p;
        if(i==HF_IN_L)p=ain_l.data(); else if(i==HF_IN_R)p=ain_r.data();
        else if(i==HF_OUT_L)p=aout_l.data(); else if(i==HF_OUT_R)p=aout_r.data();
        else if(i==HF_CONTROL)p=ctl.data(); else if(i==HF_NOTIFY)p=notify.data();
        else if(i==HF_MIDI_IN)p=midi.data(); else p=&val[i];
        d->connect_port(inst,i,p); }
    if(d->activate)d->activate(inst);
    for(int w=0;w<8;++w){ outSeq(notify); d->run(inst,NF); }

    auto setP=[&](int idx,float v){ val[idx]=v; outSeq(notify); d->run(inst,NF); };

    int bad=0;
    auto measure=[&](const char* label){
        double sumsq=0,peak=0; uint64_t cnt=0; size_t pos=0; uint64_t gi=0; bool nan=false;
        const uint64_t skip=(uint64_t)(0.3*RATE);
        while(pos<di.size()){
            uint32_t n=(uint32_t)std::min((size_t)NF,di.size()-pos);
            for(uint32_t k=0;k<NF;++k){ float s=(k<n)?di[pos+k]:0.0f; ain_l[k]=s; ain_r[k]=s; }
            outSeq(notify); d->run(inst,NF);
            for(uint32_t k=0;k<n;++k){ double v=aout_l[k];
                if(!std::isfinite(v)) nan=true;
                if(gi>=skip){ sumsq+=v*v; double av=std::fabs(v); if(av>peak)peak=av; cnt++; } gi++; }
            pos+=n;
        }
        double rms=(cnt?std::sqrt(sumsq/cnt):0.0);
        bool silent = rms < 1e-6;
        if(nan||silent) bad++;
        printf("  %-24s %9.2f %10.2f  %s%s\n", label,
               (rms>1e-9?20*std::log10(rms):-120.0), (peak>1e-9?20*std::log10(peak):-120.0),
               nan?"NAN! ":"", silent?"SILENT!":"");
    };

    // ── Diamond Plate (model 12): 8 modes + variac/rect at CH3 Modern ──
    setP(HF_AMP_MODEL, 12.0f);
    for(int w=0;w<8;++w){ outSeq(notify); d->run(inst,NF); }
    setP(HF_AMP_GAIN,0.6f); setP(HF_AMP_MASTER,0.65f);
    setP(HF_AMP_BASS,0.5f); setP(HF_AMP_MID,0.5f); setP(HF_AMP_TREBLE,0.6f); setP(HF_AMP_PRESENCE,0.5f);
    printf("Diamond Plate (model 12)  rms_dBFS  peak_dBFS\n");
    const char* rname[8]={"CH1 Clean","CH1 Pushed","CH2 Raw","CH2 Vintage","CH2 Modern","CH3 Raw","CH3 Vintage","CH3 Modern"};
    for(int m=0;m<8;++m){
        setP(HF_AMP_RC_MODE, m==0?7.0f:0.0f);
        setP(HF_AMP_RC_MODE,(float)m);
        for(int s=0;s<4;++s){ outSeq(notify); d->run(inst,NF); }
        measure(rname[m]);
    }
    setP(HF_AMP_RC_MODE,7.0f);
    for(int s=0;s<4;++s){ outSeq(notify); d->run(inst,NF); }
    setP(HF_AMP_RC_VARIAC,1.0f); measure("CH3 Mod + Spongy");
    setP(HF_AMP_RC_RECT,1.0f);   measure("CH3 Mod + Spongy+Tube");
    setP(HF_AMP_RC_VARIAC,0.0f); setP(HF_AMP_RC_RECT,0.0f);

    // ── Tremont 15 (model 13): 3 channels + bright on Crunch ──
    setP(HF_AMP_MODEL, 13.0f);
    for(int w=0;w<8;++w){ outSeq(notify); d->run(inst,NF); }
    printf("Tremont 15 (model 13)     rms_dBFS  peak_dBFS\n");
    const char* mname[3]={"Clean","Crunch","Lead"};
    for(int m=0;m<3;++m){
        setP(HF_AMP_MT_MODE, m==0?2.0f:0.0f);
        setP(HF_AMP_MT_MODE,(float)m);
        for(int s=0;s<4;++s){ outSeq(notify); d->run(inst,NF); }
        measure(mname[m]);
    }
    setP(HF_AMP_MT_MODE,1.0f);
    for(int s=0;s<4;++s){ outSeq(notify); d->run(inst,NF); }
    setP(HF_AMP_MT_BRIGHT,1.0f); measure("Crunch + Bright");

    printf("%s\n", bad? "FAILED (NaN/silent modes above)" : "ALL MODES PASS (finite, non-silent)");
    if(d->deactivate)d->deactivate(inst); if(d->cleanup)d->cleanup(inst); dlclose(h);
    return bad?1:0;
}
