// ─────────────────────────────────────────────────────────────────────────────
// GuitarAmp Suite — Cabinet LV2 plugin (dual NAM / IR)
//
// Two ways to capture a cab/rig:
//   • IR (.wav)  — convolution cab (mod:fileTypes "cabsim,ir,wav,audio").
//   • NAM (.nam) — a neural capture (full rig). When a NAM is loaded it OVERRIDES
//                  the IR and runs instead of the convolver.
// Both files are chosen in mod-ui's file browser, read + built on the LV2 worker
// thread, and swapped in lock-free (CabinetBlock::setIR for the IR; a NamModel
// pointer swap for the NAM). Both paths persist via state. Until an IR is loaded
// the embedded Greenback default IR is used. Low/High Cut + Mix shape the IR path;
// in NAM mode Mix is the dry/wet blend (the cut filters are baked into the capture).
// ─────────────────────────────────────────────────────────────────────────────
#include "lv2_util.h"
#include "IrResample.h"
#include "CabinetBlock.h"
#include "DefaultCabIR.h"
#include "CabModels.h"
#include "NamModel.h"
#include "DenormalGuard.h"
#include <new>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <fstream>

#define CAB_URI     "https://rpowell5064.github.io/guitaramp-suite/cab"
#define CAB_IR_URI  CAB_URI "#irfile"
#define CAB_NAM_URI CAB_URI "#namfile"

static constexpr int kPathMax  = 1024;
static constexpr int kMaxBlock = 512;

enum CabPorts {
    P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
    P_LOWCUT, P_HIGHCUT, P_MIX, P_BYPASS,
    P_NAM_GAIN, P_NAM_VOL,     // Neural (NAM): input drive + output level (dB), used only in NAM mode
    P_MICPOS, P_MICDIST,       // mic placement (2026-07-14): cap→cone-edge, close→back; 0/0 = as-voiced
    P_ROOMON, P_ROOMMIX, P_ROOMAMT,  // room ambience (2026-07-14): toggle + wet mix + size/decay; off = bit-identical
    P_VOICE,                   // cab voice (2026-07-22): 0 Room (untouched legacy path) / 1 Studio (recorded chain)
    P_ROOMDENSE,               // room density (2026-07-23): 0 Classic 4-comb / 1 Dense 6-comb+2AP
    P_SPKDRIVE,                // speaker drive (item #40, 2026-07-28): 0 Off / 1 Subtle / 2 Full
#ifdef HEXCHAIN_ANAGRAM
    P_ENABLED, P_RESET,        // KosmOS: lv2:enabled + kx:Reset — inserted BEFORE the
                               // atoms (mod-host breaks if control ports follow them)
#endif
    P_CONTROL, P_NOTIFY,       // atom in/out — MUST be last: mod-host breaks if control ports follow them
    P_N_PORTS
};

struct URIs {
    LV2_URID atom_Object, atom_Path, atom_URID;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID ir_file, nam_file;
};

enum WorkType { WORK_IR, WORK_NAM_LOAD, WORK_NAM_FREE };
struct WorkMsg { WorkType type; char path[kPathMax]; NamModel* nam; };

struct CabPlugin {
    double rate = 48000.0;
    CabinetBlock dsp;
    NamModel*    nam = nullptr;   // when non-null + loaded, overrides the IR

    float* ports[P_N_PORTS]          = {};
    const LV2_Atom_Sequence* control = nullptr;
    LV2_Atom_Sequence*       notify  = nullptr;

    char irPath[kPathMax]  = {0};
    char namPath[kPathMax] = {0};
    float namIn[kMaxBlock], namOut[kMaxBlock];
#ifdef HEXCHAIN_ANAGRAM
    bool resetLatch = false;   // kx:Reset edge detect
#endif

    LV2_URID_Map*        map      = nullptr;
    LV2_Worker_Schedule* schedule = nullptr;
    LV2_Atom_Forge       forge;
    URIs                 uris;
};

