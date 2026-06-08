// ─────────────────────────────────────────────────────────────────────────────
// Hex Chain — Hex Forge LV2 plugin
//
// A single plugin that hosts the entire Hex Chain as a prewired, reorderable
// pedalboard. It owns one instance of every Hex Chain DSP block and runs them in
// series. Input Trim is locked at the head of the chain; the other nine blocks
// each carry a `pos` (slot 1..9) control so the GUI can reorder them, and a
// `bypass` control so each can be switched in/out. Disabled blocks are skipped
// entirely (true passthrough — zero CPU), so an all-off Hex Forge is unity.
//
// Stereo bus with guitar-friendly mono semantics: the signal is mono until a
// stereo block (amp/cab/mod/delay/reverb) spreads it; a mono block (gate/comp/
// fuzz/drive/input-trim) that sits after a stereo block collapses L+R to mono
// first, exactly like a real mono pedal placed after a stereo effect.
//
// Worker thread (shared, tagged): amp model switches rebuild an AmpBlockExtended
// off the RT thread (it allocates an OversamplingWrapper); cab IR loads read the
// .wav + rebuild the convolver off the RT thread. The audio thread only ever does
// a pointer swap / lock-free IR publish.
//
// Symbol isolation: like amp/drive, this .so whole-archives NamCore. The version
// script (lv2/export.map) + -Bsymbolic + --exclude-libs keep NAM/Eigen globals
// local so loading Hex Forge beside the ten standalone plugins can't interpose.
// ─────────────────────────────────────────────────────────────────────────────
#include "lv2_util.h"
#include "hexforge_ports.h"

#include "BiquadFilter.h"
#include "NoiseGateBlock.h"
#include "CompressorBlock.h"
#include "OversamplingWrapper.h"
#include "EHXBigMuff.h"
#include "ToneBenderMkII.h"
#include "OverdriveBlock.h"
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include "CabinetBlock.h"
#include "DefaultCabIR.h"
#include "ModulationBlock.h"
#include "ModulationFactory.h"
#include "DelayBlock.h"
#include "DelayFactory.h"
#include "PlateReverbBlock.h"
#include "DenormalGuard.h"

#include <new>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <memory>
#include <vector>
#include <fstream>
#include <algorithm>

#define HEXFORGE_URI    "https://rpowell5064.github.io/guitaramp-suite/hexforge"
#define HEXFORGE_IR_URI HEXFORGE_URI "#irfile"

static constexpr int kMaxBlock = 512;
static constexpr int kPathMax  = 1024;

// ── Model maps (mirror the standalone amp / drive plugins) ────────────────────
static const AmpModel kAmpMap[5] = {
    AmpModel::FenderDeluxe, AmpModel::MarshallJCM800, AmpModel::EVH5150III,
    AmpModel::SunnModelT,   AmpModel::OrangeRockerverb50,
};
static const int   kCanonical[5] = { 0, 1, 2, 4, 5 };   // PowerAmp default lookup
static constexpr int kSunnIdx = 3;
static const int   kAmpTube[5]   = { 0, 1, 1, 0, 1 };   // 6L6/EL34/EL34/6L6/EL34
static const float kAmpMakeup[5] = { 1.8f, 1.0f, 1.4f, 1.0f, 1.15f };

static const OverdriveType kDriveMap[3] = {
    OverdriveType::TubeScreamer808, OverdriveType::LifePedal, OverdriveType::ProcoRAT,
};

// Binson Echorec rotary program -> playback-head bitmask (mirrors delay plugin).
static const int kEchorecProgram[12] = {
    0x01,0x02,0x04,0x08, 0x03,0x06,0x0C, 0x07,0x0E,0x0D, 0x0F,0x1F,
};

static int clampi(float v, int lo, int hi) {
    int i = static_cast<int>(v + 0.5f);
    return i < lo ? lo : (i > hi ? hi : i);
}

// ── Power-line hum twin-notch (mirrors the standalone Input Trim) ──────────────
static BiquadCoeffs makeNotch(double fc, double Q, double fs) noexcept {
    const double w = 2.0 * M_PI * fc / fs, a = std::sin(w) / (2.0 * Q), c = std::cos(w);
    const double a0 = 1.0 + a;
    BiquadCoeffs k;
    k.b0 = 1.0/a0; k.b1 = (-2.0*c)/a0; k.b2 = 1.0/a0;
    k.a1 = (-2.0*c)/a0; k.a2 = (1.0-a)/a0;
    return k;
}
struct HumFilter {
    BiquadFilter n50, n60;
    void prepare(double sr) noexcept { n50.setCoeffs(makeNotch(50.0,35.0,sr)); n60.setCoeffs(makeNotch(60.0,35.0,sr)); }
    float process(float x) noexcept { return n60.process(n50.process(x)); }
};

