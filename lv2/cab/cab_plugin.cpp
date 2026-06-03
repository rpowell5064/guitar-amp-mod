// ─────────────────────────────────────────────────────────────────────────────
// GuitarAmp Suite — Cabinet LV2 plugin
//
// Convolution cab with user IR loading. An IR .wav is chosen in mod-ui's file
// browser (mod:fileTypes "cabsim,ir,wav,audio" → the "Speaker Cabinets IRs"
// user-files folder) and delivered as a patch:Set(atom:Path) on the control port.
// The file is read + the convolver rebuilt on the LV2 worker thread; CabinetBlock
// ::setIR() then publishes it to the audio thread with a lock-free slot swap, so
// the RT thread never allocates or blocks. The chosen path persists via state.
//
// Until an IR is loaded the embedded Greenback default IR is used.
// ─────────────────────────────────────────────────────────────────────────────
#include "lv2_util.h"
#include "CabinetBlock.h"
#include "DefaultCabIR.h"
#include <new>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <fstream>

#define CAB_URI    "https://rpowell5064.github.io/guitaramp-suite/cab"
#define CAB_IR_URI CAB_URI "#irfile"

static constexpr int kPathMax = 1024;

enum CabPorts {
    P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
    P_LOWCUT, P_HIGHCUT, P_MIX, P_BYPASS,
    P_CONTROL, P_NOTIFY,
    P_N_PORTS
};

struct URIs {
    LV2_URID atom_Object, atom_Path, atom_URID;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID ir_file;
};

struct WorkMsg { char path[kPathMax]; };

struct CabPlugin {
    double rate = 48000.0;
    CabinetBlock dsp;

    float* ports[P_N_PORTS]          = {};
    const LV2_Atom_Sequence* control = nullptr;
    LV2_Atom_Sequence*       notify  = nullptr;

