// ─────────────────────────────────────────────────────────────────────────────
// GuitarAmp Suite — Amp LV2 plugin
//
// Ports the VST amp model selector (5 algorithmic amps) plus a full power-amp
// stage with bypass. Neural/NAM model loading has been removed — use a dedicated
// NAM block on the pi-Stomp instead.
//
// Analog model switches rebuild the amp off the audio thread (a fresh
// AmpBlockExtended allocates its OversamplingWrapper), so the LV2 Worker
// extension is used: the worker builds the new instance and the audio thread
// swaps it in with a single pointer assignment, freeing the old one back on the
// worker thread — the real-time thread never allocates or blocks.
//
// The power amp (PowerAmpProcessor) is a separate stage after the amp. It is
// auto-bypassed for the Sunn Model T — that model is a *complete* amp (its own
// 6550 power stage + output transformer + NFB), so the shared PA would double-
// stack the power section. Every other model is preamp-only and needs the PA.
// ─────────────────────────────────────────────────────────────────────────────
#include "lv2_util.h"
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include "DenormalGuard.h"
#include <new>
#include <cstring>

#define AMP_URI "https://rpowell5064.github.io/guitaramp-suite/amp"

// LV2 model index (0..4) → AmpModel enum. NAM is no longer selectable, so the
// list skips NeuralCustom.
static const AmpModel kModelMap[5] = {
    AmpModel::FenderDeluxe,        // 0
    AmpModel::MarshallJCM800,      // 1
    AmpModel::EVH5150III,          // 2
    AmpModel::SunnModelT,          // 3
    AmpModel::OrangeRockerverb50,  // 4
};
// LV2 model index → canonical model index (0=Fender..5=Rockerverb, with NAM=3)
// used by PowerAmpProcessor::getDefaultsForModel and the VST. The LV2 list omits
// NAM, so Sunn/Rockerverb shift down by one relative to the canonical ordering.
static const int kCanonical[5] = { 0, 1, 2, 4, 5 };
static constexpr int kSunnIdx  = 3;        // Sunn's LV2 model index
static constexpr int kMaxModel = 4;
static constexpr int kMaxBlock = 512;      // internal processing chunk

// Per-model power tube (TubeType: 0=6L6GC, 1=EL34, 2=EL84, 3=KT88), matching
// AmpModelFactory::recommendedTubeType, LV2-indexed.
static const int kModelTube[5] = {
    0,  // 0 Fender     → 6L6GC
    1,  // 1 Marshall   → EL34
    1,  // 2 EVH        → EL34
    0,  // 3 Sunn       → 6L6GC
    1,  // 4 Rockerverb → EL34
};

// Per-model output loudness makeup (linear), LV2-indexed. The clean amps (Fender)
// are much quieter than the high-gain ones; this lifts them toward a common level
// so switching models doesn't jump in volume. Reference (1.0) = Marshall JCM800.
static const float kModelMakeup[5] = {
    1.8f,  // 0 Fender Deluxe   (+5 dB)
    1.0f,  // 1 Marshall JCM800 (reference)
    1.4f,  // 2 EVH 5150 III    (+2.9 dB — was quieter than JCM800/Rockerverb at matched settings)
    1.0f,  // 3 Sunn Model T
    1.15f, // 4 Orange Rockerverb
};

enum AmpPorts {
    P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
    P_MODEL, P_GAIN, P_BASS, P_MID, P_TREBLE, P_PRES, P_MASTER, P_SAG,
    P_CHANNEL, P_RESON, P_SUNN_V2, P_SUNN_LNK, P_BYPASS,
    P_PA_BYPASS, P_PA_TUBE, P_PA_PRES, P_PA_DEPTH, P_PA_SAG, P_PA_MASTER,
    P_PA_NFB, P_PA_RESON, P_PA_AIR, P_PA_AUTO,
    P_SUNN_B2, P_SUNN_M2, P_SUNN_T2, P_SUNN_BR1, P_SUNN_BR2,  // Sunn Brite-channel
    P_N_PORTS
};

enum WorkType { WORK_LOAD, WORK_FREE };

