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
#include <lv2/time/time.h>
#include <ctime>
#include "hexforge_ports.h"
#include "hexforge_factory_presets.h"   // band/song factory presets (Banks 2..6), generated

#include "BiquadFilter.h"
#include "PickupVoicer.h"
#include "HumNotchComb.h"
#include "CalMeasure.h"
#include "EvhCaptureFit.h"
#include "RectoCaptureFit.h"
#include "PickupLoadSim.h"
#include "IrResample.h"
#include "AdaaSoftClip.h"
#include "NoiseGateBlock.h"
#include "CompressorBlock.h"
#include "OversamplingWrapper.h"
#include "EHXBigMuff.h"
#include "Octavia.h"
#include "ToneBenderMkII.h"
#include "ZVexFuzzFactory.h"
#include "OverdriveBlock.h"
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include "CabinetBlock.h"
#include "DefaultCabIR.h"
#include "CabModels.h"
#include "ModulationBlock.h"
#include "ModulationFactory.h"
#include "DelayBlock.h"
#include "DelayFactory.h"
#include "PlateReverbBlock.h"
#include "WahBlock.h"
#include "OctaveBlock.h"
#include "NailDistortion.h"
#include "NamModel.h"
#include "DenormalGuard.h"

#include <new>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include "engine/hf_platform.h"

#define HEXFORGE_URI     "https://rpowell5064.github.io/guitaramp-suite/hexforge"
#define HEXFORGE_IR_URI  HEXFORGE_URI "#irfile"
#define HEXFORGE_AMPNAM  HEXFORGE_URI "#ampnam"
#define HEXFORGE_DRNAM   HEXFORGE_URI "#drnam"
#define HEXFORGE_CABNAM  HEXFORGE_URI "#cabnam"
#define HEXFORGE_AMP2NAM HEXFORGE_URI "#amp2nam"
#define HEXFORGE_IR2_URI HEXFORGE_URI "#ir2file"
#define HEXFORGE_DR2NAM  HEXFORGE_URI "#dr2nam"
#include "engine/hf_types.inc"
static void mapURIs(HexForge* p) {
    LV2_URID_Map* m = p->map;
    p->uris.atom_Object   = m->map(m->handle, LV2_ATOM__Object);
    p->uris.atom_Path     = m->map(m->handle, LV2_ATOM__Path);
    p->uris.atom_URID     = m->map(m->handle, LV2_ATOM__URID);
    p->uris.atom_String   = m->map(m->handle, LV2_ATOM__String);
    p->uris.atom_Chunk    = m->map(m->handle, LV2_ATOM__Chunk);
    p->uris.patch_Set     = m->map(m->handle, LV2_PATCH__Set);
    p->uris.patch_Get     = m->map(m->handle, LV2_PATCH__Get);
    p->uris.patch_property= m->map(m->handle, LV2_PATCH__property);
    p->uris.patch_value   = m->map(m->handle, LV2_PATCH__value);
    p->uris.ir_file       = m->map(m->handle, HEXFORGE_IR_URI);
    p->uris.amp_nam       = m->map(m->handle, HEXFORGE_AMPNAM);
    p->uris.dr_nam        = m->map(m->handle, HEXFORGE_DRNAM);
    p->uris.cab_nam       = m->map(m->handle, HEXFORGE_CABNAM);
    p->uris.amp2_nam      = m->map(m->handle, HEXFORGE_AMP2NAM);
    p->uris.ir2_file      = m->map(m->handle, HEXFORGE_IR2_URI);
    p->uris.dr2_nam       = m->map(m->handle, HEXFORGE_DR2NAM);
    p->uris.ps_name       = m->map(m->handle, HEXFORGE_URI "#ps_name");
    p->uris.ps_index      = m->map(m->handle, HEXFORGE_URI "#ps_index");
    p->uris.ps_apply      = m->map(m->handle, HEXFORGE_URI "#ps_apply");
    p->uris.preset_blob   = m->map(m->handle, HEXFORGE_URI "#preset_blob");
    p->uris.meters        = m->map(m->handle, HEXFORGE_URI "#meters");
    p->uris.tuner         = m->map(m->handle, HEXFORGE_URI "#tuner");
    p->uris.cal           = m->map(m->handle, HEXFORGE_URI "#cal");
    p->uris.midi_MidiEvent= m->map(m->handle, LV2_MIDI_MidiEvent_URI);
    p->uris.time_Position = m->map(m->handle, LV2_TIME__Position);
    p->uris.time_bpm      = m->map(m->handle, LV2_TIME__beatsPerMinute);
    p->uris.atom_Float    = m->map(m->handle, LV2_ATOM__Float);
}
static void writeFileToNotify(HexForge* p, LV2_URID prop, const char* path) {
    const URIs& u = p->uris;
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&p->forge, 0);
    lv2_atom_forge_object(&p->forge, &frame, 0, u.patch_Set);
    lv2_atom_forge_key(&p->forge, u.patch_property);
    lv2_atom_forge_urid(&p->forge, prop);
    lv2_atom_forge_key(&p->forge, u.patch_value);
    lv2_atom_forge_path(&p->forge, path, static_cast<uint32_t>(std::strlen(path)));
    lv2_atom_forge_pop(&p->forge, &frame);
}

