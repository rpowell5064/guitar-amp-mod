#include "lv2_util.h"
#include "OverdriveBlock.h"
#include "OverdriveFactory.h"
#include <new>

#define DRIVE_URI "https://rpowell5064.github.io/guitaramp-suite/drive"

// LV2 port model indices: 0=TS808, 1=LifePedal, 2=ProcoRAT
// Maps to OverdriveType: 0, 1, 3 (NAM=2 is skipped in LV2)
static const OverdriveType kModelMap[3] = {
    OverdriveType::TubeScreamer808,
    OverdriveType::LifePedal,
    OverdriveType::ProcoRAT,
};

enum DrivePorts {
    P_IN     = 0,
    P_OUT    = 1,
    P_MODEL  = 2,
    P_DRIVE  = 3,
    P_TONE   = 4,
    P_LEVEL  = 5,
    P_MIX    = 6,
    P_OCTAVE = 7,
    P_BYPASS = 8,
    P_N_PORTS
};

struct DrivePlugin {
    OverdriveBlock dsp;
    float* ports[P_N_PORTS];
    int    lastModel = -1;
};

static LV2_Handle drive_instantiate(const LV2_Descriptor*, double rate,
                                     const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) DrivePlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 1);
    p->dsp.setType(OverdriveType::TubeScreamer808);
    return p;
}

static void drive_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<DrivePlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void drive_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<DrivePlugin*>(h);
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);

    // Model switch — remap LV2 index [0,2] to OverdriveType
    const int modelIdx = static_cast<int>(*p->ports[P_MODEL] + 0.5f);
    const int clamped  = (modelIdx < 0) ? 0 : (modelIdx > 2) ? 2 : modelIdx;
    if (clamped != p->lastModel) {
        p->lastModel = clamped;
        p->dsp.setType(kModelMap[clamped]);
    }

    p->dsp.setParameter("drive",  *p->ports[P_DRIVE]);
    p->dsp.setParameter("tone",   *p->ports[P_TONE]);
    p->dsp.setParameter("level",  *p->ports[P_LEVEL]);
    p->dsp.setParameter("mix",    *p->ports[P_MIX]);
    p->dsp.setParameter("octave", *p->ports[P_OCTAVE]);

    float* ins[1]  = { p->ports[P_IN]  };
    float* outs[1] = { p->ports[P_OUT] };
    p->dsp.process(ins, outs, static_cast<int>(n), 1);
}

static void drive_cleanup(LV2_Handle h) { delete static_cast<DrivePlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(DRIVE_URI,
    drive_instantiate, drive_connect_port,
    nullptr, drive_run, nullptr, drive_cleanup, nullptr)