    char irPath[kPathMax] = {0};

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

// Linear resample one channel from srcRate to dstRate (in place via return).
// Linear is adequate for cabinet IRs (convolution smooths the minor HF error) and
// keeps the worker allocation-light. No-op when rates already match.
static std::vector<float> resampleLinear(const std::vector<float>& in,
                                         double srcRate, double dstRate) {
    if (in.empty() || srcRate <= 0.0 || dstRate <= 0.0 ||
        std::abs(srcRate - dstRate) < 1.0)
        return in;
    const double ratio = dstRate / srcRate;
    const size_t outLen = static_cast<size_t>(in.size() * ratio + 0.5);
    std::vector<float> out(outLen);
    for (size_t i = 0; i < outLen; ++i) {
        const double sp = i / ratio;
        const size_t i0 = static_cast<size_t>(sp);
        const float  fr = static_cast<float>(sp - i0);
        const float  a  = in[i0];
        const float  b  = (i0 + 1 < in.size()) ? in[i0 + 1] : in[i0];
        out[i] = a + fr * (b - a);
    }
    return out;
}

// Read an IR .wav and resample it to the engine rate (most IR packs ship at 44.1k
// while the pi-Stomp runs at 48k — without this the cab would be pitch/length-skewed).
static bool loadIRFile(const char* path, double dstRate,
                       std::vector<float>& L, std::vector<float>& R) {
    uint32_t srcRate = 0;
    if (!readWav(path, L, R, srcRate) || L.empty()) return false;
    L = resampleLinear(L, srcRate, dstRate);
    if (!R.empty()) R = resampleLinear(R, srcRate, dstRate);
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
}

// Tell the UI which IR file is loaded.
static void writeIRToNotify(CabPlugin* p) {
    const URIs& u = p->uris;
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&p->forge, 0);
    lv2_atom_forge_object(&p->forge, &frame, 0, u.patch_Set);
    lv2_atom_forge_key(&p->forge, u.patch_property);
    lv2_atom_forge_urid(&p->forge, u.ir_file);
    lv2_atom_forge_key(&p->forge, u.patch_value);
    lv2_atom_forge_path(&p->forge, p->irPath, static_cast<uint32_t>(std::strlen(p->irPath)));
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
    p->dsp.prepare(rate, 512, 2);
    const std::vector<float> ir = DefaultCabIR::generate(rate);  // embedded default
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

// ── Worker: read the IR file and swap it in (CabinetBlock::setIR is RT-safe) ──
static LV2_Worker_Status cab_work(LV2_Handle h, LV2_Worker_Respond_Function,
                                  LV2_Worker_Respond_Handle, uint32_t, const void* data) {
    auto* p   = static_cast<CabPlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    std::vector<float> L, R;
    if (loadIRFile(msg->path, p->rate, L, R))
        p->dsp.setIR(L, R.empty() ? nullptr : &R);   // lock-free publish to audio thread
    return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status cab_work_response(LV2_Handle, uint32_t, const void*) {
    return LV2_WORKER_SUCCESS;
}

// ── Audio ────────────────────────────────────────────────────────────────────
static void cab_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<CabPlugin*>(h);
    const URIs& u = p->uris;

    const bool haveNotify = (p->notify != nullptr);
    LV2_Atom_Forge_Frame seqFrame;
    if (haveNotify) {
        lv2_atom_forge_set_buffer(&p->forge, reinterpret_cast<uint8_t*>(p->notify),
                                  p->notify->atom.size);
        lv2_atom_forge_sequence_head(&p->forge, &seqFrame, 0);
    }

    if (p->control) {
        LV2_ATOM_SEQUENCE_FOREACH(p->control, ev) {
            if (ev->body.type != u.atom_Object) continue;
            const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(&ev->body);
            if (obj->body.otype == u.patch_Set) {
                const LV2_Atom* property = nullptr;
                const LV2_Atom* value    = nullptr;
                lv2_atom_object_get(obj, u.patch_property, &property, u.patch_value, &value, 0);
                if (property && property->type == u.atom_URID &&
                    reinterpret_cast<const LV2_Atom_URID*>(property)->body == u.ir_file &&
                    value && value->type == u.atom_Path) {
                    const char* path = static_cast<const char*>(LV2_ATOM_BODY_CONST(value));
                    std::strncpy(p->irPath, path, kPathMax - 1);
                    p->irPath[kPathMax - 1] = '\0';
                    WorkMsg msg;
                    std::strncpy(msg.path, p->irPath, kPathMax - 1);
                    msg.path[kPathMax - 1] = '\0';
                    p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
                }
            } else if (obj->body.otype == u.patch_Get && haveNotify) {
                writeIRToNotify(p);
            }
        }
    }

    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);
    p->dsp.setParameter("lowCutHz",  *p->ports[P_LOWCUT]);
    p->dsp.setParameter("highCutHz", *p->ports[P_HIGHCUT]);
    p->dsp.setParameter("mix",       *p->ports[P_MIX]);
    float* ins[2]  = { p->ports[P_IN_L],  p->ports[P_IN_R]  };
    float* outs[2] = { p->ports[P_OUT_L], p->ports[P_OUT_R] };
    p->dsp.process(ins, outs, static_cast<int>(n), 2);

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

static void cab_cleanup(LV2_Handle h) { delete static_cast<CabPlugin*>(h); }

// ── State (persist the loaded IR path) ────────────────────────────────────────
static LV2_State_Status cab_save(LV2_Handle h, LV2_State_Store_Function store,
                                 LV2_State_Handle handle, uint32_t flags,
                                 const LV2_Feature* const* features) {
    auto* p = static_cast<CabPlugin*>(h);
    if (p->irPath[0] == '\0') return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    char* apath = mapPath ? mapPath->abstract_path(mapPath->handle, p->irPath) : p->irPath;
    store(handle, p->uris.ir_file, apath, std::strlen(apath) + 1,
          p->uris.atom_Path, flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    if (mapPath && apath != p->irPath) free(apath);
    return LV2_STATE_SUCCESS;
}

static LV2_State_Status cab_restore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                                    LV2_State_Handle handle, uint32_t,
                                    const LV2_Feature* const* features) {
    auto* p = static_cast<CabPlugin*>(h);
    size_t size=0; uint32_t type=0, vflags=0;
    const void* val = retrieve(handle, p->uris.ir_file, &size, &type, &vflags);
    if (!val || type != p->uris.atom_Path) return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    const char* apath = static_cast<const char*>(val);
    char* path = mapPath ? mapPath->absolute_path(mapPath->handle, apath) : const_cast<char*>(apath);

    // restore() is not real-time — read + apply directly.
    std::vector<float> L, R;
    if (loadIRFile(path, p->rate, L, R)) {
        p->dsp.setIR(L, R.empty() ? nullptr : &R);
        std::strncpy(p->irPath, path, kPathMax - 1);
        p->irPath[kPathMax - 1] = '\0';
    }
    if (mapPath && path != apath) free(path);
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