// ── Preset engine: notify emitters + recall/save/bank/move ────────────────────
// All of these run inside hf_run with the notify forge sequence already open, so
// they may append patch:Set messages to the UI.
static void forgeStringSet(HexForge* p, LV2_URID prop, const char* s) {
    const URIs& u = p->uris;
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&p->forge, 0);
    lv2_atom_forge_object(&p->forge, &frame, 0, u.patch_Set);
    lv2_atom_forge_key(&p->forge, u.patch_property);
    lv2_atom_forge_urid(&p->forge, prop);
    lv2_atom_forge_key(&p->forge, u.patch_value);
    lv2_atom_forge_string(&p->forge, s, static_cast<uint32_t>(std::strlen(s)));
    lv2_atom_forge_pop(&p->forge, &frame);
}
// "bank|slot|name0|name1|...|name31" — drives the UI bank indicator + name list.
static void emitIndex(HexForge* p) {
    if (!p->notify) return;
    char buf[6144]; int o = 0;   // 32 banks × 4 slots of names
    o += std::snprintf(buf, sizeof(buf), "%d|%d", p->curBank, p->curSlot);
    for (int b = 0; b < kBanks; ++b)
        for (int s = 0; s < kSlots; ++s) {
            const Preset& pr = p->presets[b][s];
            const char* nm = (pr.used && pr.name[0]) ? pr.name : "";
            o += std::snprintf(buf + o, sizeof(buf) - o, "|%s", nm);
            if (o >= (int)sizeof(buf) - 40) { b = kBanks; break; }
        }
    forgeStringSet(p, p->uris.ps_index, buf);
}
// "sym=val;sym=val;..." for every param port — the UI replays it via set_port_value.
static void emitApply(HexForge* p) {
    if (!p->notify) return;
    char buf[6144]; int o = 0; buf[0] = '\0';
    for (int i = 0; i < HF_N_PORTS; ++i)
        if (isParamPort(i) && o < (int)sizeof(buf) - 40)
            o += std::snprintf(buf + o, sizeof(buf) - o, "%s=%g;", HF_PORT_SYM[i], p->eff[i]);
    forgeStringSet(p, p->uris.ps_apply, buf);
}
// Publish the active bank/slot/name to a status file the pi-Stomp LCD reads
// ("<bank><slot> <name>", e.g. "1A Clean"). Written only on preset changes.
static void hfWriteStatus(HexForge* p) {
    FILE* f = std::fopen("/tmp/hexforge_status", "w");
    if (!f) return;
    const Preset& pr = p->presets[p->curBank][p->curSlot];
    std::fprintf(f, "%d%c %s\n", p->curBank + 1, 'A' + p->curSlot,
                 (pr.used && pr.name[0]) ? pr.name : "(empty)");
    std::fclose(f);
}
// ── Engine ⇄ host bridge impls (Stage B1): the LV2 side of the interfaces ─────
struct Lv2Worker final : HfWorkerIface {
    HexForge* p = nullptr;
    bool schedule(const void* msg, uint32_t size) override {
        return p->schedule->schedule_work(p->schedule->handle, size, msg) == LV2_WORKER_SUCCESS;
    }
};
struct Lv2Host final : HfHostIface {
    HexForge* p = nullptr;
    LV2_URID uridFor(int prop) const {
        switch (prop) {
            case HFP_PS_NAME:  return p->uris.ps_name;
            case HFP_METERS:   return p->uris.meters;
            case HFP_TUNER:    return p->uris.tuner;
            case HFP_CAL:      return p->uris.cal;
            case HFP_IR_FILE:  return p->uris.ir_file;
            case HFP_AMP_NAM:  return p->uris.amp_nam;
            case HFP_DR_NAM:   return p->uris.dr_nam;
            case HFP_CAB_NAM:  return p->uris.cab_nam;
            case HFP_AMP2_NAM: return p->uris.amp2_nam;
            case HFP_IR2_FILE: return p->uris.ir2_file;
            case HFP_DR2_NAM:  return p->uris.dr2_nam;
        }
        return 0;
    }
    void stringSet(int prop, const char* s) override { forgeStringSet(p, uridFor(prop), s); }
    void fileSet(int prop, const char* path) override { writeFileToNotify(p, uridFor(prop), path); }
    void emitIndex() override { ::emitIndex(p); }
    void emitApply() override { ::emitApply(p); }
    void statusDump() override { hfWriteStatus(p); }
};
#include "engine/hf_presets.inc"
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
    {   // engine ⇄ host bridges (Stage B1)
        auto* w = new(std::nothrow) Lv2Worker;
        auto* hb = new(std::nothrow) Lv2Host;
        if (!w || !hb) { delete w; delete hb; delete p; return nullptr; }
        w->p = p; hb->p = p;
        p->worker = w; p->host = hb;
    }

    p->rate = rate;
    p->trimHum.prepare(rate);
    p->trimLoad.prepare(rate);
    // (EVH lab EQ coeffs are set in run() from dbg_evhfit on first use / knob move.)
    p->rectoFit[0].prepare(rate);
    p->rectoFit[1].prepare(rate);
    // (FRFR voice EQ coeffs are set in run() from the fv_* knobs on first
    // enable + on any knob move — see the fvOn block before the Output stage.)
    p->trimVoice.reset();   // coeffs are set lazily in run() from the live HB Amount port
    p->trimBoost.reset();
    p->gate.prepare(rate, kMaxBlock, 1);
    p->comp.prepare(rate, kMaxBlock, 1);
    // Build fuzz models directly (NOT via OverdriveFactory) — same as the fuzz plugin.
    p->fuzzMuff   = std::make_unique<OversamplingWrapper>(std::make_unique<EHXBigMuff>());
    p->fuzzBender = std::make_unique<OversamplingWrapper>(std::make_unique<ToneBenderMkII>());
    p->fuzzOctavia= std::make_unique<OversamplingWrapper>(std::make_unique<Octavia>());
    p->fuzzFactory= std::make_unique<OversamplingWrapper>(std::make_unique<ZVexFuzzFactory>());
    if (!p->fuzzMuff || !p->fuzzBender || !p->fuzzOctavia || !p->fuzzFactory) { delete p; return nullptr; }
    p->fuzzMuff->prepare(rate, kMaxBlock, 1);
    p->fuzzBender->prepare(rate, kMaxBlock, 1);
    p->fuzzOctavia->prepare(rate, kMaxBlock, 1);
    p->fuzzFactory->prepare(rate, kMaxBlock, 1);
    p->fuzzMuff->setParameter("era", 2.0f);
    p->drive.prepare(rate, kMaxBlock, 1);
    p->drive2.prepare(rate, kMaxBlock, 1);
    p->pa2.prepare(rate, kMaxBlock, 1);
    p->cab2.prepare(rate, kMaxBlock, 1);
    p->drive.setType(kDriveMap[0]);
    // Nail — industrial distortion (oversampled, like the fuzzes)
    p->nail = std::make_unique<OversamplingWrapper>(std::make_unique<NailDistortion>());
    if (!p->nail) { delete p; return nullptr; }
    p->nail->prepare(rate, kMaxBlock, 1);
    p->tuner.prepare(rate);
    p->nail->setParameter("mode", 2.0f);
    p->amp = new(std::nothrow) AmpBlockExtended;
    if (!p->amp) { delete p; return nullptr; }
    p->amp->prepare(rate, kMaxBlock, 2);
    p->amp->setAmpModel(kAmpMap[1]);   // default Crunchy McCrunchFace
    p->pa.prepare(rate, kMaxBlock, 2);
    p->cab.prepare(rate, kMaxBlock, 2);
    p->cab.setIR(CabModels::generate("@factory", rate));   // enriched Factory Cab (2026-07-14)
    p->modfx.prepare(rate, kMaxBlock, 2);
    p->modfx.setType(ModulationFactory::fromIndex(0));
    p->delay.prepare(rate, kMaxBlock, 2);
    p->delay.setType(DelayFactory::fromIndex(0));
    p->reverb.prepare(rate, kMaxBlock, 2);
    p->wah.prepare(rate, kMaxBlock, 2);
    p->octave.prepare(rate, kMaxBlock, 2);
    p->autoOut.prepare(rate);
    p->eq.prepare(rate);
    // X2 second instances (v38)
    p->gate2.prepare(rate, kMaxBlock, 1);
    p->comp2.prepare(rate, kMaxBlock, 1);
    p->fuzz2Muff   = std::make_unique<OversamplingWrapper>(std::make_unique<EHXBigMuff>());
    p->fuzz2Bender = std::make_unique<OversamplingWrapper>(std::make_unique<ToneBenderMkII>());
    p->fuzz2Octavia= std::make_unique<OversamplingWrapper>(std::make_unique<Octavia>());
    p->fuzz2Factory= std::make_unique<OversamplingWrapper>(std::make_unique<ZVexFuzzFactory>());
    p->nail2       = std::make_unique<OversamplingWrapper>(std::make_unique<NailDistortion>());
    if (!p->fuzz2Muff || !p->fuzz2Bender || !p->fuzz2Octavia || !p->fuzz2Factory || !p->nail2) { delete p; return nullptr; }
    p->fuzz2Muff->prepare(rate, kMaxBlock, 1);
    p->fuzz2Bender->prepare(rate, kMaxBlock, 1);
    p->fuzz2Octavia->prepare(rate, kMaxBlock, 1);
    p->fuzz2Factory->prepare(rate, kMaxBlock, 1);
    p->fuzz2Muff->setParameter("era", 2.0f);
    p->nail2->prepare(rate, kMaxBlock, 1);
    p->nail2->setParameter("mode", 2.0f);
    p->modfx2.prepare(rate, kMaxBlock, 2);
    p->modfx2.setType(ModulationFactory::fromIndex(0));
    p->delay2.prepare(rate, kMaxBlock, 2);
    p->delay2.setType(DelayFactory::fromIndex(0));
    p->reverb2.prepare(rate, kMaxBlock, 2);
    p->wah2.prepare(rate, kMaxBlock, 2);
    p->octave2.prepare(rate, kMaxBlock, 2);
    p->eq2.prepare(rate);
    p->dblBuf.assign(size_t(rate * 0.045) + 4, 0.0f);   // doubler: 28 ms base ± 7 ms wander + margin
    p->dblApA.assign(size_t(rate * 0.0053) + 2, 0.0f);  // mono-blend phase diffusers
    p->dblApB.assign(size_t(rate * 0.0089) + 2, 0.0f);
    psInitDefaults(p);   // Bank 1 / A–D pre-filled (overwritten by hf_restore if state exists)
    hfLoadBackup(p);     // recover the user's full preset store across delete/re-add + updates
    return p;
}