// ── Movable-block identity ────────────────────────────────────────────────────
enum Block { B_GATE, B_COMP, B_FUZZ, B_DRIVE, B_AMP, B_CAB, B_MODFX, B_DELAY, B_REVERB, B_COUNT };
static const int kPosPort[B_COUNT] = {
    HF_GT_POS, HF_CP_POS, HF_FZ_POS, HF_DR_POS, HF_AMP_POS,
    HF_CAB_POS, HF_MD_POS, HF_DL_POS, HF_RV_POS,
};
static const int kEnablePort[B_COUNT] = {
    HF_GT_ENABLE, HF_CP_ENABLE, HF_FZ_ENABLE, HF_DR_ENABLE, HF_AMP_ENABLE,
    HF_CAB_ENABLE, HF_MD_ENABLE, HF_DL_ENABLE, HF_RV_ENABLE,
};

// ── Worker messaging ──────────────────────────────────────────────────────────
enum WorkType { W_AMP_LOAD, W_AMP_FREE, W_CAB_IR };
struct WorkMsg {
    WorkType          type;
    AmpBlockExtended* amp = nullptr;   // AMP_LOAD reply / AMP_FREE target
    int               modelIdx = 0;
    char              path[kPathMax] = {0};   // CAB_IR
};

struct URIs {
    LV2_URID atom_Object, atom_Path, atom_URID;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID ir_file;
};

struct HexForge {
    double rate = 48000.0;

    // DSP blocks
    HumFilter         trimHum;
    NoiseGateBlock    gate;
    CompressorBlock   comp;
    std::unique_ptr<OversamplingWrapper> fuzzMuff;   // Italian Hero
    std::unique_ptr<OversamplingWrapper> fuzzBender; // Tone Bender MkII
    OverdriveBlock    drive;
    AmpBlockExtended* amp = nullptr;                  // swapped on model change
    PowerAmpProcessor pa;
    CabinetBlock      cab;
    ModulationBlock   modfx;
    DelayBlock        delay;
    PlateReverbBlock  reverb;

    // model-switch caches
    int lastAmpModel = 1, lastAmpTube = -1, lastDriveModel = 0;
    int lastModfxType = 0, lastDelayType = 0;

    // ports
    float* ports[HF_N_PORTS] = {};
    const LV2_Atom_Sequence* control = nullptr;
    LV2_Atom_Sequence*       notify  = nullptr;

    // IR state
    char irPath[kPathMax] = {0};

    // scratch
    float mono[kMaxBlock];
    int   clipHold = 0;   // samples remaining to keep the CLIP indicator lit

    // host features
    LV2_URID_Map*        map      = nullptr;
    LV2_Worker_Schedule* schedule = nullptr;
    LV2_Atom_Forge       forge;
    URIs                 uris;
};

