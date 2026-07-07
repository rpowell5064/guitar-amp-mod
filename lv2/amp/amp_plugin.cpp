// ─────────────────────────────────────────────────────────────────────────────
// GuitarAmp Suite — Amp LV2 plugin
//
// Five algorithmic amp models + a full power-amp stage, PLUS a NAM (Neural Amp
// Modeler) slot: model index 5 ("Neural (NAM)") runs a user-loaded .nam capture
// instead of the algorithmic amp. The .nam file is chosen in mod-ui's file
// browser (mod:fileTypes → the NAM user-files folder) and delivered as a
// patch:Set(atom:Path); it is loaded on the LV2 worker thread and swapped in with
// a single pointer assignment, so the RT thread never allocates or blocks. The
// bundled NAM core parses both legacy (WaveNet/LSTM, "a1") and the new container/
// slimmable ("a2") formats. When NAM is selected the shared power-amp is bypassed
// (a capture already includes the whole amp).
//
// Analog model switches rebuild the amp off the audio thread via the same worker.
// ─────────────────────────────────────────────────────────────────────────────
#include "lv2_util.h"
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include "NamModel.h"
#include "DenormalGuard.h"
#include <new>
#include <cstring>
#include <cstdint>

#define AMP_URI     "https://rpowell5064.github.io/guitaramp-suite/amp"
#define AMP_NAM_URI AMP_URI "#nammodel"
static constexpr int kPathMax = 1024;

// LV2 model index → AmpModel enum. Index 5 = NAM (handled separately). Beardo BE
// (Friedman) is index 6, AFTER NAM, so existing saved boards (which store the model
// index) keep their meaning — inserting it earlier would shift NAM and break them.
static const AmpModel kModelMap[12] = {
    AmpModel::FenderDeluxe,        // 0
    AmpModel::MarshallJCM800,      // 1
    AmpModel::EVH5150III,          // 2
    AmpModel::SunnModelT,          // 3
    AmpModel::OrangeRockerverb50,  // 4
    AmpModel::NeuralCustom,        // 5 = NAM (handled separately; placeholder, never built here)
    AmpModel::FriedmanBEDeluxe,    // 6 = Beardo BE
    AmpModel::HiwattDR103,         // 7 = Hiwatt (high-headroom British clean)
    AmpModel::VoxAC30,             // 8 = Chime Thirty (Vox AC30 Top Boost, EL84)
    AmpModel::PeaveyBackstage,     // 9 = Backline Plus (solid-state Peavey Backstage)
    AmpModel::MarshallPlexi,       // 10 = Plexiglass (Marshall 1959 Super Lead, EL34)
    AmpModel::MesaMarkV,           // 11 = Boojum V (Mesa Mark V, 9 modes, Simul-Class)
};
static const int kCanonical[12] = { 0, 1, 2, 4, 5, 3, 6, 0, 0, 0, 1, 1 };  // LV2 idx → getDefaultsForModel idx ([7] Hiwatt, [8] Vox, [9] Backline → clean PA; [10] Plexi, [11] Mesa → JCM800 EL34 PA)
static constexpr int kSunnIdx     = 3;     // Sunn's LV2 model index
static constexpr int kNamIdx      = 5;     // NAM slot
static constexpr int kFriedmanIdx = 6;     // Beardo BE
static constexpr int kMesaIdx     = 11;    // Boojum V (Mesa Mark V)
static constexpr int kMaxModel    = 11;    // highest selectable model index (Boojum V)
static constexpr int kMaxBlock    = 512;   // internal processing chunk

static const int kModelTube[12] = { 0, 1, 1, 0, 1, 0, 1, 1, 2, 0, 1, 1 };  // [6] Friedman EL34; [7] Hiwatt EL34; [8] Vox EL84; [9] Backline solid-state; [10] Plexi EL34; [11] Mesa EL34/6L6
static const float kModelMakeup[12] = { 3.3f, 1.0f, 1.4f, 3.0f, 1.15f, 1.0f, 1.0f, 4.9f, 1.6f, 2.5f, 1.0f, 1.0f };  // [10] Plexi; [11] Mesa (per-mode makeup is inside the model)