// ── Minimal WAV reader: PCM 16/24/32-int + IEEE float32, → deinterleaved L/R ──
static bool readWav(const char* path, std::vector<float>& L, std::vector<float>& R,
                    uint32_t& outRate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    auto rd = [&](void* p, int n){ f.read(reinterpret_cast<char*>(p), n); };

    char riff[4]; rd(riff,4);
    if (std::strncmp(riff,"RIFF",4)!=0) return false;
    uint32_t rsz; rd(&rsz,4);
    char wave[4]; rd(wave,4);
    if (std::strncmp(wave,"WAVE",4)!=0) return false;

    uint16_t fmt=0, ch=0, bits=0; uint32_t sr=0;
    std::vector<uint8_t> data;
    while (f) {
        char id[4]; rd(id,4); uint32_t sz=0; rd(&sz,4);
        if (!f) break;
        if (std::strncmp(id,"fmt ",4)==0) {
            rd(&fmt,2); rd(&ch,2); rd(&sr,4);
            uint32_t br; rd(&br,4); uint16_t ba; rd(&ba,2); rd(&bits,2);
            if (sz>16) f.seekg(sz-16, std::ios::cur);
        } else if (std::strncmp(id,"data",4)==0) {
            data.resize(sz); f.read(reinterpret_cast<char*>(data.data()), sz);
        } else {
            f.seekg(sz, std::ios::cur);
        }
        if (sz & 1) f.seekg(1, std::ios::cur);
    }
    if (ch==0 || bits==0 || data.empty()) return false;
    outRate = sr;

    const size_t bps = bits/8;
    const size_t frames = data.size() / (bps*ch);
    L.assign(frames, 0.0f);
    if (ch >= 2) R.assign(frames, 0.0f); else R.clear();
    const uint8_t* p = data.data();
    for (size_t i=0;i<frames;++i) {
        for (uint16_t c=0;c<ch;++c) {
            float s=0.0f;
            if (fmt==3 && bits==32)      { float v; std::memcpy(&v,p,4); s=v; }
            else if (bits==16)           { int16_t v; std::memcpy(&v,p,2); s=v/32768.0f; }
            else if (bits==24)           { int32_t v=(p[0])|(p[1]<<8)|(p[2]<<16); if(v&0x800000) v|=~0xFFFFFF; s=v/8388608.0f; }
            else if (bits==32)           { int32_t v; std::memcpy(&v,p,4); s=v/2147483648.0f; }
            if (c==0)            L[i]=s;
            else if (c==1 && ch>=2) R[i]=s;
            p += bps;
        }
    }
    return true;
}

// IR resampling upgraded linear -> windowed-sinc 2026-07-14 (lv2/common/IrResample.h):
// linear interp baked ~-1 dB @ 10 kHz droop + imaging aliases into every 44.1 kHz IR.
static bool loadIRFile(const char* path, double dstRate,
                       std::vector<float>& L, std::vector<float>& R) {
    uint32_t srcRate = 0;
    if (!readWav(path, L, R, srcRate) || L.empty()) return false;
    L = irresample::resampleSinc(L, srcRate, dstRate);
    if (!R.empty()) R = irresample::resampleSinc(R, srcRate, dstRate);
    irresample::conditionIr(L, dstRate);               // trim silent tail + 1 s cap
    if (!R.empty()) irresample::conditionIr(R, dstRate);
    return true;
}

static void mapURIs(CabPlugin* p) {
    LV2_URID_Map* m = p->map;
    p->uris.atom_Object    = m->map(m->handle, LV2_ATOM__Object);
    p->uris.atom_Path      = m->map(m->handle, LV2_ATOM__Path);
    p->uris.atom_URID      = m->map(m->handle, LV2_ATOM__URID);
    p->uris.patch_Set      = m->map(m->handle, LV2_PATCH__Set);
    p->uris.patch_Get      = m->map(m->handle, LV2_PATCH__Get);
    p->uris.patch_property = m->map(m->handle, LV2_PATCH__property);
    p->uris.patch_value    = m->map(m->handle, LV2_PATCH__value);
    p->uris.ir_file        = m->map(m->handle, CAB_IR_URI);
    p->uris.nam_file       = m->map(m->handle, CAB_NAM_URI);
}

static void writeFileToNotify(CabPlugin* p, LV2_URID prop, const char* path) {
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&p->forge, 0);
    lv2_atom_forge_object(&p->forge, &frame, 0, p->uris.patch_Set);
    lv2_atom_forge_key(&p->forge, p->uris.patch_property);
    lv2_atom_forge_urid(&p->forge, prop);
    lv2_atom_forge_key(&p->forge, p->uris.patch_value);
    lv2_atom_forge_path(&p->forge, path, static_cast<uint32_t>(std::strlen(path)));
    lv2_atom_forge_pop(&p->forge, &frame);
}