// ── IR file reading (mirrors the standalone Cab) ──────────────────────────────
static bool readWav(const char* path, std::vector<float>& L, std::vector<float>& R, uint32_t& outRate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    auto rd = [&](void* p, int n){ f.read(reinterpret_cast<char*>(p), n); };
    char riff[4]; rd(riff,4); if (std::strncmp(riff,"RIFF",4)!=0) return false;
    uint32_t rsz; rd(&rsz,4);
    char wave[4]; rd(wave,4); if (std::strncmp(wave,"WAVE",4)!=0) return false;
    uint16_t fmt=0, ch=0, bits=0; uint32_t sr=0;
    std::vector<uint8_t> data;
    while (f) {
        char id[4]; rd(id,4); uint32_t sz=0; rd(&sz,4); if (!f) break;
        if (std::strncmp(id,"fmt ",4)==0) {
            rd(&fmt,2); rd(&ch,2); rd(&sr,4); uint32_t br; rd(&br,4); uint16_t ba; rd(&ba,2); rd(&bits,2);
            if (sz>16) f.seekg(sz-16, std::ios::cur);
        } else if (std::strncmp(id,"data",4)==0) {
            data.resize(sz); f.read(reinterpret_cast<char*>(data.data()), sz);
        } else f.seekg(sz, std::ios::cur);
        if (sz & 1) f.seekg(1, std::ios::cur);
    }
    if (ch==0 || bits==0 || data.empty()) return false;
    outRate = sr;
    const size_t bps = bits/8, frames = data.size()/(bps*ch);
    L.assign(frames, 0.0f);
    if (ch>=2) R.assign(frames, 0.0f); else R.clear();
    const uint8_t* p = data.data();
    for (size_t i=0;i<frames;++i) for (uint16_t c=0;c<ch;++c) {
        float s=0.0f;
        if (fmt==3 && bits==32) { float v; std::memcpy(&v,p,4); s=v; }
        else if (bits==16) { int16_t v; std::memcpy(&v,p,2); s=v/32768.0f; }
        else if (bits==24) { int32_t v=(p[0])|(p[1]<<8)|(p[2]<<16); if(v&0x800000) v|=~0xFFFFFF; s=v/8388608.0f; }
        else if (bits==32) { int32_t v; std::memcpy(&v,p,4); s=v/2147483648.0f; }
        if (c==0) L[i]=s; else if (c==1 && ch>=2) R[i]=s;
        p += bps;
    }
    return true;
}
static std::vector<float> resampleLinear(const std::vector<float>& in, double sr, double dr) {
    if (in.empty() || sr<=0.0 || dr<=0.0 || std::abs(sr-dr)<1.0) return in;
    const double ratio = dr/sr; const size_t out = static_cast<size_t>(in.size()*ratio + 0.5);
    std::vector<float> o(out);
    for (size_t i=0;i<out;++i){ const double sp=i/ratio; const size_t i0=static_cast<size_t>(sp);
        const float fr=static_cast<float>(sp-i0), a=in[i0], b=(i0+1<in.size())?in[i0+1]:in[i0]; o[i]=a+fr*(b-a); }
    return o;
}
static bool loadIRFile(const char* path, double dst, std::vector<float>& L, std::vector<float>& R) {
    uint32_t sr=0; if (!readWav(path,L,R,sr) || L.empty()) return false;
    L = resampleLinear(L, sr, dst); if (!R.empty()) R = resampleLinear(R, sr, dst);
    return true;
}

static void mapURIs(HexForge* p) {
    LV2_URID_Map* m = p->map;
    p->uris.atom_Object   = m->map(m->handle, LV2_ATOM__Object);
    p->uris.atom_Path     = m->map(m->handle, LV2_ATOM__Path);
    p->uris.atom_URID     = m->map(m->handle, LV2_ATOM__URID);
    p->uris.patch_Set     = m->map(m->handle, LV2_PATCH__Set);
    p->uris.patch_Get     = m->map(m->handle, LV2_PATCH__Get);
    p->uris.patch_property= m->map(m->handle, LV2_PATCH__property);
    p->uris.patch_value   = m->map(m->handle, LV2_PATCH__value);
    p->uris.ir_file       = m->map(m->handle, HEXFORGE_IR_URI);
}
static void writeIRToNotify(HexForge* p) {
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

// ── Lifecycle ─────────────────────────────────────────────────────────────────
static LV2_Handle hf_instantiate(const LV2_Descriptor*, double rate,
                                 const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) HexForge;
    if (!p) return nullptr;
    p->map      = static_cast<LV2_URID_Map*>(lv2_find_feature(features, LV2_URID__map));
    p->schedule = static_cast<LV2_Worker_Schedule*>(lv2_find_feature(features, LV2_WORKER__schedule));
    if (!p->map || !p->schedule) { delete p; return nullptr; }
    mapURIs(p);
    lv2_atom_forge_init(&p->forge, p->map);

    p->rate = rate;
    p->trimHum.prepare(rate);
    p->gate.prepare(rate, kMaxBlock, 1);
    p->comp.prepare(rate, kMaxBlock, 1);
    // Build fuzz models directly (NOT via OverdriveFactory) — same as the fuzz plugin.
    p->fuzzMuff   = std::make_unique<OversamplingWrapper>(std::make_unique<EHXBigMuff>());
    p->fuzzBender = std::make_unique<OversamplingWrapper>(std::make_unique<ToneBenderMkII>());
    if (!p->fuzzMuff || !p->fuzzBender) { delete p; return nullptr; }
    p->fuzzMuff->prepare(rate, kMaxBlock, 1);
    p->fuzzBender->prepare(rate, kMaxBlock, 1);
    p->fuzzMuff->setParameter("era", 2.0f);
    p->drive.prepare(rate, kMaxBlock, 1);
    p->drive.setType(kDriveMap[0]);
    p->amp = new(std::nothrow) AmpBlockExtended;
    if (!p->amp) { delete p; return nullptr; }
    p->amp->prepare(rate, kMaxBlock, 2);
    p->amp->setAmpModel(kAmpMap[1]);   // default Crunchy McCrunchFace
    p->pa.prepare(rate, kMaxBlock, 2);
    p->cab.prepare(rate, kMaxBlock, 2);
    p->cab.setIR(DefaultCabIR::generate(rate));
    p->modfx.prepare(rate, kMaxBlock, 2);
    p->modfx.setType(ModulationFactory::fromIndex(0));
    p->delay.prepare(rate, kMaxBlock, 2);
    p->delay.setType(DelayFactory::fromIndex(0));
    p->reverb.prepare(rate, kMaxBlock, 2);
    return p;
}