enum AmpPorts {
    P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
    P_MODEL, P_GAIN, P_BASS, P_MID, P_TREBLE, P_PRES, P_MASTER, P_SAG,
    P_CHANNEL, P_RESON, P_SUNN_V2, P_SUNN_LNK, P_BYPASS,
    P_PA_BYPASS, P_PA_TUBE, P_PA_PRES, P_PA_DEPTH, P_PA_SAG, P_PA_MASTER,
    P_PA_NFB, P_PA_RESON, P_PA_AIR, P_PA_AUTO,
    P_SUNN_B2, P_SUNN_M2, P_SUNN_T2, P_SUNN_BR1, P_SUNN_BR2,  // Sunn Brite-channel
    P_FR_CHANNEL, P_FR_FAT, P_FR_C45, P_FR_SAT,               // Beardo BE (Friedman) — 3-way channel + voicing toggles
    P_CONTROL, P_NOTIFY,                                       // atom in/out (NAM file)
    P_MV_MODE, P_MV_GEQ0, P_MV_GEQ1, P_MV_GEQ2, P_MV_GEQ3, P_MV_GEQ4, P_MV_EQPRESET,  // Cali V (Mesa Mark V): 9-mode + 5-band graphic EQ
    P_N_PORTS
};

enum WorkType { WORK_LOAD, WORK_FREE, WORK_NAM_LOAD, WORK_NAM_FREE };

struct WorkMsg {
    WorkType          type;
    AmpBlockExtended* amp = nullptr;   // amp LOAD reply / FREE target
    NamModel*         nam = nullptr;   // NAM LOAD reply / FREE target
    int               modelIdx = 0;
    char              path[kPathMax] = {0};
};

struct URIs {
    LV2_URID atom_Object, atom_Path, atom_URID;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID nam_file;
};

static int clampIdx(float v, int lo, int hi) {
    int i = static_cast<int>(v + 0.5f);
    if (i < lo) i = lo;
    if (i > hi) i = hi;
    return i;
}

struct AmpPlugin {
    double rate = 48000.0;

    AmpBlockExtended* amp = nullptr;   // double-buffered algo amp
    PowerAmpProcessor pa;
    NamModel*         nam = nullptr;   // swapped in by the worker on file load

    float* ctrl[P_N_PORTS] = {};
    const LV2_Atom_Sequence* control = nullptr;
    LV2_Atom_Sequence*       notify  = nullptr;

    int  lastModel = -1;
    int  lastTube  = -1;
    char namPath[kPathMax] = {0};
    float mono[kMaxBlock], monoOut[kMaxBlock];

    LV2_Worker_Schedule* schedule = nullptr;
    LV2_URID_Map*        map      = nullptr;
    LV2_Atom_Forge       forge;
    URIs                 uris;
};

static void mapURIs(AmpPlugin* p) {
    LV2_URID_Map* m = p->map;
    p->uris.atom_Object    = m->map(m->handle, LV2_ATOM__Object);
    p->uris.atom_Path      = m->map(m->handle, LV2_ATOM__Path);
    p->uris.atom_URID      = m->map(m->handle, LV2_ATOM__URID);
    p->uris.patch_Set      = m->map(m->handle, LV2_PATCH__Set);
    p->uris.patch_Get      = m->map(m->handle, LV2_PATCH__Get);
    p->uris.patch_property = m->map(m->handle, LV2_PATCH__property);
    p->uris.patch_value    = m->map(m->handle, LV2_PATCH__value);
    p->uris.nam_file       = m->map(m->handle, AMP_NAM_URI);
}
static void writeNamToNotify(AmpPlugin* p) {
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

static LV2_Worker_Status scheduleRebuild(AmpPlugin* p, int modelIdx) {
    WorkMsg msg; msg.type = WORK_LOAD; msg.modelIdx = modelIdx;
    return p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
}

// ── Lifecycle ──────────────────────────────────────────────────────────────
static LV2_Handle amp_instantiate(const LV2_Descriptor*, double rate,
                                   const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) AmpPlugin;
    if (!p) return nullptr;
    p->schedule = static_cast<LV2_Worker_Schedule*>(lv2_find_feature(features, LV2_WORKER__schedule));
    p->map      = static_cast<LV2_URID_Map*>(lv2_find_feature(features, LV2_URID__map));
    if (!p->schedule || !p->map) { delete p; return nullptr; }
    mapURIs(p);
    lv2_atom_forge_init(&p->forge, p->map);

    p->rate = rate;
    p->amp = new(std::nothrow) AmpBlockExtended;
    if (!p->amp) { delete p; return nullptr; }
    p->amp->prepare(rate, kMaxBlock, 2);
    p->amp->setAmpModel(AmpModel::MarshallJCM800);
    p->lastModel = 1;
    p->pa.prepare(rate, kMaxBlock, 2);
    return p;
}