// ── Lifecycle ──────────────────────────────────────────────────────────────
static LV2_Handle cab_instantiate(const LV2_Descriptor*, double rate,
                                  const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) CabPlugin;
    if (!p) return nullptr;
    p->map      = static_cast<LV2_URID_Map*>(lv2_find_feature(features, LV2_URID__map));
    p->schedule = static_cast<LV2_Worker_Schedule*>(lv2_find_feature(features, LV2_WORKER__schedule));
    if (!p->map || !p->schedule) { delete p; return nullptr; }
    mapURIs(p);
    lv2_atom_forge_init(&p->forge, p->map);

    p->rate = rate;
    p->dsp.prepare(rate, kMaxBlock, 2);
    const std::vector<float> ir = CabModels::generate("@factory", rate);   // enriched Factory Cab (2026-07-14)
    p->dsp.setIR(ir);
    return p;
}

static void cab_connect_port(LV2_Handle h, uint32_t port, void* data) {
    auto* p = static_cast<CabPlugin*>(h);
    switch (port) {
        case P_CONTROL: p->control = static_cast<const LV2_Atom_Sequence*>(data); break;
        case P_NOTIFY:  p->notify  = static_cast<LV2_Atom_Sequence*>(data);       break;
        default:        if (port < P_N_PORTS) p->ports[port] = static_cast<float*>(data);
    }
}

// ── Worker: build IR / NAM off the RT thread ──
static LV2_Worker_Status cab_work(LV2_Handle h, LV2_Worker_Respond_Function respond,
                                  LV2_Worker_Respond_Handle handle, uint32_t, const void* data) {
    auto* p   = static_cast<CabPlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == WORK_IR) {
        std::vector<float> L, R;
        if (msg->path[0] == '@')                                  // built-in synthetic cab (@vox2x12, @greenback, …)
            p->dsp.setIR(CabModels::generate(msg->path, p->rate));
        else if (msg->path[0] && loadIRFile(msg->path, p->rate, L, R))
            p->dsp.setIR(L, R.empty() ? nullptr : &R);   // lock-free publish
        else
            p->dsp.setIR(CabModels::generate("@factory", p->rate));   // empty/@factory → enriched Factory Cab
        return LV2_WORKER_SUCCESS;
    }
    if (msg->type == WORK_NAM_FREE) { delete msg->nam; return LV2_WORKER_SUCCESS; }
    // WORK_NAM_LOAD
    auto* nm = new(std::nothrow) NamModel;
    if (!nm) return LV2_WORKER_ERR_NO_SPACE;
    if (nm->loadFromFile(msg->path)) nm->reset(p->rate, kMaxBlock);
    WorkMsg reply; reply.type = WORK_NAM_LOAD; reply.nam = nm;
    respond(handle, sizeof(reply), &reply);
    return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status cab_work_response(LV2_Handle h, uint32_t, const void* data) {
    auto* p = static_cast<CabPlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type != WORK_NAM_LOAD) return LV2_WORKER_SUCCESS;
    NamModel* old = p->nam;
    p->nam = msg->nam;
    WorkMsg freeMsg; freeMsg.type = WORK_NAM_FREE; freeMsg.nam = old;
    p->schedule->schedule_work(p->schedule->handle, sizeof(freeMsg), &freeMsg);
    return LV2_WORKER_SUCCESS;
}

// ── Audio ────────────────────────────────────────────────────────────────────
static void cab_run(LV2_Handle h, uint32_t n) {
#ifndef HEXCHAIN_ANAGRAM
    DenormalGuard denormalGuard;   // flush denormals (NAM/IR state can spike CPU in decay/silence)
#endif  // KosmOS forbids touching global CPU registers (FTZ) — even scoped
    auto* p = static_cast<CabPlugin*>(h);
    const URIs& u = p->uris;

#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset trigger (rising edge): re-init the convolver, then rebuild the
    // active IR on the worker (prepare() drops the loaded IR; the worker
    // regenerates it from irPath — "" → the enriched Factory Cab). NAM state
    // resets in place. See anagram/ANAGRAM-NOTES.md.
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) {
            p->resetLatch = true;
            p->dsp.prepare(p->rate, kMaxBlock, 2);
            WorkMsg msg; msg.type = WORK_IR; msg.nam = nullptr;
            std::strncpy(msg.path, p->irPath, kPathMax - 1); msg.path[kPathMax - 1] = '\0';
            p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
            if (p->nam) p->nam->reset(p->rate, kMaxBlock);
        }
    } else p->resetLatch = false;