static void hf_connect_port(LV2_Handle h, uint32_t port, void* data) {
    auto* p = static_cast<HexForge*>(h);
    if (port == HF_CONTROL)      p->control = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == HF_NOTIFY)  p->notify  = static_cast<LV2_Atom_Sequence*>(data);
    else if (port == HF_MIDI_IN) p->midiIn  = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port < HF_N_PORTS) {
        p->hostPorts[port] = static_cast<float*>(data);
        // Once primed, param ports stay pointed at eff[]; only re-point others.
        if (!p->primed || !isParamPort(static_cast<int>(port)))
            p->ports[port] = static_cast<float*>(data);
    }
}

// ── Worker ────────────────────────────────────────────────────────────────────
// mod-ui MATERIALIZES atom:Path values when a pedalboard is saved: a built-in
// "@sentinel" cab becomes "<pedalboard>/effect-N/@sentinel" on restore, plus a
// BROKEN self-referential symlink on disk. Found 2026-08-21: every preset with
// a non-@factory synthetic cab had silently fallen back to the factory V30
// since the pedalboard was last saved (the '@' prefix check missed the
// absolute form, then the symlink failed to load). Recover the sentinel from
// the BASENAME so any path shape mod-ui hands back resolves correctly.
static const char* cabSentinel(const char* path) {
    const char* base = std::strrchr(path, '/');
    base = base ? base + 1 : path;
    return base[0] == '@' ? base : nullptr;
}
#include "engine/hf_worker.inc"
#include "engine/hf_run_core.inc"

