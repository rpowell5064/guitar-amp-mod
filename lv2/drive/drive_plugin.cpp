// ─────────────────────────────────────────────────────────────────────────────
// GuitarAmp Suite — Drive/Boost LV2 plugin
//
// Three algorithmic pedals (Green Man / New Dawn / Dear Rodent Boy) plus a NAM
// slot: model 3 ("Neural (NAM)") runs a user-loaded .nam pedal capture. The .nam
// is chosen in mod-ui's file browser, loaded on the LV2 worker thread, and
// swapped in with a lock-free pointer assignment. The bundled core parses legacy
// (a1) and new container/slimmable (a2) formats. Level + Mix apply to the NAM
// (drive/tone/octave are baked into the capture).
// ─────────────────────────────────────────────────────────────────────────────
#include "lv2_util.h"
#include "OverdriveBlock.h"
#include "OverdriveFactory.h"
#include "NamModel.h"
#include "DenormalGuard.h"
#include <new>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>

#define DRIVE_URI     "https://rpowell5064.github.io/guitaramp-suite/drive"
#define DRIVE_NAM_URI DRIVE_URI "#nammodel"
static constexpr int kPathMax  = 1024;
static constexpr int kMaxBlock = 512;

// LV2 model indices: 0=Green Man(TS808), 1=New Dawn(LifePedal), 2=Dear Rodent Boy(RAT)
static const OverdriveType kModelMap[10] = {
    OverdriveType::TubeScreamer808,     // 0 = Green Man
    OverdriveType::LifePedal,           // 1 = New Dawn
    OverdriveType::ProcoRAT,            // 2 = Dear Rodent Boy
    OverdriveType::ProcoRAT,            // 3 = NAM placeholder (handled separately, never built here)
    OverdriveType::DS1,                 // 4 = Grunge DS (Boss DS-1)
    OverdriveType::Klon,                // 5 = Gilded Horse (Klon)
    OverdriveType::SuperOverdriveSD1,   // 6 = Super Nova (Boss SD-1)
    OverdriveType::DOD250,              // 7 = Preamp 250 (DOD Overdrive Preamp 250)
    OverdriveType::EchoplexPreamp,      // 8 = Echo Primer (Echoplex EP-3 JFET preamp)
    OverdriveType::TubeDriver,          // 9 = Tube Chauffeur (Butler Tube Driver)
};
static constexpr int kNamIdx   = 3;   // Neural (NAM) slot
static constexpr int kMaxModel = 9;   // highest selectable model index (Echo Primer)

enum DrivePorts {
    P_IN = 0, P_OUT, P_MODEL, P_DRIVE, P_TONE, P_LEVEL, P_MIX, P_OCTAVE, P_BYPASS,
    P_NAM_GAIN, P_NAM_VOL,     // Neural (NAM): input drive + output level (dB), used only in NAM mode
    P_CONTROL, P_NOTIFY,       // atom in/out — MUST be last: mod-host breaks if control ports follow them
    P_N_PORTS
};

enum WorkType { WORK_NAM_LOAD, WORK_NAM_FREE };
struct WorkMsg { WorkType type; NamModel* nam = nullptr; char path[kPathMax] = {0}; };

struct URIs {
    LV2_URID atom_Object, atom_Path, atom_URID;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID nam_file;
};

static int clampi(float v, int lo, int hi) { int i = int(v + 0.5f); return i < lo ? lo : (i > hi ? hi : i); }

struct DrivePlugin {
    double rate = 48000.0;
    OverdriveBlock dsp;
    NamModel* nam = nullptr;

    float* ports[P_N_PORTS] = {};
    const LV2_Atom_Sequence* control = nullptr;
    LV2_Atom_Sequence*       notify  = nullptr;

    int  lastModel = -1;
    char namPath[kPathMax] = {0};
    float namIn[kMaxBlock], namOut[kMaxBlock];

    LV2_Worker_Schedule* schedule = nullptr;
    LV2_URID_Map*        map      = nullptr;
    LV2_Atom_Forge       forge;
    URIs                 uris;
};

static void mapURIs(DrivePlugin* p) {
    LV2_URID_Map* m = p->map;
    p->uris.atom_Object    = m->map(m->handle, LV2_ATOM__Object);
    p->uris.atom_Path      = m->map(m->handle, LV2_ATOM__Path);
    p->uris.atom_URID      = m->map(m->handle, LV2_ATOM__URID);
    p->uris.patch_Set      = m->map(m->handle, LV2_PATCH__Set);
    p->uris.patch_Get      = m->map(m->handle, LV2_PATCH__Get);
    p->uris.patch_property = m->map(m->handle, LV2_PATCH__property);
    p->uris.patch_value    = m->map(m->handle, LV2_PATCH__value);
    p->uris.nam_file       = m->map(m->handle, DRIVE_NAM_URI);
}
static void writeNamToNotify(DrivePlugin* p) {
    const URIs& u = p->uris;
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&p->forge, 0);
    lv2_atom_forge_object(&p->forge, &frame, 0, u.patch_Set);
    lv2_atom_forge_key(&p->forge, u.patch_property);
    lv2_atom_forge_urid(&p->forge, u.nam_file);
    lv2_atom_forge_key(&p->forge, u.patch_value);
    lv2_atom_forge_path(&p->forge, p->namPath, static_cast<uint32_t>(std::strlen(p->namPath)));
    lv2_atom_forge_pop(&p->forge, &frame);
}