// Message passed between the audio thread and the worker thread.
struct WorkMsg {
    WorkType          type;
    AmpBlockExtended* amp;       // FREE: instance to delete; LOAD reply: new instance
    int               modelIdx;  // model to pre-set on the new instance
};

static int clampIdx(float v, int lo, int hi) {
    int i = static_cast<int>(v + 0.5f);
    if (i < lo) i = lo;
    if (i > hi) i = hi;
    return i;
}

struct AmpPlugin {
    double rate = 48000.0;

    AmpBlockExtended* amp = nullptr;   // double-buffered: swapped on model change
    PowerAmpProcessor pa;

    float* ctrl[P_N_PORTS] = {};       // control/audio port buffers

    int  lastModel = -1;
    int  lastTube  = -1;

    LV2_Worker_Schedule* schedule = nullptr;
};

// Schedule an off-thread rebuild of the amp instance on a model change: the
// worker builds a fresh AmpBlockExtended (where the OversamplingWrapper
// allocation happens) and the audio thread swaps it in.
static LV2_Worker_Status scheduleRebuild(AmpPlugin* p, int modelIdx) {
    WorkMsg msg;
    msg.type     = WORK_LOAD;
    msg.amp      = nullptr;
    msg.modelIdx = modelIdx;
    return p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
}

// ── LV2 lifecycle ─────────────────────────────────────────────────────────────
static LV2_Handle amp_instantiate(const LV2_Descriptor*, double rate,
                                   const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) AmpPlugin;
    if (!p) return nullptr;

    p->schedule = static_cast<LV2_Worker_Schedule*>(lv2_find_feature(features, LV2_WORKER__schedule));
    if (!p->schedule) {   // required feature
        delete p;
        return nullptr;
    }

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
    if (port < P_N_PORTS) p->ctrl[port] = static_cast<float*>(data);
}

// ── Worker thread: build new amp instance / free old instance ─────────────────
static LV2_Worker_Status amp_work(LV2_Handle h, LV2_Worker_Respond_Function respond,
                                  LV2_Worker_Respond_Handle handle,
                                  uint32_t, const void* data) {
    auto* p = static_cast<AmpPlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);

    if (msg->type == WORK_FREE) {
        delete msg->amp;
        return LV2_WORKER_SUCCESS;
    }

    // WORK_LOAD — build a fresh amp off the RT thread (analog models allocate
    // their OversamplingWrapper here).
    auto* na = new(std::nothrow) AmpBlockExtended;
    if (!na) return LV2_WORKER_ERR_NO_SPACE;
    na->prepare(p->rate, kMaxBlock, 2);
    na->setAmpModel(kModelMap[clampIdx(static_cast<float>(msg->modelIdx), 0, kMaxModel)]);

    WorkMsg reply;
    reply.type     = WORK_LOAD;
    reply.amp      = na;
    reply.modelIdx = msg->modelIdx;
    respond(handle, sizeof(reply), &reply);
    return LV2_WORKER_SUCCESS;
}

// Runs on the audio thread (host calls it at the top of run()) — pointer swap only.
static LV2_Worker_Status amp_work_response(LV2_Handle h, uint32_t, const void* data) {
    auto* p = static_cast<AmpPlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);

    AmpBlockExtended* old = p->amp;
    p->amp = msg->amp;                       // atomic-enough: single pointer store
    // The new instance was built with this model; if the port has since moved on,
    // run() will see the mismatch and schedule another rebuild (converges).
    p->lastModel = msg->modelIdx;

    WorkMsg freeMsg;                         // free the old instance off the RT thread
    freeMsg.type = WORK_FREE;
    freeMsg.amp  = old;
    p->schedule->schedule_work(p->schedule->handle, sizeof(freeMsg), &freeMsg);
    return LV2_WORKER_SUCCESS;
}