// ── run(): LV2 event plumbing around the host-API-free engine (Stage B2) ──────
// Order preserved exactly from the monolith: prime → atom control loop → engine
// (live-edit detect, command edges, MIDI-press replay, audio, notify pushes) →
// close the notify sequence.
static void hf_run(LV2_Handle h, uint32_t n) {
    DenormalGuard denormalGuard;
    auto* p = static_cast<HexForge*>(h);
    const URIs& u = p->uris;
    hfPrime(p);

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
                if (!prop || prop->type!=u.atom_URID || !val) continue;
                const LV2_URID which = reinterpret_cast<const LV2_Atom_URID*>(prop)->body;
                // Rename the active preset (UI -> plugin, String value).
                if (val->type == u.atom_String) {
                    if (which == u.ps_name) {
                        const char* s = static_cast<const char*>(LV2_ATOM_BODY_CONST(val));
                        Preset& pr = p->presets[p->curBank][p->curSlot];
                        std::strncpy(pr.name, s, sizeof(pr.name)-1); pr.name[sizeof(pr.name)-1]='\0';
                        for (char* c=pr.name; *c; ++c) if (*c=='|') *c=' ';   // keep index delimiter clean
                        pr.used = true;
                        if (haveNotify) p->host->emitIndex();
                        p->host->statusDump();
                        hfWriteBackup(p);
                    }
                    continue;
                }
                if (val->type != u.atom_Path) continue;
                const char* path = static_cast<const char*>(LV2_ATOM_BODY_CONST(val));
                char* dst = nullptr; WorkMsg msg;
                if      (which == u.ir_file) { dst = p->irPath;     msg.type = W_CAB_IR; }
                else if (which == u.amp_nam) { dst = p->ampNamPath; msg.type = W_NAM_LOAD; msg.namSlot = 0; }
                else if (which == u.dr_nam)  { dst = p->drNamPath;  msg.type = W_NAM_LOAD; msg.namSlot = 1; }
                else if (which == u.cab_nam) { dst = p->cabNamPath; msg.type = W_NAM_LOAD; msg.namSlot = 2; }
                else if (which == u.amp2_nam){ dst = p->amp2NamPath; msg.type = W_NAM_LOAD; msg.namSlot = 3; }
                else if (which == u.dr2_nam) { dst = p->dr2NamPath; msg.type = W_NAM_LOAD; msg.namSlot = 4; }
                else if (which == u.ir2_file){
                    // Cab 2 user IR: "@builtin" (or empty) = defer to the rb_cab
                    // dropdown -- clear the override and let run() regenerate the
                    // sentinel; a real path loads on the Cab 2 slot.
                    const char* e2 = (std::strcmp(path, "@builtin") == 0) ? "" : path;
                    std::strncpy(p->ir2Path, e2, kPathMax-1); p->ir2Path[kPathMax-1]='\0';
                    if (std::strcmp(e2, "@nocab") == 0) { p->lastCab2Model = -2; continue; }   // bypassed below; nothing to load
                    if (e2[0]) {
                        WorkMsg m2; m2.type = W_CAB_IR; m2.namSlot = 1;
                        std::strncpy(m2.path, e2, kPathMax-1); m2.path[kPathMax-1]='\0';
                        p->worker->schedule(&m2, sizeof(m2));
                    } else p->lastCab2Model = -1;   // re-sentinel from rb_cab next block
                    continue;
                }
                if (dst) {
                    // "Factory Cab" sentinel → clear IR to the built-in default.
                    const char* eff = (dst == p->irPath && std::strcmp(path, kFactoryIR) == 0) ? "" : path;
                    std::strncpy(dst, eff, kPathMax-1); dst[kPathMax-1]='\0';
                    std::strncpy(msg.path, dst, kPathMax-1); msg.path[kPathMax-1]='\0';
                    p->worker->schedule(&msg, sizeof(msg));
                }
            } else if (obj->body.otype == u.patch_Get && haveNotify) {
                p->host->fileSet(HFP_IR_FILE, p->irPath);
                p->host->fileSet(HFP_AMP_NAM, p->ampNamPath);
                p->host->fileSet(HFP_DR_NAM, p->drNamPath);
                p->host->fileSet(HFP_CAB_NAM, p->cabNamPath);
                p->host->fileSet(HFP_AMP2_NAM, p->amp2NamPath);
                p->host->fileSet(HFP_IR2_FILE, p->ir2Path);
                p->host->fileSet(HFP_DR2_NAM, p->dr2NamPath);
                p->host->stringSet(HFP_PS_NAME, p->presets[p->curBank][p->curSlot].name);
                p->host->emitIndex();
                p->host->emitApply();   // sync knobs to the active preset's effective values
            } else if (obj->body.otype == u.time_Position) {
                // Host tempo (tap-tempo / MIDI clock) → cache BPM for the synced Delay/Mod.
                const LV2_Atom* bpmA = nullptr;
                lv2_atom_object_get(obj, u.time_bpm, &bpmA, 0);
                if (bpmA && bpmA->type == u.atom_Float) {
                    const float b = reinterpret_cast<const LV2_Atom_Float*>(bpmA)->body;
                    if (b >= 20.0f && b <= 400.0f) p->hostBpm = b;
                }
            }
        }
    }

    // ── Footswitch MIDI (pi-Stomp CC 60..63): each CC message = one switch press,
    // replayed inside the engine at the exact point the decode used to sit ──
    int swPresses[16]; int nSw = 0;
    if (p->midiIn) {
        LV2_ATOM_SEQUENCE_FOREACH(p->midiIn, ev) {
            if (ev->body.type != u.midi_MidiEvent || ev->body.size < 3) continue;
            const uint8_t* m = static_cast<const uint8_t*>(LV2_ATOM_BODY_CONST(&ev->body));
            if ((m[0] & 0xF0) != 0xB0) continue;            // Control Change, any channel
            if (m[2] < 64) continue;                        // press-down only (ignore the release = value 0)
            const int sw = static_cast<int>(m[1]) - kMidiBaseCC;
            if (sw >= 0 && sw <= 3 && nSw < 16) swPresses[nSw++] = sw;
        }
    }

    hfEngineRun(p, n, swPresses, nSw);

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

