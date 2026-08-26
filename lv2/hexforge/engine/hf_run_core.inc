// ─────────────────────────────────────────────────────────────────────────────
// Hex Forge engine — hf_run_core.inc (Stage A textual split, M0)
// hf_run: the whole per-block engine (atom loop still inside; the LV2/
// engine boundary inside hf_run is drawn in Stage B).
// NOT a standalone TU: #included by hexforge_plugin.cpp at the exact spot
// this text was cut from. Do not add includes or include guards here.
// ─────────────────────────────────────────────────────────────────────────────
static void hf_run(LV2_Handle h, uint32_t n) {
    DenormalGuard denormalGuard;
    auto* p = static_cast<HexForge*>(h);
    const URIs& u = p->uris;
    // ── Output Voice: FRFR (2026-08-21, global layer — auto-cal pattern, never
    // preset-captured). OFF = Headphones/Studio = bit-identical. ON = the
    // FRFR-10 room voice: de-close-mic EQ post-mono-sum (below), plus the
    // doubler and airFeel are auto-muted (mono speaker; the real room supplies
    // the reflections a close-mic IR lacks).
    const bool fvOn = p->ports[HF_OUT_VOICE] && *p->ports[HF_OUT_VOICE] > 0.5f;

    // ── Prime the preset override layer (once, after all ports are connected) ──
    // Param ports are redirected to read eff[]; everything else reads the host
    // buffer. Done before the atom loop so a first-cycle patch:Get sees real values.
    if (!p->primed) {
        for (int i=0;i<HF_N_PORTS;++i) {
            if (i==HF_CONTROL || i==HF_NOTIFY || i==HF_MIDI_IN) continue;
            if (isParamPort(i)) {
                p->eff[i]      = p->hostPorts[i] ? *p->hostPorts[i] : 0.0f;
                p->lastPort[i] = p->eff[i];
                p->ports[i]    = &p->eff[i];
            } else {
                p->ports[i]    = p->hostPorts[i];
            }
        }
        p->primed = true;
    }

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
                        if (haveNotify) emitIndex(p);
                        hfWriteStatus(p);
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
                        p->schedule->schedule_work(p->schedule->handle, sizeof(m2), &m2);
                    } else p->lastCab2Model = -1;   // re-sentinel from rb_cab next block
                    continue;
                }
                if (dst) {
                    // "Factory Cab" sentinel → clear IR to the built-in default.
                    const char* eff = (dst == p->irPath && std::strcmp(path, kFactoryIR) == 0) ? "" : path;
                    std::strncpy(dst, eff, kPathMax-1); dst[kPathMax-1]='\0';
                    std::strncpy(msg.path, dst, kPathMax-1); msg.path[kPathMax-1]='\0';
                    p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
                }
            } else if (obj->body.otype == u.patch_Get && haveNotify) {
                writeFileToNotify(p, u.ir_file, p->irPath);
                writeFileToNotify(p, u.amp_nam, p->ampNamPath);
                writeFileToNotify(p, u.dr_nam, p->drNamPath);
                writeFileToNotify(p, u.cab_nam, p->cabNamPath);
                writeFileToNotify(p, u.amp2_nam, p->amp2NamPath);
                writeFileToNotify(p, u.ir2_file, p->ir2Path);
                writeFileToNotify(p, u.dr2_nam, p->dr2NamPath);
                forgeStringSet(p, u.ps_name, p->presets[p->curBank][p->curSlot].name);
                emitIndex(p);
                emitApply(p);   // sync knobs to the active preset's effective values
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

    // ── Preset engine: detect live edits, run commands ──
    p->sampleClock += static_cast<int64_t>(n);   // for double-tap timing
    // A knob/host move on any param port overrides its recalled value.
    for (int i=0;i<HF_N_PORTS;++i) if (isParamPort(i)) {
        const float hv = p->hostPorts[i] ? *p->hostPorts[i] : p->lastPort[i];
        if (hv != p->lastPort[i]) { p->eff[i] = hv; p->lastPort[i] = hv; }
    }
    // Apply a preset restored from State (first run after hf_restore).
    if (p->pendingRecall) { p->pendingRecall = false; psRecall(p, p->curBank, p->curSlot); }
    // Command edges (rising 0->1). The four A/B/C/D switches feed the combo
    // detector (single press → recall; two within the window → bank). They reach
    // the plugin either as MIDI-bound control ports (pi-Stomp footswitches send
    // CC 127 on press → port 1) or pulsed by the modgui buttons. The bank/save/
    // move pulses come from the custom modgui.
    auto rose = [&](int port, float& prev)->bool {
        const float v = p->hostPorts[port] ? *p->hostPorts[port] : 0.0f;
        const bool r = (prev < 0.5f && v >= 0.5f); prev = v; return r;
    };
    // A/B/C/D switches recall on EITHER edge (so each press selects its preset,
    // whether the MIDI-bound toggle flips on or off). An ~80 ms debounce drops the
    // modgui button's quick 1->0 release pulse so it counts as one press.
    auto swEdge = [&](int port, float& prev, int sw)->bool {
        const float v = p->hostPorts[port] ? *p->hostPorts[port] : 0.0f;
        const bool flipped = (v >= 0.5f) != (prev >= 0.5f); prev = v;
        if (!flipped) return false;
        if (p->sampleClock - p->lastEdgeSample[sw] < static_cast<int64_t>(p->rate * 0.08)) return false;
        p->lastEdgeSample[sw] = p->sampleClock;
        return true;
    };
    if (swEdge(HF_SW_A, p->swPrev[0], 0)) psSwitchPress(p, 0);
    if (swEdge(HF_SW_B, p->swPrev[1], 1)) psSwitchPress(p, 1);
    if (swEdge(HF_SW_C, p->swPrev[2], 2)) psSwitchPress(p, 2);
    if (swEdge(HF_SW_D, p->swPrev[3], 3)) psSwitchPress(p, 3);
    if (rose(HF_PS_BANK_UP, p->cmdPrev[0])) psBankDelta(p, +1);
    if (rose(HF_PS_BANK_DN, p->cmdPrev[1])) psBankDelta(p, -1);
    if (rose(HF_PS_SAVE,    p->cmdPrev[2])) psSave(p);
    if (rose(HF_PS_MOVE_UP, p->cmdPrev[3])) psMoveDelta(p, -1);
    if (rose(HF_PS_MOVE_DN, p->cmdPrev[4])) psMoveDelta(p, +1);
    if (rose(HF_PS_BACKUP,  p->cmdPrev[5])) hfWriteBackup(p);              // snapshot all 32 to disk
    if (rose(HF_PS_RESTORE, p->cmdPrev[6]) && hfLoadBackup(p))            // pull the backup into this instance
        psRecallRequest(p, p->curBank, p->curSlot);                      // re-applies sound + refreshes the UI list (seamless)
    // Direct jump from the UI list: recall when ps_goto changes to a valid index.
    {
        const float gf = p->hostPorts[HF_PS_GOTO] ? *p->hostPorts[HF_PS_GOTO] : -1.0f;
        const int g = static_cast<int>(std::lround(gf));
        if (g >= 0 && g < kBanks * kSlots && g != p->lastGoto) { p->lastGoto = g; psRecallRequest(p, g / kSlots, g % kSlots); }
        else if (g < 0) p->lastGoto = -1;
    }
    // ── Footswitch MIDI (pi-Stomp CC 60..63): each CC message = one switch press ──
    if (p->midiIn) {
        LV2_ATOM_SEQUENCE_FOREACH(p->midiIn, ev) {
            if (ev->body.type != u.midi_MidiEvent || ev->body.size < 3) continue;
            const uint8_t* m = static_cast<const uint8_t*>(LV2_ATOM_BODY_CONST(&ev->body));
            if ((m[0] & 0xF0) != 0xB0) continue;            // Control Change, any channel
            if (m[2] < 64) continue;                        // press-down only (ignore the release = value 0)
            const int sw = static_cast<int>(m[1]) - kMidiBaseCC;
            if (sw >= 0 && sw <= 3) psSwitchPress(p, sw);
        }
    }
    // Mirror the active bank/slot to the UI output ports.
    if (p->ports[HF_PS_BANK]) *p->ports[HF_PS_BANK] = static_cast<float>(p->curBank);
    if (p->ports[HF_PS_SLOT]) *p->ports[HF_PS_SLOT] = static_cast<float>(p->curSlot);

    float* inL  = p->ports[HF_IN_L];
    float* inR  = p->ports[HF_IN_R];
    float* outL = p->ports[HF_OUT_L];
    float* outR = p->ports[HF_OUT_R];

    // ── Strobe tuner: detect the dry input pitch when engaged; publish note + cents ──
    const bool tunerOn   = p->ports[HF_TUNER_ON]   && *p->ports[HF_TUNER_ON]   > 0.5f;
    // Mute is INDEPENDENT of the tuner since 2026-08-23: the IN jack LED in the
    // modgui doubles as a master mute, so it must work with the tuner closed.
    const bool tunerMute = p->ports[HF_TUNER_MUTE] && *p->ports[HF_TUNER_MUTE] > 0.5f;
    if (tunerOn) {
        for (uint32_t off = 0; off < n; off += kMaxBlock) {
            const int len = (int)((n - off > (uint32_t)kMaxBlock) ? kMaxBlock : (n - off));
            for (int i = 0; i < len; ++i) p->mono[i] = 0.5f * (inL[off+i] + inR[off+i]);
            p->tuner.process(p->mono, len);
        }
        if (p->ports[HF_TUNER_NOTE])  *p->ports[HF_TUNER_NOTE]  = (float)p->tuner.note;
        if (p->ports[HF_TUNER_CENTS]) *p->ports[HF_TUNER_CENTS] = p->tuner.cents;
    } else if (p->tuner.note != -1) {
        p->tuner.reset();
        if (p->ports[HF_TUNER_NOTE])  *p->ports[HF_TUNER_NOTE]  = -1.0f;
        if (p->ports[HF_TUNER_CENTS]) *p->ports[HF_TUNER_CENTS] = 0.0f;
    }

    // ── Auto-calibration wizard ── measures the RAW input (same tap as the tuner,
    // pre-Input-Trim: the domain every gate threshold was floor-complianced in).
    // cal_cmd is UI-pulsed: 1-3 start that phase, 9 aborts. Results/recommendation
    // ride the #cal notify string; Apply writes the cal_*_offs ports (below).
    {
        const float cv = p->ports[HF_CAL_CMD] ? *p->ports[HF_CAL_CMD] : 0.0f;
        if (cv != p->calCmdPrev) {
            p->calCmdPrev = cv;
            const int cmd = (int)(cv + 0.5f);
            if (cmd >= 1 && cmd <= 3) {
                p->calMeas.begin(p->rate);
                p->calState = cmd;
                p->calSamplesTotal = p->calSamplesLeft =
                    (uint32_t)(p->rate * (cmd == 3 ? 6.0 : 5.0));
            } else if (cmd == 9) {
                p->calState = 0;
                p->calSamplesLeft = 0;
            }
        }
        if (p->calState >= 1 && p->calState <= 3 && inL && inR) {
            const uint32_t len = n < p->calSamplesLeft ? n : p->calSamplesLeft;
            for (uint32_t i = 0; i < len; ++i)
                p->calMeas.feed(0.5f * (inL[i] + inR[i]));
            p->calSamplesLeft -= len;
            if (p->calSamplesLeft == 0) {
                const int ph = p->calState - 1;
                p->calPh[ph] = p->calMeas.finish();
                p->calPhDone[ph] = true;
                if (p->calPhDone[0] && p->calPhDone[1] && p->calPhDone[2]) {
                    p->calRec = calRecommend(p->calPh[0], p->calPh[1], p->calPh[2]);
                    p->calState = p->calRec.error ? 5 : 4;
                } else {
                    p->calState = 0;
                }
                p->calNotifyTicks = 70;   // ~5 s of #cal re-pushes at the 14 Hz throttle
            }
        }
        if (p->ports[HF_CAL_STATE])    *p->ports[HF_CAL_STATE] = (float)p->calState;
        if (p->ports[HF_CAL_PROGRESS]) *p->ports[HF_CAL_PROGRESS] =
            (p->calSamplesTotal && p->calState >= 1 && p->calState <= 3)
                ? 1.0f - (float)p->calSamplesLeft / (float)p->calSamplesTotal : 0.0f;
        // Slew the applied offset layer toward the ports (~10 dB/s, click-free Apply).
        const float trimTgt  = p->ports[HF_CAL_TRIM_OFFS]  ? *p->ports[HF_CAL_TRIM_OFFS]  : 0.0f;
        const float floorTgt = p->ports[HF_CAL_FLOOR_OFFS] ? *p->ports[HF_CAL_FLOOR_OFFS] : 0.0f;
        if (!p->calOffsInit) { p->calOffsInit = true; p->calTrimOffsSm = trimTgt; p->calFloorOffsSm = floorTgt; }
        const float step = 10.0f * (float)n / (float)p->rate;
        auto slew = [step](float cur, float tgt) {
            const float d = tgt - cur;
            return (std::fabs(d) <= step) ? tgt : cur + (d > 0.0f ? step : -step);
        };
        p->calTrimOffsSm  = slew(p->calTrimOffsSm,  trimTgt);
        p->calFloorOffsSm = slew(p->calFloorOffsSm, floorTgt);
    }
    // Mute while measuring the floors (phases 1-2: idle noise through a cranked
    // preset is the very thing being measured); phase 3 passes audio — the player
    // must hear the rig to genuinely play their hardest.
    const bool calMute = (p->calState == 1 || p->calState == 2);

    // ── Global bypass: unity passthrough (or silence while tuning-muted) ──
    if (*p->ports[HF_BYPASS] > 0.5f) {
        if (tunerMute || calMute) { std::memset(outL, 0, sizeof(float)*n); std::memset(outR, 0, sizeof(float)*n); }
        else {
            if (outL != inL) std::memcpy(outL, inL, sizeof(float)*n);
            if (outR != inR) std::memcpy(outR, inR, sizeof(float)*n);
        }
        if (p->ports[HF_CLIP]) *p->ports[HF_CLIP] = 0.0f;
        if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
        return;
    }

    // ── Configure every block from its ports (once per run) ──
    // Input trim
    const bool  itEnabled = (*p->ports[HF_IT_ENABLE] > 0.5f);
    p->trimLoad.set(*p->ports[HF_IT_LOAD]);
    const float itGain    = std::pow(10.0f, (*p->ports[HF_IT_GAIN] + p->calTrimOffsSm)/20.0f)
                            * ((*p->ports[HF_IT_PHASE] > 0.5f) ? -1.0f : 1.0f);
    const bool  itHum     = *p->ports[HF_IT_HUM] > 0.5f;
    const bool  itHB      = *p->ports[HF_IT_HUMBK] > 0.5f;
    const int   itHBModel = (int)(*p->ports[HF_IT_HBMODEL] + 0.5f);
    if (itHB) p->trimVoice.prepare(p->rate, itHBModel, *p->ports[HF_IT_HBAMT]);   // recompute on model/amount change
    const bool  itBoost   = *p->ports[HF_IT_BOOST] > 0.5f;
    if (itBoost) p->trimBoost.prepare(p->rate, *p->ports[HF_IT_BOOSTAMT]);
    // Gate
    p->gate.setBypass(false);
    p->gate.setParameter("threshold",
        std::clamp(*p->ports[HF_GT_THRESH] + p->calFloorOffsSm, -80.0f, 0.0f));
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
    const int fuzzPedal = clampi(*p->ports[HF_FZ_PEDAL], 0, 3);
    p->fuzzMuff->setBypass(false); p->fuzzBender->setBypass(false); p->fuzzOctavia->setBypass(false); p->fuzzFactory->setBypass(false);
    p->fuzzMuff->setParameter("era",   *p->ports[HF_FZ_MODE]);
    p->fuzzMuff->setParameter("drive", *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzMuff->setParameter("tone",  *p->ports[HF_FZ_TONE]);
    p->fuzzMuff->setParameter("level", *p->ports[HF_FZ_VOLUME]);
    p->fuzzBender->setParameter("attack",    *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzBender->setParameter("level",     *p->ports[HF_FZ_VOLUME]);
    p->fuzzBender->setParameter("bias",      *p->ports[HF_FZ_BIAS]);
    p->fuzzBender->setParameter("inputtrim", *p->ports[HF_FZ_INPUTTRIM]);
    p->fuzzBender->setParameter("getemp",    *p->ports[HF_FZ_GETEMP]);
    p->fuzzBender->setParameter("gvol",      *p->ports[HF_FZ_GVOL]);
    p->drive.setParameter("eco",       *p->ports[HF_DR_ECO]);
    p->fuzzOctavia->setParameter("drive", *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzOctavia->setParameter("tone",  *p->ports[HF_FZ_TONE]);
    p->fuzzOctavia->setParameter("level", *p->ports[HF_FZ_VOLUME]);
    // Fizz Factory: Sustain→Drive, Bias→Comp, Trim→Gate, Temp→Stab, Volume→Level
    p->fuzzFactory->setParameter("sustain",   *p->ports[HF_FZ_SUSTAIN]);
    p->fuzzFactory->setParameter("bias",      *p->ports[HF_FZ_BIAS]);
    p->fuzzFactory->setParameter("inputtrim", *p->ports[HF_FZ_INPUTTRIM]);
    p->fuzzFactory->setParameter("getemp",    *p->ports[HF_FZ_GETEMP]);
    p->fuzzFactory->setParameter("level",     *p->ports[HF_FZ_VOLUME]);
    // Drive
    p->drive.setBypass(false);
    const int driveModel = clampi(*p->ports[HF_DR_MODEL], 0, kDrMax);
    if (driveModel != kDrNamIdx && driveModel != p->lastDriveModel) { p->lastDriveModel = driveModel; p->drive.setType(kDriveMap[driveModel]); }
    p->drive.setParameter("drive",  *p->ports[HF_DR_DRIVE]);
    p->drive.setParameter("tone",   *p->ports[HF_DR_TONE]);
    p->drive.setParameter("level",  *p->ports[HF_DR_LEVEL]);
    p->drive.setParameter("mix",    *p->ports[HF_DR_MIX]);
    p->drive.setParameter("octave", *p->ports[HF_DR_OCTAVE]);
    // Drive B — same pedal family, no NAM (one neural slot per instance); a NAM
    // selection arriving anyway (e.g. hand-edited pedalboard) falls back to model 0.
    p->drive2.setBypass(false);
    const int drive2Model = clampi(*p->ports[HF_DR2_MODEL], 0, kDrMax);
    int drive2Algo = (drive2Model == kDrNamIdx) ? 0 : drive2Model;   // NAM (v40): algo core parked on Green Man
    if (drive2Algo != p->lastDrive2Model) { p->lastDrive2Model = drive2Algo; p->drive2.setType(kDriveMap[drive2Algo]); }
    p->drive2.setParameter("eco",    *p->ports[HF_DR2_ECO]);
    p->drive2.setParameter("drive",  *p->ports[HF_DR2_DRIVE]);
    p->drive2.setParameter("tone",   *p->ports[HF_DR2_TONE]);
    p->drive2.setParameter("level",  *p->ports[HF_DR2_LEVEL]);
    p->drive2.setParameter("mix",    *p->ports[HF_DR2_MIX]);
    p->drive2.setParameter("octave", *p->ports[HF_DR2_OCTAVE]);
    // Nail — industrial distortion (mode + drive/tone/texture/level)
    p->nail->setBypass(false);
    const int nailMode = clampi(*p->ports[HF_NAIL_MODE], 0, NailDistortion::kNumModes - 1);
    if (nailMode != p->lastNailMode) { p->lastNailMode = nailMode; p->nail->setParameter("mode", (float)nailMode); }
    p->nail->setParameter("drive",   *p->ports[HF_NAIL_DRIVE]);
    p->nail->setParameter("tone",    *p->ports[HF_NAIL_TONE]);
    p->nail->setParameter("texture", *p->ports[HF_NAIL_TEXTURE]);
    p->nail->setParameter("level",   *p->ports[HF_NAIL_LEVEL]);
    // Amp
    const int ampModel = clampi(*p->ports[HF_AMP_MODEL], 0, kMt15Idx);
    const int ampAlgo  = (ampModel == 5) ? 1 : ampModel;   // NAM(5)→1 safe; 6=Beardo,7=Hiwatt,8=Vox identity
    const bool ampIsAlgo = (ampModel <= 4) || (ampModel == kFriedmanIdx) || (ampModel == kHiwattIdx) || (ampModel == kVoxIdx) || (ampModel == kBacklineIdx) || (ampModel == kPlexiIdx) || (ampModel == kMesaIdx) || (ampModel == kRectoIdx) || (ampModel == kMt15Idx);
    const bool ecoQ = *p->ports[HF_QUALITY] > 0.5f;
    if (ampIsAlgo && (ampModel != p->lastAmpModel || ecoQ != p->lastEco)) {   // rebuild for algo models (model or Engine Quality change)
        WorkMsg msg; msg.type=W_AMP_LOAD; msg.modelIdx=ampModel; msg.eco=ecoQ;
        if (p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg) == LV2_WORKER_SUCCESS)
            { p->lastAmpModel = ampModel; p->lastEco = ecoQ; }
    }
    // ── Rig B (dual amp/cab, 2026-07-30): schedule builds + set params ──────
    const bool rbOn = *p->ports[HF_RB_ENABLE] > 0.5f;
    if (!rbOn && p->amp2 && !p->amp2Requested && !p->pendAmp2) {
        // Rig B disabled: release the second amp (it is only processed while
        // enabled, so a direct handoff to the free worker is safe) -- a player
        // who parked Rig B should get the RAM back.
        WorkMsg fm; fm.type = W_AMP_FREE; fm.amp = p->amp2;
        if (p->schedule->schedule_work(p->schedule->handle, sizeof(fm), &fm) == LV2_WORKER_SUCCESS) {
            p->amp2 = nullptr; p->lastAmp2Model = -1;
        }
    }
    if (rbOn) {
        int rbModel = clampi(*p->ports[HF_RB_AMP], 0, kMt15Idx);
        const bool rbNamSel = (rbModel == kAmpNamIdx);   // Neural on the B side (v39)
        const bool rbEco = *p->ports[HF_RB_ECO] > 0.5f;
        // amp2Requested guards the build-in-flight window: without it, !p->amp2
        // stays true until the crossfade swap lands and this scheduled a fresh
        // ~MB amp build EVERY BLOCK (user-reported RAM/CPU runaway, 2026-07-30).
        if (!rbNamSel && (rbModel != p->lastAmp2Model || rbEco != p->lastAmp2Eco || !p->amp2) && !p->amp2Requested) {
            WorkMsg msg; msg.type=W_AMP_LOAD; msg.modelIdx=rbModel; msg.eco=rbEco; msg.namSlot=1;
            if (p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg) == LV2_WORKER_SUCCESS)
                { p->lastAmp2Model = rbModel; p->lastAmp2Eco = rbEco; p->amp2Requested = true; }
        }
        const int rbCab = clampi(*p->ports[HF_RB_CAB], 0, 6);
        const bool ir2NoCab = (std::strcmp(p->ir2Path, "@nocab") == 0);   // unified picker's No Cab entry
        const bool ir2User = (p->ir2Path[0] != '\0') && !ir2NoCab;   // IR/sentinel override active (v39)
        if (ir2User && p->lastCab2Model != -2) {
            // recall/patch installed a user IR; mark the sentinel cache invalid so a
            // later clear re-generates. -2 = "user IR active".
            WorkMsg m2; m2.type = W_CAB_IR; m2.namSlot = 1;
            std::strncpy(m2.path, p->ir2Path, kPathMax-1); m2.path[kPathMax-1]='\0';
            if (p->schedule->schedule_work(p->schedule->handle, sizeof(m2), &m2) == LV2_WORKER_SUCCESS)
                p->lastCab2Model = -2;
        }
        if (!ir2User && rbCab != p->lastCab2Model && rbCab < 6) {
            static const char* kRbCabIr[6] = { "@factory", "@vox2x12", "@american-ob", "@greenback", "@hiwatt", "@doom" };
            WorkMsg msg; msg.type=W_CAB_IR; msg.namSlot=1;
            std::snprintf(msg.path, sizeof(msg.path), "%s", kRbCabIr[rbCab]);
            if (p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg) == LV2_WORKER_SUCCESS)
                p->lastCab2Model = rbCab;
        }
        if (!ir2User && rbCab >= 6) p->lastCab2Model = rbCab;   // No Cab (Direct): bypass below
        if (p->amp2) {
            AmpBlockExtended* a2 = p->amp2;
            const int rbM = p->lastAmp2Model;
            a2->setBypass(false);
            // Full A-side parity (2026-07-30 redesign): every model family gets
            // its complete control set on the B side too.
            if (rbM == kSunnIdx) {
                a2->setParameter("vol1",         *p->ports[HF_RB_GAIN]);
                a2->setParameter("vol2",         *p->ports[HF_RB_SUNN_VOL2]);
                a2->setParameter("channel_link", *p->ports[HF_RB_SUNN_LINK]);
                a2->setParameter("bass1",        *p->ports[HF_RB_BASS]);
                a2->setParameter("mid1",         *p->ports[HF_RB_MID]);
                a2->setParameter("treble1",      *p->ports[HF_RB_TREBLE]);
                a2->setParameter("bass2",        *p->ports[HF_RB_SUNN_BASS2]);
                a2->setParameter("mid2",         *p->ports[HF_RB_SUNN_MID2]);
                a2->setParameter("treble2",      *p->ports[HF_RB_SUNN_TREBLE2]);
                a2->setParameter("bright1",      *p->ports[HF_RB_SUNN_BRIGHT1]);
                a2->setParameter("bright2",      *p->ports[HF_RB_SUNN_BRIGHT2]);
            } else {
                a2->setParameter("gain",   *p->ports[HF_RB_GAIN]);
                a2->setParameter("bass",   *p->ports[HF_RB_BASS]);
                a2->setParameter("mid",    *p->ports[HF_RB_MID]);
                a2->setParameter("treble", *p->ports[HF_RB_TREBLE]);
            }
            a2->setParameter("presence",  *p->ports[HF_RB_PRESENCE]);
            a2->setParameter("master",    *p->ports[HF_RB_MASTER]);
            a2->setParameter("sag",       *p->ports[HF_RB_SAG]);
            a2->setParameter("channel",   *p->ports[HF_RB_CHANNEL]);
            a2->setParameter("resonance", *p->ports[HF_RB_RESONANCE]);
            if (rbM == kFriedmanIdx) {
                a2->setParameter("channel", *p->ports[HF_RB_FR_CHANNEL]);
                a2->setParameter("fat",     *p->ports[HF_RB_FR_FAT]);
                a2->setParameter("c45",     *p->ports[HF_RB_FR_C45]);
                a2->setParameter("sat",     *p->ports[HF_RB_FR_SAT]);
            }
            if (rbM == 10) {
                a2->setParameter("vol2",   *p->ports[HF_RB_PL_VOL2]);
                a2->setParameter("variac", *p->ports[HF_RB_PL_VARIAC]);   // brown sound (v41)
            }
            if (rbM == 1) a2->setParameter("sir34", *p->ports[HF_RB_SIR34]);   // SIR #34 (v43)
            if (rbM == kMesaIdx) {
                a2->setParameter("mode", *p->ports[HF_RB_MV_MODE]);
                a2->setParameter("geq0", *p->ports[HF_RB_MV_GEQ0]);
                a2->setParameter("geq1", *p->ports[HF_RB_MV_GEQ1]);
                a2->setParameter("geq2", *p->ports[HF_RB_MV_GEQ2]);
                a2->setParameter("geq3", *p->ports[HF_RB_MV_GEQ3]);
                a2->setParameter("geq4", *p->ports[HF_RB_MV_GEQ4]);
                a2->setParameter("eqpreset", *p->ports[HF_RB_MV_EQPRESET]);
            }
            if (rbM == kRectoIdx) {
                a2->setParameter("mode",   *p->ports[HF_RB_RC_MODE]);
                a2->setParameter("variac", *p->ports[HF_RB_RC_VARIAC]);
                a2->setParameter("rect",   *p->ports[HF_RB_RC_RECT]);
            }
            if (rbM == kMt15Idx) {
                a2->setParameter("mode",   *p->ports[HF_RB_MT_MODE]);
                a2->setParameter("bright", *p->ports[HF_RB_MT_BRIGHT]);
            }
            const int rbAlgo = (rbM < 0) ? 1 : ((rbM == 5) ? 1 : rbM);
            const float rbRcMode = *p->ports[HF_RB_RC_MODE];
            const bool rbRectoModern = (rbM == kRectoIdx) &&
                ((rbRcMode > 3.5f && rbRcMode < 4.5f) || rbRcMode > 6.5f);
            if (*p->ports[HF_RB_PAMP_AUTO] > 0.5f) {
                const auto d2 = PowerAmpProcessor::getDefaultsForModel(kCanonical[rbAlgo]);
                p->pa2.setParameter("master",   d2.master);
                p->pa2.setParameter("presence", d2.presence);
                p->pa2.setParameter("depth",    d2.depth);
                p->pa2.setParameter("nfb",      rbRectoModern ? 0.05f : d2.nfb);
                p->pa2.setParameter("sag",      d2.sag);
                { const int t2 = p->amp2->getRecommendedTubeType();
                  if (t2 >= 0) p->pa2.setTubeType(static_cast<TubeType>(t2)); }
            } else {
                p->pa2.setParameter("presence", *p->ports[HF_RB_PAMP_PRESENCE]);
                p->pa2.setParameter("depth",    *p->ports[HF_RB_PAMP_DEPTH]);
                p->pa2.setParameter("sag",      *p->ports[HF_RB_PAMP_SAG]);
                p->pa2.setParameter("master",   *p->ports[HF_RB_PAMP_MASTER]);
                p->pa2.setParameter("nfb",      *p->ports[HF_RB_PAMP_NFB]);
                p->pa2.setTubeType(static_cast<TubeType>(clampi(*p->ports[HF_RB_PAMP_TUBE], 0, 4)));   // 4 = 6V6 (2026-08-21)
            }
            p->pa2.setParameter("resonance", *p->ports[HF_RB_PAMP_RESONANCE]);
            p->pa2.setParameter("airFeel",   fvOn ? 0.0f : *p->ports[HF_RB_PAMP_AIRFEEL]);   // FRFR voice: air off
            p->pa2.setParameter("coupling",  *p->ports[HF_RB_PAMP_COUPL]);
            {   // per-amp voicing rows stay authoritative for the deep internals
                const auto d2 = PowerAmpProcessor::getDefaultsForModel(kCanonical[rbAlgo]);
                p->pa2.setParameter("bloomvca", d2.bloomVca);
                p->pa2.setParameter("duty",     d2.duty);
                p->pa2.setParameter("evengen",  d2.evenDepth);
                p->pa2.setParameter("padrive",  d2.paDrive);
                p->pa2.setParameter("pamakeup", d2.paMakeup);
                p->pa2.setParameter("ripplesag",d2.rippleSagCoupling);
                p->pa2.setParameter("ltptail",  d2.ltpTail);
                p->pa2.setParameter("fluxOT",   d2.fluxOT ? 1.0f : 0.0f);
            }
            p->pa2.setBypass((*p->ports[HF_RB_PAMP_BYPASS] > 0.5f) || rbM == kSunnIdx);
            // Full cab parity
            p->cab2.setParameter("lowCutHz",  *p->ports[HF_RB_LOWCUT]);
            p->cab2.setParameter("highCutHz", *p->ports[HF_RB_HIGHCUT]);
            p->cab2.setParameter("mix",       *p->ports[HF_RB_CABMIX]);
            p->cab2.setParameter("micpos",    *p->ports[HF_RB_CABMICPOS]);
            p->cab2.setParameter("micdist",   *p->ports[HF_RB_CABMICDIST]);
            p->cab2.setParameter("roomon",    *p->ports[HF_RB_CABROOMON]);
            p->cab2.setParameter("roommix",   *p->ports[HF_RB_CABROOMMIX]);
            p->cab2.setParameter("roomamt",   *p->ports[HF_RB_CABROOMAMT]);
            p->cab2.setParameter("roomdense", *p->ports[HF_RB_CABROOMDENSE]);
            p->cab2.setParameter("voice",     *p->ports[HF_RB_CABVOICE]);
            p->cab2.setParameter("monoroom",  (p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f) ? 1.0f : 0.0f);
            { const int spk = clampi(*p->ports[HF_RB_CABSPKDRIVE], 0, 2);
              p->cab2.setParameter("spkdrive",    spk > 0 ? 1.0f : 0.0f);
              p->cab2.setParameter("spkdriveamt", spk >= 2 ? 0.75f : (spk == 1 ? 0.35f : 0.0f)); }
            // No Cab (Direct): the player may run rig A into a real power amp +
            // physical cab -- rig B then skips speaker sim too.
            p->cab2.setBypass((std::strcmp(p->ir2Path, "@nocab") == 0)
                              || (p->ir2Path[0] == '\0' && clampi(*p->ports[HF_RB_CAB], 0, 6) >= 6));
        }
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
    // Beardo BE (Friedman): its own 3-way channel (Clean/BE/HBE) + voicing toggles.
    if (ampModel == kFriedmanIdx) {
        amp->setParameter("channel", *p->ports[HF_AMP_FR_CHANNEL]);
        amp->setParameter("fat",     *p->ports[HF_AMP_FR_FAT]);
        amp->setParameter("c45",     *p->ports[HF_AMP_FR_C45]);
        amp->setParameter("sat",     *p->ports[HF_AMP_FR_SAT]);
    }
    // Plexiglass (Marshall 1959): Vol II — the jumpered Normal-channel volume (0 = pre-Vol-II voicing).
    if (ampModel == 10) {
        amp->setParameter("vol2",   *p->ports[HF_AMP_PL_VOL2]);
        amp->setParameter("variac", *p->ports[HF_AMP_PL_VARIAC]);   // brown sound (v41)
    }
    if (ampModel == 1) amp->setParameter("sir34", *p->ports[HF_AMP_SIR34]);   // SIR #34 (v43)
    // Cali V (Mesa Mark V): 9-mode selector (0..8) + 5-band graphic EQ (each 0..1, 0.5 = flat).
    if (ampModel == kMesaIdx) {
        amp->setParameter("mode", *p->ports[HF_AMP_MV_MODE]);
        amp->setParameter("geq0", *p->ports[HF_AMP_MV_GEQ0]);
        amp->setParameter("geq1", *p->ports[HF_AMP_MV_GEQ1]);
        amp->setParameter("geq2", *p->ports[HF_AMP_MV_GEQ2]);
        amp->setParameter("geq3", *p->ports[HF_AMP_MV_GEQ3]);
        amp->setParameter("geq4", *p->ports[HF_AMP_MV_GEQ4]);
        amp->setParameter("eqpreset", *p->ports[HF_AMP_MV_EQPRESET]);   // 0 Custom (sliders) or baked "V"
    }
    // Diamond Plate (Dual Rectifier): 8-mode selector + power-section feel switches.
    if (ampModel == kRectoIdx) {
        amp->setParameter("mode",   *p->ports[HF_AMP_RC_MODE]);
        amp->setParameter("variac", *p->ports[HF_AMP_RC_VARIAC]);
        amp->setParameter("rect",   *p->ports[HF_AMP_RC_RECT]);
    }
    // Tremont 15 (PRS MT15): Clean/Crunch/Lead + the clean/crunch bright switch.
    if (ampModel == kMt15Idx) {
        amp->setParameter("mode",   *p->ports[HF_AMP_MT_MODE]);
        amp->setParameter("bright", *p->ports[HF_AMP_MT_BRIGHT]);
    }
    // Recto Modern modes (4, 7) disconnect the power-amp NFB loop on the real amp.
    const float rcMode = *p->ports[HF_AMP_RC_MODE];
    const bool rectoModern = (ampModel == kRectoIdx) &&
                             ((rcMode > 3.5f && rcMode < 4.5f) || rcMode > 6.5f);
    int desiredTube;
    if (*p->ports[HF_AMP_PAMP_AUTO] > 0.5f) {
        const auto d = PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]);
        p->pa.setParameter("master", d.master); p->pa.setParameter("presence", d.presence);
        p->pa.setParameter("depth", d.depth);   p->pa.setParameter("nfb", rectoModern ? 0.05f : d.nfb);
        p->pa.setParameter("sag", d.sag);
        p->pa.setParameter("resonance", *p->ports[HF_AMP_PAMP_RESONANCE]);
        p->pa.setParameter("airFeel",   fvOn ? 0.0f : *p->ports[HF_AMP_PAMP_AIRFEEL]);   // FRFR voice: real room supplies the air
        desiredTube = kAmpTube[ampAlgo];
    } else {
        p->pa.setParameter("presence",  *p->ports[HF_AMP_PAMP_PRESENCE]);
        p->pa.setParameter("depth",     *p->ports[HF_AMP_PAMP_DEPTH]);
        p->pa.setParameter("sag",       *p->ports[HF_AMP_PAMP_SAG]);
        p->pa.setParameter("master",    *p->ports[HF_AMP_PAMP_MASTER]);
        p->pa.setParameter("nfb",       *p->ports[HF_AMP_PAMP_NFB]);
        p->pa.setParameter("resonance", *p->ports[HF_AMP_PAMP_RESONANCE]);
        p->pa.setParameter("airFeel",   fvOn ? 0.0f : *p->ports[HF_AMP_PAMP_AIRFEEL]);   // FRFR voice: real room supplies the air
        desiredTube = clampi(*p->ports[HF_AMP_PAMP_TUBE], 0, 4);   // 4 = 6V6 (2026-08-21)
    }
    // Post-saturation sag-VCA depth is a per-amp voicing value with no user port.
    p->pa.setParameter("bloomvca", PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).bloomVca);
    p->pa.setParameter("duty",     PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).duty);
    p->pa.setParameter("evengen",  PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).evenDepth);
    p->pa.setParameter("ripplesag",PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).rippleSagCoupling);
    p->pa.setParameter("ltptail",  PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).ltpTail);
    p->pa.setParameter("fluxOT",   PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).fluxOT ? 1.0f : 0.0f);
    // Per-amp PA drive/makeup (2026-07-31 fix): these were applied only to Rig B's
    // pa2 -- the main rig ran them at the 1.0 default, so EVH/Rockerverb/Vox (the
    // three models with non-neutral values) were 2-3x over-driven vs their
    // capture-tuned voicing. 1.0/1.0 for every other model = bit-identical.
    p->pa.setParameter("padrive",  PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).paDrive);
    p->pa.setParameter("pamakeup", PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).paMakeup);
    // HG round 2 per-amp bakes (2026-08-20): knee/tilt from AmpDefaults (the
    // tilt setters early-out on unchanged values, so per-run pushes are cheap).
    p->pa.setParameter("pakneecurve",  PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).kneeCurve);
    p->pa.setParameter("shapertilt",   PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).shaperTiltDb);
    p->pa.setParameter("shapertiltlo", PowerAmpProcessor::getDefaultsForModel(kCanonical[ampAlgo]).shaperTiltLoDb);
    // (The 2026-08-20 restored Fender PA-compression lab lived here for mv184
    // only. VERDICT: the user finally swept it by ear and chose STOCK
    // (depth 0.15 / release 13 ms) — nothing baked; the PA-compression
    // frontier is now closed WITH an ears verdict, not just by abandonment.
    // See pa-compression-fender memory.)
    // EVH capture-fit voicing: applied post-PA in the amp block below. LAB-
    // blendable (2026-08-22): dbg_evhfit scales the fit dB; default 0.8875 =
    // the 2026-08-19 bake.
    const bool evhFitOn = kAmpMap[ampAlgo] == AmpModel::EVH5150III;
    if (evhFitOn) {
        // (The 2026-08-22 dbg_evhfit lab was CANCELED unused — the blend stays
        // fixed at the 2026-08-19 ear-chosen bake; the blendable machinery is
        // kept dormant for the next lab session.)
        const float eb = 0.8875f;
        if (eb != p->evhLabLast) {
            p->evhLabLast = eb;
            for (int c = 0; c < 2; ++c) {
                p->evhLab[c][0].setCoeffs(Filters::lowshelf (  55.0, -5.0 * eb, p->rate));
                p->evhLab[c][1].setCoeffs(Filters::peaking  ( 130.0,  6.5 * eb, 1.1, p->rate));
                p->evhLab[c][2].setCoeffs(Filters::peaking  (1200.0,  2.5 * eb, 0.8, p->rate));
                p->evhLab[c][3].setCoeffs(Filters::highshelf(2400.0,  4.5 * eb, p->rate));
                p->evhLab[c][4].setCoeffs(Filters::highshelf(7500.0,  5.5 * eb, p->rate));
            }
            p->evhLabGain = std::pow(0.6918f, eb / 0.8875f);   // bake-anchored neutralizer
        }
    }
    // (The 2026-08-20 dbg_hgfit LAB lived here; retired at mv181 after the user
    // approved both candidates at blend 0.72 — values baked into AmpDefaults
    // rows 1/8 with the Plexi/MarkV row-10 split.)
    // Recto CH3-Modern capture-fit voicing (BAKED 2026-08-20 at the user's
    // blend 1.0; the nonlinear half — satDrive 1.84, tightHP 200 — is baked in
    // the model's mode-7 ModeCfg row): applied post-PA in the amp block below,
    // Diamond Plate + mode 7 only. See lv2/common/RectoCaptureFit.h.
    const bool rectoFitOn = (ampModel == kRectoIdx) && rcMode > 6.5f;
    if (desiredTube != p->lastAmpTube) { p->lastAmpTube = desiredTube; p->pa.setTubeType(static_cast<TubeType>(desiredTube)); }
    const bool paBypass = (*p->ports[HF_AMP_PAMP_BYPASS] > 0.5f) || (ampModel == kSunnIdx);
    p->pa.setBypass(paBypass);
    const float ampMakeup = kAmpMakeup[ampAlgo];
    // Cab
    p->cab.setBypass(false);
    p->cab.setParameter("lowCutHz",  *p->ports[HF_CAB_LOWCUT]);
    p->cab.setParameter("highCutHz", *p->ports[HF_CAB_HIGHCUT]);
    p->cab.setParameter("mix",       *p->ports[HF_CAB_MIX]);
    p->cab.setParameter("micpos",    *p->ports[HF_CAB_MICPOS]);   // mic placement (2026-07-14; 0/0 = as-voiced)
    p->cab.setParameter("micdist",   *p->ports[HF_CAB_MICDIST]);
    p->cab.setParameter("roomon",    *p->ports[HF_CAB_ROOMON]);   // room ambience (2026-07-14; off = bit-identical)
    p->cab.setParameter("roommix",   *p->ports[HF_CAB_ROOMMIX]);
    p->cab.setParameter("roomamt",   *p->ports[HF_CAB_ROOMAMT]);
    p->cab.setParameter("voice",     *p->ports[HF_CAB_VOICE]);   // Room / Studio (recorded chain)
    // monoRoom (2026-08-21): with the output mono-summed, the cab room's two
    // decorrelated banks fold into a doubled static comb — flanger-like woosh
    // on broadband playing (Periphery presets, one-speaker rig). One bank when
    // mono; bit-identical when the sum is off.
    const bool monoSum = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
    p->cab.setParameter("monoroom", monoSum ? 1.0f : 0.0f);
    p->pa.setParameter("coupling",   *p->ports[HF_AMP_PAMP_COUPL]);   // speaker-impedance coupling (v24)
    p->reverb.setParameter("density", *p->ports[HF_RV_DENSITY]);      // classic / dense tank (v25)
    p->reverb.setParameter("type",    *p->ports[HF_RV_TYPE]);          // plate / spring / ambient (v26/v27)
    p->reverb.setParameter("bloom",   *p->ports[HF_RV_BLOOM]);         // Hex Ambient bloom (v27)
    p->cab.setParameter("roomdense",  *p->ports[HF_CAB_ROOMDENSE]);    // classic / dense room (v26)
    {   // Speaker Drive (item #40, v28): 0 Off/1 Subtle/2 Full -> CabinetBlock's two
        // internal params. Subtle/Full (0.35/0.75) chosen conservatively for a brand-
        // new, not-yet-user-tuned character feature.
        const int spk = static_cast<int>(*p->ports[HF_CAB_SPKDRIVE] + 0.5f);
        p->cab.setParameter("spkdrive",    spk > 0 ? 1.0f : 0.0f);
        p->cab.setParameter("spkdriveamt", spk >= 2 ? 0.75f : (spk == 1 ? 0.35f : 0.0f));
    }
    {   // EQ block: preset base curve + slider offsets + level (rebuilds only on change)
        const float eqDb[GraphicEQ::kBands] = {
            *p->ports[HF_EQ_100], *p->ports[HF_EQ_200], *p->ports[HF_EQ_400],
            *p->ports[HF_EQ_800], *p->ports[HF_EQ_1K6], *p->ports[HF_EQ_3K2],
        };
        p->eq.update((int)*p->ports[HF_EQ_PRESET], eqDb, *p->ports[HF_EQ_LEVEL]);
    }
    // Modfx
    p->modfx.setBypass(false);
    const int modfxType = clampi(*p->ports[HF_MD_TYPE], 0, 8);   // was stuck at 6: Seasick Vibe (7) silently ran as Nevermind Chorus
    if (modfxType != p->lastModfxType) { p->lastModfxType = modfxType; p->modfx.setType(ModulationFactory::fromIndex(modfxType)); }
    // Mod clock sync: lock the LFO to host BPM x division (0 => free-run from the Rate knob).
    if (*p->ports[HF_MD_SYNC] > 0.5f) {
        const int mv = clampi(*p->ports[HF_MD_DIV], 0, 7);
        const float periodSec = (60.0f / p->hostBpm) * kDivFactor[mv];
        p->modfx.setSyncHz(periodSec > 0.0f ? (1.0f / periodSec) : 0.0f);
    } else {
        p->modfx.setSyncHz(0.0f);
    }
    p->modfx.setParameter("rate",        *p->ports[HF_MD_RATE]);
    p->modfx.setParameter("depth",       *p->ports[HF_MD_DEPTH]);
    p->modfx.setParameter("mix",         *p->ports[HF_MD_MIX]);
    p->modfx.setParameter("centerDelay", *p->ports[HF_MD_OFFSET]);   // ms (delay-line types only)
    p->modfx.setParameter("shape", *p->ports[HF_MD_SHAPE]);          // tremolo waveform (ignored by other types)
    // Mono mode: force EVERY width-based stereo effect to CENTERED (width 0) so a summed-
    // mono rig keeps full-level content. Otherwise the delay's pan (Seraph) loses ~6 dB to
    // pan law, the Digital delay's L/R time offset combs, and the chorus/Uni-Vibe LFO-phase
    // spread partially cancels — all read as the effect "cutting out" in mono. Reverb has no
    // width knob (decorrelated tails just narrow, never null) so the output sum covers it.
    const bool monoOut = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
    p->modfx.setParameter("stereoWidth", monoOut ? 0.0f : *p->ports[HF_MD_WIDTH]);
    // Delay
    p->delay.setBypass(false);
    const int delayType = clampi(*p->ports[HF_DL_TYPE], 0, 4);
    if (delayType != p->lastDelayType) { p->lastDelayType = delayType; p->delay.setType(DelayFactory::fromIndex(delayType)); }
    // Delay clock sync: delay time = host BPM x division (else the manual Time knob).
    float dlMs = *p->ports[HF_DL_TIME];
    if (*p->ports[HF_DL_SYNC] > 0.5f) {
        const int dv = clampi(*p->ports[HF_DL_DIV], 0, 7);
        dlMs = (60000.0f / p->hostBpm) * kDivFactor[dv];
        if (dlMs < 1.0f) dlMs = 1.0f; else if (dlMs > 2000.0f) dlMs = 2000.0f;
    }
    p->delay.setParameter("timeMs",       dlMs);
    p->delay.setParameter("feedback",     *p->ports[HF_DL_FEEDBACK]);
    p->delay.setParameter("mix",          *p->ports[HF_DL_MIX]);
    p->delay.setParameter("stereoWidth",  monoOut ? 0.0f : *p->ports[HF_DL_WIDTH]);
    p->delay.setParameter("wowDepth",     *p->ports[HF_DL_WOW]);
    p->delay.setParameter("flutterDepth", *p->ports[HF_DL_FLUTTER]);
    p->delay.setParameter("headMask", static_cast<float>(kEchorecProgram[clampi(*p->ports[HF_DL_HEADS],0,11)]));
    p->delay.setParameter("pattern",  *p->ports[HF_DL_PATTERN]);    // Seraph
    p->delay.setParameter("ducking",  *p->ports[HF_DL_DUCKING]);    // Seraph
    p->delay.setParameter("modDepth", *p->ports[HF_DL_MODDEPTH]);   // Seraph
    p->delay.setParameter("modRate",  *p->ports[HF_DL_MODRATE]);    // Seraph
    p->delay.setParameter("age",      *p->ports[HF_DL_AGE]);        // EP-3 (v42)
    // Reverb
    p->reverb.setBypass(false);
    p->reverb.setParameter("preDelayMs", *p->ports[HF_RV_PREDELAY]);
    p->reverb.setParameter("decayTime",  *p->ports[HF_RV_DECAY]);
    p->reverb.setParameter("damping",    *p->ports[HF_RV_DAMPING]);
    p->reverb.setParameter("modDepth",   *p->ports[HF_RV_MODDEPTH]);
    p->reverb.setParameter("modRate",    *p->ports[HF_RV_MODRATE]);
    p->reverb.setParameter("mix",        *p->ports[HF_RV_MIX]);
    // Wah
    p->wah.setBypass(false);
    p->wah.setParameter("type",  *p->ports[HF_WH_TYPE]);
    p->wah.setParameter("freq",  *p->ports[HF_WH_FREQ]);
    p->wah.setParameter("depth", *p->ports[HF_WH_DEPTH]);
    p->wah.setParameter("sens",  *p->ports[HF_WH_SENS]);
    p->wah.setParameter("q",     *p->ports[HF_WH_Q]);
    p->wah.setParameter("mix",   *p->ports[HF_WH_MIX]);
    // Octave
    p->octave.setBypass(false);
    p->octave.setParameter("up",       *p->ports[HF_OC_UP]);
    p->octave.setParameter("down",     *p->ports[HF_OC_DOWN]);
    p->octave.setParameter("dry",      *p->ports[HF_OC_DRY]);
    p->octave.setParameter("micro",    *p->ports[HF_OC_MICRO]);
    p->octave.setParameter("interval", *p->ports[HF_OC_INTERVAL]);

    // ── X2 second instances (v38): params applied only while the block is enabled
    // (cheap idle; values re-read on the first enabled run, so nothing goes stale).
    int fz2Pedal = 0;
    if (*p->ports[HF_GT2_ENABLE] > 0.5f) {
        p->gate2.setBypass(false);
        p->gate2.setParameter("threshold",
            std::clamp(*p->ports[HF_GT2_THRESH] + p->calFloorOffsSm, -80.0f, 0.0f));
        p->gate2.setParameter("attack",     *p->ports[HF_GT2_ATTACK]);
        p->gate2.setParameter("hold",       *p->ports[HF_GT2_HOLD]);
        p->gate2.setParameter("release",    *p->ports[HF_GT2_RELEASE]);
        p->gate2.setParameter("hysteresis", *p->ports[HF_GT2_HYST]);
    }
    if (*p->ports[HF_CP2_ENABLE] > 0.5f) {
        p->comp2.setBypass(false);
        p->comp2.setParameter("type",      *p->ports[HF_CP2_TYPE]);
        p->comp2.setParameter("threshold", *p->ports[HF_CP2_THRESH]);
        p->comp2.setParameter("ratio",     *p->ports[HF_CP2_RATIO]);
        p->comp2.setParameter("attack",    *p->ports[HF_CP2_ATTACK]);
        p->comp2.setParameter("release",   *p->ports[HF_CP2_RELEASE]);
        p->comp2.setParameter("knee",      *p->ports[HF_CP2_KNEE]);
        p->comp2.setParameter("makeup",    *p->ports[HF_CP2_MAKEUP]);
    }
    if (*p->ports[HF_FZ2_ENABLE] > 0.5f) {
        fz2Pedal = clampi(*p->ports[HF_FZ2_PEDAL], 0, 3);
        p->fuzz2Muff->setBypass(false); p->fuzz2Bender->setBypass(false);
        p->fuzz2Octavia->setBypass(false); p->fuzz2Factory->setBypass(false);
        p->fuzz2Muff->setParameter("era",   *p->ports[HF_FZ2_MODE]);
        p->fuzz2Muff->setParameter("drive", *p->ports[HF_FZ2_SUSTAIN]);
        p->fuzz2Muff->setParameter("tone",  *p->ports[HF_FZ2_TONE]);
        p->fuzz2Muff->setParameter("level", *p->ports[HF_FZ2_VOLUME]);
        p->fuzz2Bender->setParameter("attack",    *p->ports[HF_FZ2_SUSTAIN]);
        p->fuzz2Bender->setParameter("level",     *p->ports[HF_FZ2_VOLUME]);
        p->fuzz2Bender->setParameter("bias",      *p->ports[HF_FZ2_BIAS]);
        p->fuzz2Bender->setParameter("inputtrim", *p->ports[HF_FZ2_INPUTTRIM]);
        p->fuzz2Bender->setParameter("getemp",    *p->ports[HF_FZ2_GETEMP]);
        p->fuzz2Bender->setParameter("gvol",      *p->ports[HF_FZ2_GVOL]);
        p->fuzz2Octavia->setParameter("drive", *p->ports[HF_FZ2_SUSTAIN]);
        p->fuzz2Octavia->setParameter("tone",  *p->ports[HF_FZ2_TONE]);
        p->fuzz2Octavia->setParameter("level", *p->ports[HF_FZ2_VOLUME]);
        p->fuzz2Factory->setParameter("sustain",   *p->ports[HF_FZ2_SUSTAIN]);
        p->fuzz2Factory->setParameter("bias",      *p->ports[HF_FZ2_BIAS]);
        p->fuzz2Factory->setParameter("inputtrim", *p->ports[HF_FZ2_INPUTTRIM]);
        p->fuzz2Factory->setParameter("getemp",    *p->ports[HF_FZ2_GETEMP]);
        p->fuzz2Factory->setParameter("level",     *p->ports[HF_FZ2_VOLUME]);
    }
    if (*p->ports[HF_NAIL2_ENABLE] > 0.5f) {
        p->nail2->setBypass(false);
        const int nail2Mode = clampi(*p->ports[HF_NAIL2_MODE], 0, NailDistortion::kNumModes - 1);
        if (nail2Mode != p->lastNail2Mode) { p->lastNail2Mode = nail2Mode; p->nail2->setParameter("mode", (float)nail2Mode); }
        p->nail2->setParameter("drive",   *p->ports[HF_NAIL2_DRIVE]);
        p->nail2->setParameter("tone",    *p->ports[HF_NAIL2_TONE]);
        p->nail2->setParameter("texture", *p->ports[HF_NAIL2_TEXTURE]);
        p->nail2->setParameter("level",   *p->ports[HF_NAIL2_LEVEL]);
    }
    if (*p->ports[HF_MD2_ENABLE] > 0.5f) {
        p->modfx2.setBypass(false);
        const int modfx2Type = clampi(*p->ports[HF_MD2_TYPE], 0, 8);
        if (modfx2Type != p->lastModfx2Type) { p->lastModfx2Type = modfx2Type; p->modfx2.setType(ModulationFactory::fromIndex(modfx2Type)); }
        if (*p->ports[HF_MD2_SYNC] > 0.5f) {
            const int mv2 = clampi(*p->ports[HF_MD2_DIV], 0, 7);
            const float ps2 = (60.0f / p->hostBpm) * kDivFactor[mv2];
            p->modfx2.setSyncHz(ps2 > 0.0f ? (1.0f / ps2) : 0.0f);
        } else p->modfx2.setSyncHz(0.0f);
        p->modfx2.setParameter("rate",        *p->ports[HF_MD2_RATE]);
        p->modfx2.setParameter("depth",       *p->ports[HF_MD2_DEPTH]);
        p->modfx2.setParameter("mix",         *p->ports[HF_MD2_MIX]);
        p->modfx2.setParameter("centerDelay", *p->ports[HF_MD2_OFFSET]);
        p->modfx2.setParameter("shape",       *p->ports[HF_MD2_SHAPE]);
        const bool monoOut2 = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
        p->modfx2.setParameter("stereoWidth", monoOut2 ? 0.0f : *p->ports[HF_MD2_WIDTH]);
    }
    if (*p->ports[HF_DL2_ENABLE] > 0.5f) {
        p->delay2.setBypass(false);
        const int delay2Type = clampi(*p->ports[HF_DL2_TYPE], 0, 4);
        if (delay2Type != p->lastDelay2Type) { p->lastDelay2Type = delay2Type; p->delay2.setType(DelayFactory::fromIndex(delay2Type)); }
        float dl2Ms = *p->ports[HF_DL2_TIME];
        if (*p->ports[HF_DL2_SYNC] > 0.5f) {
            const int dv2 = clampi(*p->ports[HF_DL2_DIV], 0, 7);
            dl2Ms = (60000.0f / p->hostBpm) * kDivFactor[dv2];
            if (dl2Ms < 1.0f) dl2Ms = 1.0f; else if (dl2Ms > 2000.0f) dl2Ms = 2000.0f;
        }
        const bool monoOut2 = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
        p->delay2.setParameter("timeMs",       dl2Ms);
        p->delay2.setParameter("feedback",     *p->ports[HF_DL2_FEEDBACK]);
        p->delay2.setParameter("mix",          *p->ports[HF_DL2_MIX]);
        p->delay2.setParameter("stereoWidth",  monoOut2 ? 0.0f : *p->ports[HF_DL2_WIDTH]);
        p->delay2.setParameter("wowDepth",     *p->ports[HF_DL2_WOW]);
        p->delay2.setParameter("flutterDepth", *p->ports[HF_DL2_FLUTTER]);
        p->delay2.setParameter("headMask", static_cast<float>(kEchorecProgram[clampi(*p->ports[HF_DL2_HEADS],0,11)]));
        p->delay2.setParameter("pattern",  *p->ports[HF_DL2_PATTERN]);
        p->delay2.setParameter("ducking",  *p->ports[HF_DL2_DUCKING]);
        p->delay2.setParameter("modDepth", *p->ports[HF_DL2_MODDEPTH]);
        p->delay2.setParameter("modRate",  *p->ports[HF_DL2_MODRATE]);
        p->delay2.setParameter("age",      *p->ports[HF_DL2_AGE]);   // EP-3 (v42)
    }
    if (*p->ports[HF_RV2_ENABLE] > 0.5f) {
        p->reverb2.setBypass(false);
        p->reverb2.setParameter("density",    *p->ports[HF_RV2_DENSITY]);
        p->reverb2.setParameter("type",       *p->ports[HF_RV2_TYPE]);
        p->reverb2.setParameter("bloom",      *p->ports[HF_RV2_BLOOM]);
        p->reverb2.setParameter("preDelayMs", *p->ports[HF_RV2_PREDELAY]);
        p->reverb2.setParameter("decayTime",  *p->ports[HF_RV2_DECAY]);
        p->reverb2.setParameter("damping",    *p->ports[HF_RV2_DAMPING]);
        p->reverb2.setParameter("modDepth",   *p->ports[HF_RV2_MODDEPTH]);
        p->reverb2.setParameter("modRate",    *p->ports[HF_RV2_MODRATE]);
        p->reverb2.setParameter("mix",        *p->ports[HF_RV2_MIX]);
    }
    if (*p->ports[HF_WH2_ENABLE] > 0.5f) {
        p->wah2.setBypass(false);
        p->wah2.setParameter("type",  *p->ports[HF_WH2_TYPE]);
        p->wah2.setParameter("freq",  *p->ports[HF_WH2_FREQ]);
        p->wah2.setParameter("depth", *p->ports[HF_WH2_DEPTH]);
        p->wah2.setParameter("sens",  *p->ports[HF_WH2_SENS]);
        p->wah2.setParameter("q",     *p->ports[HF_WH2_Q]);
        p->wah2.setParameter("mix",   *p->ports[HF_WH2_MIX]);
    }
    if (*p->ports[HF_OC2_ENABLE] > 0.5f) {
        p->octave2.setBypass(false);
        p->octave2.setParameter("up",       *p->ports[HF_OC2_UP]);
        p->octave2.setParameter("down",     *p->ports[HF_OC2_DOWN]);
        p->octave2.setParameter("dry",      *p->ports[HF_OC2_DRY]);
        p->octave2.setParameter("micro",    *p->ports[HF_OC2_MICRO]);
        p->octave2.setParameter("interval", *p->ports[HF_OC2_INTERVAL]);
    }
    if (*p->ports[HF_EQ2_ENABLE] > 0.5f) {
        const float eq2Db[GraphicEQ::kBands] = {
            *p->ports[HF_EQ2_100], *p->ports[HF_EQ2_200], *p->ports[HF_EQ2_400],
            *p->ports[HF_EQ2_800], *p->ports[HF_EQ2_1K6], *p->ports[HF_EQ2_3K2],
        };
        p->eq2.update((int)*p->ports[HF_EQ2_PRESET], eq2Db, *p->ports[HF_EQ2_LEVEL]);
    }

    // ── Resolve chain order (Input Trim locked first; rest sorted by pos) ──
    int order[B_COUNT];
    for (int i=0;i<B_COUNT;++i) order[i] = i;
    int posv[B_COUNT];
    for (int i=0;i<B_COUNT;++i) posv[i] = clampi(*p->ports[kPosPort[i]], 1, 24);  // 24 movable blocks since the X2 clones (v38) — clamp must track B_COUNT
    // Selection sort by the TOTAL order (pos, canonical index). 2026-08-22
    // release audit: the old strict-< compare was NOT the stable tie-break its
    // comment claimed — a swap could flip two equal-pos blocks when a smaller-
    // pos block sat after them. All factory presets were unaffected (verified:
    // their sub-tie blocks are parent-id-ordered), but a user drag-reorder
    // could have flipped e.g. dual delays. The pair compare removes ties
    // entirely, so the swap-based sort is exact.
    for (int a=0;a<B_COUNT-1;++a) {
        int best=a;
        for (int b=a+1;b<B_COUNT;++b)
            if (posv[order[b]] <  posv[order[best]] ||
                (posv[order[b]] == posv[order[best]] && order[b] < order[best])) best=b;
        if (best!=a) { int t=order[a]; order[a]=order[best]; order[best]=t; }
    }
    bool enabled[B_COUNT];   // "run this block": in the chain AND not bypassed
    for (int i=0;i<B_COUNT;++i)
        enabled[i] = (*p->ports[kEnablePort[i]] > 0.5f) && (*p->ports[kBypassPort[i]] <= 0.5f);
    // ── Seamless switching: detect discontinuous events, freeze topology while
    // the mute ramp falls, adopt the new state at the zero point (hfApplySwitch).
    if (!p->swPrevInit || p->swAcceptNext) {
        p->swPrevInit = true; p->swAcceptNext = false;
        for (int i = 0; i < B_COUNT; ++i) p->swEnabledPrev[i] = enabled[i];
        for (int w = 0; w < kSwWatchN; ++w) p->swWatchPrev[w] = *p->ports[kSwWatch[w]];
    } else {
        bool topo = false;
        for (int i = 0; i < B_COUNT; ++i) if (enabled[i] != p->swEnabledPrev[i]) topo = true;
        bool step = false;
        for (int w = 0; w < kSwWatchN; ++w)
            if (*p->ports[kSwWatch[w]] != p->swWatchPrev[w]) { step = true; p->swWatchPrev[w] = *p->ports[kSwWatch[w]]; }
        if (topo && !p->swHold) {
            p->swHold = true;
            for (int i = 0; i < B_COUNT; ++i) p->swEnabledHeld[i] = p->swEnabledPrev[i];
            if (p->swFadeState == 0) p->swFadeState = 1;
        }
        if (step && p->swFadeState == 0) p->swFadeState = 1;
    }
    if (p->swHold) for (int i = 0; i < B_COUNT; ++i) enabled[i] = p->swEnabledHeld[i];
    else           for (int i = 0; i < B_COUNT; ++i) p->swEnabledPrev[i] = enabled[i];

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
                float x = p->trimLoad.process(L[i]);   // pickup loading: physically first
                if (itHum) x = p->trimHum.process(x);
                if (itHB)    x = p->trimVoice.process(x);   // single-coil -> humbucker voicing
                if (itBoost) x = p->trimBoost.process(x);   // output boost + beef
                x *= itGain;
                L[i] = x; R[i] = x;
            }
        }

        struct CpuClk { static double now() { return hfplat::monotonicSeconds(); } };
        const double chunkT0 = CpuClk::now();
        // Rig B: run amp2 -> pa2 -> cab2 on the tapped buffer and mix into L/R.
        // Called after the main CAB (or after the AMP when no cab is active).
        auto rigBMix = [&](void) {
            const bool rbNamRun = (clampi(*p->ports[HF_RB_AMP], 0, kMt15Idx) == kAmpNamIdx)
                                  && p->amp2Nam && p->amp2Nam->isLoaded();
            if (!p->rigBHeld || (!rbNamRun && !p->amp2)) return;
            p->rigBHeld = false;
            const bool cab2In = *p->ports[HF_RB_CAB2ON] > 0.5f;
            const double rbT0 = CpuClk::now();
            float* io1[1] = { p->rigBBuf };
            if (rbNamRun) {
                // Neural Amp 2 (v39): mono capture + Gain/Level trims, no power amp
                // (the capture carries its own) -- mirrors the A-side NAM path.
                const float ig = std::pow(10.0f, *p->ports[HF_RB_NAM_GAIN] / 20.0f);
                const float og = std::pow(10.0f, *p->ports[HF_RB_NAM_VOL]  / 20.0f);
                for (int i=0;i<len;++i) p->rigBBuf[i] *= ig;
                p->amp2Nam->processBuffer(p->rigBBuf, p->monoOut, len);
                for (int i=0;i<len;++i) p->rigBBuf[i] = p->monoOut[i] * og;
            } else {
            p->amp2->process(io1, io1, len, 1);
            p->pa2.process(io1, io1, len, 1);
            p->amp2->setExternalSag(p->pa2.getSagEnvNorm());
            const int rbAlgo = (p->lastAmp2Model < 0) ? 1 : ((p->lastAmp2Model == 5) ? 1 : p->lastAmp2Model);
            const float mk2 = kAmpMakeup[rbAlgo];
            if (mk2 != 1.0f) for (int i=0;i<len;++i) p->rigBBuf[i] *= mk2;
            }
            // Cab 2 only when it has been brought into the chain; otherwise rig B
            // joins BEFORE Cab 1 and shares it (incl. sharing its bypass -- the
            // real-poweramp rig works naturally).
            if (cab2In) {
                const double c2T0 = CpuClk::now();
                p->cab2.process(io1, io1, len, 1);
                p->cpuCab2 += CpuClk::now() - c2T0;
            }
            // rb_locut (v44): HP the layer BEFORE it blends — the dual-rig clean
            // blends partially cancel rig A, and the clean layer's dynamics pump
            // the cancellation depth in the low-mids (the Periphery woosh,
            // isolated by build-tools/woosh2.cpp). 0 = off = bit-identical.
            const float lc = p->ports[HF_RB_LOCUT] ? *p->ports[HF_RB_LOCUT] : 0.0f;
            if (lc > 10.0f) {
                if (lc != p->rbLoCutLast) { p->rbLoCutLast = lc;
                    p->rbLoCut.setCoeffs(Filters::highpass(lc, 0.707, p->rate)); }
                for (int i=0;i<len;++i) p->rigBBuf[i] = p->rbLoCut.process(p->rigBBuf[i]);
            }
            const float blend = *p->ports[HF_RB_BLEND];
            float g = std::pow(10.0f, *p->ports[HF_RB_LEVEL] / 20.0f) * blend;
            if (*p->ports[HF_RB_POL] > 0.5f) g = -g;
            const float ga = 1.0f - blend;
            for (int i=0;i<len;++i) {
                L[i] = ga * L[i] + g * p->rigBBuf[i];
                R[i] = ga * R[i] + g * p->rigBBuf[i];
            }
            p->cpuRigB += CpuClk::now() - rbT0;
        };
        for (int oi=0; oi<B_COUNT; ++oi) {
            const int id = order[oi];
            if (!enabled[id]) continue;
            const double blkT0 = CpuClk::now();
            switch (id) {
                case B_DRIVE2:
                    if (drive2Model == kDrNamIdx && p->dr2Nam && p->dr2Nam->isLoaded()) {
                        // Neural Drive 2 (v40): mirror of the Drive 1 NAM path.
                        const float ig2=std::pow(10.0f,*p->ports[HF_DR2_NAM_GAIN]/20.0f);
                        const float og2=std::pow(10.0f,*p->ports[HF_DR2_NAM_VOL] /20.0f);
                        for (int i=0;i<len;++i) p->mono[i]=ig2*(stereo?0.5f*(L[i]+R[i]):L[i]);
                        p->dr2Nam->processBuffer(p->mono, p->monoOut, len);
                        const float mix2=*p->ports[HF_DR2_MIX], wet2=og2*mix2, dry2=1.0f-mix2;
                        for (int i=0;i<len;++i){ float d=stereo?0.5f*(L[i]+R[i]):L[i]; float o=dry2*d+wet2*p->monoOut[i]; L[i]=o; R[i]=o; }
                    } else runMono(p->drive2, L, R, len, p->mono, stereo);
                    break;
                case B_GATE: {
                    // Keyed gate (2026-07-29 idle-whine fix): detector on the RAW
                    // pre-InputTrim input -- the IT pickup voicing + boost lift the
                    // in-chain floor up to ~+20 dB, which grazed the open threshold
                    // and hysteresis-latched the gate open on the rig's hum floor
                    // (the thresholds were floor-complianced against RAW levels).
                    if (!stereo) for (int i=0;i<len;++i) p->mono[i] = L[i];
                    else         for (int i=0;i<len;++i) p->mono[i] = 0.5f*(L[i]+R[i]);
                    float* io[1] = { p->mono };
                    p->gate.processKeyed(io, inL + off, io, len, 1);
                    for (int i=0;i<len;++i) { L[i] = p->mono[i]; R[i] = p->mono[i]; }
                    break;
                }
                case B_COMP:  runMono(p->comp, L, R, len, p->mono, stereo); break;
                case B_FUZZ:  runMono(fuzzPedal==0 ? *p->fuzzMuff : (fuzzPedal==1 ? *p->fuzzBender : (fuzzPedal==2 ? *p->fuzzOctavia : *p->fuzzFactory)), L, R, len, p->mono, stereo); break;
                case B_DRIVE:
                    if (driveModel == kDrNamIdx && p->drNam && p->drNam->isLoaded()) {
                        // Neural drive: mono; NAM Gain (input drive) + NAM Level (output), both dB; Mix dry/wet.
                        const float ig=std::pow(10.0f,*p->ports[HF_DR_NAM_GAIN]/20.0f);
                        const float og=std::pow(10.0f,*p->ports[HF_DR_NAM_VOL] /20.0f);
                        for (int i=0;i<len;++i) p->mono[i]=ig*(stereo?0.5f*(L[i]+R[i]):L[i]);
                        p->drNam->processBuffer(p->mono, p->monoOut, len);
                        const float mix=*p->ports[HF_DR_MIX], wet=og*mix, dry=1.0f-mix;
                        for (int i=0;i<len;++i){ float d=stereo?0.5f*(L[i]+R[i]):L[i]; float o=dry*d+wet*p->monoOut[i]; L[i]=o; R[i]=o; }
                    } else runMono(p->drive, L, R, len, p->mono, stereo);
                    break;
                case B_AMP: {
                    // Rig B tap: the parallel rig feeds from the main amp's input.
                    if (*p->ports[HF_RB_ENABLE] > 0.5f
                        && (p->amp2 || (p->amp2Nam && p->amp2Nam->isLoaded()))) {
                        if (!stereo) for (int i=0;i<len;++i) p->rigBBuf[i] = L[i];
                        else         for (int i=0;i<len;++i) p->rigBBuf[i] = 0.5f*(L[i]+R[i]);
                        p->rigBHeld = true;
                    }
                    if (!stereo) for (int i=0;i<len;++i) R[i]=L[i];
                    // NOTE: the amp gets the RAW input (as it did before 2026-07-03 and as the
                    // standalone Amp plugin still does). The old `kAmpInputCeil` tanh "input ceiling"
                    // was a nonlinearity at ANY setting — even when barely attenuating, it added
                    // harmonics that the high-gain front-ends then amplified into a fuzzy/dark/woolly
                    // voice. Removed. A hot upstream fuzz is handled by preset gain-staging (clean
                    // amps + tamed fuzz volume), not by pre-distorting every amp's input.
                    if (ampModel == kAmpNamIdx && p->ampNam && p->ampNam->isLoaded()) {
                        // Neural amp: mono capture -> both; NAM Gain (input drive) + NAM Level (output), dB; no power amp.
                        const float ig=std::pow(10.0f,*p->ports[HF_AMP_NAM_GAIN]/20.0f);
                        const float og=std::pow(10.0f,*p->ports[HF_AMP_NAM_VOL] /20.0f);
                        for (int i=0;i<len;++i) p->mono[i]=ig*0.5f*(L[i]+R[i]);
                        p->ampNam->processBuffer(p->mono, p->monoOut, len);
                        for (int i=0;i<len;++i){ float y=p->monoOut[i]*og; L[i]=y; R[i]=y; }
                    } else if (!stereo) {
                        // Mono chain (2026-07-23 perf): everything upstream is identical L/R,
                        // so run the 4x-oversampled cascade + PA ONCE and mirror — the amp is
                        // the heaviest internal DSP and this halves it. Bit-identical output.
                        float* io1[1] = { L };
                        amp->process(io1, io1, len, 1);
                        p->pa.process(io1, io1, len, 1);
                        amp->setExternalSag(p->pa.getSagEnvNorm()); // item #22, 2026-07-28
                        if (evhFitOn) for (int i=0;i<len;++i) {
                            float v = L[i];
                            for (auto& f : p->evhLab[0]) v = f.process(v);
                            L[i] = p->evhLabGain * v;
                        }
                        if (rectoFitOn) for (int i=0;i<len;++i) L[i]=p->rectoFit[0].process(L[i]);
                        if (ampMakeup != 1.0f) for (int i=0;i<len;++i) L[i]*=ampMakeup;
                        for (int i=0;i<len;++i) R[i]=L[i];
                    } else {
                        float* io[2] = { L, R };
                        amp->process(io, io, len, 2);
                        p->pa.process(io, io, len, 2);
                        amp->setExternalSag(p->pa.getSagEnvNorm()); // item #22, 2026-07-28
                        if (evhFitOn) for (int i=0;i<len;++i) {
                            float vl = L[i], vr = R[i];
                            for (auto& f : p->evhLab[0]) vl = f.process(vl);
                            for (auto& f : p->evhLab[1]) vr = f.process(vr);
                            L[i] = p->evhLabGain * vl; R[i] = p->evhLabGain * vr;
                        }
                        if (rectoFitOn) for (int i=0;i<len;++i) {
                            L[i]=p->rectoFit[0].process(L[i]);
                            R[i]=p->rectoFit[1].process(R[i]);
                        }
                        if (ampMakeup != 1.0f) for (int i=0;i<len;++i){ L[i]*=ampMakeup; R[i]*=ampMakeup; }
                    }
                    // NOTE: the amp's output is mono-identical when its input was — do NOT
                    // force the stereo flag; downstream blocks keep their mono fast paths.
                    break;
                }
                case B_CAB:
                    if (p->cabNam && p->cabNam->isLoaded()) {
                        // Neural cab/rig overrides the IR convolver; NAM Gain (input) + NAM Level (output), dB; Mix = dry/wet.
                        const bool wasMono = !stereo;
                        if (wasMono) for (int i=0;i<len;++i) R[i]=L[i];
                        const float ig=std::pow(10.0f,*p->ports[HF_CAB_NAM_GAIN]/20.0f);
                        const float og=std::pow(10.0f,*p->ports[HF_CAB_NAM_VOL] /20.0f);
                        for (int i=0;i<len;++i) p->mono[i]=ig*0.5f*(L[i]+R[i]);
                        p->cabNam->processBuffer(p->mono, p->monoOut, len);
                        const float mix=*p->ports[HF_CAB_MIX], dry=1.0f-mix;
                        for (int i=0;i<len;++i){ float w=p->monoOut[i]*og*mix; L[i]=dry*L[i]+w; R[i]=dry*R[i]+w; }
                        if (!wasMono) stereo = true;   // mono in -> mono-identical out
                    } else if (!stereo) {
                        // Mono chain (2026-07-23 perf): ONE convolution + EQ, fanned out with
                        // per-channel room — bit-identical to two identical channels.
                        p->cab.processMonoToStereo(L, R, len);
                        // Only the room decorrelates; the output is truly stereo iff it ran.
                        if (p->cab.getParameter("roomon") > 0.5f
                            && p->cab.getParameter("roommix") > 0.001f
                            && p->cab.getParameter("voice") < 0.5f) stereo = true;
                    } else runStereo(p->cab, L, R, len, stereo);
                    break;
                case B_MODFX:  runStereo(p->modfx,  L, R, len, stereo); break;
                case B_DELAY:  runStereo(p->delay,  L, R, len, stereo); break;
                case B_REVERB: runStereo(p->reverb, L, R, len, stereo); break;
                case B_WAH:    runMono(p->wah, L, R, len, p->mono, stereo); break;
                case B_OCTAVE: runMono(p->octave, L, R, len, p->mono, stereo); break;
                case B_NAIL:   runMono(*p->nail, L, R, len, p->mono, stereo); break;
                case B_EQ:     p->eq.processCh(L, len, 0);
                               if (stereo) p->eq.processCh(R, len, 1); break;
                // X2 second instances (v38)
                case B_GATE2: {   // keyed on the RAW pre-InputTrim input, like Gate 1
                    if (!stereo) for (int i=0;i<len;++i) p->mono[i] = L[i];
                    else         for (int i=0;i<len;++i) p->mono[i] = 0.5f*(L[i]+R[i]);
                    float* io2[1] = { p->mono };
                    p->gate2.processKeyed(io2, inL + off, io2, len, 1);
                    for (int i=0;i<len;++i) { L[i] = p->mono[i]; R[i] = p->mono[i]; }
                    break;
                }
                case B_COMP2:   runMono(p->comp2, L, R, len, p->mono, stereo); break;
                case B_FUZZ2:   runMono(fz2Pedal==0 ? *p->fuzz2Muff : (fz2Pedal==1 ? *p->fuzz2Bender : (fz2Pedal==2 ? *p->fuzz2Octavia : *p->fuzz2Factory)), L, R, len, p->mono, stereo); break;
                case B_NAIL2:   runMono(*p->nail2, L, R, len, p->mono, stereo); break;
                case B_MODFX2:  runStereo(p->modfx2,  L, R, len, stereo); break;
                case B_DELAY2:  runStereo(p->delay2,  L, R, len, stereo); break;
                case B_REVERB2: runStereo(p->reverb2, L, R, len, stereo); break;
                case B_WAH2:    runMono(p->wah2, L, R, len, p->mono, stereo); break;
                case B_OCTAVE2: runMono(p->octave2, L, R, len, p->mono, stereo); break;
                case B_EQ2:     p->eq2.processCh(L, len, 0);
                                if (stereo) p->eq2.processCh(R, len, 1); break;
            }
            p->cpuAcc[id] += CpuClk::now() - blkT0;
            if (id == B_CAB && *p->ports[HF_RB_CAB2ON] > 0.5f) rigBMix();       // Cab 2 present: join after Cab 1
            if (id == B_AMP && !(*p->ports[HF_RB_CAB2ON] > 0.5f)) rigBMix();     // no Cab 2: join before Cab 1 (shared)
        }
        rigBMix();   // no active cab in the chain: join after the last block
        p->cpuAcc[B_COUNT] += CpuClk::now() - chunkT0;
        p->cpuSamps += len;
        if (p->cpuSamps >= 24000) {   // publish ~2x/sec: % of the audio-time budget
            const double budget = double(p->cpuSamps) / p->rate;
            // Explicit map: the contiguous cpu_gt..cpu_eq region covers the original
            // 13 blocks; later blocks (Drive B...) have their meters appended after
            // cpu_total, so HF_CPU_GT + id would land on the wrong port.
            static const int kCpuPort[B_COUNT] = {
                HF_CPU_GT, HF_CPU_CP, HF_CPU_FZ, HF_CPU_DR, HF_CPU_AMP, HF_CPU_CAB,
                HF_CPU_MD, HF_CPU_DL, HF_CPU_RV, HF_CPU_WH, HF_CPU_OC, HF_CPU_NAIL,
                HF_CPU_EQ, HF_CPU_DR2,
                HF_CPU_GT2, HF_CPU_CP2, HF_CPU_FZ2, HF_CPU_NAIL2, HF_CPU_MD2,
                HF_CPU_DL2, HF_CPU_RV2, HF_CPU_WH2, HF_CPU_OC2, HF_CPU_EQ2,
            };
            for (int b = 0; b < B_COUNT; ++b) {
                if (p->hostPorts[kCpuPort[b]])
                    *p->hostPorts[kCpuPort[b]] = float(100.0 * p->cpuAcc[b] / budget);
                p->cpuAcc[b] = 0.0;
            }
            if (p->hostPorts[HF_CPU_TOTAL])
                *p->hostPorts[HF_CPU_TOTAL] = float(100.0 * p->cpuAcc[B_COUNT] / budget);
            p->cpuAcc[B_COUNT] = 0.0;
            if (p->hostPorts[HF_CPU_RIGB])
                *p->hostPorts[HF_CPU_RIGB] = float(100.0 * p->cpuRigB / budget);
            p->cpuRigB = 0.0;
            if (p->hostPorts[HF_CPU_CAB2])
                *p->hostPorts[HF_CPU_CAB2] = float(100.0 * p->cpuCab2 / budget);
            p->cpuCab2 = 0.0;
            p->cpuSamps = 0;
        }
        // If nothing ever spread to stereo, R already mirrors L (mono blocks wrote both;
        // if the whole chain was empty, copy L→R for a mono-correct output).
        if (!stereo) for (int i=0;i<len;++i) R[i] = L[i];
    }

    // ── Doubler (v3, 2026-07-23): LOOSE-TIMING double-track. v2's constant -6 cent
    // detune was still a chorus by construction — a steady pitch offset beats against
    // the dry at a fixed rate (that's the micropitch effect, user heard it). A real
    // second take holds NO pitch offset: its timing wanders slowly around a ~28 ms
    // lag, so pitch deviation drifts through ZERO (peaks ±4-9 cents, mean 0) and the
    // wander is NON-periodic (three incommensurate slow sines ≈ human sloppiness,
    // dominant periods 4-21 s — far below any chorus LFO). Deterministic, no RNG.
    if (p->ports[HF_OUT_DOUBLER] && *p->ports[HF_OUT_DOUBLER] > 0.5f && !p->dblBuf.empty()
        && !fvOn) {   // FRFR voice: doubler auto-muted (one mono speaker would comb the two takes)
        const int len = (int)p->dblBuf.size();
        const bool monoRig = p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f;
        constexpr double kTwoPi = 6.283185307179586;
        const double w1 = kTwoPi * 0.11  / p->rate;       // timing-wander rates (Hz)
        const double w2 = kTwoPi * 0.23  / p->rate;
        const double w3 = kTwoPi * 0.047 / p->rate;
        for (uint32_t i = 0; i < n; ++i) {
            p->dblBuf[(size_t)p->dblW] = 0.5f * (outL[i] + outR[i]);
            p->dblPhase += w1; if (p->dblPhase >= kTwoPi) p->dblPhase -= kTwoPi;
            p->dblPh2   += w2; if (p->dblPh2   >= kTwoPi) p->dblPh2   -= kTwoPi;
            p->dblPh3   += w3; if (p->dblPh3   >= kTwoPi) p->dblPh3   -= kTwoPi;
            const double wob = 0.0035 * std::sin(p->dblPhase)
                             + 0.0014 * std::sin(p->dblPh2)
                             + 0.0025 * std::sin(p->dblPh3);      // ±6.9 ms, non-periodic
            double rp = (double)p->dblW - (0.028 + wob) * p->rate; // 21-35 ms behind the dry
            while (rp < 0.0) rp += (double)len;
            const int   i0 = (int)rp;
            const float fr = (float)(rp - (double)i0);
            const int   i1 = (i0 + 1 == len) ? 0 : i0 + 1;
            const float w  = p->dblBuf[(size_t)i0] * (1.0f - fr) + p->dblBuf[(size_t)i1] * fr;
            if (monoRig) {
                // Mono rig: keep the dry take intact on BOTH sides and tuck the second
                // take ~7.5 dB under. Two allpasses scramble its phase first, so the
                // sum reads as thickness, not a harmonic comb against the dry.
                float v = w;
                { float& cell = p->dblApA[(size_t)p->dblApAw];
                  const float y = cell - 0.55f * v; cell = v + 0.55f * y;
                  if (++p->dblApAw >= (int)p->dblApA.size()) p->dblApAw = 0; v = y; }
                { float& cell = p->dblApB[(size_t)p->dblApBw];
                  const float y = cell - 0.55f * v; cell = v + 0.55f * y;
                  if (++p->dblApBw >= (int)p->dblApB.size()) p->dblApBw = 0; v = y; }
                outL[i] += 0.42f * v;
                outR[i] += 0.42f * v;
            } else {
                // Stereo rig: the right channel becomes the second take (hard double).
                outR[i] = 0.30f * outR[i] + 0.85f * w;
            }
            if (++p->dblW >= len) p->dblW = 0;
        }
    }

    // ── Mono sum (default ON): collapse L+R -> 0.5*(L+R) so a MONO rig (pi-Stomp
    // into one amp) keeps ALL panned/stereo-widened content (Seraph engines,
    // choruses, wide delay/reverb) instead of losing whatever's on the unused jack.
    // Our width is pan/decorrelation (never anti-phase), so the sum can't null. ──
    if (p->ports[HF_OUT_MONO] && *p->ports[HF_OUT_MONO] > 0.5f)
        for (uint32_t i = 0; i < n; ++i) { const float mo = 0.5f*(outL[i]+outR[i]); outL[i]=mo; outR[i]=mo; }

    // ── Output Voice: FRFR de-close-mic EQ (2026-08-21) — post-mono-sum, pre-
    // Output. The IRs are close-mic recordings; through a flat speaker in a
    // real room that perspective reads boxy/hyped. Four levers (LAB knobs for
    // the FRFR-10 ears session, bake + retire after): steep low cut (10" + the
    // floor-coupling boost), 160 Hz proximity tame, ~4 kHz close-mic presence
    // dip, 8 kHz fizz tilt (a horn tweeter reproduces IR fizz too faithfully).
    if (fvOn) {
        const float cut  = p->ports[HF_FV_LOCUT] ? *p->ports[HF_FV_LOCUT] : 100.75f;
        const float prox = p->ports[HF_FV_PROX]  ? *p->ports[HF_FV_PROX]  : 1.815f;
        const float pres = p->ports[HF_FV_PRES]  ? *p->ports[HF_FV_PRES]  : 3.225f;
        const float fizz = p->ports[HF_FV_FIZZ]  ? *p->ports[HF_FV_FIZZ]  : 2.70f;
        if (cut != p->fvLastCut || prox != p->fvLastProx || pres != p->fvLastPres || fizz != p->fvLastFizz) {
            p->fvLastCut = cut; p->fvLastProx = prox; p->fvLastPres = pres; p->fvLastFizz = fizz;
            for (int c = 0; c < 2; ++c) {
                p->fvHP[c].setCoeffs(Filters::highpass(cut, 0.707, p->rate));
                p->fvProx[c].setCoeffs(Filters::peaking(160.0, -prox, 1.0, p->rate));
                p->fvPres[c].setCoeffs(Filters::peaking(4000.0, -pres, 1.2, p->rate));
                p->fvFizz[c].setCoeffs(Filters::highshelf(8000.0, -fizz, p->rate));
            }
        }
        for (uint32_t i = 0; i < n; ++i) {
            outL[i] = p->fvFizz[0].process(p->fvPres[0].process(p->fvProx[0].process(p->fvHP[0].process(outL[i]))));
            outR[i] = p->fvFizz[1].process(p->fvPres[1].process(p->fvProx[1].process(p->fvHP[1].process(outR[i]))));
        }
    }

    // ── Master output level (the "Output" stage — last in the chain) ──
    // The knob is in dB (0 dB = unity, up to +12 dB boost); convert to a linear
    // gain and apply it smoothed. Auto-Limit only adds the clip-safe limiter on
    // top — below the ceiling both modes sound identical.
    const float outGain = dbToGain(*p->ports[HF_OUT_LEVEL]);
    const bool  outLimit = *p->ports[HF_OUT_AUTO] > 0.5f;
    p->autoOut.process(outL, outR, n, outGain, outLimit);

    // ── Seamless-switch mute ramp: ~2.5 ms down, apply everything at zero, ~10 ms up ──
    if (p->swFadeState != 0) {
        const float stepOut = 1.0f / (0.0025f * (float)p->rate);
        const float stepIn  = 1.0f / (0.0100f * (float)p->rate);
        for (uint32_t i = 0; i < n; ++i) {
            if (p->swFadeState == 1) {
                p->swFadeGain -= stepOut;
                if (p->swFadeGain < 0.0f) p->swFadeGain = 0.0f;
            } else {
                p->swFadeGain += stepIn;
                if (p->swFadeGain >= 1.0f) { p->swFadeGain = 1.0f; p->swFadeState = 0; }
            }
            outL[i] *= p->swFadeGain; outR[i] *= p->swFadeGain;
        }
        if (p->swFadeState == 1 && p->swFadeGain <= 0.0f) {
            hfApplySwitch(p);
            p->swFadeState = 2;
        }
    }

    // Silence the output while tuning (mute engaged) or measuring the calibration
    // floors — both engines still read the dry input upstream of the chain.
    if (tunerMute || calMute) { std::memset(outL, 0, sizeof(float)*n); std::memset(outR, 0, sizeof(float)*n); }

    // ── Clip indicator: latch for ~250 ms whenever the output hits full scale ──
    float peak = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float a = std::fabs(outL[i]); if (a > peak) peak = a;
        a = std::fabs(outR[i]);       if (a > peak) peak = a;
    }
    if (peak >= 0.999f) p->clipHold = static_cast<int>(p->rate * 0.25);
    if (p->ports[HF_CLIP]) *p->ports[HF_CLIP] = (p->clipHold > 0) ? 1.0f : 0.0f;
    if (p->clipHold > 0)   p->clipHold -= static_cast<int>(n);

    // ── Input / output level meters: smoothed peak, mapped -60..0 dB -> 0..1 ──
    float ipk = 0.0f;
    if (inL && inR) for (uint32_t i = 0; i < n; ++i) {
        float a = std::fabs(inL[i]); if (a > ipk) ipk = a;
        a = std::fabs(inR[i]);       if (a > ipk) ipk = a;
    }
    p->meterIn  = std::fmax(p->meterIn  * 0.82f, ipk);
    p->meterOut = std::fmax(p->meterOut * 0.82f, peak);   // 'peak' = output peak from the clip scan
    auto mnorm = [](float pk) -> float {
        if (pk < 1.0e-4f) return 0.0f;
        float v = (20.0f * std::log10(pk) + 60.0f) / 60.0f;
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    };
    const float mIn  = mnorm(p->meterIn);
    const float mOut = mnorm(p->meterOut);
    if (p->ports[HF_IN_METER])  *p->ports[HF_IN_METER]  = mIn;
    if (p->ports[HF_OUT_METER]) *p->ports[HF_OUT_METER] = mOut;
    // Push to the custom modgui via the notify atom (~25 Hz) — output control ports
    // don't reliably reach the icon's change callback on MODEP, so reuse the preset
    // UI's atom channel.
    p->meterFrames += n;
    if (haveNotify && p->meterFrames >= (uint32_t)(p->rate / 14.0f)) {
        p->meterFrames = 0;
        // Deadband: only push when a bar moved >~1.5% — steady levels/silence cost nothing,
        // which keeps mod-ui's browser UI from churning on a constant atom stream.
        if (std::fabs(mIn - p->meterSentIn) > 0.015f || std::fabs(mOut - p->meterSentOut) > 0.015f) {
            p->meterSentIn = mIn; p->meterSentOut = mOut;
            char mbuf[24]; std::snprintf(mbuf, sizeof(mbuf), "%.3f|%.3f", mIn, mOut);
            forgeStringSet(p, p->uris.meters, mbuf);
        }
        // Strobe tuner readout → same atom channel (output ports don't reach the modgui).
        if (tunerOn) {
            char tbuf[24]; std::snprintf(tbuf, sizeof(tbuf), "%d|%.1f", p->tuner.note, p->tuner.cents);
            forgeStringSet(p, p->uris.tuner, tbuf);
        }
        // Calibration wizard: state|progress|floorDb|humFrac|touchDb|play10msDb|
        // recTrim|recFloor|err — pushed while running + ~5 s after completion so a
        // mid-run page reload resyncs (output ports alone don't reach the modgui).
        const bool calRunning = (p->calState >= 1 && p->calState <= 3);
        if (calRunning || p->calNotifyTicks > 0) {
            if (!calRunning) --p->calNotifyTicks;
            const float prog = (p->calSamplesTotal && p->calState >= 1 && p->calState <= 3)
                ? 1.0f - (float)p->calSamplesLeft / (float)p->calSamplesTotal : 0.0f;
            char cbuf[112];
            std::snprintf(cbuf, sizeof(cbuf), "%d|%.2f|%.1f|%.2f|%.1f|%.1f|%.1f|%.1f|%d",
                          p->calState, prog,
                          p->calPh[0].peakDb, p->calPh[0].humFrac,
                          p->calPh[1].peakDb, p->calPh[2].rms10msMaxDb,
                          p->calRec.trimOffsDb, p->calRec.floorOffsDb, p->calRec.error);
            forgeStringSet(p, p->uris.cal, cbuf);
        }
    }

    if (haveNotify) lv2_atom_forge_pop(&p->forge, &seqFrame);
}