static void hf_connect_port(LV2_Handle h, uint32_t port, void* data) {
    auto* p = static_cast<HexForge*>(h);
    if (port == HF_CONTROL)      p->control = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == HF_NOTIFY)  p->notify  = static_cast<LV2_Atom_Sequence*>(data);
    else if (port < HF_N_PORTS)  p->ports[port] = static_cast<float*>(data);
}

// ── Worker ────────────────────────────────────────────────────────────────────
static LV2_Worker_Status hf_work(LV2_Handle h, LV2_Worker_Respond_Function respond,
                                 LV2_Worker_Respond_Handle handle, uint32_t, const void* data) {
    auto* p = static_cast<HexForge*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == W_AMP_FREE) { delete msg->amp; return LV2_WORKER_SUCCESS; }
    if (msg->type == W_CAB_IR) {
        std::vector<float> L, R;
        if (loadIRFile(msg->path, p->rate, L, R)) p->cab.setIR(L, R.empty()?nullptr:&R);
        return LV2_WORKER_SUCCESS;
    }
    // W_AMP_LOAD — build a fresh amp off the RT thread.
    auto* na = new(std::nothrow) AmpBlockExtended;
    if (!na) return LV2_WORKER_ERR_NO_SPACE;
    na->prepare(p->rate, kMaxBlock, 2);
    na->setAmpModel(kAmpMap[clampi(static_cast<float>(msg->modelIdx), 0, 4)]);
    WorkMsg reply; reply.type = W_AMP_LOAD; reply.amp = na; reply.modelIdx = msg->modelIdx;
    respond(handle, sizeof(reply), &reply);
    return LV2_WORKER_SUCCESS;
}
static LV2_Worker_Status hf_work_response(LV2_Handle h, uint32_t, const void* data) {
    auto* p = static_cast<HexForge*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type != W_AMP_LOAD) return LV2_WORKER_SUCCESS;
    AmpBlockExtended* old = p->amp;
    p->amp = msg->amp;
    p->lastAmpModel = msg->modelIdx;
    WorkMsg freeMsg; freeMsg.type = W_AMP_FREE; freeMsg.amp = old;
    p->schedule->schedule_work(p->schedule->handle, sizeof(freeMsg), &freeMsg);
    return LV2_WORKER_SUCCESS;
}

// ── Audio helpers ─────────────────────────────────────────────────────────────
// Run a mono block on the chunk: collapse to mono per real-pedal semantics, then
// mirror the result back to both channels.
static inline void runMono(AudioBlock& b, float* L, float* R, int len, float* s, bool stereo) {
    if (!stereo) for (int i=0;i<len;++i) s[i] = L[i];
    else         for (int i=0;i<len;++i) s[i] = 0.5f*(L[i]+R[i]);
    float* io[1] = { s };
    b.process(io, io, len, 1);
    for (int i=0;i<len;++i) { L[i] = s[i]; R[i] = s[i]; }
}
static inline void runStereo(AudioBlock& b, float* L, float* R, int len, bool& stereo) {
    if (!stereo) for (int i=0;i<len;++i) R[i] = L[i];
    float* io[2] = { L, R };
    b.process(io, io, len, 2);
    stereo = true;
}