// ── Audio ─────────────────────────────────────────────────────────────────────
static void amp_run(LV2_Handle h, uint32_t n) {
    DenormalGuard denormalGuard;   // flush denormals: keeps CPU flat into decay/silence
                                   // (prevents xrun-induced note cut-outs on the pi-Stomp)
    auto* p = static_cast<AmpPlugin*>(h);

    const int modelIdx = clampIdx(*p->ctrl[P_MODEL], 0, kMaxModel);

    AmpBlockExtended* amp = p->amp;

    // Whole-plugin bypass.
    const bool fullBypass = *p->ctrl[P_BYPASS] > 0.5f;
    amp->setBypass(fullBypass);

    // Model switch — rebuilt off the audio thread (analog models allocate an
    // OversamplingWrapper). lastModel is committed only if the request was queued,
    // so a momentarily-full worker queue can't strand a stale model.
    if (modelIdx != p->lastModel) {
        if (scheduleRebuild(p, modelIdx) == LV2_WORKER_SUCCESS)
            p->lastModel = modelIdx;
    }

    // Preamp / model parameters.
    if (modelIdx == kSunnIdx) {
        amp->setParameter("vol1",         *p->ctrl[P_GAIN]);      // "Normal Vol"
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
    amp->setParameter("channel",  *p->ctrl[P_CHANNEL]);    // EVH / Rockerverb mode
    amp->setParameter("resonance",*p->ctrl[P_RESON]);      // EVH resonance

    // Power-amp parameters. In Auto mode the voicing + tube follow the selected
    // amp model (getDefaultsForModel + per-model tube table); the pamp_* knobs
    // are honoured only when Auto is off.
    int desiredTube;
    if (*p->ctrl[P_PA_AUTO] > 0.5f) {
        const auto d = PowerAmpProcessor::getDefaultsForModel(kCanonical[modelIdx]);
        p->pa.setParameter("master",   d.master);
        p->pa.setParameter("presence", d.presence);
        p->pa.setParameter("depth",    d.depth);
        p->pa.setParameter("nfb",      d.nfb);
        p->pa.setParameter("sag",      d.sag);
        // resonance and airFeel have no per-model default — stay user-controlled.
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
    if (desiredTube != p->lastTube) {
        p->lastTube = desiredTube;
        p->pa.setTubeType(static_cast<TubeType>(desiredTube));
    }

    // Auto-bypass the modeled power amp for Sunn — the Sunn model is a *complete*
    // amp (its own 6550 power stage + output transformer + NFB internally), so
    // running the shared PowerAmpProcessor after it double-stacks the power
    // section. Every other analog model is preamp-only and needs the PA.
    const bool paBypass = fullBypass ||
                          (*p->ctrl[P_PA_BYPASS] > 0.5f) ||
                          (modelIdx == kSunnIdx);
    p->pa.setBypass(paBypass);

    // Process in <= kMaxBlock chunks so internal scratch buffers never overflow.
    float* inL  = p->ctrl[P_IN_L];
    float* inR  = p->ctrl[P_IN_R];
    float* outL = p->ctrl[P_OUT_L];
    float* outR = p->ctrl[P_OUT_R];
    for (uint32_t off = 0; off < n; off += kMaxBlock) {
        const int len = static_cast<int>((n - off > kMaxBlock) ? kMaxBlock : (n - off));
        float* ins[2]  = { inL  + off, inR  + off };
        float* outs[2] = { outL + off, outR + off };
        amp->process(ins, outs, len, 2);
        p->pa.process(outs, outs, len, 2);   // in-place; honors its own bypass
    }

    // Per-model loudness makeup (skip on full bypass so true bypass stays unity).
    if (!fullBypass) {
        const float mk = kModelMakeup[modelIdx];
        if (mk != 1.0f)
            for (uint32_t i = 0; i < n; ++i) { outL[i] *= mk; outR[i] *= mk; }
    }
}

static void amp_cleanup(LV2_Handle h) {
    auto* p = static_cast<AmpPlugin*>(h);
    delete p->amp;
    delete p;
}

// ── Extension data ────────────────────────────────────────────────────────────
static const void* amp_extension_data(const char* uri) {
    static const LV2_Worker_Interface worker = { amp_work, amp_work_response, nullptr };
    if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
    return nullptr;
}

LV2_EXPORT_DESCRIPTOR(AMP_URI,
    amp_instantiate, amp_connect_port,
    nullptr, amp_run, nullptr, amp_cleanup, amp_extension_data)
