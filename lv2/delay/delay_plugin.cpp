#include "lv2_util.h"
#include "DelayBlock.h"
#include "DelayFactory.h"
#include <new>

#define DELAY_URI "https://rpowell5064.github.io/guitaramp-suite/delay"

enum DelayPorts {
    P_IN_L    = 0,
    P_IN_R    = 1,
    P_OUT_L   = 2,
    P_OUT_R   = 3,
    P_TYPE    = 4,
    P_TIME    = 5,
    P_FB      = 6,
    P_MIX     = 7,
    P_WIDTH   = 8,
    P_WOW     = 9,
    P_FLUTTER = 10,
    P_BYPASS  = 11,
    P_HEADS   = 12,   // Echorec rotary program (0..11); ignored by Digital/Tape
    P_N_PORTS
};

// The 12 Echorec rotary programs -> playback-head bitmask.
// bit0=H1 (1/4) · bit1=H2 (1/2) · bit2=H3 (3/4) · bit3=H4 (full) · bit4=dense tap.
static const int kEchorecProgram[12] = {
    0x01, 0x02, 0x04, 0x08,   //  1- 4: single heads H1, H2, H3, H4
    0x03, 0x06, 0x0C,         //  5- 7: H1+H2, H2+H3, H3+H4
    0x07, 0x0E, 0x0D,         //  8-10: H1+H2+H3, H2+H3+H4, H1+H3+H4
    0x0F, 0x1F,               // 11-12: all heads, all heads + dense tap
};

struct DelayPlugin {
    DelayBlock dsp;
    float* ports[P_N_PORTS];
    int    lastType = -1;
};

static LV2_Handle delay_instantiate(const LV2_Descriptor*, double rate,
                                     const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) DelayPlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 2);
    return p;
}

static void delay_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<DelayPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void delay_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<DelayPlugin*>(h);
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);

    const int type = static_cast<int>(*p->ports[P_TYPE] + 0.5f);
    if (type != p->lastType) {
        p->lastType = type;
        p->dsp.setType(DelayFactory::fromIndex(type));
    }

    p->dsp.setParameter("timeMs",       *p->ports[P_TIME]);
    p->dsp.setParameter("feedback",     *p->ports[P_FB]);
    p->dsp.setParameter("mix",          *p->ports[P_MIX]);
    p->dsp.setParameter("stereoWidth",  *p->ports[P_WIDTH]);
    p->dsp.setParameter("wowDepth",     *p->ports[P_WOW]);
    p->dsp.setParameter("flutterDepth", *p->ports[P_FLUTTER]);

    int prog = static_cast<int>(*p->ports[P_HEADS] + 0.5f);
    prog = (prog < 0) ? 0 : (prog > 11) ? 11 : prog;
    p->dsp.setParameter("headMask", static_cast<float>(kEchorecProgram[prog]));

    float* ins[2]  = { p->ports[P_IN_L],  p->ports[P_IN_R]  };
    float* outs[2] = { p->ports[P_OUT_L], p->ports[P_OUT_R] };
    p->dsp.process(ins, outs, static_cast<int>(n), 2);
}

static void delay_cleanup(LV2_Handle h) { delete static_cast<DelayPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(DELAY_URI,
    delay_instantiate, delay_connect_port,
    nullptr, delay_run, nullptr, delay_cleanup, nullptr)