static void hf_cleanup(LV2_Handle h) {
    auto* p = static_cast<HexForge*>(h);
    delete p->amp;
    delete p->ampNam; delete p->drNam; delete p->cabNam; delete p->amp2Nam; delete p->dr2Nam;
    delete p->worker; delete p->host;   // Stage B1 bridges
    delete p;
}

// ── State (persist the loaded IR path) ────────────────────────────────────────
static LV2_State_Status hf_save(LV2_Handle h, LV2_State_Store_Function store,
                                LV2_State_Handle handle, uint32_t flags,
                                const LV2_Feature* const* features) {
    auto* p = static_cast<HexForge*>(h);
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    auto saveOne = [&](LV2_URID prop, const char* raw) {
        if (raw[0] == '\0') return;
        char* ap = mapPath ? mapPath->abstract_path(mapPath->handle, const_cast<char*>(raw)) : const_cast<char*>(raw);
        store(handle, prop, ap, std::strlen(ap)+1, p->uris.atom_Path,
              flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
        if (mapPath && ap != raw) free(ap);
    };
    saveOne(p->uris.ir_file, p->irPath);
    saveOne(p->uris.amp_nam, p->ampNamPath);
    saveOne(p->uris.dr_nam,  p->drNamPath);
    saveOne(p->uris.cab_nam, p->cabNamPath);
    saveOne(p->uris.amp2_nam, p->amp2NamPath);
    saveOne(p->uris.ir2_file, p->ir2Path);
    saveOne(p->uris.dr2_nam, p->dr2NamPath);

    // ── Preset store: one self-describing Chunk holding all 8×4 presets ──
    std::vector<uint8_t> blob;
    auto putBytes = [&](const void* d, size_t n){ const uint8_t* b=(const uint8_t*)d; blob.insert(blob.end(), b, b+n); };
    auto putU32   = [&](uint32_t v){ putBytes(&v, 4); };
    auto putPath  = [&](const char* raw){   // stored portable (abstract)
        char* ap = (raw[0] && mapPath) ? mapPath->abstract_path(mapPath->handle, const_cast<char*>(raw)) : nullptr;
        const char* s = ap ? ap : raw;
        uint32_t len = static_cast<uint32_t>(std::strlen(s));
        putU32(len); putBytes(s, len);
        if (ap) free(ap);
    };
    putU32(44);                 // version (44: + rb_locut; 43: + SIR #34 pair; 42: + EP-3 Age pair; 41: + Plexi Variac; 40: + fuzz/nail Eco + Drive 2 NAM; 39: + Amp 2 NAM trims/path + Cab 2 IR path; 38: + X2 clone families; 37: + Cab 2 presence; 36: + Rig B full parity; 35: + Rig B dual amp/cab; 34: + Drive B block; 33: + Drive Eco; 32: + Engine Quality; 31: + CPU meter outputs; 30: + fuzz guitar vol; 29: + tremolo shape; 28: + speaker drive; 27: + ambient bloom; 26: + reverb type / room density; 25: + reverb density; 24: + pickup load / coupling; 19: + NAM gain/level trims; 18: + Mod Center Delay; 17: + Cali V EQ preset; 16: + Cali V graphic EQ; 15: + Cali V Mesa mode; 14: + Octave shimmer; 13: + tempo-sync; 12: + Nail; 11: + factory rev; 10: + Output Mono Sum; 9: + per-block bypass; 8: + Wah/Octave; 7: + Seraph; 6: + Boost; 5: + HB Model; 4: + HB voicing; 3: dB; 2: linear)
    putU32(kBanks); putU32(kSlots); putU32(HF_N_PORTS); putU32(kFactoryRev);
    for (int b=0;b<kBanks;++b) for (int s=0;s<kSlots;++s) {
        const Preset& pr = p->presets[b][s];
        putU32(pr.used ? 1u : 0u);
        putBytes(pr.name, sizeof(pr.name));
        putBytes(pr.vals, sizeof(pr.vals));
        putPath(pr.irPath); putPath(pr.ampNamPath); putPath(pr.drNamPath); putPath(pr.cabNamPath);
        putPath(pr.amp2NamPath); putPath(pr.ir2Path);   // v39
        putPath(pr.dr2NamPath);                         // v40
    }
    putU32(static_cast<uint32_t>(p->curBank));
    putU32(static_cast<uint32_t>(p->curSlot));
    store(handle, p->uris.preset_blob, blob.data(), blob.size(), p->uris.atom_Chunk,
          flags | LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    hfWriteBackup(p);   // mirror the store to the off-instance backup on every board save
    return LV2_STATE_SUCCESS;
}
static LV2_State_Status hf_restore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                                   LV2_State_Handle handle, uint32_t,
                                   const LV2_Feature* const* features) {
    auto* p = static_cast<HexForge*>(h);
    auto* mapPath = static_cast<LV2_State_Map_Path*>(lv2_find_feature(features, LV2_STATE__mapPath));
    size_t size=0; uint32_t type=0, vflags=0;

    auto absOf = [&](const void* val) -> char* {
        const char* ap = static_cast<const char*>(val);
        return mapPath ? mapPath->absolute_path(mapPath->handle, ap) : const_cast<char*>(ap);
    };
    // IR
    const void* irv = retrieve(handle, p->uris.ir_file, &size, &type, &vflags);
    if (irv && type == p->uris.atom_Path) {
        char* path = absOf(irv);
        std::vector<float> L, R;
        if (loadIRFile(path, p->rate, L, R)) {
            p->cab.setIR(L, R.empty()?nullptr:&R);
            std::strncpy(p->irPath, path, kPathMax-1); p->irPath[kPathMax-1]='\0';
        }
        if (mapPath && path != irv) free(path);
    }
    // NAM x3
    auto restoreNam = [&](LV2_URID prop, NamModel** slot, char* pathDst) {
        const void* v = retrieve(handle, prop, &size, &type, &vflags);
        if (!v || type != p->uris.atom_Path) return;
        char* path = absOf(v);
        auto* nm = new(std::nothrow) NamModel;
        if (nm && nm->loadFromFile(path)) {
            nm->reset(p->rate, kMaxBlock);
            delete *slot; *slot = nm;
            std::strncpy(pathDst, path, kPathMax-1); pathDst[kPathMax-1]='\0';
        } else delete nm;
        if (mapPath && path != v) free(path);
    };
    restoreNam(p->uris.amp_nam, &p->ampNam, p->ampNamPath);
    restoreNam(p->uris.dr_nam,  &p->drNam,  p->drNamPath);
    restoreNam(p->uris.cab_nam, &p->cabNam, p->cabNamPath);
    restoreNam(p->uris.amp2_nam, &p->amp2Nam, p->amp2NamPath);
    restoreNam(p->uris.dr2_nam, &p->dr2Nam, p->dr2NamPath);
    {   // Cab 2 user IR: restore the override path; run() schedules the load
        size_t size=0; uint32_t type=0, vflags=0;
        const void* v = retrieve(handle, p->uris.ir2_file, &size, &type, &vflags);
        if (v && type == p->uris.atom_Path) {
            char* path = const_cast<char*>(static_cast<const char*>(v));
            if (mapPath) path = mapPath->absolute_path(mapPath->handle, path);
            std::strncpy(p->ir2Path, path, kPathMax-1); p->ir2Path[kPathMax-1]='\0';
            p->lastCab2Model = -1;   // (re)load on the first enabled block
            if (mapPath && path != v) free(path);
        }
    }

    // ── Preset store ──
    const void* bv = retrieve(handle, p->uris.preset_blob, &size, &type, &vflags);
    if (bv && type == p->uris.atom_Chunk && size >= 12) {
        const uint8_t* d = static_cast<const uint8_t*>(bv); size_t off = 0;
        auto getU32 = [&](uint32_t& v)->bool { if (off+4>size) return false; std::memcpy(&v, d+off, 4); off+=4; return true; };
        auto getPath = [&](char* dst){
            uint32_t len=0; dst[0]='\0';
            if (!getU32(len) || off+len>size) { off = size; return; }
            char tmp[kPathMax]; uint32_t m = len < kPathMax-1 ? len : kPathMax-1;
            std::memcpy(tmp, d+off, m); tmp[m]='\0'; off += len;
            if (tmp[0] && mapPath) { char* ab = mapPath->absolute_path(mapPath->handle, tmp);
                std::strncpy(dst, ab, kPathMax-1); dst[kPathMax-1]='\0'; if (ab != tmp) free(ab); }
            else { std::strncpy(dst, tmp, kPathMax-1); dst[kPathMax-1]='\0'; }
        };
        uint32_t ver=0, nb=0, ns=0, np=0; getU32(ver); getU32(nb); getU32(ns);
        if (ver < 2 || ver > 44) return LV2_STATE_SUCCESS;    // unknown layout — start fresh
        const bool migrateOutDb = (ver == 2);     // v2 stored out_level as 0..1 linear
        const bool needMigrate  = (ver < 44);     // ...v44 rb_locut
        getU32(np);
        if (ver == 36 && np >= 318) ver = 37;     // deployed v36 stamps already carry rb_cab2on (318-param layout)                                 // param-port count at save time
        uint32_t factoryRev = 0; if (ver >= 11) getU32(factoryRev);   // v11+: factory-preset revision
        const uint32_t npc = np < (uint32_t)HF_N_PORTS ? np : (uint32_t)HF_N_PORTS;
        for (uint32_t b=0;b<nb;++b) for (uint32_t s=0;s<ns;++s) {
            uint32_t used=0; getU32(used);
            char name[32] = {0}; if (off+sizeof(name)<=size){ std::memcpy(name,d+off,sizeof(name)); off+=sizeof(name); }
            float vals[HF_N_PORTS]; for (int i=0;i<HF_N_PORTS;++i) vals[i]=0.0f;
            if (off + (size_t)np*4 <= size) { std::memcpy(vals, d+off, (size_t)npc*4); off += (size_t)np*4; }
            else off = size;
            if (needMigrate) migratePorts(vals, ver);                 // insert voicing/boost + Seraph ports
            if (migrateOutDb) vals[HF_OUT_LEVEL] = linToDb(vals[HF_OUT_LEVEL]);  // 0..1 -> dB
            if (ver < 10) vals[HF_OUT_MONO] = 1.0f;   // pre-v10 saves default to MONO
            char ir[kPathMax],an[kPathMax],dn[kPathMax],cn[kPathMax],a2[kPathMax],i2[kPathMax],d2n[kPathMax];
            getPath(ir); getPath(an); getPath(dn); getPath(cn);
            a2[0] = i2[0] = d2n[0] = '\0';
            if (ver >= 39) { getPath(a2); getPath(i2); }   // v39: Amp 2 NAM + Cab 2 IR
            if (ver >= 40) getPath(d2n);                   // v40: Drive 2 NAM
            if (b<kBanks && s<kSlots) {     // ignore extras if a future build grows the grid
                Preset& pr = p->presets[b][s];
                if (used == 0 && pr.used) continue;   // keep factory-seeded preset in an empty restored slot
                pr.used = (used != 0);
                std::memcpy(pr.name, name, sizeof(pr.name)); pr.name[sizeof(pr.name)-1]='\0';
                std::memcpy(pr.vals, vals, sizeof(pr.vals));
                std::strncpy(pr.irPath,ir,kPathMax-1);     pr.irPath[kPathMax-1]='\0';
                std::strncpy(pr.ampNamPath,an,kPathMax-1); pr.ampNamPath[kPathMax-1]='\0';
                std::strncpy(pr.drNamPath,dn,kPathMax-1);  pr.drNamPath[kPathMax-1]='\0';
                std::strncpy(pr.cabNamPath,cn,kPathMax-1); pr.cabNamPath[kPathMax-1]='\0';
            std::strncpy(pr.amp2NamPath,a2,kPathMax-1); pr.amp2NamPath[kPathMax-1]='\0';
            std::strncpy(pr.ir2Path,i2,kPathMax-1);     pr.ir2Path[kPathMax-1]='\0';
            std::strncpy(pr.dr2NamPath,d2n,kPathMax-1); pr.dr2NamPath[kPathMax-1]='\0';
            }
        }
        uint32_t cb=0, cs=0; getU32(cb); getU32(cs);   // (saved cursor read for byte alignment, then discarded)
        p->curBank = 0; p->curSlot = 0;   // ALWAYS land on the first preset (Bank 1 / A) on load
        if (factoryRev < kFactoryRev) seedFactoryPresets(p);   // refresh updated factory slots (user slots kept)
        p->pendingRecall = true;   // first run applies the active preset to the DSP
    }
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
