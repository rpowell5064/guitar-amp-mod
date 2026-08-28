// Worst-case CPU benchmark for Hex Forge: every block enabled (gate→comp→fuzz→drive
// →amp→cab→mod→delay→reverb→wah→octave), swept across all algorithmic amp models,
// timing d->run() at a 64-frame block. Reports mean/max microseconds per block and the
// % of each candidate JACK period deadline, so we can pick the lowest buffer that runs
// clean with margin. A synchronous LV2-worker emulation lets amp models actually load.
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/atom/atom.h>
#include "hexforge_ports.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

static std::map<std::string,uint32_t> g_uris;
static LV2_URID map_uri(LV2_URID_Map_Handle,const char* u){
    auto it=g_uris.find(u); if(it!=g_uris.end())return it->second;
    uint32_t id=(uint32_t)g_uris.size()+1; g_uris[u]=id; return id; }

static const LV2_Worker_Interface* g_worker=nullptr;
static LV2_Handle g_inst=nullptr;
static std::vector<uint8_t> g_resp; static bool g_haveResp=false;
static LV2_Worker_Status do_respond(LV2_Worker_Respond_Handle,uint32_t size,const void* data){
    g_resp.assign((const uint8_t*)data,(const uint8_t*)data+size); g_haveResp=true; return LV2_WORKER_SUCCESS; }
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle,uint32_t size,const void* data){
    if(g_worker&&g_worker->work) g_worker->work(g_inst,do_respond,nullptr,size,data);
    return LV2_WORKER_SUCCESS; }

static double now_us(){ timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e6 + t.tv_nsec/1e3; }

int main(int argc, char** argv){
    const char* SO="/home/pistomp/guitar-amp-mod/build/guitaramp_hexforge.so";
    const char* BUNDLE="/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* URI="https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const char* DI="/home/pistomp/di.f32";
    const double RATE=48000.0;
    // Any all-digits argv = the JACK period to bench (default 64); "eco" still
    // selects Engine Quality Eco. e.g. `hexforge_bench 32` or `hexforge_bench eco 16`.
    uint32_t NF=64;
    for(int a=1;a<argc;++a){ char* e=nullptr; long v=strtol(argv[a],&e,10);
        if(e&&*e=='\0'&&v>=16&&v<=1024) NF=(uint32_t)v; }

    FILE* fd=fopen(DI,"rb"); std::vector<float> di;
    if(fd){ fseek(fd,0,SEEK_END); long sz=ftell(fd); fseek(fd,0,SEEK_SET); di.resize(sz/4); fread(di.data(),4,di.size(),fd); fclose(fd);}
    if(di.empty()) di.assign(48000,0.05f);

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
    g_inst=inst;
    g_worker=(const LV2_Worker_Interface*)(d->extension_data?d->extension_data(LV2_WORKER__interface):nullptr);

    std::vector<float> ainL(NF),ainR(NF),aoutL(NF),aoutR(NF), val(HF_N_PORTS,0.0f);
    uint32_t seqURID=map_uri(nullptr,LV2_ATOM__Sequence);
    std::vector<uint8_t> ctl(8192,0),midi(8192,0),notify(65536,0);
    auto inSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=sizeof(LV2_Atom_Sequence_Body);s->atom.type=seqURID;s->body.unit=0;s->body.pad=0;};
    auto outSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=(uint32_t)(b.size()-sizeof(LV2_Atom));s->atom.type=seqURID;};
    inSeq(ctl);inSeq(midi);outSeq(notify);

    // Worst-case: every block enabled + driven.
    // argv[1] == "eco" benches Engine Quality Eco (2x amp OS). NOTE: set AFTER
    // the first runs (below) so the change-watch fires and the deferred amp swap
    // actually lands -- pre-setting the port before run #1 never triggers the
    // swap (the ps_goto lesson, port-init edition).
    bool wantEco=false;
    for(int a=1;a<argc;++a) if(std::string(argv[a])=="eco") wantEco=true;
    val[HF_BYPASS]=0; val[HF_OUT_AUTO]=1; val[HF_OUT_LEVEL]=-18; val[HF_PS_GOTO]=-1;
    val[HF_IT_ENABLE]=1; val[HF_IT_HUM]=1; val[HF_IT_HUMBK]=1; val[HF_IT_BOOST]=1;
    int ens[]={HF_GT_ENABLE,HF_CP_ENABLE,HF_FZ_ENABLE,HF_DR_ENABLE,HF_AMP_ENABLE,HF_CAB_ENABLE,HF_MD_ENABLE,HF_DL_ENABLE,HF_RV_ENABLE,HF_WH_ENABLE,HF_OC_ENABLE};
    for(int e:ens) val[e]=1.0f;
    val[HF_FZ_SUSTAIN]=0.7f; val[HF_FZ_VOLUME]=0.6f;
    val[HF_DR_DRIVE]=0.4f; val[HF_DR_LEVEL]=0.6f; val[HF_DR_MIX]=1.0f;
    val[HF_AMP_GAIN]=0.7f; val[HF_AMP_MASTER]=0.6f;
    val[HF_MD_MIX]=0.5f; val[HF_DL_MIX]=0.3f; val[HF_RV_MIX]=0.3f;
    val[HF_WH_MIX]=0.8f; val[HF_OC_UP]=0.5f; val[HF_OC_DOWN]=0.5f;

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
        if(g_haveResp){ if(g_worker&&g_worker->work_response) g_worker->work_response(inst,(uint32_t)g_resp.size(),g_resp.data()); g_haveResp=false; if(g_worker&&g_worker->end_run) g_worker->end_run(inst); }
    };

    const char* names[15]={"Fender","JCM800","EVH5150","Sunn","Rockerverb","NAM(skip)","Friedman","Hiwatt",
                           "Vox","Backline","Plexi","CaliV","Recto","Tremont","BlueLiner"};
    printf("bench period: %u frames (deadline %.0f us)\n", NF, NF/RATE*1e6);
    printf("amp model        mean_us   max_us   load@NF  max@NF  load@128\n");
    size_t pos=0;
    for(int s=0;s<20;++s) runBlock(pos);            // prime the watch state at Standard
    if(wantEco){ val[HF_QUALITY]=1.0f; for(int s=0;s<200;++s) runBlock(pos); }  // change-event -> ramp -> swap
    for(int m=0;m<15;++m){
        if(m==5) continue;                          // NAM = user file, skip
        val[HF_AMP_MODEL]=(float)m;
        for(int s=0;s<200;++s) runBlock(pos);       // settle + rebuild amp via worker
        double sum=0,mx=0; const int N=2000;
        for(int s=0;s<N;++s){ double t0=now_us(); runBlock(pos); double dt=now_us()-t0; sum+=dt; if(dt>mx)mx=dt; }
        double mean=sum/N;
        double dNF=NF/RATE*1e6, d128=128/RATE*1e6;
        printf("%-14s %9.1f %8.1f  %6.0f%% %7.0f%% %7.0f%%\n", names[m], mean, mx,
               100*mean/dNF, 100*mx/dNF, 100*mean/d128);
    }
    if(d->deactivate)d->deactivate(inst);
    if(d->cleanup)d->cleanup(inst);
    dlclose(h);
    return 0;
}