#endif

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
                if (!prop || prop->type != u.atom_URID || !val || val->type != u.atom_Path) continue;
                const LV2_URID which = reinterpret_cast<const LV2_Atom_URID*>(prop)->body;
                const char* path = static_cast<const char*>(LV2_ATOM_BODY_CONST(val));
                if (which == u.ir_file) {
                    // "Factory Cab" sentinel → clear IR to the built-in default.
                    const char* eff = (std::strcmp(path, "@factory") == 0) ? "" : path;
                    std::strncpy(p->irPath, eff, kPathMax - 1); p->irPath[kPathMax - 1] = '\0';
                    WorkMsg msg; msg.type = WORK_IR;
                    std::strncpy(msg.path, p->irPath, kPathMax - 1); msg.path[kPathMax - 1] = '\0';
                    p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
                } else if (which == u.nam_file) {
                    std::strncpy(p->namPath, path, kPathMax - 1); p->namPath[kPathMax - 1] = '\0';
                    WorkMsg msg; msg.type = WORK_NAM_LOAD;
                    std::strncpy(msg.path, p->namPath, kPathMax - 1); msg.path[kPathMax - 1] = '\0';
                    p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
                }
            } else if (obj->body.otype == u.patch_Get && haveNotify) {
                writeFileToNotify(p, u.ir_file, p->irPath);
                writeFileToNotify(p, u.nam_file, p->namPath);
            }
        }
    }

#ifdef HEXCHAIN_ANAGRAM
    // lv2:enabled (KosmOS bypass, 1 = on) shares the bypass/passthrough paths.
    const bool bypass = (*p->ports[P_BYPASS] > 0.5f) ||
                        (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f);
#else
    const bool bypass = *p->ports[P_BYPASS] > 0.5f;
#endif
    float* inL  = p->ports[P_IN_L];  float* inR  = p->ports[P_IN_R];
    float* outL = p->ports[P_OUT_L]; float* outR = p->ports[P_OUT_R];

    if (p->nam && p->nam->isLoaded() && !bypass) {
        // ── NAM mode (overrides the IR) ── mono capture → both channels; Mix = dry/wet.
        // NAM Gain drives the capture (input trim), NAM Level trims the output (both dB).
        const float mix     = *p->ports[P_MIX];
        const float dry     = 1.0f - mix;
        const float inGain  = std::pow(10.0f, *p->ports[P_NAM_GAIN] / 20.0f);
        const float outGain = std::pow(10.0f, *p->ports[P_NAM_VOL]  / 20.0f);
        for (uint32_t off = 0; off < n; off += kMaxBlock) {
            const int len = static_cast<int>((n - off > (uint32_t)kMaxBlock) ? kMaxBlock : (n - off));
            for (int i = 0; i < len; ++i) p->namIn[i] = inGain * 0.5f * (inL[off + i] + inR[off + i]);
            p->nam->processBuffer(p->namIn, p->namOut, len);
            for (int i = 0; i < len; ++i) {
                const float w = p->namOut[i] * outGain * mix;
                outL[off + i] = dry * inL[off + i] + w;
                outR[off + i] = dry * inR[off + i] + w;
            }
        }
    } else if (p->nam && p->nam->isLoaded()) {   // NAM loaded but bypassed → passthrough
        if (outL != inL) std::memcpy(outL, inL, sizeof(float) * n);
        if (outR != inR) std::memcpy(outR, inR, sizeof(float) * n);
    } else {
        // ── IR convolver mode ──
        p->dsp.setBypass(bypass);
        p->dsp.setParameter("lowCutHz",  *p->ports[P_LOWCUT]);
        p->dsp.setParameter("highCutHz", *p->ports[P_HIGHCUT]);
        p->dsp.setParameter("mix",       *p->ports[P_MIX]);
        p->dsp.setParameter("micpos",    *p->ports[P_MICPOS]);   // mic placement (0/0 = as-voiced)
        p->dsp.setParameter("micdist",   *p->ports[P_MICDIST]);
        p->dsp.setParameter("roomon",    *p->ports[P_ROOMON]);   // room ambience (off = bit-identical)
        p->dsp.setParameter("roommix",   *p->ports[P_ROOMMIX]);
        p->dsp.setParameter("roomamt",   *p->ports[P_ROOMAMT]);
        p->dsp.setParameter("voice",     *p->ports[P_VOICE]);
        p->dsp.setParameter("roomdense", *p->ports[P_ROOMDENSE]);
        // Speaker Drive (item #40): one enumerated depth control (0 Off/1 Subtle/2
        // Full) maps to CabinetBlock's two internal params -- Subtle/Full chosen
        // conservatively (0.35/0.75) since this is a brand-new, not-yet-user-tuned
        // character feature; the internal API stays continuous for future tuning.
        {
            const int spk = static_cast<int>(*p->ports[P_SPKDRIVE] + 0.5f);
            p->dsp.setParameter("spkdrive",    spk > 0 ? 1.0f : 0.0f);
            p->dsp.setParameter("spkdriveamt", spk >= 2 ? 0.75f : (spk == 1 ? 0.35f : 0.0f));
        }
        float* ins[2]  = { inL,  inR  };
        float* outs[2] = { outL, outR };
        p->dsp.process(ins, outs, static_cast<int>(n), 2);
    }

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

