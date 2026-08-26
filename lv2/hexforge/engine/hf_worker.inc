// ─────────────────────────────────────────────────────────────────────────────
// Hex Forge engine — hf_worker.inc (Stage A textual split, M0)
// Worker: hf_work/hf_work_response, audio helpers, tail resets, the
// zero-point hfApplySwitch.
// NOT a standalone TU: #included by hexforge_plugin.cpp at the exact spot
// this text was cut from. Do not add includes or include guards here.
// ─────────────────────────────────────────────────────────────────────────────
// Off-RT work body, host-agnostic (Stage B3): the wrapper's worker adapter
// hands in a respond callback that queues the reply back to the audio thread,
// where hfWorkResponse() consumes it. Returns false only on allocation failure.
using HfRespondFn = void (*)(void* handle, uint32_t size, const void* data);
static bool hfWork(HexForge* p, const void* data, HfRespondFn respond, void* handle) {
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == W_AMP_FREE) { delete msg->amp; return true; }
    if (msg->type == W_NAM_FREE) { delete msg->nam; return true; }
    if (msg->type == W_CAB_IR) {
        std::vector<float> L, R;
        const char* sent = cabSentinel(msg->path);
        if (msg->namSlot == 1) {   // Rig B cab: built-in sentinel OR a user .wav (v39)
            if (sent)
                p->cab2.setIR(CabModels::generate(sent, p->rate));
            else if (msg->path[0] && loadIRFile(msg->path, p->rate, L, R))
                p->cab2.setIR(L, R.empty()?nullptr:&R);
            else p->cab2.setIR(CabModels::generate("@factory", p->rate));
            return true;
        }
        if (sent)                                                         // built-in synthetic cab
            p->cab.setIR(CabModels::generate(sent, p->rate));
        else if (msg->path[0] && loadIRFile(msg->path, p->rate, L, R)) p->cab.setIR(L, R.empty()?nullptr:&R);
        else p->cab.setIR(CabModels::generate("@factory", p->rate));   // empty path = clear to (enriched) Factory Cab
        return true;
    }
    if (msg->type == W_NAM_LOAD) {
        auto* nm = new(std::nothrow) NamModel;
        if (!nm) return false;
        if (nm->loadFromFile(msg->path)) nm->reset(p->rate, kMaxBlock);
        WorkMsg reply; reply.type = W_NAM_LOAD; reply.nam = nm; reply.namSlot = msg->namSlot;
        respond(handle, sizeof(reply), &reply);
        return true;
    }
    // W_AMP_LOAD — build a fresh amp off the RT thread.
    auto* na = new(std::nothrow) AmpBlockExtended;
    if (!na) return false;
    na->prepare(p->rate, kMaxBlock, 2);
    na->setEco(msg->eco);   // Engine Quality: must precede setAmpModel (wrapper built there)
    na->setAmpModel(kAmpMap[clampi(static_cast<float>(msg->modelIdx), 0, kMt15Idx)]);
    WorkMsg reply; reply.type = W_AMP_LOAD; reply.amp = na; reply.modelIdx = msg->modelIdx; reply.namSlot = msg->namSlot;   // slot 1 = Rig B (forgetting this routed B builds into the MAIN amp)
    respond(handle, sizeof(reply), &reply);
    return true;
}
// Reply consumer — MUST run on the audio thread (pointer swaps + fade re-arm).
static void hfWorkResponse(HexForge* p, const void* data) {
    const auto* msg = static_cast<const WorkMsg*>(data);
    if (msg->type == W_NAM_LOAD) {
        // Defer the swap to the mute-ramp's zero point (a mid-waveform model swap
        // is a guaranteed pop). If a previous pending model was never applied,
        // free it instead of leaking.
        const int sl = msg->namSlot;
        if (p->pendNam[sl]) {
            WorkMsg fm; fm.type = W_NAM_FREE; fm.nam = p->pendNam[sl];
            p->worker->schedule(&fm, sizeof(fm));
        }
        p->pendNam[sl] = msg->nam;
        // Re-arm during fade-IN too (state 2): a fast load's response lands while the recall
        // that requested it is still fading back in — with the ==0 check only, the pending
        // model was never applied until the NEXT switch (measured: every preset ran the
        // PREVIOUS preset's model; race is latent on-device as well). State 1 needs nothing
        // (zero point is coming anyway).
        if (p->swFadeState == 0 || p->swFadeState == 2) p->swFadeState = 1;
        return;
    }
    if (msg->type != W_AMP_LOAD) return;
    if (msg->namSlot == 1) {   // Rig B amp reply
        if (p->pendAmp2) {
            WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = p->pendAmp2;
            p->worker->schedule(&fm, sizeof(fm));
        }
        p->pendAmp2 = msg->amp;
        p->pendAmp2Model = msg->modelIdx;
        if (p->swFadeState == 0 || p->swFadeState == 2) p->swFadeState = 1;
        return;
    }
    if (p->pendAmp) {
        WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = p->pendAmp;
        p->worker->schedule(&fm, sizeof(fm));
    }
    p->pendAmp = msg->amp;
    p->pendAmpModel = msg->modelIdx;
    if (p->swFadeState == 0 || p->swFadeState == 2) p->swFadeState = 1;   // re-arm during fade-in (see NAM note above)
    return;
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

// ── Seamless switching: tail resets + the zero-point apply ───────────────────
static void hfResetBlockTails(HexForge* p, int b) {
    switch (b) {   // long-memory blocks only; the mute ramp covers the ms-scale rest
        case B_DELAY:  p->delay.reset();  break;
        case B_REVERB: p->reverb.reset(); break;
        case B_MODFX:  p->modfx.reset();  break;
        case B_CAB:    p->cab.reset();    break;
        default: break;
    }
}
static void hfResetTails(HexForge* p) {
    p->delay.reset(); p->reverb.reset(); p->modfx.reset(); p->cab.reset(); p->eq.reset();
    std::fill(p->dblBuf.begin(), p->dblBuf.end(), 0.0f);
    std::fill(p->dblApA.begin(), p->dblApA.end(), 0.0f);
    std::fill(p->dblApB.begin(), p->dblApB.end(), 0.0f);
}
// Runs while the output is at ZERO: apply every deferred discontinuity at once.
static void hfApplySwitch(HexForge* p) {
    // blocks (re-)engaging: clear their stale tails so nothing old replays
    for (int i = 0; i < B_COUNT; ++i) {
        const bool now = (*p->ports[kEnablePort[i]] > 0.5f) && (*p->ports[kBypassPort[i]] <= 0.5f);
        if (p->swHold && now && !p->swEnabledHeld[i]) hfResetBlockTails(p, i);
    }
    p->swHold = false;
    if (p->swPendRecall) {
        p->swPendRecall = false;
        psRecall(p, p->swPendBank, p->swPendSlot);
        hfResetTails(p);
    }
    if (p->pendAmp) {
        AmpBlockExtended* old = p->amp;
        p->amp = p->pendAmp; p->pendAmp = nullptr;
        p->lastAmpModel = p->pendAmpModel;
        WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = old;
        p->worker->schedule(&fm, sizeof(fm));
    }
    if (p->pendAmp2) {
        AmpBlockExtended* old = p->amp2;
        p->amp2 = p->pendAmp2; p->pendAmp2 = nullptr;
        p->lastAmp2Model = p->pendAmp2Model;
        p->amp2Requested = false;
        if (old) { WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = old;
                   p->worker->schedule(&fm, sizeof(fm)); }
    }
    for (int sl = 0; sl < 5; ++sl) if (p->pendNam[sl]) {
        NamModel** slot = (sl == 0) ? &p->ampNam : (sl == 1) ? &p->drNam : (sl == 2) ? &p->cabNam
                        : (sl == 3) ? &p->amp2Nam : &p->dr2Nam;
        NamModel* old = *slot;
        *slot = p->pendNam[sl]; p->pendNam[sl] = nullptr;
        WorkMsg fm; fm.type = W_NAM_FREE; fm.nam = old;
        p->worker->schedule(&fm, sizeof(fm));
    }
    // v40: fuzz/nail Eco -- rebuild the oversampling wrappers at the new factor
    // HERE (output is at zero; the same allocate-on-switch class of behavior the
    // OverdriveBlock eco crossfade already ships). States reset; params reapplied
    // by the per-block sections next run.
    auto ecoNow = [&](int port){ return *p->ports[port] > 0.5f; };
    auto rebuildFuzz = [&](std::unique_ptr<OversamplingWrapper>& w, std::unique_ptr<AmpModelBase> m, bool eco){
        w = std::make_unique<OversamplingWrapper>(std::move(m), eco ? 2 : 4);
        if (w) w->prepare(p->rate, kMaxBlock, 1);
    };
    if (ecoNow(HF_FZ_ECO) != p->lastFzEco) {
        p->lastFzEco = ecoNow(HF_FZ_ECO);
        rebuildFuzz(p->fuzzMuff,    std::make_unique<EHXBigMuff>(),      p->lastFzEco);
        rebuildFuzz(p->fuzzBender,  std::make_unique<ToneBenderMkII>(),  p->lastFzEco);
        rebuildFuzz(p->fuzzOctavia, std::make_unique<Octavia>(),         p->lastFzEco);
        rebuildFuzz(p->fuzzFactory, std::make_unique<ZVexFuzzFactory>(), p->lastFzEco);
    }
    if (ecoNow(HF_FZ2_ECO) != p->lastFz2Eco) {
        p->lastFz2Eco = ecoNow(HF_FZ2_ECO);
        rebuildFuzz(p->fuzz2Muff,    std::make_unique<EHXBigMuff>(),      p->lastFz2Eco);
        rebuildFuzz(p->fuzz2Bender,  std::make_unique<ToneBenderMkII>(),  p->lastFz2Eco);
        rebuildFuzz(p->fuzz2Octavia, std::make_unique<Octavia>(),         p->lastFz2Eco);
        rebuildFuzz(p->fuzz2Factory, std::make_unique<ZVexFuzzFactory>(), p->lastFz2Eco);
    }
    if (ecoNow(HF_NAIL_ECO) != p->lastNailEco) {
        p->lastNailEco = ecoNow(HF_NAIL_ECO);
        rebuildFuzz(p->nail,  std::make_unique<NailDistortion>(), p->lastNailEco);
        if (p->nail) p->nail->setParameter("mode", (float)p->lastNailMode);
    }
    if (ecoNow(HF_NAIL2_ECO) != p->lastNail2Eco) {
        p->lastNail2Eco = ecoNow(HF_NAIL2_ECO);
        rebuildFuzz(p->nail2, std::make_unique<NailDistortion>(), p->lastNail2Eco);
        if (p->nail2) p->nail2->setParameter("mode", (float)p->lastNail2Mode);
    }
    p->swAcceptNext = true;   // adopt the new topology silently next block
}

