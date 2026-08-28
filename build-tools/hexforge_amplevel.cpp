// Measure each amp model's output level at IDENTICAL settings (gain/master/EQ all at
// the same spot, cab on, no other blocks), to quantify how much quieter the clean
// models are vs the distorted ones. Worker emulation so amp models actually load.
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
#include <algorithm>

static std::map<std::string,uint32_t> g_uris;
static LV2_URID map_uri(LV2_URID_Map_Handle,const char* u){
    auto it=g_uris.find(u); if(it!=g_uris.end())return it->second;
    uint32_t id=(uint32_t)g_uris.size()+1; g_uris[u]=id; return id; }
static const LV2_Worker_Interface* g_worker=nullptr; static LV2_Handle g_inst=nullptr;
static std::vector<uint8_t> g_resp; static bool g_haveResp=false;
static LV2_Worker_Status do_respond(LV2_Worker_Respond_Handle,uint32_t size,const void* data){
    g_resp.assign((const uint8_t*)data,(const uint8_t*)data+size); g_haveResp=true; return LV2_WORKER_SUCCESS; }
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle,uint32_t size,const void* data){
    if(g_worker&&g_worker->work) g_worker->work(g_inst,do_respond,nullptr,size,data); return LV2_WORKER_SUCCESS; }

int main(){
    const char* SO="/home/pistomp/guitar-amp-mod/build/guitaramp_hexforge.so";
    const char* BUNDLE="/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* URI="https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const char* DI="/home/pistomp/di.f32";
    const double RATE=48000.0; const uint32_t NF=64;
    FILE* fd=fopen(DI,"rb"); std::vector<float> di;
    if(fd){fseek(fd,0,SEEK_END);long sz=ftell(fd);fseek(fd,0,SEEK_SET);di.resize(sz/4);fread(di.data(),4,di.size(),fd);fclose(fd);}
    if(di.empty()){fprintf(stderr,"no di\n");return 2;}
    void* h=dlopen(SO,RTLD_NOW|RTLD_LOCAL); if(!h){fprintf(stderr,"%s\n",dlerror());return 2;}
    auto descfn=(const LV2_Descriptor*(*)(uint32_t))dlsym(h,"lv2_descriptor");
    const LV2_Descriptor* d=nullptr; for(uint32_t i=0;;++i){auto x=descfn(i);if(!x)break;if(!strcmp(x->URI,URI)){d=x;break;}}
    if(!d)return 2;
    LV2_URID_Map map{nullptr,map_uri}; LV2_Worker_Schedule sched{nullptr,sched_work};
    LV2_Feature fmap{LV2_URID__map,&map}, fsched{LV2_WORKER__schedule,&sched}; const LV2_Feature* feats[]={&fmap,&fsched,nullptr};
    LV2_Handle inst=d->instantiate(d,RATE,BUNDLE,feats); if(!inst)return 3; g_inst=inst;
    g_worker=(const LV2_Worker_Interface*)(d->extension_data?d->extension_data(LV2_WORKER__interface):nullptr);

    std::vector<float> ainL(NF),ainR(NF),aoutL(NF),aoutR(NF), val(HF_N_PORTS,0.0f);
    uint32_t seqURID=map_uri(nullptr,LV2_ATOM__Sequence);
    std::vector<uint8_t> ctl(8192,0),midi(8192,0),notify(65536,0);
    auto inSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=sizeof(LV2_Atom_Sequence_Body);s->atom.type=seqURID;s->body.unit=0;s->body.pad=0;};
    auto outSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=(uint32_t)(b.size()-sizeof(LV2_Atom));s->atom.type=seqURID;};
    inSeq(ctl);inSeq(midi);outSeq(notify);

    // Identical, neutral setting for every amp; only amp+cab in the chain.
    val[HF_BYPASS]=0; val[HF_OUT_AUTO]=0; val[HF_OUT_LEVEL]=-20; val[HF_PS_GOTO]=-1;
    val[HF_IT_ENABLE]=1; val[HF_AMP_ENABLE]=1; val[HF_CAB_ENABLE]=1;
    int off[]={HF_GT_ENABLE,HF_CP_ENABLE,HF_FZ_ENABLE,HF_DR_ENABLE,HF_MD_ENABLE,HF_DL_ENABLE,HF_RV_ENABLE,HF_WH_ENABLE,HF_OC_ENABLE};
    for(int e:off) val[e]=0.0f;
    val[HF_AMP_GAIN]=0.5f; val[HF_AMP_BASS]=0.5f; val[HF_AMP_MID]=0.5f; val[HF_AMP_TREBLE]=0.5f;
    val[HF_AMP_PRESENCE]=0.5f; val[HF_AMP_MASTER]=0.7f; val[HF_AMP_SAG]=0.3f;

    for(int i=0;i<HF_N_PORTS;++i){ void* p;
        if(i==HF_IN_L)p=ainL.data(); else if(i==HF_IN_R)p=ainR.data();
        else if(i==HF_OUT_L)p=aoutL.data(); else if(i==HF_OUT_R)p=aoutR.data();
        else if(i==HF_CONTROL)p=ctl.data(); else if(i==HF_NOTIFY)p=notify.data();
        else if(i==HF_MIDI_IN)p=midi.data(); else p=&val[i];
        d->connect_port(inst,i,p); }
    if(d->activate)d->activate(inst);

    auto runBlock=[&](size_t& pos){
        for(uint32_t k=0;k<NF;++k){ float s=di[pos%di.size()]; ainL[k]=s; ainR[k]=s; ++pos; }
        outSeq(notify); d->run(inst,NF);
        if(g_haveResp){ if(g_worker&&g_worker->work_response)g_worker->work_response(inst,(uint32_t)g_resp.size(),g_resp.data()); g_haveResp=false; if(g_worker&&g_worker->end_run)g_worker->end_run(inst); }
    };
    const char* names[15]={"Fender(Clean)","JCM800","EVH5150","Sunn","Rockerverb","NAM(skip)","Friedman","Hiwatt(Clean)","Vox(Clean)","Backline(Clean)","Plexi","CaliV","Recto","Tremont","BlueLiner(Bass)"};
    const bool isClean[12]={true,false,false,false,false,false,false,true,true,true,false,false};
    printf("amp model         rms_dBFS  peak_dBFS  (gain/master/EQ all at noon, cab on)\n");
    size_t pos=0;
    for(int m=0;m<15;++m){
        if(m==5) continue;
        val[HF_AMP_MODEL]=(float)m;
        for(int s=0;s<400;++s) runBlock(pos);        // settle + rebuild
        double sumsq=0,peak=0; uint64_t cnt=0;
        for(int s=0;s<3000;++s){ size_t p0=pos; runBlock(pos);
            for(uint32_t k=0;k<NF;++k){ double v=aoutL[k]; sumsq+=v*v; double a=std::fabs(v); if(a>peak)peak=a; cnt++; } (void)p0; }
        double rms=std::sqrt(sumsq/cnt);
        printf("%-16s %8.2f %9.2f  %s\n", names[m], rms>1e-9?20*std::log10(rms):-120.0,
               peak>1e-9?20*std::log10(peak):-120.0, isClean[m]?"<- CLEAN":"");
    }
    if(d->cleanup)d->cleanup(inst);
    dlclose(h);
    return 0;
}