static void hf_run(LV2_Handle h, uint32_t n) {
    DenormalGuard denormalGuard;
    auto* p = static_cast<HexForge*>(h);
    const URIs& u = p->uris;

    // ── Atom: IR file set / get, + open notify sequence ──
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
                const LV2_Atom *prop=nullptr, *val=nullptr;
                lv2_atom_object_get(obj, u.patch_property, &prop, u.patch_value, &val, 0);
                if (prop && prop->type==u.atom_URID &&
                    reinterpret_cast<const LV2_Atom_URID*>(prop)->body==u.ir_file &&
                    val && val->type==u.atom_Path) {
                    const char* path = static_cast<const char*>(LV2_ATOM_BODY_CONST(val));
                    std::strncpy(p->irPath, path, kPathMax-1); p->irPath[kPathMax-1]='\0';
                    WorkMsg msg; msg.type=W_CAB_IR;
                    std::strncpy(msg.path, p->irPath, kPathMax-1); msg.path[kPathMax-1]='\0';
                    p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
                }
            } else if (obj->body.otype == u.patch_Get && haveNotify) {
                writeIRToNotify(p);
            }
        }
    }

    float* inL  = p->ports[HF_IN_L];
    float* inR  = p->ports[HF_IN_R];
    float* outL = p->ports[HF_OUT_L];
    float* outR = p->ports[HF_OUT_R];

    // ── Global bypass: unity passthrough ──
    if (*p->ports[HF_BYPASS] > 0.5f) {
        if (outL != inL) std::memcpy(outL, inL, sizeof(float)*n);
        if (outR != inR) std::memcpy(outR, inR, sizeof(float)*n);
        if (p->ports[HF_CLIP]) *p->ports[HF_CLIP] = 0.0f;
        if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
        return;
    }

    // ── Configure every block from its ports (once per run) ──
    // Input trim
    const bool  itEnabled = (*p->ports[HF_IT_ENABLE] > 0.5f);
    const float itGain    = std::pow(10.0f, *p->ports[HF_IT_GAIN]/20.0f)
                            * ((*p->ports[HF_IT_PHASE] > 0.5f) ? -1.0f : 1.0f);
    const bool  itHum     = *p->ports[HF_IT_HUM] > 0.5f;
    // Gate
    p->gate.setBypass(false);
    p->gate.setParameter("threshold",  *p->ports[HF_GT_THRESH]);
    p->gate.setParameter("attack",     *p->ports[HF_GT_ATTACK]);
    p->gate.setParameter("hold",       *p->ports[HF_GT_HOLD]);
    p->gate.setParameter("release",    *p->ports[HF_GT_RELEASE]);
    p->gate.setParameter("hysteresis", *p->ports[HF_GT_HYST]);
    // Comp
    p->comp.setBypass(false);
    p->comp.setParameter("type",      *p->ports[HF_CP_TYPE]);
    p->comp.setParameter("threshold", *p->ports[HF_CP_THRESH]);
    p->comp.setParameter("ratio",     *p->ports[HF_CP_RATIO]);
    p->comp.setParameter("attack",    *p->ports[HF_CP_ATTACK]);
    p->comp.setParameter("release",   *p->ports[HF_CP_RELEASE]);
    p->comp.setParameter("knee",      *p->ports[HF_CP_KNEE]);
    p->comp.setParameter("makeup",    *p->ports[HF_CP_MAKEUP]);
    // Fuzz (which pedal chosen at process time)
    const int fuzzPedal = clampi(*p->ports[HF_FZ_PEDAL], 0, 1);
    p->fuzzMuff->setBypass(false); p->fuzzBender->setBypass(false);
    p->fuzzMuff->setParameter("era",   *p->ports[HF_FZ_MODE]);
    p->fuzzMuff->setParameter("drive", *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzMuff->setParameter("tone",  *p->ports[HF_FZ_TONE]);
    p->fuzzMuff->setParameter("level", *p->ports[HF_FZ_VOLUME]);
    p->fuzzBender->setParameter("attack",    *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzBender->setParameter("level",     *p->ports[HF_FZ_VOLUME]);
    p->fuzzBender->setParameter("bias",      *p->ports[HF_FZ_BIAS]);
    p->fuzzBender->setParameter("inputtrim", *p->ports[HF_FZ_INPUTTRIM]);
    p->fuzzBender->setParameter("getemp",    *p->ports[HF_FZ_GETEMP]);
    // Drive
    p->drive.setBypass(false);
    const int driveModel = clampi(*p->ports[HF_DR_MODEL], 0, 2);
    if (driveModel != p->lastDriveModel) { p->lastDriveModel = driveModel; p->drive.setType(kDriveMap[driveModel]); }
    p->drive.setParameter("drive",  *p->ports[HF_DR_DRIVE]);
    p->drive.setParameter("tone",   *p->ports[HF_DR_TONE]);
    p->drive.setParameter("level",  *p->ports[HF_DR_LEVEL]);
    p->drive.setParameter("mix",    *p->ports[HF_DR_MIX]);
    p->drive.setParameter("octave", *p->ports[HF_DR_OCTAVE]);
    // Amp
    const int ampModel = clampi(*p->ports[HF_AMP_MODEL], 0, 4);
    if (ampModel != p->lastAmpModel) {
        WorkMsg msg; msg.type=W_AMP_LOAD; msg.modelIdx=ampModel;
        if (p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg) == LV2_WORKER_SUCCESS)
            p->lastAmpModel = ampModel;
    }
    AmpBlockExtended* amp = p->amp;
    amp->setBypass(false);
    if (ampModel == kSunnIdx) {
        amp->setParameter("vol1",         *p->ports[HF_AMP_GAIN]);
        amp->setParameter("vol2",         *p->ports[HF_AMP_SUNN_VOL2]);
        amp->setParameter("channel_link", *p->ports[HF_AMP_SUNN_LINK]);
        amp->setParameter("bass1",        *p->ports[HF_AMP_BASS]);
        amp->setParameter("mid1",         *p->ports[HF_AMP_MID]);
        amp->setParameter("treble1",      *p->ports[HF_AMP_TREBLE]);
        amp->setParameter("bass2",        *p->ports[HF_AMP_SUNN_BASS2]);
        amp->setParameter("mid2",         *p->ports[HF_AMP_SUNN_MID2]);
        amp->setParameter("treble2",      *p->ports[HF_AMP_SUNN_TREBLE2]);
        amp->setParameter("bright1",      *p->ports[HF_AMP_SUNN_BRIGHT1]);
        amp->setParameter("bright2",      *p->ports[HF_AMP_SUNN_BRIGHT2]);
    } else {
        amp->setParameter("gain",   *p->ports[HF_AMP_GAIN]);
        amp->setParameter("bass",   *p->ports[HF_AMP_BASS]);
        amp->setParameter("mid",    *p->ports[HF_AMP_MID]);
        amp->setParameter("treble", *p->ports[HF_AMP_TREBLE]);
    }
    amp->setParameter("presence",  *p->ports[HF_AMP_PRESENCE]);
    amp->setParameter("master",    *p->ports[HF_AMP_MASTER]);
    amp->setParameter("sag",       *p->ports[HF_AMP_SAG]);
    amp->setParameter("channel",   *p->ports[HF_AMP_CHANNEL]);
    amp->setParameter("resonance", *p->ports[HF_AMP_RESONANCE]);
    int desiredTube;
    if (*p->ports[HF_AMP_PAMP_AUTO] > 0.5f) {
        const auto d = PowerAmpProcessor::getDefaultsForModel(kCanonical[ampModel]);
        p->pa.setParameter("master", d.master); p->pa.setParameter("presence", d.presence);
        p->pa.setParameter("depth", d.depth);   p->pa.setParameter("nfb", d.nfb);
        p->pa.setParameter("sag", d.sag);
        p->pa.setParameter("resonance", *p->ports[HF_AMP_PAMP_RESONANCE]);
        p->pa.setParameter("airFeel",   *p->ports[HF_AMP_PAMP_AIRFEEL]);
        desiredTube = kAmpTube[ampModel];
    } else {
        p->pa.setParameter("presence",  *p->ports[HF_AMP_PAMP_PRESENCE]);
        p->pa.setParameter("depth",     *p->ports[HF_AMP_PAMP_DEPTH]);
        p->pa.setParameter("sag",       *p->ports[HF_AMP_PAMP_SAG]);
        p->pa.setParameter("master",    *p->ports[HF_AMP_PAMP_MASTER]);
        p->pa.setParameter("nfb",       *p->ports[HF_AMP_PAMP_NFB]);
        p->pa.setParameter("resonance", *p->ports[HF_AMP_PAMP_RESONANCE]);
        p->pa.setParameter("airFeel",   *p->ports[HF_AMP_PAMP_AIRFEEL]);
        desiredTube = clampi(*p->ports[HF_AMP_PAMP_TUBE], 0, 3);
    }
    if (desiredTube != p->lastAmpTube) { p->lastAmpTube = desiredTube; p->pa.setTubeType(static_cast<TubeType>(desiredTube)); }
    const bool paBypass = (*p->ports[HF_AMP_PAMP_BYPASS] > 0.5f) || (ampModel == kSunnIdx);
    p->pa.setBypass(paBypass);
    const float ampMakeup = kAmpMakeup[ampModel];
    // Cab
    p->cab.setBypass(false);
    p->cab.setParameter("lowCutHz",  *p->ports[HF_CAB_LOWCUT]);
    p->cab.setParameter("highCutHz", *p->ports[HF_CAB_HIGHCUT]);
    p->cab.setParameter("mix",       *p->ports[HF_CAB_MIX]);
    // Modfx
    p->modfx.setBypass(false);
    const int modfxType = clampi(*p->ports[HF_MD_TYPE], 0, 1);
    if (modfxType != p->lastModfxType) { p->lastModfxType = modfxType; p->modfx.setType(ModulationFactory::fromIndex(modfxType)); }
    p->modfx.setParameter("rate",        *p->ports[HF_MD_RATE]);
    p->modfx.setParameter("depth",       *p->ports[HF_MD_DEPTH]);
    p->modfx.setParameter("mix",         *p->ports[HF_MD_MIX]);
    p->modfx.setParameter("stereoWidth", *p->ports[HF_MD_WIDTH]);
    // Delay
    p->delay.setBypass(false);
    const int delayType = clampi(*p->ports[HF_DL_TYPE], 0, 2);
    if (delayType != p->lastDelayType) { p->lastDelayType = delayType; p->delay.setType(DelayFactory::fromIndex(delayType)); }
    p->delay.setParameter("timeMs",       *p->ports[HF_DL_TIME]);
    p->delay.setParameter("feedback",     *p->ports[HF_DL_FEEDBACK]);
    p->delay.setParameter("mix",          *p->ports[HF_DL_MIX]);
    p->delay.setParameter("stereoWidth",  *p->ports[HF_DL_WIDTH]);
    p->delay.setParameter("wowDepth",     *p->ports[HF_DL_WOW]);
    p->delay.setParameter("flutterDepth", *p->ports[HF_DL_FLUTTER]);
    p->delay.setParameter("headMask", static_cast<float>(kEchorecProgram[clampi(*p->ports[HF_DL_HEADS],0,11)]));
    // Reverb
    p->reverb.setBypass(false);
    p->reverb.setParameter("preDelayMs", *p->ports[HF_RV_PREDELAY]);
    p->reverb.setParameter("decayTime",  *p->ports[HF_RV_DECAY]);
    p->reverb.setParameter("damping",    *p->ports[HF_RV_DAMPING]);
    p->reverb.setParameter("modDepth",   *p->ports[HF_RV_MODDEPTH]);
    p->reverb.setParameter("modRate",    *p->ports[HF_RV_MODRATE]);
    p->reverb.setParameter("mix",        *p->ports[HF_RV_MIX]);

    // ── Resolve chain order (Input Trim locked first; rest sorted by pos) ──
    int order[B_COUNT];
    for (int i=0;i<B_COUNT;++i) order[i] = i;
    int posv[B_COUNT];
    for (int i=0;i<B_COUNT;++i) posv[i] = clampi(*p->ports[kPosPort[i]], 1, 9);
    // stable selection sort by (pos, canonical index)
    for (int a=0;a<B_COUNT-1;++a) {
        int best=a;
        for (int b=a+1;b<B_COUNT;++b)
            if (posv[order[b]] < posv[order[best]]) best=b;
        if (best!=a) { int t=order[a]; order[a]=order[best]; order[best]=t; }
    }
    bool enabled[B_COUNT];
    for (int i=0;i<B_COUNT;++i) enabled[i] = (*p->ports[kEnablePort[i]] > 0.5f);

    // ── Process in <= kMaxBlock chunks; each chunk runs the whole chain ──
    for (uint32_t off=0; off<n; off+=kMaxBlock) {
        const int len = static_cast<int>((n-off > (uint32_t)kMaxBlock) ? kMaxBlock : (n-off));
        float* L = outL + off; float* R = outR + off;
        // seed chunk from input
        for (int i=0;i<len;++i) { L[i] = inL[off+i]; R[i] = inR[off+i]; }
        bool stereo = false;

        // Input Trim (locked head of chain)
        if (itEnabled) {
            for (int i=0;i<len;++i) {
                float x = L[i];
                if (itHum) x = p->trimHum.process(x);
                x *= itGain;
                L[i] = x; R[i] = x;
            }
        }

        for (int oi=0; oi<B_COUNT; ++oi) {
            const int id = order[oi];
            if (!enabled[id]) continue;
            switch (id) {
                case B_GATE:  runMono(p->gate, L, R, len, p->mono, stereo); break;
                case B_COMP:  runMono(p->comp, L, R, len, p->mono, stereo); break;
                case B_FUZZ:  runMono(fuzzPedal==0 ? *p->fuzzMuff : *p->fuzzBender, L, R, len, p->mono, stereo); break;
                case B_DRIVE: runMono(p->drive, L, R, len, p->mono, stereo); break;
                case B_AMP: {
                    if (!stereo) for (int i=0;i<len;++i) R[i]=L[i];
                    float* io[2] = { L, R };
                    amp->process(io, io, len, 2);
                    p->pa.process(io, io, len, 2);
                    if (ampMakeup != 1.0f) for (int i=0;i<len;++i){ L[i]*=ampMakeup; R[i]*=ampMakeup; }
                    stereo = true;
                    break;
                }
                case B_CAB:    runStereo(p->cab,    L, R, len, stereo); break;
                case B_MODFX:  runStereo(p->modfx,  L, R, len, stereo); break;
                case B_DELAY:  runStereo(p->delay,  L, R, len, stereo); break;
                case B_REVERB: runStereo(p->reverb, L, R, len, stereo); break;
            }
        }
        // If nothing ever spread to stereo, R already mirrors L (mono blocks wrote both;
        // if the whole chain was empty, copy L→R for a mono-correct output).
        if (!stereo) for (int i=0;i<len;++i) R[i] = L[i];
    }

    // ── Master output level (the "Output" stage — last in the chain) ──
    const float outLevel = *p->ports[HF_OUT_LEVEL];
    if (outLevel != 1.0f)
        for (uint32_t i = 0; i < n; ++i) { outL[i] *= outLevel; outR[i] *= outLevel; }

    // ── Clip indicator: latch for ~250 ms whenever the output hits full scale ──
    float peak = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float a = std::fabs(outL[i]); if (a > peak) peak = a;
        a = std::fabs(outR[i]);       if (a > peak) peak = a;
    }
    if (peak >= 0.999f) p->clipHold = static_cast<int>(p->rate * 0.25);
    if (p->ports[HF_CLIP]) *p->ports[HF_CLIP] = (p->clipHold > 0) ? 1.0f : 0.0f;
    if (p->clipHold > 0)   p->clipHold -= static_cast<int>(n);

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