static void amp_connect_port(LV2_Handle h, uint32_t port, void* data) {
    auto* p = static_cast<AmpPlugin*>(h);
    if (port == P_CONTROL)     p->control = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == P_NOTIFY) p->notify  = static_cast<LV2_Atom_Sequence*>(data);
    else if (port < P_N_PORTS) p->ctrl[port] = static_cast<float*>(data);
}

// ── Worker thread ────────────────────────────────────────────────────────────
static LV2_Worker_Status amp_work(LV2_Handle h, LV2_Worker_Respond_Function respond,
                                  LV2_Worker_Respond_Handle handle,
                                  uint32_t, const void* data) {
    auto* p = static_cast<AmpPlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);

    if (msg->type == WORK_FREE)     { delete msg->amp; return LV2_WORKER_SUCCESS; }
    if (msg->type == WORK_NAM_FREE) { delete msg->nam; return LV2_WORKER_SUCCESS; }

    if (msg->type == WORK_NAM_LOAD) {
        auto* nm = new(std::nothrow) NamModel;
        if (!nm) return LV2_WORKER_ERR_NO_SPACE;
        if (nm->loadFromFile(msg->path)) nm->reset(p->rate, kMaxBlock);
        WorkMsg reply; reply.type = WORK_NAM_LOAD; reply.nam = nm;
        respond(handle, sizeof(reply), &reply);
        return LV2_WORKER_SUCCESS;
    }

    // WORK_LOAD — build a fresh algo amp off the RT thread.
    auto* na = new(std::nothrow) AmpBlockExtended;
    if (!na) return LV2_WORKER_ERR_NO_SPACE;
    na->prepare(p->rate, kMaxBlock, 2);
    na->setAmpModel(kModelMap[clampIdx(static_cast<float>(msg->modelIdx), 0, kMaxModel)]);
    WorkMsg reply; reply.type = WORK_LOAD; reply.amp = na; reply.modelIdx = msg->modelIdx;
    respond(handle, sizeof(reply), &reply);
    return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status amp_work_response(LV2_Handle h, uint32_t, const void* data) {
    auto* p = static_cast<AmpPlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == WORK_NAM_LOAD) {
        NamModel* old = p->nam;
        p->nam = msg->nam;
        WorkMsg freeMsg; freeMsg.type = WORK_NAM_FREE; freeMsg.nam = old;
        p->schedule->schedule_work(p->schedule->handle, sizeof(freeMsg), &freeMsg);
        return LV2_WORKER_SUCCESS;
    }
    // WORK_LOAD reply — swap the algo amp.
    AmpBlockExtended* old = p->amp;
    p->amp = msg->amp;
    p->lastModel = msg->modelIdx;
    WorkMsg freeMsg; freeMsg.type = WORK_FREE; freeMsg.amp = old;
    p->schedule->schedule_work(p->schedule->handle, sizeof(freeMsg), &freeMsg);
    return LV2_WORKER_SUCCESS;
}