static LV2_Handle drive_instantiate(const LV2_Descriptor*, double rate,
                                    const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) DrivePlugin;
    if (!p) return nullptr;
    p->schedule = static_cast<LV2_Worker_Schedule*>(lv2_find_feature(features, LV2_WORKER__schedule));
    p->map      = static_cast<LV2_URID_Map*>(lv2_find_feature(features, LV2_URID__map));
    if (!p->schedule || !p->map) { delete p; return nullptr; }
    mapURIs(p);
    lv2_atom_forge_init(&p->forge, p->map);
    p->rate = rate;
    p->dsp.prepare(rate, kMaxBlock, 1);
    p->dsp.setType(OverdriveType::TubeScreamer808);
    return p;
}

static void drive_connect_port(LV2_Handle h, uint32_t port, void* data) {
    auto* p = static_cast<DrivePlugin*>(h);
    if (port == P_CONTROL)     p->control = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == P_NOTIFY) p->notify  = static_cast<LV2_Atom_Sequence*>(data);
    else if (port < P_N_PORTS) p->ports[port] = static_cast<float*>(data);
}

static LV2_Worker_Status drive_work(LV2_Handle h, LV2_Worker_Respond_Function respond,
                                    LV2_Worker_Respond_Handle handle, uint32_t, const void* data) {
    auto* p = static_cast<DrivePlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == WORK_NAM_FREE) { delete msg->nam; return LV2_WORKER_SUCCESS; }
    auto* nm = new(std::nothrow) NamModel;
    if (!nm) return LV2_WORKER_ERR_NO_SPACE;
    if (nm->loadFromFile(msg->path)) nm->reset(p->rate, kMaxBlock);
    WorkMsg reply; reply.type = WORK_NAM_LOAD; reply.nam = nm;
    respond(handle, sizeof(reply), &reply);
    return LV2_WORKER_SUCCESS;
}
static LV2_Worker_Status drive_work_response(LV2_Handle h, uint32_t, const void* data) {
    auto* p = static_cast<DrivePlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    NamModel* old = p->nam;
    p->nam = msg->nam;
    WorkMsg freeMsg; freeMsg.type = WORK_NAM_FREE; freeMsg.nam = old;
    p->schedule->schedule_work(p->schedule->handle, sizeof(freeMsg), &freeMsg);
    return LV2_WORKER_SUCCESS;
}