static void hf_cleanup(LV2_Handle h) {
    auto* p = static_cast<HexForge*>(h);
    delete p->amp;
    delete p;
}

// ── State (persist the loaded IR path) ────────────────────────────────────────
static LV2_State_Status hf_save(LV2_Handle h, LV2_State_Store_Function store,
                                LV2_State_Handle handle, uint32_t flags,
                                const LV2_Feature* const* features) {
    auto* p = static_cast<HexForge*>(h);
    if (p->irPath[0]=='\0') return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    char* ap = mapPath ? mapPath->abstract_path(mapPath->handle, p->irPath) : p->irPath;
    store(handle, p->uris.ir_file, ap, std::strlen(ap)+1, p->uris.atom_Path,
          flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    if (mapPath && ap != p->irPath) free(ap);
    return LV2_STATE_SUCCESS;
}
static LV2_State_Status hf_restore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                                   LV2_State_Handle handle, uint32_t,
                                   const LV2_Feature* const* features) {
    auto* p = static_cast<HexForge*>(h);
    size_t size=0; uint32_t type=0, vflags=0;
    const void* val = retrieve(handle, p->uris.ir_file, &size, &type, &vflags);
    if (!val || type != p->uris.atom_Path) return LV2_STATE_SUCCESS;
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    const char* ap = static_cast<const char*>(val);
    char* path = mapPath ? mapPath->absolute_path(mapPath->handle, ap) : const_cast<char*>(ap);
    std::vector<float> L, R;
    if (loadIRFile(path, p->rate, L, R)) {
        p->cab.setIR(L, R.empty()?nullptr:&R);
        std::strncpy(p->irPath, path, kPathMax-1); p->irPath[kPathMax-1]='\0';
    }
    if (mapPath && path != ap) free(path);
    return LV2_STATE_SUCCESS;
}

static const void* hf_extension_data(const char* uri) {
    static const LV2_Worker_Interface worker = { hf_work, hf_work_response, nullptr };
    static const LV2_State_Interface  state  = { hf_save, hf_restore };
    if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
    if (!std::strcmp(uri, LV2_STATE__interface))  return &state;
    return nullptr;
}

LV2_EXPORT_DESCRIPTOR(HEXFORGE_URI,
    hf_instantiate, hf_connect_port,
    nullptr, hf_run, nullptr, hf_cleanup, hf_extension_data)