// ── Audio ─────────────────────────────────────────────────────────────────────
static void amp_run(LV2_Handle h, uint32_t n) {
    DenormalGuard denormalGuard;
    auto* p = static_cast<AmpPlugin*>(h);
    const URIs& u = p->uris;

    // Open the notify sequence + handle incoming NAM file set/get.
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

    const int  modelIdx   = clampIdx(*p->ctrl[P_MODEL], 0, kMaxModel);
    const bool isNam       = (modelIdx == kNamIdx);
    const bool fullBypass  = *p->ctrl[P_BYPASS] > 0.5f;
    float* inL  = p->ctrl[P_IN_L];  float* inR  = p->ctrl[P_IN_R];
    float* outL = p->ctrl[P_OUT_L]; float* outR = p->ctrl[P_OUT_R];

    if (fullBypass) {
        if (outL != inL) std::memcpy(outL, inL, sizeof(float) * n);
        if (outR != inR) std::memcpy(outR, inR, sizeof(float) * n);
        if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
        return;
    }

    if (isNam) {
        // ── Neural (NAM) path ── mono capture; bypass the algo amp + power amp.
        const float outGain = *p->ctrl[P_MASTER];   // master = simple output trim
        if (p->nam && p->nam->isLoaded()) {
            for (uint32_t off = 0; off < n; off += kMaxBlock) {
                const int len = static_cast<int>((n - off > (uint32_t)kMaxBlock) ? kMaxBlock : (n - off));
                for (int i = 0; i < len; ++i) p->mono[i] = 0.5f * (inL[off + i] + inR[off + i]);
                p->nam->processBuffer(p->mono, p->monoOut, len);
                for (int i = 0; i < len; ++i) { const float y = p->monoOut[i] * outGain; outL[off + i] = y; outR[off + i] = y; }
            }
        } else {  // no model loaded yet → pass through
            if (outL != inL) std::memcpy(outL, inL, sizeof(float) * n);
            if (outR != inR) std::memcpy(outR, inR, sizeof(float) * n);
        }
        if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
        return;
    }

    // ── Algorithmic amp path ──
    AmpBlockExtended* amp = p->amp;
    amp->setBypass(false);

    if (modelIdx != p->lastModel) {
        if (scheduleRebuild(p, modelIdx) == LV2_WORKER_SUCCESS) p->lastModel = modelIdx;
    }

    if (modelIdx == kSunnIdx) {
        amp->setParameter("vol1",         *p->ctrl[P_GAIN]);
        amp->setParameter("vol2",         *p->ctrl[P_SUNN_V2]);
        amp->setParameter("channel_link", *p->ctrl[P_SUNN_LNK]);
        amp->setParameter("bass1",        *p->ctrl[P_BASS]);
        amp->setParameter("mid1",         *p->ctrl[P_MID]);
        amp->setParameter("treble1",      *p->ctrl[P_TREBLE]);
        amp->setParameter("bass2",        *p->ctrl[P_SUNN_B2]);
        amp->setParameter("mid2",         *p->ctrl[P_SUNN_M2]);
        amp->setParameter("treble2",      *p->ctrl[P_SUNN_T2]);
        amp->setParameter("bright1",      *p->ctrl[P_SUNN_BR1]);
        amp->setParameter("bright2",      *p->ctrl[P_SUNN_BR2]);
    } else {
        amp->setParameter("gain",   *p->ctrl[P_GAIN]);
        amp->setParameter("bass",   *p->ctrl[P_BASS]);
        amp->setParameter("mid",    *p->ctrl[P_MID]);
        amp->setParameter("treble", *p->ctrl[P_TREBLE]);
    }
    amp->setParameter("presence", *p->ctrl[P_PRES]);
    amp->setParameter("master",   *p->ctrl[P_MASTER]);
    amp->setParameter("sag",      *p->ctrl[P_SAG]);
    amp->setParameter("channel",  *p->ctrl[P_CHANNEL]);
    amp->setParameter("resonance",*p->ctrl[P_RESON]);

    // Beardo BE (Friedman) — its own 3-way channel (Clean/BE/HBE) + voicing toggles.
    if (modelIdx == kFriedmanIdx) {
        amp->setParameter("channel", *p->ctrl[P_FR_CHANNEL]);
        amp->setParameter("fat",     *p->ctrl[P_FR_FAT]);
        amp->setParameter("c45",     *p->ctrl[P_FR_C45]);
        amp->setParameter("sat",     *p->ctrl[P_FR_SAT]);
    }

    // Cali V (Mesa Mark V) — 9-mode channel switcher + 5-band graphic EQ. Setters
    // guard on change internally, so pushing every block is cheap.
    if (modelIdx == kMesaIdx) {
        amp->setParameter("mode",     *p->ctrl[P_MV_MODE]);
        amp->setParameter("geq0",     *p->ctrl[P_MV_GEQ0]);
        amp->setParameter("geq1",     *p->ctrl[P_MV_GEQ1]);
        amp->setParameter("geq2",     *p->ctrl[P_MV_GEQ2]);
        amp->setParameter("geq3",     *p->ctrl[P_MV_GEQ3]);
        amp->setParameter("geq4",     *p->ctrl[P_MV_GEQ4]);
        amp->setParameter("eqpreset", *p->ctrl[P_MV_EQPRESET]);
    }

    int desiredTube;
    if (*p->ctrl[P_PA_AUTO] > 0.5f) {
        const auto d = PowerAmpProcessor::getDefaultsForModel(kCanonical[modelIdx]);
        p->pa.setParameter("master",   d.master);
        p->pa.setParameter("presence", d.presence);
        p->pa.setParameter("depth",    d.depth);
        p->pa.setParameter("nfb",      d.nfb);
        p->pa.setParameter("sag",      d.sag);
        p->pa.setParameter("resonance", *p->ctrl[P_PA_RESON]);
        p->pa.setParameter("airFeel",   *p->ctrl[P_PA_AIR]);
        desiredTube = kModelTube[modelIdx];
    } else {
        p->pa.setParameter("presence",  *p->ctrl[P_PA_PRES]);
        p->pa.setParameter("depth",     *p->ctrl[P_PA_DEPTH]);
        p->pa.setParameter("sag",       *p->ctrl[P_PA_SAG]);
        p->pa.setParameter("master",    *p->ctrl[P_PA_MASTER]);
        p->pa.setParameter("nfb",       *p->ctrl[P_PA_NFB]);
        p->pa.setParameter("resonance", *p->ctrl[P_PA_RESON]);
        p->pa.setParameter("airFeel",   *p->ctrl[P_PA_AIR]);
        desiredTube = clampIdx(*p->ctrl[P_PA_TUBE], 0, 3);
    }
    // Post-saturation sag-VCA depth is a per-amp voicing value with no user port.
    p->pa.setParameter("bloomvca", PowerAmpProcessor::getDefaultsForModel(kCanonical[modelIdx]).bloomVca);
    if (desiredTube != p->lastTube) { p->lastTube = desiredTube; p->pa.setTubeType(static_cast<TubeType>(desiredTube)); }

    const bool paBypass = (*p->ctrl[P_PA_BYPASS] > 0.5f) || (modelIdx == kSunnIdx);
    p->pa.setBypass(paBypass);

    for (uint32_t off = 0; off < n; off += kMaxBlock) {
        const int len = static_cast<int>((n - off > (uint32_t)kMaxBlock) ? kMaxBlock : (n - off));
        float* ins[2]  = { inL  + off, inR  + off };
        float* outs[2] = { outL + off, outR + off };
        amp->process(ins, outs, len, 2);
        p->pa.process(outs, outs, len, 2);
    }
    const float mk = kModelMakeup[modelIdx];
    if (mk != 1.0f) for (uint32_t i = 0; i < n; ++i) { outL[i] *= mk; outR[i] *= mk; }

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

static void amp_cleanup(LV2_Handle h) {
    auto* p = static_cast<AmpPlugin*>(h);
    delete p->amp;
    delete p->nam;
    delete p;
}

// ── State (persist the loaded NAM path) ───────────────────────────────────────
static LV2_State_Status amp_save(LV2_Handle h, LV2_State_Store_Function store,
                                 LV2_State_Handle handle, uint32_t flags,
                                 const LV2_Feature* const* features) {
    auto* p = static_cast<AmpPlugin*>(h);
    if (p->namPath[0] == '\0') return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    char* ap = mapPath ? mapPath->abstract_path(mapPath->handle, p->namPath) : p->namPath;
    store(handle, p->uris.nam_file, ap, std::strlen(ap) + 1, p->uris.atom_Path,
          flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    if (mapPath && ap != p->namPath) free(ap);
    return LV2_STATE_SUCCESS;
}
static LV2_State_Status amp_restore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                                    LV2_State_Handle handle, uint32_t,
                                    const LV2_Feature* const* features) {
    auto* p = static_cast<AmpPlugin*>(h);
    size_t size = 0; uint32_t type = 0, vflags = 0;
    const void* val = retrieve(handle, p->uris.nam_file, &size, &type, &vflags);
    if (!val || type != p->uris.atom_Path) return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    const char* ap = static_cast<const char*>(val);
    char* path = mapPath ? mapPath->absolute_path(mapPath->handle, ap) : const_cast<char*>(ap);
    auto* nm = new(std::nothrow) NamModel;   // restore() is not RT — load directly
    if (nm && nm->loadFromFile(path)) {
        nm->reset(p->rate, kMaxBlock);
        delete p->nam; p->nam = nm;
        std::strncpy(p->namPath, path, kPathMax - 1); p->namPath[kPathMax - 1] = '\0';
    } else delete nm;
    if (mapPath && path != ap) free(path);
    return LV2_STATE_SUCCESS;
}

static const void* amp_extension_data(const char* uri) {
    static const LV2_Worker_Interface worker = { amp_work, amp_work_response, nullptr };
    static const LV2_State_Interface  state  = { amp_save, amp_restore };
    if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
    if (!std::strcmp(uri, LV2_STATE__interface))  return &state;
    return nullptr;
}

LV2_EXPORT_DESCRIPTOR(AMP_URI,
    amp_instantiate, amp_connect_port,
    nullptr, amp_run, nullptr, amp_cleanup, amp_extension_data)
