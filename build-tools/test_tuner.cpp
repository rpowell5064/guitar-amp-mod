// Test the strobe-tuner DSP: feed pure sines at the open-string pitches, print detected note+cents.
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
#include <map>
#include <vector>

static std::map<std::string,uint32_t> g_uris;
static LV2_URID map_uri(LV2_URID_Map_Handle,const char* u){
    auto it=g_uris.find(u); if(it!=g_uris.end())return it->second;
    uint32_t id=(uint32_t)g_uris.size()+1; g_uris[u]=id; return id; }
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle,uint32_t,const void*){return LV2_WORKER_SUCCESS;}

int main(){
    const char* SO="/home/pistomp/guitar-amp-mod/build/guitaramp_hexforge.so";
    const char* BUNDLE="/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* URI="https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const double RATE=48000.0; const uint32_t NF=64;
    void* h=dlopen(SO,RTLD_NOW|RTLD_LOCAL); if(!h){fprintf(stderr,"dlopen %s\n",dlerror());return 2;}
    auto descfn=(const LV2_Descriptor*(*)(uint32_t))dlsym(h,"lv2_descriptor");
    const LV2_Descriptor* d=nullptr; for(uint32_t i=0;;++i){auto*x=descfn(i); if(!x)break; if(!strcmp(x->URI,URI)){d=x;break;}}
    if(!d){fprintf(stderr,"uri not found\n");return 2;}
    LV2_URID_Map map{nullptr,map_uri};
    LV2_Worker_Schedule sched{nullptr,sched_work};
    LV2_Feature fmap{LV2_URID__map,&map}, fsched{LV2_WORKER__schedule,&sched};
    const LV2_Feature* feats[]={&fmap,&fsched,nullptr};
    auto inst=d->instantiate(d,RATE,BUNDLE,feats); if(!inst){fprintf(stderr,"instantiate null\n");return 3;}

    std::vector<float> ain_l(NF),ain_r(NF),aout_l(NF),aout_r(NF), val(HF_N_PORTS,0.0f);
    uint32_t seqURID=map_uri(nullptr,LV2_ATOM__Sequence);
    std::vector<uint8_t> ctl(8192,0),midi(8192,0),notify(65536,0);
    auto inSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=sizeof(LV2_Atom_Sequence_Body);s->atom.type=seqURID;s->body.unit=0;s->body.pad=0;};
    auto outSeq=[&](std::vector<uint8_t>&b){auto*s=(LV2_Atom_Sequence*)b.data();s->atom.size=(uint32_t)(b.size()-sizeof(LV2_Atom));s->atom.type=seqURID;};
    inSeq(ctl);inSeq(midi);outSeq(notify);
    val[HF_TUNER_ON]=1.0f;
    for(int i=0;i<HF_N_PORTS;++i){ void* p;
        if(i==HF_IN_L)p=ain_l.data(); else if(i==HF_IN_R)p=ain_r.data();
        else if(i==HF_OUT_L)p=aout_l.data(); else if(i==HF_OUT_R)p=aout_r.data();
        else if(i==HF_CONTROL)p=ctl.data(); else if(i==HF_NOTIFY)p=notify.data();
        else if(i==HF_MIDI_IN)p=midi.data(); else p=&val[i];
        d->connect_port(inst,i,p); }
    if(d->activate)d->activate(inst);

    const char* NN[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    double freqs[]={82.41,110.0,146.83,196.0,246.94,329.63};
    const char* names[]={"E2","A2","D3","G3","B3","E4"};
    for(int f=0;f<6;++f){
        double phase=0;
        int blocks=(int)(0.6*RATE/NF);
        for(int b=0;b<blocks;++b){
            for(uint32_t k=0;k<NF;++k){ float s=(float)(0.3*sin(phase)); phase+=2*M_PI*freqs[f]/RATE; ain_l[k]=s; ain_r[k]=s; }
            outSeq(notify); d->run(inst,NF);
        }
        int note=(int)val[HF_TUNER_NOTE];
        printf("in %6.2f Hz (%s)  ->  note=%2d (%s)  cents=%+6.1f\n",
               freqs[f], names[f], note, (note>=0&&note<12)?NN[note]:"--", val[HF_TUNER_CENTS]);
    }
    return 0;
}
