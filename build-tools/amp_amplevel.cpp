// Measure each amp model's output level in the STANDALONE Amp plugin (guitaramp_amp)
// at identical noon settings, to level kModelMakeup for A/B parity (like hexforge_amplevel
// does for Hex Forge). Worker emulation so amp models actually load. No cab in this plugin.
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/atom/atom.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <map>

// amp_plugin.cpp AmpPorts enum (kept in sync by hand):
enum { P_IN_L=0,P_IN_R,P_OUT_L,P_OUT_R,P_MODEL,P_GAIN,P_BASS,P_MID,P_TREBLE,P_PRES,P_MASTER,P_SAG,
       P_CHANNEL,P_RESON,P_SUNN_V2,P_SUNN_LNK,P_BYPASS,P_PA_BYPASS,P_PA_TUBE,P_PA_PRES,P_PA_DEPTH,
       P_PA_SAG,P_PA_MASTER,P_PA_NFB,P_PA_RESON,P_PA_AIR,P_PA_AUTO,P_SUNN_B2,P_SUNN_M2,P_SUNN_T2,
       P_SUNN_BR1,P_SUNN_BR2,P_FR_CHANNEL,P_FR_FAT,P_FR_C45,P_FR_SAT,P_MV_MODE,P_MV_GEQ0,P_MV_GEQ1,
       P_MV_GEQ2,P_MV_GEQ3,P_MV_GEQ4,P_MV_EQPRESET,P_NAM_GAIN,P_NAM_VOL,P_PL_VOL2,
       P_RC_MODE,P_RC_VARIAC,P_RC_RECT,P_MT_MODE,P_MT_BRIGHT,P_CONTROL,P_NOTIFY,P_N_PORTS };

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
    const char* SO="/home/pistomp/guitar-amp-mod/build/guitaramp_amp.so";
    const char* BUNDLE="/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* URI="https://rpowell5064.github.io/guitaramp-suite/amp";
    const char* DI="/home/pistomp/di.f32";
    const double RATE=48000.0; const uint32_t NF=64;
    FILE* fd=fopen(DI,"rb"); std::vector<float> di;
    if(fd){fseek(fd,0,SEEK_END);long sz=ftell(fd);fseek(fd,0,SEEK_SET);di.resize(sz/4);fread(di.data(),4,di.size(),fd);fclose(fd);}
    if(di.empty()){fprintf(stderr,"no di\n");return 2;}
    void* h=dlopen(SO,RTLD_NOW|RTLD_LOCAL); if(!h){fprintf(stderr,"%s\n",dlerror());return 2;}
    auto descfn=(const LV2_Descriptor*(*)(uint32_t))dlsym(h,"lv2_descriptor");
    const LV2_Descriptor* d=nullptr; for(uint32_t i=0;;++i){auto x=descfn(i);if(!x)break;if(!strcmp(x->URI,URI)){d=x;break;}}
    if(!d){fprintf(stderr,"uri not found\n");return 2;}
    LV2_URID_Map map{nullptr,map_uri}; LV2_Worker_Schedule sched{nullptr,sched_work};
    LV2_Feature fmap{LV2_URID__map,&map}, fsched{LV2_WORKER__schedule,&sched}; const LV2_Feature* feats[]={&fmap,&fsched,nullptr};
    LV2_Handle inst=d->instantiate(d,RATE,BUNDLE,feats); if(!inst)return 3; g_inst=inst;
    g_worker=(const LV2_Worker_Interface*)(d->extension_data?d->extension_data(LV2_WORKER__interface):nullptr);

    std::vector<float> ainL(NF),ainR(NF),aoutL(NF),aoutR(NF), val(P_N_PORTS,0.0f);
    uint32_t seqURID=map_uri(nullptr,LV2_ATOM__Sequence);
    std::vector<uint8_t> ctl(8192,0),notify(65536,0);
    auto inSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=sizeof(LV2_Atom_Sequence_Body);s->atom.type=seqURID;s->body.unit=0;s->body.pad=0;};
    auto outSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=(uint32_t)(b.size()-sizeof(LV2_Atom));s->atom.type=seqURID;};
    inSeq(ctl);outSeq(notify);

    // Identical neutral setting for every amp.
    val[P_GAIN]=0.5f; val[P_BASS]=0.5f; val[P_MID]=0.5f; val[P_TREBLE]=0.5f; val[P_PRES]=0.5f;
    val[P_MASTER]=0.7f; val[P_SAG]=0.3f; val[P_CHANNEL]=0.0f; val[P_RESON]=0.5f; val[P_BYPASS]=0.0f;
    val[P_PA_BYPASS]=0.0f; val[P_PA_PRES]=0.5f; val[P_PA_DEPTH]=0.5f; val[P_PA_SAG]=0.3f;
    val[P_PA_MASTER]=0.7f; val[P_PA_NFB]=0.5f; val[P_PA_RESON]=0.5f; val[P_PA_AIR]=0.0f; val[P_PA_AUTO]=0.0f;
    val[P_MV_MODE]=6.0f; val[P_MV_GEQ0]=0.5f; val[P_MV_GEQ1]=0.5f; val[P_MV_GEQ2]=0.5f; val[P_MV_GEQ3]=0.5f; val[P_MV_GEQ4]=0.5f;

    for(int i=0;i<P_N_PORTS;++i){ void* p;
        if(i==P_IN_L)p=ainL.data(); else if(i==P_IN_R)p=ainR.data();
        else if(i==P_OUT_L)p=aoutL.data(); else if(i==P_OUT_R)p=aoutR.data();
        else if(i==P_CONTROL)p=ctl.data(); else if(i==P_NOTIFY)p=notify.data();
        else p=&val[i];
        d->connect_port(inst,i,p); }
    if(d->activate)d->activate(inst);

    auto runBlock=[&](size_t& pos){
        for(uint32_t k=0;k<NF;++k){ float s=di[pos%di.size()]; ainL[k]=s; ainR[k]=s; ++pos; }
        outSeq(notify); d->run(inst,NF);
        if(g_haveResp){ if(g_worker&&g_worker->work_response)g_worker->work_response(inst,(uint32_t)g_resp.size(),g_resp.data()); g_haveResp=false; if(g_worker&&g_worker->end_run)g_worker->end_run(inst); }
    };
    const char* names[14]={"Fender(Clean)","JCM800","EVH5150","Sunn","Rockerverb","NAM(skip)","Friedman","Hiwatt(Clean)","Vox(Clean)","Backline(Clean)","Plexi","CaliV","Recto","Tremont"};
    const bool isClean[12]={true,false,false,false,false,false,false,true,true,true,false,false};
    printf("amp model         rms_dBFS  peak_dBFS  (standalone Amp plugin, noon, no cab)\n");
    size_t pos=0;
    for(int m=0;m<14;++m){
        if(m==5) continue;
        val[P_MODEL]=(float)m;
        for(int s=0;s<400;++s) runBlock(pos);
        double sumsq=0,peak=0; uint64_t cnt=0;
        for(int s=0;s<3000;++s){ runBlock(pos);
            for(uint32_t k=0;k<NF;++k){ double v=aoutL[k]; sumsq+=v*v; double a=std::fabs(v); if(a>peak)peak=a; cnt++; } }
        double rms=std::sqrt(sumsq/cnt);
        printf("%-16s %8.2f %9.2f  %s\n", names[m], rms>1e-9?20*std::log10(rms):-120.0,
               peak>1e-9?20*std::log10(peak):-120.0, isClean[m]?"<- CLEAN":"");
    }
    if(d->cleanup)d->cleanup(inst);
    dlclose(h);
    return 0;
}