static void cab_cleanup(LV2_Handle h) {
    auto* p = static_cast<CabPlugin*>(h);
    delete p->nam;
    delete p;
}

// ── State (persist both file paths) ───────────────────────────────────────────
static LV2_State_Status cab_save(LV2_Handle h, LV2_State_Store_Function store,
                                 LV2_State_Handle handle, uint32_t flags,
                                 const LV2_Feature* const* features) {
    auto* p = static_cast<CabPlugin*>(h);
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    auto saveOne = [&](LV2_URID prop, const char* raw) {
        if (raw[0] == '\0') return;
        char* ap = mapPath ? mapPath->abstract_path(mapPath->handle, const_cast<char*>(raw)) : const_cast<char*>(raw);
        store(handle, prop, ap, std::strlen(ap) + 1, p->uris.atom_Path,
              flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
        if (mapPath && ap != raw) free(ap);
    };
    saveOne(p->uris.ir_file,  p->irPath);
    saveOne(p->uris.nam_file, p->namPath);
    return LV2_STATE_SUCCESS;
}

static LV2_State_Status cab_restore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                                    LV2_State_Handle handle, uint32_t,
                                    const LV2_Feature* const* features) {
    auto* p = static_cast<CabPlugin*>(h);
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    size_t size = 0; uint32_t type = 0, vflags = 0;

    const void* irv = retrieve(handle, p->uris.ir_file, &size, &type, &vflags);
    if (irv && type == p->uris.atom_Path) {
        const char* ap = static_cast<const char*>(irv);
        char* path = mapPath ? mapPath->absolute_path(mapPath->handle, ap) : const_cast<char*>(ap);
        std::vector<float> L, R;
        if (loadIRFile(path, p->rate, L, R)) {
            p->dsp.setIR(L, R.empty() ? nullptr : &R);
            std::strncpy(p->irPath, path, kPathMax - 1); p->irPath[kPathMax - 1] = '\0';
        }
        if (mapPath && path != ap) free(path);
    }

    const void* nv = retrieve(handle, p->uris.nam_file, &size, &type, &vflags);
    if (nv && type == p->uris.atom_Path) {
        const char* ap = static_cast<const char*>(nv);
        char* path = mapPath ? mapPath->absolute_path(mapPath->handle, ap) : const_cast<char*>(ap);
        auto* nm = new(std::nothrow) NamModel;
        if (nm && nm->loadFromFile(path)) {
            nm->reset(p->rate, kMaxBlock);
            delete p->nam; p->nam = nm;
            std::strncpy(p->namPath, path, kPathMax - 1); p->namPath[kPathMax - 1] = '\0';
        } else delete nm;
        if (mapPath && path != ap) free(path);
    }
    return LV2_STATE_SUCCESS;
}

static const void* cab_extension_data(const char* uri) {
    static const LV2_Worker_Interface worker = { cab_work, cab_work_response, nullptr };
    static const LV2_State_Interface  state  = { cab_save, cab_restore };
    if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
    if (!std::strcmp(uri, LV2_STATE__interface))  return &state;
    return nullptr;
}

LV2_EXPORT_DESCRIPTOR(CAB_URI,
    cab_instantiate, cab_connect_port,
    nullptr, cab_run, nullptr, cab_cleanup, cab_extension_data)
