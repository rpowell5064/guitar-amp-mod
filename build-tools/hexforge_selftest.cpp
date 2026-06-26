// Minimal worker-capable LV2 host to verify Hex Forge instantiates and seeds its
// factory presets. Loads the .so, connects ports, pulses ps_backup (rising edge ->
// hfWriteBackup), which persists the in-memory preset store to the .dat.
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/atom/atom.h>
#include "hexforge_ports.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>

static std::map<std::string,uint32_t> g_uris;
static LV2_URID map_uri(LV2_URID_Map_Handle, const char* uri){
    auto it=g_uris.find(uri); if(it!=g_uris.end()) return it->second;
    uint32_t id=(uint32_t)g_uris.size()+1; g_uris[uri]=id; return id;
}
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle,uint32_t,const void*){
    return LV2_WORKER_SUCCESS;   // no work is scheduled for preset seeding
}

int main(){
    const char* BUNDLE="/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* SO="/home/pistomp/.lv2/guitaramp-suite.lv2/guitaramp_hexforge.so";
    const char* URI="https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const double RATE=48000.0; const uint32_t NF=64;

    void* h=dlopen(SO, RTLD_NOW|RTLD_LOCAL);
    if(!h){ fprintf(stderr,"dlopen FAIL: %s\n",dlerror()); return 2; }
    auto descfn=(const LV2_Descriptor*(*)(uint32_t))dlsym(h,"lv2_descriptor");
    if(!descfn){ fprintf(stderr,"no lv2_descriptor\n"); return 2; }
    const LV2_Descriptor* d=nullptr;
    for(uint32_t i=0;;++i){ const LV2_Descriptor* x=descfn(i); if(!x) break; if(!strcmp(x->URI,URI)){ d=x; break; } }
    if(!d){ fprintf(stderr,"hexforge URI not found in .so\n"); return 2; }

    LV2_URID_Map map{nullptr,map_uri};
    LV2_Worker_Schedule sched{nullptr,sched_work};
    LV2_Feature fmap{LV2_URID__map,&map};
    LV2_Feature fsched{LV2_WORKER__schedule,&sched};
    const LV2_Feature* feats[]={&fmap,&fsched,nullptr};

    LV2_Handle inst=d->instantiate(d,RATE,BUNDLE,feats);
    if(!inst){ fprintf(stderr,"INSTANTIATE FAILED (null)\n"); return 3; }
    printf("instantiate OK\n");

    // buffers
    std::vector<float> ain_l(NF,0),ain_r(NF,0),aout_l(NF,0),aout_r(NF,0);
    std::vector<float> val(HF_N_PORTS,0.0f);
    const uint32_t SEQ=(uint32_t)g_uris.size(); // placeholder
    uint32_t seqURID=map_uri(nullptr,LV2_ATOM__Sequence);
    std::vector<uint8_t> ctlbuf(8192,0), midibuf(8192,0), notifybuf(65536,0);
    auto mkEmptyIn=[&](std::vector<uint8_t>& b){ auto* s=(LV2_Atom_Sequence*)b.data();
        s->atom.size=sizeof(LV2_Atom_Sequence_Body); s->atom.type=seqURID; s->body.unit=0; s->body.pad=0; };
    auto mkOut=[&](std::vector<uint8_t>& b){ auto* s=(LV2_Atom_Sequence*)b.data();
        s->atom.size=(uint32_t)(b.size()-sizeof(LV2_Atom)); s->atom.type=seqURID; };
    mkEmptyIn(ctlbuf); mkEmptyIn(midibuf); mkOut(notifybuf);

    val[HF_PS_GOTO]=-1.0f;          // idle (don't trigger a goto-recall)
    val[HF_PS_BACKUP]=1.0f;         // held high -> rising edge on first run -> hfWriteBackup

    for(int i=0;i<HF_N_PORTS;++i){
        void* p=nullptr;
        if(i==HF_IN_L)p=ain_l.data(); else if(i==HF_IN_R)p=ain_r.data();
        else if(i==HF_OUT_L)p=aout_l.data(); else if(i==HF_OUT_R)p=aout_r.data();
        else if(i==HF_CONTROL)p=ctlbuf.data(); else if(i==HF_NOTIFY)p=notifybuf.data();
        else if(i==HF_MIDI_IN)p=midibuf.data(); else p=&val[i];
        d->connect_port(inst,i,p);
    }
    if(d->activate) d->activate(inst);
    d->run(inst,NF);     // first block: ps_backup rising edge -> writes the .dat
    mkOut(notifybuf);    // reset out seq, run once more for good measure
    d->run(inst,NF);
    if(d->deactivate) d->deactivate(inst);
    if(d->cleanup) d->cleanup(inst);
    dlclose(h);
    printf("ran OK, backup pulsed\n");
    return 0;
}