static void drive_run(LV2_Handle h, uint32_t n) {
    DenormalGuard denormalGuard;   // flush denormals (NAM state can spike CPU in decay/silence)
    auto* p = static_cast<DrivePlugin*>(h);
    const URIs& u = p->uris;

    const bool haveNotify = (p->notify != nullptr);
    LV2_Atom_Forge_Frame seqFrame;
    if (haveNotify) {
        lv2_atom_forge_set_buffer(&p->forge, reinterpret_cast<uint8_t*>(p->notify), p->notify->atom.size);
        lv2_atom_forge_sequence_head(&p->forge, &seqFrame, 0);
    }
    if (p->control) {
        LV2_ATOM_SEQUENCE_FOREACH(p->control, ev) {
            if (ev->body.type != u.atom_Object) continue;
            const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(&ev->body);
            if (obj->body.otype == u.patch_Set) {
                const LV2_Atom *prop = nullptr, *val = nullptr;
                lv2_atom_object_get(obj, u.patch_property, &prop, u.patch_value, &val, 0);
                if (prop && prop->type == u.atom_URID &&
                    reinterpret_cast<const LV2_Atom_URID*>(prop)->body == u.nam_file &&
                    val && val->type == u.atom_Path) {
                    const char* path = static_cast<const char*>(LV2_ATOM_BODY_CONST(val));
                    std::strncpy(p->namPath, path, kPathMax - 1); p->namPath[kPathMax - 1] = '\0';
                    WorkMsg msg; msg.type = WORK_NAM_LOAD;
                    std::strncpy(msg.path, p->namPath, kPathMax - 1); msg.path[kPathMax - 1] = '\0';
                    p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
                }
            } else if (obj->body.otype == u.patch_Get && haveNotify) {
                writeNamToNotify(p);
            }
        }
    }

    const float* in  = p->ports[P_IN];
    float*       out = p->ports[P_OUT];
    const int    model = clampi(*p->ports[P_MODEL], 0, kMaxModel);

    if (*p->ports[P_BYPASS] > 0.5f) {
        if (out != in) std::memcpy(out, in, sizeof(float) * n);
        if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
        return;
    }

    if (model == kNamIdx) {
        // ── Neural (NAM) path ── mono; NAM Gain drives the capture (input trim),
        // NAM Level trims the output (both dB); Mix is dry/wet.
        if (p->nam && p->nam->isLoaded()) {
            const float inGain  = std::pow(10.0f, *p->ports[P_NAM_GAIN] / 20.0f);
            const float outGain = std::pow(10.0f, *p->ports[P_NAM_VOL]  / 20.0f);
            const float mix     = *p->ports[P_MIX];
            const float wetGain = outGain * mix;
            const float dryGain = 1.0f - mix;
            for (uint32_t off = 0; off < n; off += kMaxBlock) {
                const int len = static_cast<int>((n - off > (uint32_t)kMaxBlock) ? kMaxBlock : (n - off));
                for (int i = 0; i < len; ++i) p->namIn[i] = inGain * in[off + i];
                p->nam->processBuffer(p->namIn, p->namOut, len);
                for (int i = 0; i < len; ++i) out[off + i] = dryGain * in[off + i] + wetGain * p->namOut[i];
            }
        } else if (out != in) std::memcpy(out, in, sizeof(float) * n);
        if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
        return;
    }

    // ── Algorithmic pedal path ──
    p->dsp.setBypass(false);
    if (model != p->lastModel) { p->lastModel = model; p->dsp.setType(kModelMap[model]); }
    p->dsp.setParameter("drive",  *p->ports[P_DRIVE]);
    p->dsp.setParameter("tone",   *p->ports[P_TONE]);
    p->dsp.setParameter("level",  *p->ports[P_LEVEL]);
    p->dsp.setParameter("mix",    *p->ports[P_MIX]);
    p->dsp.setParameter("octave", *p->ports[P_OCTAVE]);
    float* ins[1]  = { const_cast<float*>(in) };
    float* outs[1] = { out };
    p->dsp.process(ins, outs, static_cast<int>(n), 1);

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

static void drive_cleanup(LV2_Handle h) {
    auto* p = static_cast<DrivePlugin*>(h);
    delete p->nam;
    delete p;
}

// ── State (persist the loaded NAM path) ───────────────────────────────────────
static LV2_State_Status drive_save(LV2_Handle h, LV2_State_Store_Function store,
                                   LV2_State_Handle handle, uint32_t flags,
                                   const LV2_Feature* const* features) {
    auto* p = static_cast<DrivePlugin*>(h);
    if (p->namPath[0] == '\0') return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    char* ap = mapPath ? mapPath->abstract_path(mapPath->handle, p->namPath) : p->namPath;
    store(handle, p->uris.nam_file, ap, std::strlen(ap) + 1, p->uris.atom_Path,
          flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    if (mapPath && ap != p->namPath) free(ap);
    return LV2_STATE_SUCCESS;
}
static LV2_State_Status drive_restore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                                      LV2_State_Handle handle, uint32_t,
                                      const LV2_Feature* const* features) {
    auto* p = static_cast<DrivePlugin*>(h);
    size_t size = 0; uint32_t type = 0, vflags = 0;
    const void* val = retrieve(handle, p->uris.nam_file, &size, &type, &vflags);
    if (!val || type != p->uris.atom_Path) return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    const char* ap = static_cast<const char*>(val);
    char* path = mapPath ? mapPath->absolute_path(mapPath->handle, ap) : const_cast<char*>(ap);
    auto* nm = new(std::nothrow) NamModel;
    if (nm && nm->loadFromFile(path)) {
        nm->reset(p->rate, kMaxBlock);
        delete p->nam; p->nam = nm;
        std::strncpy(p->namPath, path, kPathMax - 1); p->namPath[kPathMax - 1] = '\0';
    } else delete nm;
    if (mapPath && path != ap) free(path);
    return LV2_STATE_SUCCESS;
}

static const void* drive_extension_data(const char* uri) {
    static const LV2_Worker_Interface worker = { drive_work, drive_work_response, nullptr };
    static const LV2_State_Interface  state  = { drive_save, drive_restore };
    if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
    if (!std::strcmp(uri, LV2_STATE__interface))  return &state;
    return nullptr;
}

LV2_EXPORT_DESCRIPTOR(DRIVE_URI,
    drive_instantiate, drive_connect_port,
    nullptr, drive_run, nullptr, drive_cleanup, drive_extension_data)
