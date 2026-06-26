// Loudness measurement host: instantiate Hex Forge, recall each preset (ps_goto),
// run a DI test signal through it with the output auto-limiter OFF, and report
// output RMS + peak in dBFS. Used to set per-preset out_level for loudness parity.
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
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle,uint32_t,const void*){return LV2_WORKER_SUCCESS;}

int main(int argc,char** argv){
    const char* SO="/home/pistomp/guitar-amp-mod/build/guitaramp_hexforge.so";
    const char* BUNDLE="/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* URI="https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const char* DI="/home/pistomp/di.f32";
    const double RATE=48000.0; const uint32_t NF=64;

    // load DI
    FILE* fd=fopen(DI,"rb"); if(!fd){fprintf(stderr,"no di\n");return 2;}
    fseek(fd,0,SEEK_END); long sz=ftell(fd); fseek(fd,0,SEEK_SET);
    std::vector<float> di(sz/4); fread(di.data(),4,di.size(),fd); fclose(fd);

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

    std::vector<float> ain_l(NF),ain_r(NF),aout_l(NF),aout_r(NF), val(HF_N_PORTS,0.0f);
    uint32_t seqURID=map_uri(nullptr,LV2_ATOM__Sequence);
    std::vector<uint8_t> ctl(8192,0),midi(8192,0),notify(65536,0);
    auto inSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=sizeof(LV2_Atom_Sequence_Body);s->atom.type=seqURID;s->body.unit=0;s->body.pad=0;};
    auto outSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=(uint32_t)(b.size()-sizeof(LV2_Atom));s->atom.type=seqURID;};
    inSeq(ctl);inSeq(midi);outSeq(notify);

    val[HF_OUT_AUTO]=0.0f;   // disable peak limiter -> linear measurement
    val[HF_PS_GOTO]=-1.0f; val[HF_PS_BACKUP]=0.0f;
    for(int i=0;i<HF_N_PORTS;++i){ void* p;
        if(i==HF_IN_L)p=ain_l.data(); else if(i==HF_IN_R)p=ain_r.data();
        else if(i==HF_OUT_L)p=aout_l.data(); else if(i==HF_OUT_R)p=aout_r.data();
        else if(i==HF_CONTROL)p=ctl.data(); else if(i==HF_NOTIFY)p=notify.data();
        else if(i==HF_MIDI_IN)p=midi.data(); else p=&val[i];
        d->connect_port(inst,i,p); }
    if(d->activate)d->activate(inst);

    const uint32_t skip=(uint32_t)(0.3*RATE), endcut=(uint32_t)(0.2*RATE);
    printf("idx  rms_dBFS  peak_dBFS\n");
    for(int a=1;a<argc;++a){
        int idx=atoi(argv[a]);
        // Force a FIXED -20 dB output level for every preset so the measurement is
        // independent of each preset's stored out_level. Recall with the host out_level
        // port at 0 (so lastPort=0 != -20), then set it to -20 -> the plugin's override
        // layer folds the "knob move" into eff[out_level].
        val[HF_OUT_LEVEL]=0.0f; val[HF_PS_GOTO]=(float)idx;
        outSeq(notify); d->run(inst,NF);           // recall preset
        val[HF_OUT_LEVEL]=-20.0f;
        outSeq(notify); d->run(inst,NF);           // fold out_level=-20 into eff
        outSeq(notify); d->run(inst,NF);           // settle
        // measure over the DI
        double sumsq=0,peak=0; uint64_t cnt=0; size_t pos=0; uint64_t gi=0;
        while(pos<di.size()){
            uint32_t n=(uint32_t)std::min((size_t)NF,di.size()-pos);
            for(uint32_t k=0;k<NF;++k){ float s=(k<n)?di[pos+k]:0.0f; ain_l[k]=s; ain_r[k]=s; }
            outSeq(notify); d->run(inst,NF);
            for(uint32_t k=0;k<n;++k){ if(gi>=skip && gi<di.size()-endcut){ double v=aout_l[k]; sumsq+=v*v; double av=std::fabs(v); if(av>peak)peak=av; cnt++; } gi++; }
            pos+=n;
        }
        double rms=(cnt? std::sqrt(sumsq/cnt):0.0);
        double rdb=(rms>1e-9?20*std::log10(rms):-120.0);
        double pdb=(peak>1e-9?20*std::log10(peak):-120.0);
        printf("%-4d %8.2f %9.2f\n", idx, rdb, pdb);
        val[HF_PS_GOTO]=-1.0f; outSeq(notify); d->run(inst,NF);   // idle between presets
    }
    if(d->deactivate)d->deactivate(inst);
    if(d->cleanup)d->cleanup(inst);
    dlclose(h);
    return 0;
}
