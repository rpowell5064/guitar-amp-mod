// ─────────────────────────────────────────────────────────────────────────────
// Hex Forge engine — hf_presets.inc (Stage A textual split, M0)
// Preset engine: backup store, migratePorts (+static_assert web), blob
// serialize/deserialize, recall/save/bank/move, factory preset tables.
// NOT a standalone TU: #included by hexforge_plugin.cpp at the exact spot
// this text was cut from. Do not add includes or include guards here.
// ─────────────────────────────────────────────────────────────────────────────
// ── Preset store backup file (survives delete/re-add + bundle updates) ─────────
// The preset store also lives in a single file OUTSIDE the plugin instance, so a
// fresh instance can recover all 32 presets after the plugin is deleted+re-added
// or the bundle is updated. Same self-describing layout as the State blob
// (version 3) but with raw absolute file paths (no LV2 mapPath outside State).
static std::string hfBackupDir() {
    return hfplat::configDir();
}
// ── UI notification payload builders (M2: shared by the LV2 forge emitters and
// the desktop webview bridge — one format, two hosts) ─────────────────────────
// "bank|slot|name0|name1|...|name127" — drives the UI bank indicator + name list.
static int hfIndexString(HexForge* p, char* buf, int cap) {
    int o = std::snprintf(buf, cap, "%d|%d", p->curBank, p->curSlot);
    for (int b = 0; b < kBanks; ++b)
        for (int s = 0; s < kSlots; ++s) {
            const Preset& pr = p->presets[b][s];
            const char* nm = (pr.used && pr.name[0]) ? pr.name : "";
            o += std::snprintf(buf + o, cap - o, "|%s", nm);
            if (o >= cap - 40) { b = kBanks; break; }
        }
    return o;
}
// "sym=val;sym=val;..." for every param port — the UI replays it via set_port_value.
static int hfApplyString(HexForge* p, char* buf, int cap) {
    int o = 0; buf[0] = '\0';
    for (int i = 0; i < HF_N_PORTS; ++i)
        if (isParamPort(i) && o < cap - 40)
            o += std::snprintf(buf + o, cap - o, "%s=%g;", HF_PORT_SYM[i], p->eff[i]);
    return o;
}
// Reconstruct an old positional value array (the npc old values sit at the start
// of vals[], rest zero) onto the CURRENT (v7) port layout, inserting defaults for
// ports that didn't exist in `srcVer`. Two insertion regions, both mid-enum:
//   * Input-Trim voicing/boost block, 5 contiguous ports [HF_IT_HUMBK..HF_IT_BOOSTAMT],
//     added incrementally: v4 humbk+hbamt, v5 hbmodel, v6 boost+boostamt.
//   * Delay Seraph block, 4 contiguous ports [HF_DL_PATTERN..HF_DL_MODRATE], added v7.
// Done as a single left-to-right walk over the new layout: gap slots get defaults,
// every other slot consumes the next old value (preserves order across both gaps).
static_assert(HF_IT_HBAMT == HF_IT_HUMBK + 1 && HF_IT_HBMODEL == HF_IT_HUMBK + 2 &&
              HF_IT_BOOST == HF_IT_HUMBK + 3 && HF_IT_BOOSTAMT == HF_IT_HUMBK + 4,
              "voicing/boost ports must be contiguous for the preset migration");
static_assert(HF_DL_DUCKING == HF_DL_PATTERN + 1 && HF_DL_MODDEPTH == HF_DL_PATTERN + 2 &&
              HF_DL_MODRATE == HF_DL_PATTERN + 3 && HF_DL_PATTERN > HF_IT_BOOSTAMT,
              "Seraph delay ports must be contiguous and after the IT block");
//   * Wah + Octave blocks, 13 contiguous ports [HF_WH_POS..HF_OC_DRY], added v8.
static_assert(HF_OC_DRY == HF_WH_POS + 12 && HF_WH_POS > HF_DL_MODRATE,
              "Wah+Octave ports must be contiguous and after the Delay block");
//   * Per-block bypass, 11 contiguous toggles [HF_GT_BYPASS..HF_OC_BYPASS], added v9.
static_assert(HF_OC_BYPASS == HF_GT_BYPASS + 10 && HF_GT_BYPASS > HF_OC_DRY && HF_OC_BYPASS < HF_SW_A,
              "bypass ports must be contiguous, after the param blocks and before the commands");
//   * Nail block, 8 contiguous ports [HF_NAIL_POS..HF_NAIL_BYPASS], added v12.
static_assert(HF_NAIL_BYPASS == HF_NAIL_POS + 7 && HF_NAIL_POS == HF_OC_BYPASS + 1 && HF_NAIL_BYPASS < HF_SW_A,
              "Nail ports must be contiguous, right after the bypass toggles and before the commands");
//   * Tempo-sync ports, 4 contiguous [HF_DL_SYNC..HF_MD_DIV], added v13.
static_assert(HF_MD_DIV == HF_DL_SYNC + 3 && HF_DL_SYNC == HF_NAIL_BYPASS + 1 && HF_MD_DIV < HF_SW_A,
              "tempo-sync ports must be contiguous, after the Nail block and before the commands");
//   * Octave microtonal shimmer, 2 contiguous [HF_OC_MICRO, HF_OC_INTERVAL], added v14.
static_assert(HF_OC_INTERVAL == HF_OC_MICRO + 1 && HF_OC_MICRO == HF_MD_DIV + 1 && HF_OC_INTERVAL < HF_SW_A,
              "octave shimmer ports must be contiguous, after tempo-sync and before the commands");
//   * Cali V (Mesa Mark V) mode selector [HF_AMP_MV_MODE] added v15, then the 5-band graphic EQ
//     [HF_AMP_MV_GEQ0..4] added v16 — all 6 contiguous, after the octave ports, before the commands.
static_assert(HF_AMP_MV_MODE == HF_OC_INTERVAL + 1 && HF_AMP_MV_GEQ0 == HF_AMP_MV_MODE + 1
              && HF_AMP_MV_GEQ4 == HF_AMP_MV_GEQ0 + 4 && HF_AMP_MV_EQPRESET == HF_AMP_MV_GEQ4 + 1,
              "amp_mv_mode + 5 graphic-EQ + eqpreset must be contiguous, before md_offset");
//   * Modulation Center Delay [HF_MD_OFFSET] added v18 — right after the Cali V EQ preset.
static_assert(HF_MD_OFFSET == HF_AMP_MV_EQPRESET + 1 && HF_AMP_NAM_GAIN == HF_MD_OFFSET + 1,
              "md_offset must sit right after the Cali V EQ preset, before the NAM trims");
//   * NAM input/output trims — 6 contiguous ports [HF_AMP_NAM_GAIN..HF_CAB_NAM_VOL] added v19;
//     then the Plexi Vol II (amp_pl_vol2) and the cab mic placement (cab_micpos/cab_micdist),
//     all append-only default-0 (2026-07-14), as the last preset params before the commands.
static_assert(HF_AMP_NAM_VOL == HF_AMP_NAM_GAIN + 1 && HF_DR_NAM_GAIN == HF_AMP_NAM_GAIN + 2
              && HF_DR_NAM_VOL == HF_AMP_NAM_GAIN + 3 && HF_CAB_NAM_GAIN == HF_AMP_NAM_GAIN + 4
              && HF_CAB_NAM_VOL == HF_AMP_NAM_GAIN + 5 && HF_AMP_PL_VOL2 == HF_CAB_NAM_VOL + 1
              && HF_CAB_MICPOS == HF_AMP_PL_VOL2 + 1 && HF_CAB_MICDIST == HF_CAB_MICPOS + 1
              && HF_CAB_ROOMON == HF_CAB_MICDIST + 1 && HF_CAB_ROOMMIX == HF_CAB_ROOMON + 1
              && HF_CAB_ROOMAMT == HF_CAB_ROOMMIX + 1,
              "NAM trims, Plexi Vol II, mic placement, then cab room must sit before the Recto block");
//   * Diamond Plate (Mesa Dual Rectifier) mode + Variac + rectifier — 3 contiguous ports
//     [HF_AMP_RC_MODE..HF_AMP_RC_RECT], added v20, as the last preset params before the commands.
static_assert(HF_AMP_RC_MODE == HF_CAB_ROOMAMT + 1 && HF_AMP_RC_VARIAC == HF_AMP_RC_MODE + 1
              && HF_AMP_RC_RECT == HF_AMP_RC_MODE + 2,
              "Recto ports must be contiguous after the cab room");
//   * Tremont 15 (PRS MT15) channel + bright — 2 contiguous ports [HF_AMP_MT_MODE,
//     HF_AMP_MT_BRIGHT], added v21, as the last preset params before the commands.
static_assert(HF_AMP_MT_MODE == HF_AMP_RC_RECT + 1 && HF_AMP_MT_BRIGHT == HF_AMP_MT_MODE + 1,
              "MT15 ports must be contiguous after the Recto block");
//   * Cab Voice + Output Doubler — 2 contiguous ports, added v22, last before the commands.
static_assert(HF_CAB_VOICE == HF_AMP_MT_BRIGHT + 1 && HF_OUT_DOUBLER == HF_CAB_VOICE + 1,
              "cab voice + doubler must be contiguous");
//   * EQ block — 11 contiguous ports [EQ_POS..EQ_BYPASS], added v23, last before the commands.
static_assert(HF_EQ_POS == HF_OUT_DOUBLER + 1 && HF_EQ_ENABLE == HF_EQ_POS + 1
              && HF_EQ_PRESET == HF_EQ_POS + 2 && HF_EQ_100 == HF_EQ_POS + 3
              && HF_EQ_LEVEL == HF_EQ_POS + 9 && HF_EQ_BYPASS == HF_EQ_POS + 10,
              "EQ block ports must be contiguous");
//   * Fidelity pair — pickup load + speaker coupling, added v24, last before the commands.
static_assert(HF_IT_LOAD == HF_EQ_BYPASS + 1 && HF_AMP_PAMP_COUPL == HF_IT_LOAD + 1,
              "fidelity ports must be contiguous");
static_assert(HF_RV_DENSITY == HF_AMP_PAMP_COUPL + 1, "reverb density after the fidelity pair");
//   * Reverb Type + Cab Room Density — added v26, last before the commands.
static_assert(HF_RV_TYPE == HF_RV_DENSITY + 1 && HF_CAB_ROOMDENSE == HF_RV_TYPE + 1,
              "v26 ports must be contiguous");
//   * Hex Ambient bloom — 1 port, added v27.
static_assert(HF_RV_BLOOM == HF_CAB_ROOMDENSE + 1,
              "v27 port must be contiguous");
//   * Cab Speaker Drive (item #40) — 1 port, added v28.
static_assert(HF_CAB_SPKDRIVE == HF_RV_BLOOM + 1,
              "v28 port must be contiguous");
//   * Tremolo Shape (roadmap #34) — 1 port, added v29.
static_assert(HF_MD_SHAPE == HF_CAB_SPKDRIVE + 1,
              "v29 port must be contiguous");
//   * Fuzz Guitar Vol (roadmap #45) — 1 port, added v30, last before the commands.
static_assert(HF_FZ_GVOL == HF_MD_SHAPE + 1 && HF_QUALITY == HF_FZ_GVOL + 1
              && HF_DR_ECO == HF_QUALITY + 1 && HF_DR2_POS == HF_DR_ECO + 1
              && HF_DR2_BYPASS == HF_DR2_POS + 9 && HF_RB_ENABLE == HF_DR2_BYPASS + 1
              && HF_RB_POL == HF_RB_ENABLE + 16 && HF_RB_RESONANCE == HF_RB_POL + 1
              && HF_RB_CABSPKDRIVE == HF_RB_RESONANCE + 44 && HF_RB_CAB2ON == HF_RB_CABSPKDRIVE + 1,
              "params: Drive 2, Rig B core, the 45-port parity family, Cab 2 presence (v37)");
//   * X2 second instances (v38): ten contiguous clone families end the param range.
static_assert(HF_GT2_POS == HF_RB_CAB2ON + 1 && HF_GT2_BYPASS == HF_GT2_POS + 7
              && HF_CP2_POS == HF_GT2_BYPASS + 1 && HF_FZ2_POS == HF_CP2_POS + 10
              && HF_NAIL2_POS == HF_FZ2_POS + 12 && HF_MD2_POS == HF_NAIL2_POS + 8
              && HF_DL2_POS == HF_MD2_POS + 12 && HF_RV2_POS == HF_DL2_POS + 17
              && HF_WH2_POS == HF_RV2_POS + 12 && HF_OC2_POS == HF_WH2_POS + 9
              && HF_EQ2_POS == HF_OC2_POS + 8 && HF_EQ2_BYPASS == HF_EQ2_POS + 10
              && HF_RB_NAM_GAIN == HF_EQ2_BYPASS + 1 && HF_RB_NAM_VOL == HF_RB_NAM_GAIN + 1,
              "v38 X2 clone families, then the v39 Amp 2 NAM trims");
//   * v40 quick wins: fuzz/nail Eco x4 + Drive 2 NAM trims end the param range.
static_assert(HF_FZ_ECO == HF_RB_NAM_VOL + 1 && HF_NAIL_ECO == HF_FZ_ECO + 1
              && HF_FZ2_ECO == HF_NAIL_ECO + 1 && HF_NAIL2_ECO == HF_FZ2_ECO + 1
              && HF_DR2_NAM_GAIN == HF_NAIL2_ECO + 1 && HF_DR2_NAM_VOL == HF_DR2_NAM_GAIN + 1,
              "v40: eco x4 + Drive 2 NAM trims");
//   * v41: Plexi Variac (brown sound), both amps, end the param range.
static_assert(HF_AMP_PL_VARIAC == HF_DR2_NAM_VOL + 1 && HF_RB_PL_VARIAC == HF_AMP_PL_VARIAC + 1,
              "v41: the Plexi Variac pair");
//   * v42: EP-3 Echo Age, both delay instances.
static_assert(HF_DL_AGE == HF_RB_PL_VARIAC + 1 && HF_DL2_AGE == HF_DL_AGE + 1,
              "v42: the EP-3 Age pair");
//   * v43: SIR #34 mod toggles, both amps.
static_assert(HF_AMP_SIR34 == HF_DL2_AGE + 1 && HF_RB_SIR34 == HF_AMP_SIR34 + 1,
              "v43: the SIR #34 pair");
//   * v44: rig-B blend low cut.
static_assert(HF_RB_LOCUT == HF_RB_SIR34 + 1,
              "v44: rb_locut follows the SIR pair");
//   * v45: Blue Liner (Ampeg SVT) Ultra-Lo/Ultra-Hi/Mid-Freq, A-side + Rig B —
//     the six-port tail; rb_sv_midfreq ends the param range.
static_assert(HF_AMP_SV_ULTRALO == HF_RB_LOCUT + 1 &&
              HF_AMP_SV_ULTRAHI == HF_AMP_SV_ULTRALO + 1 &&
              HF_AMP_SV_MIDFREQ == HF_AMP_SV_ULTRAHI + 1 &&
              HF_RB_SV_ULTRALO  == HF_AMP_SV_MIDFREQ + 1 &&
              HF_RB_SV_ULTRAHI  == HF_RB_SV_ULTRALO + 1 &&
              HF_RB_SV_MIDFREQ  == HF_RB_SV_ULTRAHI + 1 &&
              HF_RB_SV_MIDFREQ  == HF_SW_A - 1,
              "v45: the Blue Liner six-port tail ends the param range");
static void migratePorts(float* vals, uint32_t srcVer) noexcept {
    static const float vdef[5] = {0.0f, 1.0f, 0.0f, 0.0f, 4.0f};  // humbk,hbamt,hbmodel,boost,boostamt
    static const float ddef[4] = {1.0f, 0.0f, 0.0f, 0.3f};        // pattern,ducking,moddepth,modrate
    static const float wodef[13] = {10.0f, 0.0f, 0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f,   // Wah: pos,en,type,freq,depth,sens,q,mix
                                    11.0f, 0.0f, 0.0f, 0.5f, 1.0f};                     // Octave: pos,en,up,down,dry
    const int itExisting = (srcVer < 4) ? 0 : (srcVer == 4) ? 2 : (srcVer == 5) ? 3 : 5;
    const int itAt = HF_IT_HUMBK + itExisting, itEnd = HF_IT_HUMBK + 5;   // IT gap [itAt,itEnd)
    const bool dlGap = (srcVer < 7);
    const int dlAt = HF_DL_PATTERN, dlEnd = HF_DL_PATTERN + 4;            // DL gap [dlAt,dlEnd)
    const bool woGap = (srcVer < 8);
    const int woAt = HF_WH_POS, woEnd = HF_WH_POS + 13;                   // Wah+Octave gap [woAt,woEnd)
    // v9 inserted 11 per-block bypass toggles [HF_GT_BYPASS..HF_OC_BYPASS] BEFORE the command
    // ports, so a v8 blob's command/status values shift up by 11; the new bypass slots default
    // to 0 (active). This is a positional insert (not a trailing append) — the gap is required.
    // The bypass group is a FIXED 11 toggles (gt..oc); the Nail block (v12) has its OWN bypass in
    // its own group, so this gap stays 11 even though B_COUNT is now 12 (don't use B_COUNT here).
    const bool byGap = (srcVer < 9);
    const int byAt = HF_GT_BYPASS, byEnd = HF_OC_BYPASS + 1;              // 11 bypass ports [byAt,byEnd), default 0
    // v12 inserted the Nail block — 8 contiguous ports [HF_NAIL_POS..HF_NAIL_BYPASS], right after
    // the bypass toggles and before the command ports; a pre-v12 blob's command/status values shift
    // up by 8 and the new nail slots default to OFF at slot 12.
    static const float naildef[8] = {12.0f, 0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f}; // pos,en,mode,drive,tone,texture,level,byp
    const bool nailGap = (srcVer < 12);
    const int nailAt = HF_NAIL_POS, nailEnd = HF_NAIL_POS + 8;            // Nail gap [nailAt,nailEnd)
    // v13 inserted 4 tempo-sync ports [HF_DL_SYNC..HF_MD_DIV] before the commands; defaults =
    // sync OFF, Delay div 1/8 (5), Mod div 1/4 (2).
    static const float syncdef[4] = {0.0f, 5.0f, 0.0f, 2.0f};             // dl_sync,dl_div,md_sync,md_div
    const bool syncGap = (srcVer < 13);
    const int syncAt = HF_DL_SYNC, syncEnd = HF_DL_SYNC + 4;              // sync gap [syncAt,syncEnd)
    // v14 inserted the Octave microtonal shimmer [HF_OC_MICRO, HF_OC_INTERVAL] before the
    // commands; defaults = shimmer OFF (0), interval 0 (quarter-tone up).
    static const float ocdef[2] = {0.0f, 0.0f};                          // oc_micro, oc_interval
    const bool ocGap = (srcVer < 14);
    const int ocAt = HF_OC_MICRO, ocEnd = HF_OC_MICRO + 2;               // oc gap [ocAt,ocEnd)
    // v15 inserted the Cali V (Mesa Mark V) mode selector [HF_AMP_MV_MODE] before the commands;
    // default 6 (Mark IIC+, the hero lead mode).
    const bool mvGap = (srcVer < 15);
    const int mvAt = HF_AMP_MV_MODE, mvEnd = HF_AMP_MV_MODE + 1;         // mode gap [mvAt,mvEnd)
    // v16 inserted the Cali V 5-band graphic EQ [HF_AMP_MV_GEQ0..4]; default 0.5 (flat) each.
    const bool gvGap = (srcVer < 16);
    const int gvAt = HF_AMP_MV_GEQ0, gvEnd = HF_AMP_MV_GEQ0 + 5;         // geq gap [gvAt,gvEnd)
    // v17 inserted the Cali V graphic-EQ preset selector [HF_AMP_MV_EQPRESET]; default 0 (Custom).
    const bool eqGap = (srcVer < 17);
    const int eqAt = HF_AMP_MV_EQPRESET, eqEnd = HF_AMP_MV_EQPRESET + 1;
    // v18 appended the Modulation Center Delay [HF_MD_OFFSET] before the commands; default 0 ms
    // (= stock chorus/flanger/small-clone voicing).
    const bool mdoGap = (srcVer < 18);
    const int mdoAt = HF_MD_OFFSET, mdoEnd = HF_MD_OFFSET + 1;
    // v19 appended 6 NAM input/output trims [HF_AMP_NAM_GAIN..HF_CAB_NAM_VOL] before the commands;
    // default 0 dB (unity) for each.
    const bool namGap = (srcVer < 19);
    const int namAt = HF_AMP_NAM_GAIN, namEnd = HF_AMP_NAM_GAIN + 6;
    // v20 appended the Diamond Plate (Dual Rectifier) ports [HF_AMP_RC_MODE..HF_AMP_RC_RECT];
    // defaults = CH3 Modern (7), Bold (0), Silicon (0) — plain zero-fill would recall CH1 Clean.
    static const float rcdef[3] = {7.0f, 0.0f, 0.0f};
    const bool rcGap = (srcVer < 20);
    const int rcAt = HF_AMP_RC_MODE, rcEnd = HF_AMP_RC_MODE + 3;
    // v21 appended the Tremont 15 (PRS MT15) ports [HF_AMP_MT_MODE, HF_AMP_MT_BRIGHT];
    // defaults = Lead (2), bright off — plain zero-fill would recall the Clean channel.
    static const float mtdef[2] = {2.0f, 0.0f};
    const bool mtGap = (srcVer < 21);
    const int mtAt = HF_AMP_MT_MODE, mtEnd = HF_AMP_MT_MODE + 2;
    // v22 appended cab voice + output doubler; defaults Room (0) / off (0).
    const bool cvGap = (srcVer < 22);
    const int cvAt = HF_CAB_VOICE, cvEnd = HF_CAB_VOICE + 2;
    // v23 appended the EQ block group [EQ_POS..EQ_BYPASS]: pos 6 (after the cab),
    // palette (enable 0), Manual preset, all bands/level 0 dB, active (bypass 0).
    static const float eqbdef[11] = {6.0f, 0,0,0,0,0,0,0,0,0,0};
    const bool eqbGap = (srcVer < 23);
    const int eqbAt = HF_EQ_POS, eqbEnd = HF_EQ_POS + 11;
    // v24 appended pickup load + speaker coupling; both default 0 (off).
    const bool fidGap = (srcVer < 24);
    const int fidAt = HF_IT_LOAD, fidEnd = HF_IT_LOAD + 2;
    // v25 appended reverb density; default 0 (Classic tank).
    const bool rdGap = (srcVer < 25);
    const int rdAt = HF_RV_DENSITY;
    // v26 appended reverb type + cab room density; both default 0.
    const bool rtGap = (srcVer < 26);
    const int rtAt = HF_RV_TYPE, rtEnd = HF_RV_TYPE + 2;
    // v27 appended the Hex Ambient bloom; default 0.5 (inert unless type = Ambient).
    const bool blGap = (srcVer < 27);
    const int blAt = HF_RV_BLOOM;
    // v28 appended Cab Speaker Drive (item #40); default 0 (Off).
    const bool spkGap = (srcVer < 28);
    const int spkAt = HF_CAB_SPKDRIVE;
    // v29 appended Tremolo Shape (roadmap #34); default 0 (Bias = shipped voicing).
    const bool shpGap = (srcVer < 29);
    const int shpAt = HF_MD_SHAPE;
    // v30 appended Fuzz Guitar Vol (roadmap #45); default 1.0 (full = bit-identical).
    const bool gvGap2 = (srcVer < 30);
    const int gvAt2 = HF_FZ_GVOL;
    // v31 inserted 14 CPU-meter OUTPUT ports [HF_CPU_GT..HF_CPU_TOTAL] before HF_MIDI_IN
    // (2026-07-30). Outputs are meaningless in blobs; zero-fill.
    const bool cpuGap = (srcVer < 31);
    const int cpuAt = HF_CPU_GT, cpuEnd = HF_CPU_GT + 14;
    // v32 appended Engine Quality (Eco oversampling switch); default 0 = Standard.
    const bool qGap = (srcVer < 32);
    const int qAt = HF_QUALITY;
    // v33 appended Drive Eco; default 0 = Standard.
    const bool deGap = (srcVer < 33);
    const int deAt = HF_DR_ECO;
    // v34 appended the Drive B family (10 ports: pos..bypass) + its CPU meter
    // (after cpu_total). Defaults: parked at slot 14, disabled, Green Man, knobs
    // at noon-ish, mix full.
    static const float dr2def[10] = {14.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f};
    const bool dr2Gap = (srcVer < 34);
    const int dr2At = HF_DR2_POS, dr2End = HF_DR2_POS + 10;
    // v35 appended the Rig B family (17 ports: enable, amp core, cab, blend) +
    // its CPU meter. Disabled by default; knobs at their port defaults.
    static const float rbdef[17] = {0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.7f, 0.3f, 0.0f, 0.0f,
                                    0.0f, 80.0f, 16000.0f, 0.5f, 0.0f, 0.0f};
    const bool rbGap = (srcVer < 35);
    const int rbAt = HF_RB_ENABLE, rbEnd = HF_RB_ENABLE + 17;
    // v36 appended the 45-port Rig B parity family (amp extras + PA + cab extras);
    // defaults cloned from the A-side ports.
    static const float rb2def[45] = {0,0.5f,0,0.5f,0.5f,0.5f,0,0, 1,0,0,0, 6,0.5f,0.5f,0.5f,0.5f,0.5f,0,
                                     7,0,0, 2,0, 0, 0,1,1,0.55f,0.18f,0.33f,0.62f,0.42f,0.5f,0,0,
                                     1,0,0,1,0.12f,0.35f,0,0,0};
    const bool rb2Gap = (srcVer < 36);
    const int rb2At = HF_RB_RESONANCE, rb2End = HF_RB_RESONANCE + 45;
    // v37 appended Cab 2 presence (rb_cab2on); default 0 (Cab 2 out of the chain).
    // ARCHAEOLOGY (2026-07-30): this gap was missed when the port landed, so the
    // deployed build stamped 318-param blobs as v36 — the loaders disambiguate by
    // the saved param count (np >= 318 -> treated as v37) before calling this.
    const bool c2Gap = (srcVer < 37);
    const int c2At = HF_RB_CAB2ON;
    // v38 inserted the ten X2 clone families [HF_GT2_POS..HF_EQ2_BYPASS] (107
    // ports) + their 10 CPU meters at the tail. Defaults: parked slots 15..24,
    // OUT of the chain (enable 0), params at their port defaults, active.
    static const float x2def[107] = {
        15.0f, 0.0f, -60.0f, 2.0f, 120.0f, 250.0f, 8.0f, 0.0f,   // gt2
        16.0f, 0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f, 3.0f, 0.0f, 0.0f,   // cp2
        17.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f, 0.65f, 0.5f, 0.5f, 0.4f, 1.0f, 0.0f,   // fz2
        18.0f, 0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f,   // nail2
        19.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,   // md2
        20.0f, 0.0f, 0.0f, 250.0f, 0.4f, 0.15f, 0.5f, 0.003f, 0.001f, 10.0f, 1.0f, 0.0f, 0.0f, 0.3f, 0.0f, 5.0f, 0.0f,   // dl2
        21.0f, 0.0f, 10.0f, 1.5f, 0.3f, 0.0f, 0.8f, 0.15f, 0.0f, 0.0f, 0.5f, 0.0f,   // rv2
        22.0f, 0.0f, 0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f, 0.0f,   // wh2
        23.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,   // oc2
        24.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,   // eq2
    };
    const bool x2Gap = (srcVer < 38);
    const int x2At = HF_GT2_POS, x2End = HF_GT2_POS + 107;
    // v39 appended the Amp 2 NAM Gain/Level trims; default 0 dB each.
    const bool rnGap = (srcVer < 39);
    const int rnAt = HF_RB_NAM_GAIN, rnEnd = HF_RB_NAM_GAIN + 2;
    // v40 appended fuzz/nail Eco x4 + Drive 2 NAM trims; all default 0.
    const bool qwGap = (srcVer < 40);
    const int qwAt = HF_FZ_ECO, qwEnd = HF_FZ_ECO + 6;
    // v41 appended the Plexi Variac pair; default 0 (stock wall voltage).
    const bool vrGap = (srcVer < 41);
    const int vrAt = HF_AMP_PL_VARIAC, vrEnd = HF_AMP_PL_VARIAC + 2;
    // v42 appended the EP-3 Echo Age pair; default 0.35 (NON-zero -- a lightly
    // played deck, matching the port default so the knob recalls where it sits).
    const bool agGap = (srcVer < 42);
    const int agAt = HF_DL_AGE, agEnd = HF_DL_AGE + 2;
    // v43 appended the SIR #34 mod toggles; default 0 (stock JCM800).
    const bool smGap = (srcVer < 43);
    const int smAt = HF_AMP_SIR34, smEnd = HF_AMP_SIR34 + 2;
    // v44 appended the rig-B blend low cut; default 0 = off (bit-identical).
    const bool rlGap = (srcVer < 44);
    const int rlAt = HF_RB_LOCUT;
    // v45 appended the Blue Liner (Ampeg SVT) six-port tail [HF_AMP_SV_ULTRALO..
    // HF_RB_SV_MIDFREQ]; Ultra switches off, Mid-Freq 1 (800 Hz) — plain
    // zero-fill would recall the 220 Hz mid position on BOTH rigs.
    static const float svdef[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    const bool svGap = (srcVer < 45);
    const int svAt = HF_AMP_SV_ULTRALO, svEnd = HF_AMP_SV_ULTRALO + 6;

    // v47 (2026-09-05): out_phase appended at the tail. Gap-fills to 1.0, NOT 0 —
    // output invert is ON by default (real amps invert; the reference rig ships inverted),
    // so an old blob must come back inverted too or the same preset would sum
    // differently before and after the upgrade.
    const bool opGap = (srcVer < 47);
    const int  opAt  = HF_OUT_PHASE;
    float old[HF_N_PORTS];
    std::memcpy(old, vals, sizeof(old));   // snapshot (old values at front, tail zero)
    int o = 0;
    for (int i = 0; i < HF_N_PORTS; ++i) {
        if      (i >= itAt && i < itEnd)                 vals[i] = vdef[i - HF_IT_HUMBK];
        else if (dlGap && i >= dlAt && i < dlEnd)        vals[i] = ddef[i - dlAt];
        else if (woGap && i >= woAt && i < woEnd)        vals[i] = wodef[i - woAt];
        else if (byGap && i >= byAt && i < byEnd)        vals[i] = 0.0f;       // new bypass = active
        else if (nailGap && i >= nailAt && i < nailEnd)  vals[i] = naildef[i - nailAt];
        else if (syncGap && i >= syncAt && i < syncEnd)  vals[i] = syncdef[i - syncAt];
        else if (ocGap && i >= ocAt && i < ocEnd)        vals[i] = ocdef[i - ocAt];
        else if (mvGap && i >= mvAt && i < mvEnd)        vals[i] = 6.0f;   // Mesa mode default = Mark IIC+
        else if (gvGap && i >= gvAt && i < gvEnd)        vals[i] = 0.5f;   // Mesa graphic-EQ band = flat
        else if (eqGap && i >= eqAt && i < eqEnd)        vals[i] = 0.0f;   // Mesa EQ preset = Custom
        else if (mdoGap && i >= mdoAt && i < mdoEnd)     vals[i] = 0.0f;   // Mod Center Delay = 0 ms
        else if (namGap && i >= namAt && i < namEnd)     vals[i] = 0.0f;   // NAM gain/level trims = 0 dB
        else if (rcGap && i >= rcAt && i < rcEnd)        vals[i] = rcdef[i - rcAt];  // Recto: CH3 Modern/Bold/Silicon
        else if (mtGap && i >= mtAt && i < mtEnd)        vals[i] = mtdef[i - mtAt];  // MT15: Lead/bright off
        else if (cvGap && i >= cvAt && i < cvEnd)        vals[i] = 0.0f;             // cab voice Room / doubler off
        else if (eqbGap && i >= eqbAt && i < eqbEnd)     vals[i] = eqbdef[i - eqbAt];  // EQ: palette, flat
        else if (fidGap && i >= fidAt && i < fidEnd)     vals[i] = 0.0f;             // pickup load / coupling off
        else if (rdGap && i == rdAt)                     vals[i] = 0.0f;             // reverb density Classic
        else if (rtGap && i >= rtAt && i < rtEnd)        vals[i] = 0.0f;             // plate / classic room
        else if (blGap && i == blAt)                     vals[i] = 0.5f;             // ambient bloom (inert on plate/spring)
        else if (spkGap && i == spkAt)                   vals[i] = 0.0f;             // speaker drive Off
        else if (shpGap && i == shpAt)                   vals[i] = 0.0f;             // tremolo shape Bias
        else if (gvGap2 && i == gvAt2)                   vals[i] = 1.0f;             // fuzz guitar vol FULL
        else if (cpuGap && i >= cpuAt && i < cpuEnd)     vals[i] = 0.0f;
        else if (qGap && i == qAt)                       vals[i] = 0.0f;             // quality Standard
        else if (deGap && i == deAt)                     vals[i] = 0.0f;             // drive eco Standard
        else if (dr2Gap && i >= dr2At && i < dr2End)     vals[i] = dr2def[i - dr2At]; // Drive B parked
        else if (dr2Gap && i == HF_CPU_DR2)              vals[i] = 0.0f;              // Drive B meter
        else if (rbGap && i >= rbAt && i < rbEnd)        vals[i] = rbdef[i - rbAt];   // Rig B off
        else if (rbGap && i == HF_CPU_RIGB)              vals[i] = 0.0f;              // Rig B meter
        else if (rb2Gap && i >= rb2At && i < rb2End)     vals[i] = rb2def[i - rb2At]; // Rig B parity
        else if (c2Gap && i == c2At)                     vals[i] = 0.0f;              // Cab 2 out of the chain
        else if (x2Gap && i >= x2At && i < x2End)        vals[i] = x2def[i - x2At];   // X2 clones parked
        else if (x2Gap && i >= HF_CPU_GT2 && i <= HF_CPU_EQ2) vals[i] = 0.0f;         // X2 meters (outputs)
        else if (rnGap && i >= rnAt && i < rnEnd)        vals[i] = 0.0f;              // Amp 2 NAM trims 0 dB
        else if (qwGap && i >= qwAt && i < qwEnd)        vals[i] = 0.0f;              // v40 eco/trims
        else if (vrGap && i >= vrAt && i < vrEnd)        vals[i] = 0.0f;              // v41 variac stock
        else if (agGap && i >= agAt && i < agEnd)        vals[i] = 0.35f;             // v42 EP-3 age
        else if (smGap && i >= smAt && i < smEnd)        vals[i] = 0.0f;              // v43 SIR #34 stock
        else if (rlGap && i == rlAt)                     vals[i] = 0.0f;              // v44 rig-B low cut off
        else if (svGap && i >= svAt && i < svEnd)        vals[i] = svdef[i - svAt];   // v45 Blue Liner: Ultras off, mid 800 Hz
        else if (opGap && i == opAt)                     vals[i] = 1.0f;             // v47 output phase INVERTED (matches real amps / the reference rig)
        else                                             vals[i] = old[o++];
    }
}
static void seedFactoryPresets(HexForge* p);   // fwd decl (defined after the factory arrays)
// Layout version the generated hexforge_factory_presets.h rows were captured at.
// MUST be bumped when gen_hexforge_presets.py regenerates the table on a newer
// layout. 30 = the fz_gvol-era 224-port layout.
static constexpr uint32_t kFactoryTableVer = 30;
static void hfSerialize(HexForge* p, std::vector<uint8_t>& blob) {
    auto putBytes = [&](const void* d, size_t n){ const uint8_t* b=(const uint8_t*)d; blob.insert(blob.end(), b, b+n); };
    auto putU32   = [&](uint32_t v){ putBytes(&v, 4); };
    auto putPath  = [&](const char* s){ uint32_t len=(uint32_t)std::strlen(s); putU32(len); putBytes(s, len); };
    putU32(47); putU32(kBanks); putU32(kSlots); putU32(HF_N_PORTS); putU32(kFactoryRev);   // v46: Cali Ch3 value transform (no layout change); v45: + the Blue Liner (SVT) six-port tail; v44: + rb_locut; v43: + SIR #34 pair; v42: + EP-3 Age pair; v41: + Plexi Variac; v40: + eco x4 + Drive 2 NAM; v39: + Amp 2 NAM + Cab 2 IR paths; v38: + X2 clone families; v37: + Cab 2 presence; v31: + 14 CPU meter outputs (tail, pre-MIDI); v30: + fuzz guitar vol; v29: + tremolo shape; v28: + speaker drive; v27: + ambient bloom; v26: + reverb type / room density; v25: + reverb density
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


}
static bool hfDeserialize(HexForge* p, const uint8_t* d, size_t size) {
    size_t off = 0;
    auto getU32  = [&](uint32_t& v)->bool { if (off+4>size) return false; std::memcpy(&v, d+off, 4); off+=4; return true; };
    auto getPath = [&](char* dst){ uint32_t len=0; dst[0]='\0';
        if (!getU32(len) || off+len>size) { off=size; return; }
        uint32_t m = len < kPathMax-1 ? len : kPathMax-1;
        std::memcpy(dst, d+off, m); dst[m]='\0'; off += len; };
    uint32_t ver=0, nb=0, ns=0, np=0;
    if (!getU32(ver)) return false; getU32(nb); getU32(ns);
    if (ver < 2 || ver > 47) return false;   // 47 = out_phase tail port
    const bool migrateOutDb = (ver == 2);
    const bool needMigrate  = (ver < 47);  // ...v47 out_phase tail port
    getU32(np);
    if (ver == 36 && np >= 318) ver = 37;   // deployed v36 stamps already carry rb_cab2on (318-param layout)
    uint32_t factoryRev = 0; if (ver >= 11) getU32(factoryRev);   // v11+: factory-preset revision
    const uint32_t npc = np < (uint32_t)HF_N_PORTS ? np : (uint32_t)HF_N_PORTS;
    for (uint32_t b=0;b<nb;++b) for (uint32_t s=0;s<ns;++s) {
        uint32_t used=0; getU32(used);
        char name[32] = {0}; if (off+sizeof(name)<=size){ std::memcpy(name,d+off,sizeof(name)); off+=sizeof(name); }
        float vals[HF_N_PORTS]; for (int i=0;i<HF_N_PORTS;++i) vals[i]=0.0f;
        if (off + (size_t)np*4 <= size) { std::memcpy(vals, d+off, (size_t)npc*4); off += (size_t)np*4; }
        else off = size;
        if (needMigrate) migratePorts(vals, ver);   // insert voicing/boost + Seraph ports (defaults)
        if (ver < 46) {   // v46: Cali V Ch3 knob-law VALUE transform (sound-preserving; see hexforge_plugin.cpp)
            auto invG = [](float g) { return std::min(1.0f, std::min(g / 0.6f, g + 0.1f)); };
            auto invT = [](float t) { return std::min(1.0f, t <= 0.12f ? t / 0.24f : 0.5f + (t - 0.12f) / 0.8f); };
            if (static_cast<int>(vals[HF_AMP_MODEL] + 0.5f) == 11 && vals[HF_AMP_MV_MODE] >= 5.5f) {
                vals[HF_AMP_GAIN] = invG(vals[HF_AMP_GAIN]); vals[HF_AMP_TREBLE] = invT(vals[HF_AMP_TREBLE]);
            }
            if (static_cast<int>(vals[HF_RB_AMP] + 0.5f) == 11 && vals[HF_RB_MV_MODE] >= 5.5f) {
                vals[HF_RB_GAIN] = invG(vals[HF_RB_GAIN]); vals[HF_RB_TREBLE] = invT(vals[HF_RB_TREBLE]);
            }
        }
        if (migrateOutDb) vals[HF_OUT_LEVEL] = linToDb(vals[HF_OUT_LEVEL]);
        if (ver < 10) vals[HF_OUT_MONO] = 1.0f;   // pre-v10 saves default to MONO
        char ir[kPathMax],an[kPathMax],dn[kPathMax],cn[kPathMax],a2[kPathMax],i2[kPathMax],d2n[kPathMax];
        getPath(ir); getPath(an); getPath(dn); getPath(cn);
        a2[0] = i2[0] = d2n[0] = '\0';
        if (ver >= 39) { getPath(a2); getPath(i2); }   // v39: Amp 2 NAM + Cab 2 IR
        if (ver >= 40) getPath(d2n);                   // v40: Drive 2 NAM
        if (b<kBanks && s<kSlots) {
            Preset& pr = p->presets[b][s];
            // Don't let an empty backup slot wipe a factory-seeded preset: this is how
            // new factory presets (e.g. the Banks 2..6 band/song set) reach users whose
            // saved store predates them. A user's own saved preset (used=1) still wins.
            if (used == 0 && pr.used) continue;
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
    return true;
}
// Write the store to the backup file (atomic: tmp + rename). Called on every
// save/rename/move and on board save — light, user-triggered file I/O.
static void hfWriteBackup(HexForge* p) {
    std::string dir = hfBackupDir();
    hfplat::ensureConfigDir();
    const std::string path = dir + "/hexforge-presets.dat";
    const std::string tmp  = path + ".tmp";
    std::vector<uint8_t> blob; hfSerialize(p, blob);
    FILE* f = hfplat::fopenBinary(tmp.c_str(), "wb");
    if (!f) return;
    std::fwrite(blob.data(), 1, blob.size(), f);
    std::fclose(f);
    std::rename(tmp.c_str(), path.c_str());
}
// Load the store from the backup file into presets[]. Returns true on success.
static bool hfLoadBackup(HexForge* p) {
    const std::string path = hfBackupDir() + "/hexforge-presets.dat";
    FILE* f = hfplat::fopenBinary(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return false; }
    std::vector<uint8_t> blob(static_cast<size_t>(sz));
    size_t rd = std::fread(blob.data(), 1, static_cast<size_t>(sz), f);
    std::fclose(f);
    if (rd != static_cast<size_t>(sz)) return false;
    return hfDeserialize(p, blob.data(), blob.size());
}

// Schedule a worker (re)load for a file slot whose path changed (empty path =
// clear: NAM loads an unloaded model the DSP ignores; IR resets to the default).
static void schedPath(HexForge* p, char* cur, const char* want, WorkType wt, int namSlot) {
    if (std::strcmp(cur, want) == 0) return;
    std::strncpy(cur, want, kPathMax - 1); cur[kPathMax - 1] = '\0';
    WorkMsg m; m.type = wt; m.namSlot = namSlot;
    std::strncpy(m.path, want, kPathMax - 1); m.path[kPathMax - 1] = '\0';
    p->worker->schedule(&m, sizeof(m));
}
// Recall: move the active position to (bank,slot). If the slot holds a preset,
// load its params into eff[] and (re)load its files; if empty, just move the
// cursor (sound unchanged) so the UI can Save the current sound into it.
static void psRecall(HexForge* p, int bank, int slot) {
    p->curBank = bank & (kBanks - 1);
    p->curSlot = slot & (kSlots - 1);
    const Preset& pr = p->presets[p->curBank][p->curSlot];
    p->host->statusDump();
    if (pr.used) {
        for (int i = 0; i < HF_N_PORTS; ++i) if (isParamPort(i)) {
            p->eff[i] = pr.vals[i];
            p->lastPort[i] = p->hostPorts[i] ? *p->hostPorts[i] : pr.vals[i];
        }
        schedPath(p, p->irPath,     pr.irPath,     W_CAB_IR,   0);
        schedPath(p, p->ampNamPath, pr.ampNamPath, W_NAM_LOAD, 0);
        schedPath(p, p->drNamPath,  pr.drNamPath,  W_NAM_LOAD, 1);
        schedPath(p, p->cabNamPath, pr.cabNamPath, W_NAM_LOAD, 2);
        schedPath(p, p->amp2NamPath, pr.amp2NamPath, W_NAM_LOAD, 3);
        schedPath(p, p->dr2NamPath, pr.dr2NamPath, W_NAM_LOAD, 4);
        if (std::strcmp(p->ir2Path, pr.ir2Path) != 0) {   // Cab 2 IR: user file or back to the rb_cab built-in
            std::strncpy(p->ir2Path, pr.ir2Path, kPathMax-1); p->ir2Path[kPathMax-1]='\0';
            p->lastCab2Model = -1;                        // forces the run() logic to (re)load next block
        }
        if (p->uiNotify) {   // best-effort UI sync; headless recall still changes sound
            p->host->emitApply();
            p->host->fileSet(HFP_IR_FILE, p->irPath);
            p->host->fileSet(HFP_AMP_NAM, p->ampNamPath);
            p->host->fileSet(HFP_DR_NAM,  p->drNamPath);
            p->host->fileSet(HFP_CAB_NAM, p->cabNamPath);
            p->host->fileSet(HFP_AMP2_NAM, p->amp2NamPath);
            p->host->fileSet(HFP_IR2_FILE, p->ir2Path);
            p->host->fileSet(HFP_DR2_NAM, p->dr2NamPath);
        }
    }
    p->host->emitIndex();
}
// Save: overwrite the active slot with the live (edited) settings + current files.
static void psSave(HexForge* p) {
    Preset& pr = p->presets[p->curBank][p->curSlot];
    for (int i = 0; i < HF_N_PORTS; ++i) if (isParamPort(i)) pr.vals[i] = p->eff[i];
    std::strncpy(pr.irPath,     p->irPath,     kPathMax - 1); pr.irPath[kPathMax - 1] = '\0';
    std::strncpy(pr.ir2Path,    p->ir2Path,    kPathMax - 1); pr.ir2Path[kPathMax - 1] = '\0';
    std::strncpy(pr.amp2NamPath, p->amp2NamPath, kPathMax - 1); pr.amp2NamPath[kPathMax - 1] = '\0';
    std::strncpy(pr.dr2NamPath, p->dr2NamPath, kPathMax - 1); pr.dr2NamPath[kPathMax - 1] = '\0';
    std::strncpy(pr.ampNamPath, p->ampNamPath, kPathMax - 1); pr.ampNamPath[kPathMax - 1] = '\0';
    std::strncpy(pr.drNamPath,  p->drNamPath,  kPathMax - 1); pr.drNamPath[kPathMax - 1] = '\0';
    std::strncpy(pr.cabNamPath, p->cabNamPath, kPathMax - 1); pr.cabNamPath[kPathMax - 1] = '\0';
    if (pr.name[0] == '\0')
        std::snprintf(pr.name, sizeof(pr.name), "%c%d", 'A' + p->curSlot, p->curBank + 1);
    pr.used = true;
    p->host->emitIndex();
    p->host->statusDump();
    hfWriteBackup(p);   // keep the off-instance backup current
}
// RT trigger: the actual recall runs at the mute-ramp's zero point (seamless).
static void psRecallRequest(HexForge* p, int bank, int slot) {
    p->swPendBank = bank; p->swPendSlot = slot; p->swPendRecall = true;
    if (p->swFadeState == 0) p->swFadeState = 1;
}
static void psBankDelta(HexForge* p, int d) { psRecallRequest(p, p->curBank + d, p->curSlot); }
// A footswitch tap. Single tap recalls that slot immediately (no delay).
// Double-tapping A or D within ~0.4 s navigates banks — A = down, D = up — and
// lands on that same slot letter in the new bank.
static void psSwitchPress(HexForge* p, int sw) {
    const int64_t now = p->sampleClock;
    const int64_t win = static_cast<int64_t>(p->rate * 0.4);
    const bool dbl = (now - p->lastTapSample[sw]) < win;
    p->lastTapSample[sw] = now;
    if      (sw == 0 && dbl) psRecallRequest(p, p->curBank - 1, 0);   // double-tap A → bank down, slot A
    else if (sw == 3 && dbl) psRecallRequest(p, p->curBank + 1, 3);   // double-tap D → bank up, slot D
    else                     psRecallRequest(p, p->curBank, sw);      // single tap → recall this slot
}
// Move the active preset earlier/later across the flat 32-slot order (= "sort").
// The cursor follows the moved preset; no audio change (same preset content).
static void psMoveDelta(HexForge* p, int d) {
    int flat = p->curBank * kSlots + p->curSlot, tgt = flat + d;
    if (tgt < 0 || tgt >= kBanks * kSlots) return;
    Preset tmp = p->presets[p->curBank][p->curSlot];
    p->presets[p->curBank][p->curSlot] = p->presets[tgt / kSlots][tgt % kSlots];
    p->presets[tgt / kSlots][tgt % kSlots] = tmp;
    p->curBank = tgt / kSlots; p->curSlot = tgt % kSlots;
    p->host->emitIndex();
    hfWriteBackup(p);
}

// ── Factory presets (Bank 1 / A–D) ────────────────────────────────────────────
// A fresh Hex Forge instance starts with Bank 1 pre-filled with these four
// sounds (Clean / Crunch / Rhythm / Lead) so it's a usable rig the moment it
// loads — the same starting point as the factory pedalboard. Values were
// captured from the dialed-in rig; the IR slot is left empty so they use the
// built-in Factory Cab (no licensed IR file is referenced). hf_restore still
// overwrites these from saved State, so a user's own presets always win.
static const char* const kFactoryName[kSlots] = { "Clean", "Crunch", "Rhythm", "Lead" };
static const float kFactoryVals[kSlots][HF_N_PORTS] = {
// Clean  (out_level -24.0 dB: user's hand-dialed master Output, 2026-06-29)
{ 0, 0, 0, 0, 0, 0, 0, -23.5, 0, 1, 0, 0, 1, 1, 0, -60, 5, 50, 100, 6, 2, 0, 0, -20, 1, 5, 5, 3, 0, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 1, 0, 0.19, 0.5, 0.58, 1, 0.3, 5, 1, 0, 0.5775, 0.615, 0.635, 0.605, 0.5, 0.7525, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 16000, 1, 7, 0, 0, 0.5, 0.5, 0.5, 0.5, 8, 1, 2, 250, 0.21315, 0.3, 0.5, 0.003, 0.001, 3, 9, 1, 10, 1.5, 0.3, 0, 0.01, 0.335, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
// Crunch
{ 0, 0, 0, 0, 0, 0, 0, -20.2, 0, 1, 0, 0, 1, 1, 1, -60, 5, 50, 100, 6, 2, 1, 0, -20, 1, 5, 5, 3, 0, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 0, 0, 0.145, 0.555, 0.6525, 1, 0.3, 5, 1, 1, 0.2275, 0.365, 0.6825, 0.66, 0.3975, 0.495, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 8660, 1, 7, 0, 0, 0.5, 0.5, 0.5, 0.5, 8, 0, 0, 250, 0.4, 0.3, 0.5, 0.003, 0.001, 10, 9, 1, 10, 1.5, 0.3, 0.5, 0.8, 0.3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
// Rhythm
{ 0, 0, 0, 0, 0, 0, 0, -20.5, 0, 1, 0, 1, 1, 1, 1, -52, 0.1, 101.25, 213.975, 6, 2, 1, 0, -20, 0, 1.725, 6.85, 3, 2.05, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 1, 0, 0.02, 0.5775, 1, 1, 0.3, 5, 1, 2, 0.6225, 0.415, 0.755, 0.72, 0.5, 0.3525, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 8705, 1, 7, 0, 0, 0.5, 0.5, 0.5, 0.5, 8, 0, 0, 250, 0.4, 0.3, 0.5, 0.003, 0.001, 10, 9, 1, 10, 1.5, 0.3, 0.0175, 0.01, 0.1175, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
// Lead
{ 0, 0, 0, 0, 0, 0, 0, -21.3, 0, 1, 0, 1, 1, 1, 1, -61.6, 0.1, 67.5, 238.85, 6, 2, 1, 0, -18, 0, 2.775, 6.175, 3, 0.55, 3, 0, 0, 2, 0.55, 0.5, 0.65, 0.5, 0.5, 0.4, 4, 1, 0, 0.0625, 0.6, 0.7175, 1, 0.3, 5, 1, 4, 0.6075, 0.45, 0.7475, 0.7675, 0.6025, 0.47, 0.3, 0, 0, 0.5, 0, 0, 1, 0.55, 0.18, 0.33, 0.62, 0.42, 0.5, 0, 1, 0.5, 0.5, 0.5, 0, 0, 6, 1, 80, 9470, 1, 7, 1, 0, 0.12, 0.6125, 0.5, 0.5, 8, 1, 0, 445.777, 0.31605, 0.17, 0.5, 0.003, 0.001, 10, 9, 1, 10, 1.5, 0.3, 0, 0.01, 0.1475, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
// Write the built-in factory presets over their slots (leaves every non-factory
// slot untouched — so user presets survive). Used both for a fresh instance AND
// to REFRESH factory slots when a saved store predates the current kFactoryRev
// (otherwise updated factory presets in the binary never reach an existing user —
// the persisted .dat / pedalboard state would keep overriding them forever).
static void seedFactoryPresets(HexForge* p) {
    // Bank 1 (index 0): stock Clean/Crunch/Rhythm/Lead — captured as v3-layout rows
    // (before the IT voicing/boost + Seraph ports), so migrate them to the current layout.
    for (int s = 0; s < kSlots; ++s) {
        Preset& pr = p->presets[0][s];
        pr.used = true;
        std::snprintf(pr.name, sizeof(pr.name), "%s", kFactoryName[s]);
        std::memcpy(pr.vals, kFactoryVals[s], sizeof(pr.vals));
        migratePorts(pr.vals, 3);
        // Ports appended after the v3 capture migrate in as 0 — give Bank 1 the same subtle
        // default room every other preset carries (user 2026-07-14: "make sure my first four
        // presets have it").
        pr.vals[HF_CAB_ROOMON]  = 1.0f;
        pr.vals[HF_CAB_ROOMMIX] = 0.12f;
        pr.vals[HF_CAB_ROOMAMT] = 0.35f;
        // Fidelity polish (2026-07-23): speaker coupling + pickup load per stock rig
        // (Clean=Fender, Crunch=JCM800, Rhythm=EVH tight, Lead=Orange).
        static const float kSeedCoupl[kSlots] = {0.20f, 0.30f, 0.10f, 0.25f};
        static const float kSeedLoad [kSlots] = {0.12f, 0.08f, 0.00f, 0.05f};
        pr.vals[HF_AMP_PAMP_COUPL] = kSeedCoupl[s];
        pr.vals[HF_IT_LOAD]        = kSeedLoad[s];
        pr.vals[HF_RV_DENSITY]     = 1.0f;   // dense tank everywhere (user 2026-07-23)
        pr.vals[HF_CAB_ROOMDENSE]  = 1.0f;   // dense cab room everywhere (user 2026-07-23)
        pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0] = pr.amp2NamPath[0] = pr.ir2Path[0] = pr.dr2NamPath[0] = '\0';
    }
    // Band/song presets (generated in the current layout).
    for (int i = 0; i < kFactoryExtraCount; ++i) {
        const HfFactoryPreset& fp = kFactoryExtra[i];
        if (fp.bank < 0 || fp.bank >= kBanks || fp.slot < 0 || fp.slot >= kSlots) continue;
        Preset& pr = p->presets[fp.bank][fp.slot];
        pr.used = true;
        std::snprintf(pr.name, sizeof(pr.name), "%s", fp.name);
        std::memcpy(pr.vals, fp.vals, sizeof(pr.vals));
        // The kFactoryExtra rows are literals captured at their GENERATION layout
        // (v30, 224 ports). Ports appended before HF_SW_A since then shift the
        // command/meter tail onto the new params -- a raw memcpy hands every
        // factory preset garbage for quality/dr2/rig-B values (found 2026-07-30:
        // rig B silent everywhere because rb_master inherited a stale zero).
        // Migrate the row like any old store blob.
        migratePorts(pr.vals, kFactoryTableVer);
        pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0] = pr.amp2NamPath[0] = pr.ir2Path[0] = pr.dr2NamPath[0] = '\0';
        if (fp.cabIr && fp.cabIr[0]) { std::strncpy(pr.irPath, fp.cabIr, kPathMax-1); pr.irPath[kPathMax-1]='\0'; }
    }
    // DUAL RIGS (rev 78, 2026-07-30 user request: "figure out which ones would
    // benefit from dual amps and cabs"): presets whose documented artist rigs ran
    // multiple amps at once gain a parallel Amp 2/Cab 2 layer -- Muse (Bellamy's
    // clean AC30 under the fuzz rig), Gilmour (Hiwatt DR103 + Fender Twin),
    // QOTSA (solid-state + tube blend -> Orange layer), NIN Fragile-era (Sunn
    // low wall), Mastodon (Marshall + Orange backline). PHASE + PARITY measured
    // on-device per preset (rig_meas chord-burst RMS + rig_dbg wiggle passes,
    // 2026-07-30): the Desert Robot Orange layer reinforces INVERTED; the
    // Fender-under-Hiwatt pair blends at NORMAL polarity (the first measurement
    // hit the zero-write port trap -- rb_amp=0 never landed -- and measured a
    // Crunchy B side; inverted CANCELS with the real Clean Meanie: -9 dB). dOut
    // = measured out_level adjustment restoring RMS parity (+-0.2 dB).
    {
        struct RbSeed { int bank, slot;
                        float amp, gain, bass, mid, treble, pres, master, sag, cab, blend, pol, dOut; };
        static const RbSeed kRbSeeds[] = {
            {14,0,  8, 0.32f, 0.50f, 0.55f, 0.60f, 0.50f, 0.75f, 0.40f, 1, 0.30f, 0, +2.9f},  // Plug-In Junior: clean Chime Thirty under the FF
            {14,1,  8, 0.30f, 0.50f, 0.55f, 0.60f, 0.50f, 0.75f, 0.40f, 1, 0.25f, 0, +2.1f},  // Knights of Fuzz: same, subtler (user's build -- A side untouched)
            { 4,0,  0, 0.28f, 0.55f, 0.45f, 0.60f, 0.55f, 0.80f, 0.30f, 2, 0.35f, 0,  0.0f},  // Dark Side Air: Twin under the Hiwatt (normal pol; re-measured with the REAL Clean Meanie B)
            { 4,2,  0, 0.30f, 0.50f, 0.45f, 0.55f, 0.50f, 0.80f, 0.30f, 2, 0.25f, 0, +1.2f},  // Numb Sustain: Twin body under the lead (normal pol, re-measured)
            { 9,2,  4, 0.50f, 0.60f, 0.55f, 0.45f, 0.40f, 0.70f, 0.45f, 0, 0.30f, 1, +2.3f},  // Desert Robot: Orange girth under the Backline SS
            {11,1,  3, 0.55f, 0.65f, 0.40f, 0.40f, 0.40f, 0.70f, 0.50f, 5, 0.28f, 0, +2.0f},  // World Went Away: Sunn low wall (Fragile layering)
            { 6,0,  4, 0.48f, 0.50f, 0.60f, 0.50f, 0.40f, 0.70f, 0.40f, 0, 0.25f, 0,  0.0f},  // Skye Crusher: Orange beside the JCM800 (coherent, parity exact)
            // Round 2 (rev 80, user: "the ghost and periphery presets"; + Skye (No Mod) per user):
            { 3,0,  4, 0.50f, 0.55f, 0.50f, 0.50f, 0.45f, 0.70f, 0.40f, 0, 0.30f, 0, -1.2f},  // Imperial Rhythm: Orange under the Friedman (coherent: 10.9 dB pol swing)
            { 3,1,  4, 0.45f, 0.55f, 0.50f, 0.50f, 0.45f, 0.70f, 0.40f, 0, 0.28f, 1, +1.1f},  // Imperial Lead: same pair -- but the lead's pedal chain flips the net phase (inverted reinforces)
            { 7,2,  0, 0.30f, 0.30f, 0.50f, 0.60f, 0.50f, 0.80f, 0.15f, 0, 0.20f, 1, +1.7f},  // Flatliner: djent parallel CLEAN blend (tight low-bass Fender for pick attack)
            { 7,3,  0, 0.30f, 0.30f, 0.50f, 0.60f, 0.50f, 0.80f, 0.15f, 0, 0.20f, 1, +1.7f},  // Prayer Djent: same clean blend
            {10,2,  0, 0.30f, 0.50f, 0.45f, 0.60f, 0.55f, 0.80f, 0.30f, 2, 0.30f, 0, +2.0f},  // Streets Chime: the Edge's Fender side beside the AC30 (dOut covers amp2 + dl2 jointly)
            { 6,1,  4, 0.65f, 0.50f, 0.60f, 0.50f, 0.40f, 0.70f, 0.40f, 0, 0.25f, 0, -0.9f},  // Skye (No Mod): CRUNCHY Orange layer (rev 83 -- a clean layer diluted the riffs) + hotter A gain
            // Round 3 (rev 82, user: "the cardinal presets too, those are ghost"):
            { 3,2,  4, 0.62f, 0.55f, 0.50f, 0.50f, 0.45f, 0.75f, 0.40f, 0, 0.25f, 1, -1.1f},  // Cardinal Rhythm: CRUNCHY Orange under the Cali V (inverted; user dial-in untouched)
            { 3,3,  4, 0.60f, 0.55f, 0.50f, 0.50f, 0.45f, 0.70f, 0.40f, 0, 0.28f, 1, +1.8f},  // Cardinal Lead: CRUNCHY Orange under the Friedman (inverted)
        };
        for (const RbSeed& rs : kRbSeeds) {
            Preset& pr = p->presets[rs.bank][rs.slot];
            if (!pr.used) continue;
            pr.vals[HF_RB_ENABLE]   = 1.0f;
            pr.vals[HF_RB_AMP]      = rs.amp;
            pr.vals[HF_RB_GAIN]     = rs.gain;
            pr.vals[HF_RB_BASS]     = rs.bass;
            pr.vals[HF_RB_MID]      = rs.mid;
            pr.vals[HF_RB_TREBLE]   = rs.treble;
            pr.vals[HF_RB_PRESENCE] = rs.pres;
            pr.vals[HF_RB_MASTER]   = rs.master;
            pr.vals[HF_RB_SAG]      = rs.sag;
            pr.vals[HF_RB_CAB2ON]   = 1.0f;
            pr.vals[HF_RB_CAB]      = rs.cab;
            pr.vals[HF_RB_BLEND]    = rs.blend;
            pr.vals[HF_RB_LEVEL]    = 0.0f;
            pr.vals[HF_RB_POL]      = rs.pol;
            pr.vals[HF_OUT_LEVEL]  += rs.dOut;   // measured loudness-parity compensation
        }
        // X2 SECOND DELAYS (rev 80): documented two-delay rigs. Slotted at the same
        // pos as Delay 1 -- the canonical tie-break runs dl2 right after dl.
        struct Dl2Seed { int bank, slot; float time, fb, mix; };
        static const Dl2Seed kDl2Seeds[] = {
            {10, 2, 478.0f, 0.35f, 0.22f},   // Streets Chime: Edge's second delay (quarter note at 125.5 BPM beside the 359 ms dotted eighth)
            { 4, 2, 380.0f, 0.30f, 0.14f},   // Numb Sustain: second rack delay under the lead (parity-neutral, +0.05 dB measured)
        };
        // Skye (No Mod) = the RIFF preset (user 2026-07-30: "add more gain...
        // it should be for the riffs"): JCM800 gain 0.62 -> 0.72 (the same step
        // as the rev-34 ear-pass), out_level re-measured for parity below.
        p->presets[6][1].vals[HF_AMP_GAIN] = 0.82f;   // rev 83: "too clean... heavy mastodon riffs"
        for (const Dl2Seed& ds : kDl2Seeds) {
            Preset& pr = p->presets[ds.bank][ds.slot];
            if (!pr.used) continue;
            pr.vals[HF_DL2_ENABLE]   = 1.0f;
            pr.vals[HF_DL2_POS]      = pr.vals[HF_DL_POS];
            pr.vals[HF_DL2_TYPE]     = 0.0f;    // Digital
            pr.vals[HF_DL2_TIME]     = ds.time;
            pr.vals[HF_DL2_FEEDBACK] = ds.fb;
            pr.vals[HF_DL2_MIX]      = ds.mix;
            pr.vals[HF_DL2_WIDTH]    = 0.6f;
            pr.vals[HF_DL2_BYPASS]   = 0.0f;
        }
    }

    // 2026-07-13 (rev 27): packed the collection so no populated bank before the last has blank slots (user
    // request). Banks 0-13 are now FULL; the MUSE bank (index 14) is the LAST populated bank and holds only
    // A/B (Plug In Baby + Knights of Cydonia); index 15 was emptied (its Muse presets moved to 14). Clear the
    // now-vacated factory slots so stale presets from older revs don't linger (a user is extremely unlikely
    // to have saved over former-factory slots).
    // BANK 18 (index 17) -- CLASSIC ROCK / METAL (rev 91, user request):
    // A Frayed Justice (Metallica ...And Justice for All: Cali V IIC+ deep-V
    //   GEQ scoop, tight gate, V30s, famously DRY), B Brown Sound '84 (Van
    //   Halen: Plexi + VARIAC, Vol I dimed + jumpered Vol II, Phase 90 IN
    //   FRONT of the amp, tape echo after the cab, Greenbacks), C Acca Dacca
    //   (AC/DC: non-master Plexi at the edge, jumpered, bright, dry, no
    //   pedals), D Jungle Sleaze (GnR Appetite: the S.I.R.-modded 1959 ~= our
    //   JCM800, mid-forward, Greenbacks, darker top, small plate).
    // Donor row = stock Crunch (migrated, simple chain), every optional block
    // reset OFF, then per-preset builds. out_levels measured on-device.
    {
        auto mk = [&](int slot, const char* name) -> Preset& {
            Preset& pr = p->presets[17][slot];
            pr = p->presets[0][1];
            pr.used = true;
            std::snprintf(pr.name, sizeof(pr.name), "%s", name);
            pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0]
                = pr.amp2NamPath[0] = pr.ir2Path[0] = pr.dr2NamPath[0] = '\0';
            static const int offs[] = { HF_CP_ENABLE, HF_FZ_ENABLE, HF_DR_ENABLE,
                HF_MD_ENABLE, HF_DL_ENABLE, HF_RV_ENABLE, HF_WH_ENABLE, HF_OC_ENABLE,
                HF_NAIL_ENABLE, HF_EQ_ENABLE, HF_DR2_ENABLE, HF_RB_ENABLE, HF_RB_CAB2ON,
                HF_GT2_ENABLE, HF_CP2_ENABLE, HF_FZ2_ENABLE, HF_NAIL2_ENABLE, HF_MD2_ENABLE,
                HF_DL2_ENABLE, HF_RV2_ENABLE, HF_WH2_ENABLE, HF_OC2_ENABLE, HF_EQ2_ENABLE };
            for (int o : offs) pr.vals[o] = 0.0f;
            pr.vals[HF_AMP_PL_VARIAC] = 0.0f; pr.vals[HF_AMP_PL_VOL2] = 0.0f;
            pr.vals[HF_CAB_MICPOS] = 0.0f; pr.vals[HF_CAB_MICDIST] = 0.0f;
            pr.vals[HF_CAB_ROOMON] = 1.0f; pr.vals[HF_CAB_ROOMMIX] = 0.10f;
            pr.vals[HF_CAB_ROOMAMT] = 0.35f; pr.vals[HF_CAB_ROOMDENSE] = 1.0f;
            pr.vals[HF_RV_DENSITY] = 1.0f;
            pr.vals[HF_OUT_LEVEL] = -16.0f;   // provisional, measured below
            return pr;
        };
        {   // A: Frayed Justice
            Preset& pr = mk(0, "Frayed Justice");
            pr.vals[HF_GT_THRESH] = -42.0f; pr.vals[HF_GT_ATTACK] = 1.0f;
            pr.vals[HF_GT_HOLD] = 120.0f; pr.vals[HF_GT_RELEASE] = 200.0f;
            pr.vals[HF_AMP_MODEL] = 11.0f; pr.vals[HF_AMP_GAIN] = 0.52f;
            pr.vals[HF_AMP_BASS] = 0.08f; pr.vals[HF_AMP_MID] = 0.5f;
            pr.vals[HF_AMP_TREBLE] = 0.40f; pr.vals[HF_AMP_PRESENCE] = 0.45f;
            pr.vals[HF_AMP_MASTER] = 0.6f; pr.vals[HF_AMP_SAG] = 0.2f;
            pr.vals[HF_AMP_MV_MODE] = 6.0f;   // IIC+
            pr.vals[HF_AMP_MV_GEQ0] = 0.75f; pr.vals[HF_AMP_MV_GEQ1] = 0.54f;
            pr.vals[HF_AMP_MV_GEQ2] = 0.08f; pr.vals[HF_AMP_MV_GEQ3] = 0.42f;
            pr.vals[HF_AMP_MV_GEQ4] = 0.75f;   // deep-V faders (mirrors preset 3)
            pr.vals[HF_AMP_MV_EQPRESET] = 3.0f;
            pr.vals[HF_CAB_LOWCUT] = 95.0f; pr.vals[HF_CAB_HIGHCUT] = 7800.0f;
            pr.vals[HF_CAB_ROOMMIX] = 0.06f;   // AJFA is DRY
        }
        {   // B: Brown Sound '84
            Preset& pr = mk(1, "Brown Sound '84");
            // Second pass (user: "try another pass at the van halen tone"):
            // MORE amp, LESS top -- gain/Vol II up, sag deeper (the variac'd
            // supply moving under the note is the brown in brown sound), the
            // treble/presence tilt inverted (treble down .55, presence UP .65
            // -- warm plates, present cab), mids pushed .68. The Phase 90 is
            // a real 100%-wet script pedal: mix up .45, rate slowed .35.
            // Tape echo eased to .15 so the swirl leads, not the repeats.
            pr.vals[HF_GT_THRESH] = -48.0f;
            // Round 3 (user: "the plexi seems short on gain"): the model is
            // capture-anchored to a REAL 1959, which is short on gain for VH
            // by itself -- Ed's push was the Echoplex EP-3 preamp slamming
            // the front end. Preamp 250 as the EP-3 stand-in: barely any
            // drive of its own, level .78 hot into V1.
            pr.vals[HF_DR_ENABLE] = 1.0f; pr.vals[HF_DR_MODEL] = 7.0f;
            pr.vals[HF_DR_DRIVE] = 0.25f; pr.vals[HF_DR_TONE] = 0.55f;
            pr.vals[HF_DR_LEVEL] = 0.78f;
            pr.vals[HF_AMP_MODEL] = 10.0f; pr.vals[HF_AMP_GAIN] = 0.95f;
            pr.vals[HF_AMP_PL_VOL2] = 0.50f; pr.vals[HF_AMP_PL_VARIAC] = 1.0f;
            pr.vals[HF_AMP_BASS] = 0.50f; pr.vals[HF_AMP_MID] = 0.68f;
            pr.vals[HF_AMP_TREBLE] = 0.55f; pr.vals[HF_AMP_PRESENCE] = 0.65f;
            pr.vals[HF_AMP_MASTER] = 0.90f; pr.vals[HF_AMP_SAG] = 0.50f;
            pr.vals[HF_MD_ENABLE] = 1.0f; pr.vals[HF_MD_POS] = 3.0f;   // Phase 90 IN FRONT
            pr.vals[HF_MD_TYPE] = 2.0f; pr.vals[HF_MD_RATE] = 0.35f;
            pr.vals[HF_MD_DEPTH] = 0.50f; pr.vals[HF_MD_MIX] = 0.45f;
            pr.vals[HF_DL_ENABLE] = 1.0f; pr.vals[HF_DL_TYPE] = 1.0f;   // tape echo
            pr.vals[HF_DL_TIME] = 310.0f; pr.vals[HF_DL_FEEDBACK] = 0.32f;
            pr.vals[HF_DL_MIX] = 0.15f; pr.vals[HF_DL_WIDTH] = 0.55f;
            pr.vals[HF_RV_ENABLE] = 1.0f; pr.vals[HF_RV_TYPE] = 0.0f;
            pr.vals[HF_RV_MIX] = 0.14f; pr.vals[HF_RV_DECAY] = 1.6f;
            pr.vals[HF_RV_PREDELAY] = 15.0f;
            pr.vals[HF_CAB_LOWCUT] = 85.0f; pr.vals[HF_CAB_HIGHCUT] = 8000.0f;
            pr.vals[HF_CAB_MICPOS] = 0.10f; pr.vals[HF_CAB_MICDIST] = 0.10f;
            pr.vals[HF_CAB_ROOMMIX] = 0.12f;
        }
        {   // C: Acca Dacca
            Preset& pr = mk(2, "Sister's Singer");
            pr.vals[HF_GT_THRESH] = -50.0f;
            pr.vals[HF_AMP_MODEL] = 10.0f; pr.vals[HF_AMP_GAIN] = 0.55f;
            pr.vals[HF_AMP_PL_VOL2] = 0.35f;
            pr.vals[HF_AMP_BASS] = 0.5f; pr.vals[HF_AMP_MID] = 0.58f;
            pr.vals[HF_AMP_TREBLE] = 0.60f; pr.vals[HF_AMP_PRESENCE] = 0.6f;
            pr.vals[HF_AMP_MASTER] = 0.80f; pr.vals[HF_AMP_SAG] = 0.30f;
            pr.vals[HF_CAB_LOWCUT] = 90.0f; pr.vals[HF_CAB_HIGHCUT] = 8000.0f;
        }
        {   // D: Jungle Sleaze
            Preset& pr = mk(3, "Jungle Sleaze");
            // Rev 92 (user): this slot IS the Welcome to the Jungle DELAY
            // INTRO, not the rhythm tone -- gain eased to .45 (driven single
            // notes, not the wall), and the famous cascading echo: clean
            // DIGITAL repeats at 470 ms (~quarter note at the song's ~124
            // bpm), feedback .55 for the 4-5 trailing images, mix .42 so the
            // echoes answer at nearly full voice. Small plate behind it.
            pr.vals[HF_GT_THRESH] = -48.0f;
            pr.vals[HF_AMP_MODEL] = 1.0f; pr.vals[HF_AMP_GAIN] = 0.45f;
            pr.vals[HF_AMP_BASS] = 0.55f; pr.vals[HF_AMP_MID] = 0.62f;
            pr.vals[HF_AMP_TREBLE] = 0.55f; pr.vals[HF_AMP_PRESENCE] = 0.5f;
            pr.vals[HF_AMP_MASTER] = 0.70f; pr.vals[HF_AMP_SAG] = 0.35f;
            pr.vals[HF_DL_ENABLE] = 1.0f; pr.vals[HF_DL_TYPE] = 0.0f;   // clean digital
            pr.vals[HF_DL_TIME] = 470.0f; pr.vals[HF_DL_FEEDBACK] = 0.55f;
            pr.vals[HF_DL_MIX] = 0.42f; pr.vals[HF_DL_WIDTH] = 0.45f;
            pr.vals[HF_CAB_LOWCUT] = 85.0f; pr.vals[HF_CAB_HIGHCUT] = 7500.0f;
            pr.vals[HF_CAB_MICPOS] = 0.15f; pr.vals[HF_CAB_MICDIST] = 0.10f;
            pr.vals[HF_RV_ENABLE] = 1.0f; pr.vals[HF_RV_TYPE] = 0.0f;
            pr.vals[HF_RV_MIX] = 0.12f; pr.vals[HF_RV_DECAY] = 1.4f;
        }
        // Cab IRs: Greenbacks for the Marshalls, factory V30 for the Mesa.
        std::strncpy(p->presets[17][1].irPath, "@greenback", kPathMax-1);
        std::strncpy(p->presets[17][2].irPath, "@greenback", kPathMax-1);
        std::strncpy(p->presets[17][3].irPath, "@greenback", kPathMax-1);
        // Loudness parity, measured on-device vs the Bank-1 Crunch anchor
        // (chord-burst domain: anchor -16.8): all four seeded hot, trimmed to it.
        p->presets[17][0].vals[HF_OUT_LEVEL] = -19.0f;
        p->presets[17][1].vals[HF_OUT_LEVEL] = -22.7f;   // re-trimmed after the EP-3 boost (round 3, clean-HOME measured)
        p->presets[17][2].vals[HF_OUT_LEVEL] = -19.4f;
        p->presets[17][3].vals[HF_OUT_LEVEL] = -18.0f;   // re-trimmed for the delay intro
        // USER FINAL Frayed Justice (rev 92, 2026-07-31, store-diffed
        // on-device -- DO NOT RETUNE): the user's rework, baked verbatim.
        // Chain reordered amp-first (amp 2 / cab 3 / EQ 4 / reverb 5),
        // treble dumped to .17 with presence up, the Mid-Boost EQ curve
        // post-cab, Speaker Drive Full, a touch of plate -- and a full B
        // rig: second Cali V in mode 7 with its own GEQ hump + Greenback
        // Cab 2, blended .60. (Cali V is untouched by the spkdrive loop,
        // so the baked cab_spkdrive survives.)
        {
            Preset& fj = p->presets[17][0];
            fj.vals[HF_CP_POS] = 6.0f;  fj.vals[HF_FZ_POS] = 7.0f;
            fj.vals[HF_DR_POS] = 8.0f;  fj.vals[HF_AMP_POS] = 2.0f;
            fj.vals[HF_AMP_BASS] = 0.09f;     fj.vals[HF_AMP_MID] = 0.54f;
            fj.vals[HF_AMP_TREBLE] = 0.1725f; fj.vals[HF_AMP_PRESENCE] = 0.55f;
            fj.vals[HF_CAB_POS] = 3.0f; fj.vals[HF_MD_POS] = 9.0f;
            fj.vals[HF_DL_POS] = 10.0f; fj.vals[HF_RV_POS] = 5.0f;
            fj.vals[HF_RV_ENABLE] = 1.0f; fj.vals[HF_RV_MIX] = 0.2075f;
            fj.vals[HF_WH_POS] = 11.0f; fj.vals[HF_OC_POS] = 12.0f;
            fj.vals[HF_NAIL_POS] = 13.0f; fj.vals[HF_EQ_POS] = 4.0f;
            fj.vals[HF_EQ_ENABLE] = 1.0f; fj.vals[HF_EQ_PRESET] = 4.0f;
            fj.vals[HF_EQ_100] = 4.0f;  fj.vals[HF_EQ_200] = -2.0f;
            fj.vals[HF_EQ_400] = -5.0f; fj.vals[HF_EQ_800] = -2.0f;
            fj.vals[HF_EQ_1K6] = 2.0f;  fj.vals[HF_EQ_3K2] = 4.0f;
            fj.vals[HF_CAB_SPKDRIVE] = 2.0f;
            fj.vals[HF_RB_ENABLE] = 1.0f; fj.vals[HF_RB_AMP] = 11.0f;
            fj.vals[HF_RB_GAIN] = 0.335f;   fj.vals[HF_RB_BASS] = 0.6875f;
            fj.vals[HF_RB_MID] = 0.665f;    fj.vals[HF_RB_TREBLE] = 0.6425f;
            fj.vals[HF_RB_BLEND] = 0.6025f; fj.vals[HF_RB_LEVEL] = 0.06f;
            fj.vals[HF_RB_MV_MODE] = 7.0f;
            fj.vals[HF_RB_MV_GEQ0] = 0.416667f; fj.vals[HF_RB_MV_GEQ1] = 0.583333f;
            fj.vals[HF_RB_MV_GEQ2] = 0.708333f; fj.vals[HF_RB_MV_GEQ3] = 0.625f;
            fj.vals[HF_RB_MV_GEQ4] = 0.458333f;
            fj.vals[HF_RB_CABROOMDENSE] = 1.0f; fj.vals[HF_RB_CABSPKDRIVE] = 2.0f;
            fj.vals[HF_RB_CAB2ON] = 1.0f;
            std::strncpy(fj.ir2Path, "@greenback", kPathMax-1);
            fj.ir2Path[kPathMax-1] = '\0';
        }
        // USER FINAL B/C/D (rev 94, 2026-07-31, store-diffed on-device at
        // exact float32 precision -- DO NOT RETUNE):
        //  - Brown Sound '84: fully re-staged -- Input Trim boost +7.05 dB
        //    into the EP-3 drive eased to .135, amp gain DOWN .73 with
        //    treble/master DIMED + presence .9975 (the real dimed-Plexi
        //    move: gain from the front, top from the tone stack), Vol II
        //    OUT, sag .34, phaser slowed to .13 mono, echo up .22, an EQ
        //    parked bypassed at pos 8.
        //  - Sister's Singer: everything on 10 -- gain 1.0 / Vol II 1.0 /
        //    master 1.0, bass down .33, Deep-V-based chain EQ pre-cab,
        //    gate moved post-EQ and switched OFF (bypassed).
        //  - Jungle Sleaze: the intro goes hotter -- donor Green Man boost
        //    ON dimed (.9975) into gain .77, master .825, mic pushed in.
        {
            Preset& bs = p->presets[17][1];
            bs.vals[HF_IT_BOOST] = 1.0f;    bs.vals[HF_IT_BOOSTAMT] = 7.05f;
            bs.vals[HF_CP_POS] = 9.0f;      bs.vals[HF_FZ_POS] = 10.0f;
            bs.vals[HF_DR_POS] = 3.0f;      bs.vals[HF_DR_DRIVE] = 0.135f;
            bs.vals[HF_DR_TONE] = 0.5425f;  bs.vals[HF_DR_LEVEL] = 0.775f;
            bs.vals[HF_AMP_POS] = 4.0f;     bs.vals[HF_AMP_GAIN] = 0.7325f;
            bs.vals[HF_AMP_BASS] = 0.3775f; bs.vals[HF_AMP_MID] = 0.7775f;
            bs.vals[HF_AMP_TREBLE] = 1.0f;  bs.vals[HF_AMP_PRESENCE] = 0.9975f;
            bs.vals[HF_AMP_MASTER] = 1.0f;  bs.vals[HF_AMP_SAG] = 0.3425f;
            bs.vals[HF_CAB_POS] = 5.0f;     bs.vals[HF_MD_POS] = 2.0f;
            bs.vals[HF_MD_RATE] = 0.1325f;  bs.vals[HF_MD_DEPTH] = 0.3325f;
            bs.vals[HF_MD_MIX] = 0.4975f;   bs.vals[HF_MD_WIDTH] = 0.0f;
            bs.vals[HF_MD_TYPE] = 8.0f;     // user swapped Phaser -> Script Phaser (no-feedback P90), 2026-07-31
            bs.vals[HF_DL_POS] = 6.0f;      bs.vals[HF_DL_MIX] = 0.22f;
            bs.vals[HF_RV_POS] = 7.0f;      bs.vals[HF_WH_POS] = 11.0f;
            bs.vals[HF_OC_POS] = 12.0f;     bs.vals[HF_NAIL_POS] = 13.0f;
            bs.vals[HF_AMP_PL_VOL2] = 0.0f; bs.vals[HF_EQ_POS] = 8.0f;
            bs.vals[HF_EQ_PRESET] = 1.0f;   bs.vals[HF_EQ_100] = 3.0f;
            bs.vals[HF_EQ_400] = -1.0f;     bs.vals[HF_EQ_800] = 2.0f;
            bs.vals[HF_EQ_1K6] = 1.0f;      bs.vals[HF_EQ_3K2] = 2.0f;
            bs.vals[HF_EQ_BYPASS] = 1.0f;   bs.vals[HF_CAB_SPKDRIVE] = 2.0f;
            Preset& ss = p->presets[17][2];
            ss.vals[HF_GT_POS] = 4.0f;      ss.vals[HF_GT_ENABLE] = 0.0f;
            ss.vals[HF_GT_BYPASS] = 1.0f;
            ss.vals[HF_CP_POS] = 5.0f;      ss.vals[HF_FZ_POS] = 6.0f;
            ss.vals[HF_DR_POS] = 7.0f;      ss.vals[HF_AMP_POS] = 1.0f;
            ss.vals[HF_AMP_GAIN] = 1.0f;    ss.vals[HF_AMP_BASS] = 0.33f;
            ss.vals[HF_AMP_MID] = 0.635f;   ss.vals[HF_AMP_TREBLE] = 0.63f;
            ss.vals[HF_AMP_PRESENCE] = 0.6475f; ss.vals[HF_AMP_MASTER] = 1.0f;
            ss.vals[HF_CAB_POS] = 2.0f;     ss.vals[HF_MD_POS] = 8.0f;
            ss.vals[HF_DL_POS] = 9.0f;      ss.vals[HF_RV_POS] = 10.0f;
            ss.vals[HF_WH_POS] = 11.0f;     ss.vals[HF_OC_POS] = 12.0f;
            ss.vals[HF_NAIL_POS] = 13.0f;   ss.vals[HF_AMP_PL_VOL2] = 1.0f;
            ss.vals[HF_EQ_POS] = 3.0f;      ss.vals[HF_EQ_ENABLE] = 1.0f;
            ss.vals[HF_EQ_PRESET] = 3.0f;   ss.vals[HF_EQ_100] = 3.0f;
            ss.vals[HF_EQ_200] = 1.0f;      ss.vals[HF_EQ_400] = -3.0f;
            ss.vals[HF_EQ_1K6] = 2.0f;      ss.vals[HF_EQ_3K2] = 4.0f;
            Preset& js = p->presets[17][3];
            js.vals[HF_CP_POS] = 7.0f;      js.vals[HF_FZ_POS] = 8.0f;
            js.vals[HF_DR_POS] = 2.0f;      js.vals[HF_DR_ENABLE] = 1.0f;
            js.vals[HF_DR_DRIVE] = 0.175f;  js.vals[HF_DR_LEVEL] = 0.9975f;
            js.vals[HF_AMP_POS] = 3.0f;     js.vals[HF_AMP_GAIN] = 0.77f;
            js.vals[HF_AMP_BASS] = 0.445f;  js.vals[HF_AMP_MID] = 0.68f;
            js.vals[HF_AMP_TREBLE] = 0.6225f; js.vals[HF_AMP_MASTER] = 0.825f;
            js.vals[HF_CAB_POS] = 4.0f;     js.vals[HF_MD_POS] = 10.0f;
            js.vals[HF_DL_POS] = 5.0f;      js.vals[HF_RV_POS] = 6.0f;
            js.vals[HF_WH_POS] = 11.0f;     js.vals[HF_OC_POS] = 12.0f;
            js.vals[HF_NAIL_POS] = 13.0f;
            js.vals[HF_CAB_MICPOS] = 0.120062f; js.vals[HF_CAB_MICDIST] = 0.052566f;
            js.vals[HF_EQ_POS] = 9.0f;
        }
        // Loudness parity vs the Bank-1 Crunch anchor (rev 95, clean-HOME
        // measured AFTER the user finals; the one deliberate deviation from
        // the verbatim store rows, per the user: "make the new presets have
        // volume parity with our other presets"). All four land -16.8 exact.
        p->presets[17][0].vals[HF_OUT_LEVEL] = -17.3f;
        p->presets[17][1].vals[HF_OUT_LEVEL] = -21.8f;   // +0.2 for variac v2 physics (rev 96)
        p->presets[17][2].vals[HF_OUT_LEVEL] = -23.0f;
        p->presets[17][3].vals[HF_OUT_LEVEL] = -20.6f;
        // USER FINAL round 2 (rev 97, 2026-07-31, store-diffed at exact
        // float32 -- DO NOT RETUNE): the user moved Brown Sound '84 onto the
        // REAL EP-3 system the same day it shipped:
        //  - Echo Primer (dr_model 8) replaces the Preamp 250 stand-in --
        //    level DIMED into the front end, barely any drive (.1375).
        //  - EP-3 Echo (dl_type 4) replaces the generic tape: 241 ms, fb
        //    .08, mix .02, age .02 -- a nearly-invisible fresh-tape slap.
        //  - Rig B = a SECOND variac'd Plexi (rb_amp 10, rb_pl_variac 1,
        //    Vol II cracked .1175, everything dimed, sag .50, +1.02 dB,
        //    blend .5075) into its own close-miked @factory Cab 2.
        //  - A side: gain .84, bass DIMED, mids .9975, sag .70, Vol II .12,
        //    chain EQ moved pre (pos 2, preset 5 curve), IT boost eased to
        //    +2.04, phaser mix down .3025.
        //  - Jungle Sleaze: hotter/brighter amp (gain .805, mid .8725,
        //    treble .7275, presence .66), mic on-cap at the grille, and its
        //    B-side cab prepped (@greenback Cab 2, 10.46k highcut).
        {
            Preset& bs = p->presets[17][1];
            bs.vals[HF_IT_BOOSTAMT] = 2.04f;
            bs.vals[HF_DR_POS] = 4.0f;      bs.vals[HF_DR_MODEL] = 8.0f;
            bs.vals[HF_DR_DRIVE] = 0.1375f; bs.vals[HF_DR_TONE] = 0.5f;
            bs.vals[HF_DR_LEVEL] = 1.0f;
            bs.vals[HF_AMP_POS] = 6.0f;     bs.vals[HF_AMP_GAIN] = 0.84f;
            bs.vals[HF_AMP_BASS] = 1.0f;    bs.vals[HF_AMP_MID] = 0.9975f;
            bs.vals[HF_AMP_SAG] = 0.6975f;  bs.vals[HF_CAB_POS] = 7.0f;
            bs.vals[HF_MD_POS] = 3.0f;      bs.vals[HF_MD_RATE] = 0.125f;
            bs.vals[HF_MD_DEPTH] = 0.35f;   bs.vals[HF_MD_MIX] = 0.3025f;
            bs.vals[HF_DL_POS] = 5.0f;      bs.vals[HF_DL_TYPE] = 4.0f;
            bs.vals[HF_DL_TIME] = 240.88f;  bs.vals[HF_DL_FEEDBACK] = 0.08085f;
            bs.vals[HF_DL_MIX] = 0.02f;     bs.vals[HF_DL_WIDTH] = 0.5f;
            bs.vals[HF_DL_AGE] = 0.02f;     bs.vals[HF_RV_POS] = 8.0f;
            bs.vals[HF_AMP_PL_VOL2] = 0.12f;
            bs.vals[HF_EQ_POS] = 2.0f;      bs.vals[HF_EQ_ENABLE] = 1.0f;
            bs.vals[HF_EQ_PRESET] = 5.0f;   bs.vals[HF_EQ_100] = -1.0f;
            bs.vals[HF_EQ_400] = 1.0f;      bs.vals[HF_EQ_800] = 3.0f;
            bs.vals[HF_EQ_1K6] = 4.0f;      bs.vals[HF_EQ_3K2] = 5.0f;
            bs.vals[HF_EQ_LEVEL] = 0.12f;   bs.vals[HF_EQ_BYPASS] = 0.0f;
            bs.vals[HF_RB_AMP] = 10.0f;     bs.vals[HF_RB_GAIN] = 0.7525f;
            bs.vals[HF_RB_BASS] = 0.985f;   bs.vals[HF_RB_MID] = 1.0f;
            bs.vals[HF_RB_TREBLE] = 1.0f;   bs.vals[HF_RB_PRESENCE] = 1.0f;
            bs.vals[HF_RB_SAG] = 0.5025f;   bs.vals[HF_RB_BLEND] = 0.5075f;
            bs.vals[HF_RB_LEVEL] = 1.02f;   bs.vals[HF_RB_PL_VOL2] = 0.1175f;
            bs.vals[HF_RB_PL_VARIAC] = 1.0f;
            bs.vals[HF_RB_CABMICPOS] = 0.323924f;
            bs.vals[HF_RB_CABMICDIST] = 0.133917f;
            bs.vals[HF_RB_CABROOMDENSE] = 1.0f; bs.vals[HF_RB_CABSPKDRIVE] = 2.0f;
            bs.vals[HF_RB_CAB2ON] = 1.0f;
            std::strncpy(bs.ir2Path, "@factory", kPathMax-1);
            bs.ir2Path[kPathMax-1] = '\0';
            Preset& js = p->presets[17][3];
            js.vals[HF_AMP_GAIN] = 0.805f;  js.vals[HF_AMP_MID] = 0.8725f;
            js.vals[HF_AMP_TREBLE] = 0.7275f; js.vals[HF_AMP_PRESENCE] = 0.66f;
            js.vals[HF_DL_MIX] = 0.40f;
            js.vals[HF_CAB_MICPOS] = 0.176666f; js.vals[HF_CAB_MICDIST] = 0.0f;
            js.vals[HF_RB_HIGHCUT] = 10460.0f;
            js.vals[HF_RB_CABMICPOS] = 0.055056f; js.vals[HF_RB_CABMICDIST] = 1.0f;
            js.vals[HF_RB_CABROOMDENSE] = 1.0f;   js.vals[HF_RB_CABSPKDRIVE] = 2.0f;
            js.vals[HF_RB_CAB2ON] = 1.0f;
            // Loudness parity after the round-2 bake (measured): the EP-3
            // rework sat 2.6 dB under the anchor, Jungle 0.2 over.
            bs.vals[HF_OUT_LEVEL] = -19.2f;
            js.vals[HF_OUT_LEVEL] = -20.8f;
            // USER round 3 (rev 98, 2026-07-31, store-diffed exact float32,
            // DO NOT RETUNE): Jungle Sleaze goes SIR #34 -- the user flipped
            // the new mod ON the hour it shipped (historically right: the
            // Appetite amp WAS an S.I.R. rental) and re-dialed around the
            // bite: mids DOWN .7475 (the mod adds them), gain UP .885,
            // treble .75, presence .55, master .855, Green Man boost eased
            // (.10 drive / .695 level), gate -48.4, echo .385/.505, mic
            // back on-cap, B-side cab mic zeroed.
            js.vals[HF_GT_THRESH] = -48.4f;
            js.vals[HF_DR_DRIVE] = 0.10f;   js.vals[HF_DR_LEVEL] = 0.695f;
            js.vals[HF_AMP_GAIN] = 0.885f;  js.vals[HF_AMP_MID] = 0.7475f;
            js.vals[HF_AMP_TREBLE] = 0.75f; js.vals[HF_AMP_PRESENCE] = 0.55f;
            js.vals[HF_AMP_MASTER] = 0.855f;
            js.vals[HF_DL_MIX] = 0.385f;    js.vals[HF_DL_WIDTH] = 0.505f;
            js.vals[HF_CAB_MICPOS] = 0.120062f; js.vals[HF_CAB_MICDIST] = 0.021277f;
            js.vals[HF_RB_CABMICPOS] = 0.0f;    js.vals[HF_RB_CABMICDIST] = 0.0f;
            js.vals[HF_AMP_SIR34] = 1.0f;
            js.vals[HF_OUT_LEVEL] = -20.3f;   // parity re-trim after the #34 build (was -20.8)
            std::strncpy(js.ir2Path, "@greenback", kPathMax-1);
            js.ir2Path[kPathMax-1] = '\0';
        }
    }

    // BANK 19 (index 18) -- LOW END (rev 124, the Blue Liner bass bank):
    // A Round Trip (SVT clean round: light comp, 810, dead dry), B Fridge
    //   Grind (driven SVT, Ultra-Hi, 220 Hz mid push, 810), C Motor City
    //   (flip-top Motown: Ultra-Lo, dark top, heavier comp, 1x15, bone dry),
    //   D Fuzz Wall (Italian Hero Muff into a CLEAN SVT — the fuzz-staging
    //   rule: the fuzz is line-hot, the amp must stay clean — into the 410h).
    // KEY bass housekeeping baked into every slot: cab low_cut 35-40 (the 80
    // default guts a bass cab), room ambience OFF (a Schroeder room on a
    // 40 Hz fundamental is mud), Room cab voice (Studio brackets at 78 Hz).
    // out_levels provisional -16: measure on-device vs the Bank-1 anchor
    // (hexforge_amplevel/store-bake pass) before calling them final.
    {
        auto mkb = [&](int slot, const char* name) -> Preset& {
            Preset& pr = p->presets[18][slot];
            pr = p->presets[0][1];
            pr.used = true;
            std::snprintf(pr.name, sizeof(pr.name), "%s", name);
            pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0]
                = pr.amp2NamPath[0] = pr.ir2Path[0] = pr.dr2NamPath[0] = '\0';
            static const int offs[] = { HF_CP_ENABLE, HF_FZ_ENABLE, HF_DR_ENABLE,
                HF_MD_ENABLE, HF_DL_ENABLE, HF_RV_ENABLE, HF_WH_ENABLE, HF_OC_ENABLE,
                HF_NAIL_ENABLE, HF_EQ_ENABLE, HF_DR2_ENABLE, HF_RB_ENABLE, HF_RB_CAB2ON,
                HF_GT2_ENABLE, HF_CP2_ENABLE, HF_FZ2_ENABLE, HF_NAIL2_ENABLE, HF_MD2_ENABLE,
                HF_DL2_ENABLE, HF_RV2_ENABLE, HF_WH2_ENABLE, HF_OC2_ENABLE, HF_EQ2_ENABLE };
            for (int o : offs) pr.vals[o] = 0.0f;
            pr.vals[HF_AMP_MODEL]   = 14.0f;   // Blue Liner
            pr.vals[HF_AMP_SV_ULTRALO] = 0.0f; pr.vals[HF_AMP_SV_ULTRAHI] = 0.0f;
            pr.vals[HF_AMP_SV_MIDFREQ] = 1.0f; // 800 Hz
            pr.vals[HF_AMP_PL_VARIAC] = 0.0f;  pr.vals[HF_AMP_PL_VOL2] = 0.0f;
            pr.vals[HF_AMP_SIR34] = 0.0f;
            pr.vals[HF_AMP_MASTER] = 0.62f;    pr.vals[HF_AMP_SAG] = 0.3f;
            pr.vals[HF_CAB_LOWCUT] = 38.0f;    pr.vals[HF_CAB_HIGHCUT] = 9000.0f;
            pr.vals[HF_CAB_MICPOS] = 0.0f;     pr.vals[HF_CAB_MICDIST] = 0.0f;
            pr.vals[HF_CAB_ROOMON] = 0.0f;     pr.vals[HF_CAB_VOICE] = 0.0f;   // Room voice
            pr.vals[HF_CAB_SPKDRIVE] = 0.0f;   // its LF split sits mid-fundamental — off until auditioned
            pr.vals[HF_GT_THRESH] = -50.0f;
            pr.vals[HF_OUT_LEVEL] = -16.0f;    // overridden per preset below (rev 125 parity trims)
            return pr;
        };
        {   // A: Round Trip — the clean round SVT + fridge.
            Preset& pr = mkb(0, "Round Trip");
            pr.vals[HF_AMP_GAIN] = 0.35f;
            pr.vals[HF_AMP_BASS] = 0.55f; pr.vals[HF_AMP_MID] = 0.5f;
            pr.vals[HF_AMP_TREBLE] = 0.45f;
            pr.vals[HF_CP_ENABLE] = 1.0f;   // light glue
            pr.vals[HF_CP_RATIO] = 2.5f; pr.vals[HF_CP_THRESH] = -24.0f;
            pr.vals[HF_OUT_LEVEL] = -21.5f;   // rev 125: golden-domain parity vs the Bank-1 anchor
            std::strncpy(pr.irPath, "@bass810", kPathMax-1); pr.irPath[kPathMax-1] = '\0';
        }
        {   // B: Fridge Grind — the driven SVT rock tone.
            Preset& pr = mkb(1, "Fridge Grind");
            pr.vals[HF_AMP_GAIN] = 0.72f;
            pr.vals[HF_AMP_SV_ULTRAHI] = 1.0f;
            pr.vals[HF_AMP_SV_MIDFREQ] = 0.0f;   // 220 Hz push
            pr.vals[HF_AMP_BASS] = 0.5f; pr.vals[HF_AMP_MID] = 0.65f;
            pr.vals[HF_AMP_TREBLE] = 0.5f; pr.vals[HF_AMP_MASTER] = 0.75f;
            pr.vals[HF_OUT_LEVEL] = -26.0f;   // rev 125: parity (was clipping at the makeup bake, peak 1.49)
            std::strncpy(pr.irPath, "@bass810", kPathMax-1); pr.irPath[kPathMax-1] = '\0';
        }
        {   // C: Motor City — flip-top Motown thump.
            Preset& pr = mkb(2, "Motor City");
            pr.vals[HF_AMP_GAIN] = 0.30f;
            pr.vals[HF_AMP_SV_ULTRALO] = 1.0f;
            pr.vals[HF_AMP_BASS] = 0.55f; pr.vals[HF_AMP_MID] = 0.45f;
            pr.vals[HF_AMP_TREBLE] = 0.30f;
            pr.vals[HF_CP_ENABLE] = 1.0f;   // the studio squeeze
            pr.vals[HF_CP_RATIO] = 4.0f; pr.vals[HF_CP_THRESH] = -28.0f;
            pr.vals[HF_CAB_LOWCUT] = 40.0f;
            pr.vals[HF_OUT_LEVEL] = -19.8f;   // rev 125: parity
            std::strncpy(pr.irPath, "@bass115", kPathMax-1); pr.irPath[kPathMax-1] = '\0';
        }
        {   // D: Fuzz Wall — Muff into a CLEAN SVT (fuzz-staging rule).
            Preset& pr = mkb(3, "Fuzz Wall");
            pr.vals[HF_FZ_ENABLE] = 1.0f; pr.vals[HF_FZ_PEDAL] = 0.0f;   // Italian Hero
            pr.vals[HF_FZ_MODE] = 4.0f;   // Red Bear — the Green-Russian-style era
                                          // (fattest lows), THE bass-wall Muff (user 2026-08-28)
            pr.vals[HF_FZ_SUSTAIN] = 0.6f; pr.vals[HF_FZ_TONE] = 0.45f;
            pr.vals[HF_FZ_VOLUME] = 0.32f;   // tame the line-hot output into the amp
            pr.vals[HF_AMP_GAIN] = 0.25f;    // CLEAN amp under fuzz
            pr.vals[HF_AMP_BASS] = 0.5f; pr.vals[HF_AMP_MID] = 0.5f;
            pr.vals[HF_AMP_TREBLE] = 0.45f;
            pr.vals[HF_CAB_LOWCUT] = 40.0f;
            pr.vals[HF_OUT_LEVEL] = -18.7f;   // rev 125: parity
            std::strncpy(pr.irPath, "@bass410h", kPathMax-1); pr.irPath[kPathMax-1] = '\0';
        }
    }
    // BANK 20 (index 19) -- HELSINKI (rev 127, the Darkglass B7K bank): the four
    // "B7K Bass Pack" captures the Helsinki Grind was fitted to (2026-08-28), at
    // their MEASURED knob<->capture map (specESR on the player's DI: Clean 13.3 % /
    // Hard Rock 19.3 % / Heavy 29.3 % / Djent 31.0 %; Tone = the Grunt fat<->tight
    // lever, Mix = Blend), each into a CLEAN Blue Liner -- the pedal makes the
    // tone, the amp is the platform (same staging rule as Fuzz Wall). Bass
    // housekeeping inherited from Round Trip (low_cut 38, room OFF, Room voice);
    // the B7K's own squash replaces the comp. out_levels = golden-domain parity
    // vs the .158 rms anchor (rev 127).
    {
        auto mkh = [&](int slot, const char* name, float drive, float tone, float mix,
                       const char* cab, float out) {
            Preset& pr = p->presets[19][slot];
            pr = p->presets[18][0];            // Round Trip = the clean Blue Liner + 810 platform
            std::snprintf(pr.name, sizeof(pr.name), "%s", name);
            pr.vals[HF_CP_ENABLE] = 0.0f;
            pr.vals[HF_AMP_GAIN]  = 0.30f;     // clean platform under the pedal
            pr.vals[HF_DR_ENABLE] = 1.0f;  pr.vals[HF_DR_MODEL] = 10.0f;   // Helsinki Grind
            pr.vals[HF_DR_DRIVE]  = drive; pr.vals[HF_DR_TONE]  = tone;
            pr.vals[HF_DR_MIX]    = mix;   pr.vals[HF_DR_LEVEL] = 0.6f;
            pr.vals[HF_DR_OCTAVE] = 0.0f;
            pr.vals[HF_OUT_LEVEL] = out;
            std::strncpy(pr.irPath, cab, kPathMax-1); pr.irPath[kPathMax-1] = '\0';
        };
        mkh(0, "Helsinki Clean", 0.25f, 0.30f, 0.50f, "@bass810",  -25.2f);   // the B7K "Clean" capture
        mkh(1, "Helsinki Rock",  0.75f, 0.05f, 0.95f, "@bass810",  -26.5f);   // "Hard Rock" (fat Grunt, near-full Blend)
        mkh(2, "Helsinki Heavy", 1.00f, 0.80f, 0.85f, "@bass410h", -19.3f);   // "Heavy" (thin Grunt -- the tight djent grind)
        mkh(3, "Helsinki Djent", 1.00f, 0.00f, 0.95f, "@bass410h", -20.7f);   // "Djent" (fat Grunt dimed, full Blend)
    }
    // END BANK 20

    // Speaker Drive per-amp defaults (rev 71, item #40, user request): applied AFTER
    // every preset is loaded (Bank 1 + extras), keyed on each preset's OWN amp choice
    // (kAmpMap indices) rather than hand-editing hundreds of raw literals. Only the
    // two categories the user specified are touched -- everything else stays Off,
    // matching how it was already seeded above.
    for (int b = 0; b < kBanks; ++b) {
        for (int s = 0; s < kSlots; ++s) {
            Preset& pr = p->presets[b][s];
            if (!pr.used) continue;
            const int ampModel = static_cast<int>(pr.vals[HF_AMP_MODEL] + 0.5f);
            if (ampModel == 0 || ampModel == 8)
                pr.vals[HF_CAB_SPKDRIVE] = 1.0f;   // Subtle: Fender/Clean Meanie, Vox/Chime Thirty
            else if (ampModel == 1 || ampModel == 2 || ampModel == 6 ||
                     ampModel == 12 || ampModel == 13)
                pr.vals[HF_CAB_SPKDRIVE] = 2.0f;   // Full: JCM800, EVH, Friedman, Recto, MT15
        }
    }

    // Fuzz Guitar Vol = FULL on every preset (rev 73, 2026-07-29 hotfix): the
    // factory tables are older-layout literals, so the new trailing FZ_GVOL
    // port ZERO-FILLS -- and unlike every previous appended port its default
    // is 1.0, not 0. Zero (clamped 0.05) = guitar volume rolled off = the
    // Tone-Bender-backed pedals (I Know It / Tone Bender) go inaudible --
    // user-reported on I Know It. Absolute assignment (not +=) = idempotent;
    // applied to every used preset since the port didn't exist before, so no
    // preset ever chose a value.
    for (int b = 0; b < kBanks; ++b)
        for (int sl = 0; sl < kSlots; ++sl)
            if (p->presets[b][sl].used) p->presets[b][sl].vals[HF_FZ_GVOL] = 1.0f;

    // JCM800 gain bump (rev 72, user request after the 2026-07-28 fuzzy-fix
    // re-voice): the stage-2 duty-collapse cap tamed that stage's drive
    // contribution, so every FACTORY preset on the JCM800 (amp model 1) gets
    // +0.10 gain to land back at its pre-fix drive feel. Scoped to
    // factory-owned slots ONLY (Bank 1 + kFactoryExtra), applied after their
    // tables are re-copied above, so the += is idempotent across future
    // reseeds -- a blanket all-presets loop like Speaker Drive's above would
    // COMPOUND the += on user-saved slots at every future rev bump (Speaker
    // Drive gets away with it because it assigns absolute values).
    {
        auto bump = [&](Preset& pr) {
            if (!pr.used) return;
            if (static_cast<int>(pr.vals[HF_AMP_MODEL] + 0.5f) == 1)
                pr.vals[HF_AMP_GAIN] = std::min(pr.vals[HF_AMP_GAIN] + 0.10f, 1.0f);
        };
        for (int s = 0; s < kSlots; ++s) bump(p->presets[0][s]);
        for (int i = 0; i < kFactoryExtraCount; ++i) {
            const HfFactoryPreset& fp = kFactoryExtra[i];
            if (fp.bank >= 0 && fp.bank < kBanks && fp.slot >= 0 && fp.slot < kSlots)
                bump(p->presets[fp.bank][fp.slot]);
        }
    }

    // Friedman post-PA gain-level tracking parity (rev 130, 2026-09-03): the
    // reference-measured tracking law (trackLo .95/trackHi .12 on the PA row) makes
    // the Friedman gain knob span real loudness; only one preset moved >=0.5
    // dB in the golden domain.
    {
        Preset& pr = p->presets[3][3];
        if (pr.used) pr.vals[HF_OUT_LEVEL] += 0.6f;
    }

    // JCM800 the reference rig re-voice parity (rev 129, 2026-09-02): the 12-take Brit 800
    // 2203-High bake (railed core + open top/bottom) shifted the JCM800 presets
    // in the golden domain; the 5 that moved >=0.5 dB re-leveled to parity
    // (+= after the table re-copy = idempotent across reseeds). Peaks verified
    // <0.8 post-trim. The two USER-PRESERVED SIR #34 presets (Jungle Sleaze /
    // bank-1 Crunch) shifted ~0 dB RMS -> no trim; their upper-mid tone lifts
    // ~1.5 dB (mostly masked by the Greenback cab + 7.5k highcut) -- USER A/B.
    {
        auto trim = [&](int b, int sl, float dB) {
            Preset& pr = p->presets[b][sl];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += dB;
        };
        trim(14, 0, +1.2f);  trim(14, 1, +1.2f);   // Mesa Mark banks' JCM taps
        trim(11, 1, +0.8f);  trim(11, 0, -0.5f);   // Gojira
        trim(6, 2, +0.7f);
    }

    // EVH the reference rig re-voice parity (rev 128, 2026-09-02): the 24-take reference probe
    // bake (railed core + measured voicing, EvhCaptureFit retired) shifted
    // three EVH presets in the golden domain — Flatliner -4.3 dB, Prayer
    // Djent -3.7 dB, Castaway Groove +1.4 dB AND peaking 1.38 (capped like
    // Fridge Grind at rev 125). += after the table re-copy = idempotent
    // across reseeds (the rev-72 JCM pattern).
    {
        auto trim = [&](int b, int sl, float dB) {
            Preset& pr = p->presets[b][sl];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += dB;
        };
        trim(7, 2, +1.9f);   // Flatliner (Periphery djent) — PEAK-CAPPED: full +4.3 parity pushed the golden peak to 1.45 (the railed core is spikier on chugs); +1.9 lands ~1.1 (the rev-125 'target or peak-cap' rule)
        trim(7, 3, +3.7f);   // Prayer Djent
        trim(12, 3, -2.8f);  // Castaway Groove (Gojira Silvera, EVH Red) — peak cap
    }

    // Vox gate floor-compliance round 2 (rev 76, 2026-07-29): the Chime
    // Thirty whine = the gate HYSTERESIS-LATCHED open on the user's real
    // hands-off floor (close = thresh - hyst/2 sat below the post-comb
    // detector floor, so one transient opened it forever and the amp ran the
    // hum partials through +18 dB of bright voicing continuously). Measured:
    // constant -62 dBFS idle output on Regal Sustain (thresh -52). Raise every
    // Vox-model factory preset's threshold so the close point (-48) clears the
    // measured detector floor (~-54) with margin; notes/strums (-6..-25 raw)
    // still open at -40. Absolute clamp = idempotent.
    for (int b = 0; b < kBanks; ++b)
        for (int sl = 0; sl < kSlots; ++sl) {
            Preset& pr = p->presets[b][sl];
            if (!pr.used) continue;
            if (static_cast<int>(pr.vals[HF_AMP_MODEL] + 0.5f) == 8 &&
                pr.vals[HF_GT_THRESH] < -44.0f)
                pr.vals[HF_GT_THRESH] = -44.0f;
        }

    // Bank 1 Crunch = the USER'S on-device rework baked verbatim (rev 75,
    // 2026-07-29; store-diffed against a fresh factory seed on the Pi): the
    // +0.10 bump above still read too tame after the JCM800 re-voice, so they
    // rebuilt the preset — chain reordered to Drive->Amp->Cab->Reverb (comp
    // parked off; fuzz/EQ/mod/delay/wah/oct/nail shifted later), the Green Man
    // engaged as a clean boost (drive .07, level maxed — TS-into-Marshall
    // slam), amp gain .3275->.61 with bass/mid trimmed and master up.
    // Absolute assignments AFTER the JCM800 += bump so the effective values
    // equal their saved store exactly; out_level untouched (theirs matched).
    {
        Preset& pr = p->presets[0][1];
        pr.vals[HF_CP_POS]     = 6.0f;   pr.vals[HF_CP_ENABLE] = 0.0f;
        pr.vals[HF_CP_BYPASS]  = 1.0f;
        pr.vals[HF_FZ_POS]     = 7.0f;
        pr.vals[HF_DR_POS]     = 2.0f;   pr.vals[HF_DR_ENABLE] = 1.0f;
        pr.vals[HF_DR_DRIVE]   = 0.07f;  pr.vals[HF_DR_LEVEL]  = 0.9975f;
        pr.vals[HF_AMP_POS]    = 3.0f;
        pr.vals[HF_AMP_GAIN]   = 0.61f;  pr.vals[HF_AMP_BASS]  = 0.2525f;
        pr.vals[HF_AMP_MID]    = 0.475f; pr.vals[HF_AMP_TREBLE]= 0.67f;
        pr.vals[HF_AMP_MASTER] = 0.6825f;
        pr.vals[HF_CAB_POS]    = 4.0f;   pr.vals[HF_RV_POS]    = 5.0f;
        pr.vals[HF_EQ_POS]     = 8.0f;   pr.vals[HF_MD_POS]    = 9.0f;
        pr.vals[HF_DL_POS]     = 10.0f;  pr.vals[HF_WH_POS]    = 11.0f;
        pr.vals[HF_OC_POS]     = 12.0f;  pr.vals[HF_NAIL_POS]  = 13.0f;
    }
    // USER DIAL-INS BAKED VERBATIM (rev 84, 2026-07-30, store-diffed on-device
    // against a fresh factory seed -- DO NOT RETUNE). The user reworked the
    // rev-83 dual-rig voicings by ear on the device:
    //  - Skye (No Mod): A gain pulled to .8175 (NOTE: the fresh seed measured
    //    0.92 -- the rev-72 JCM800 +0.10 loop stacked on the rev-83 absolute
    //    0.82; this block runs AFTER that loop so the user value is final),
    //    master up .6625; the Orange B becomes a MAJOR voice (blend .57,
    //    +4.14 dB level, tight bass .37, bright, Full spkdrive + dense room).
    //  - Cardinal Rhythm: rb_pol back to NORMAL + blend .3425 / +3.12 dB level
    //    with mid-forward Orange; A-side re-dialed (bass .1925, mid .545,
    //    treble .3975, master .695, gain .15).
    //  - Cardinal Lead: A gain .45, Orange mids .595.
    {
        Preset& sk = p->presets[6][1];   // Skye (No Mod)
        sk.vals[HF_AMP_GAIN] = 0.8175f; sk.vals[HF_AMP_MID] = 0.545f;
        sk.vals[HF_AMP_MASTER] = 0.6625f;
        sk.vals[HF_RB_GAIN] = 0.7325f; sk.vals[HF_RB_BASS] = 0.37f;
        sk.vals[HF_RB_MID] = 0.495f;   sk.vals[HF_RB_TREBLE] = 0.6875f;
        sk.vals[HF_RB_PRESENCE] = 0.5075f;
        sk.vals[HF_RB_BLEND] = 0.57f;  sk.vals[HF_RB_LEVEL] = 4.14f;
        sk.vals[HF_RB_CABROOMDENSE] = 1.0f; sk.vals[HF_RB_CABSPKDRIVE] = 2.0f;
        Preset& cr = p->presets[3][2];   // Cardinal Rhythm
        cr.vals[HF_AMP_GAIN] = 0.15f;   cr.vals[HF_AMP_BASS] = 0.1925f;
        cr.vals[HF_AMP_MID] = 0.545f;   cr.vals[HF_AMP_TREBLE] = 0.3975f;
        cr.vals[HF_AMP_MASTER] = 0.695f;
        cr.vals[HF_RB_GAIN] = 0.68f;    cr.vals[HF_RB_BASS] = 0.44f;
        cr.vals[HF_RB_MID] = 0.70f;     cr.vals[HF_RB_TREBLE] = 0.6125f;
        cr.vals[HF_RB_BLEND] = 0.3425f; cr.vals[HF_RB_LEVEL] = 3.12f;
        cr.vals[HF_RB_POL] = 0.0f;
        Preset& cl = p->presets[3][3];   // Cardinal Lead
        cl.vals[HF_AMP_GAIN] = 0.45f;   cl.vals[HF_RB_MID] = 0.595f;
    }

    // NIN TOPOLOGY UPGRADES (rev 86, 2026-07-30, user: "really nail those NIN
    // presets"). Each closes a DOCUMENTED chain gap (nin-fuzz-eras research):
    //  - March Stabs (TDS): the studio chain was JMP-1 preamp -> Zoom 9030 ->
    //    cab sim. The Nail (Dahnward/Zoom mode) MOVES POST-AMP (amp 5 -> nail 6
    //    -> cab 7), drive eased for the hotter post-amp operating point.
    //  - World Went Away (Fragile): the wall deepens -- Sunn B layer heavier
    //    (blend .34, gain .60) + the Swollen-Pickle-mode Nail drives harder.
    //  - Broken Crush (Broken EP): the 9030's STACKED distortion patch -- a
    //    Grunge DS stage feeds Nail Broke (two chained algorithms, documented).
    //  - Con Molars (With Teeth): Nail's internal cab-sim was DOUBLE-cabbing on
    //    top of the real JCM800+Greenback re-amp; texture -> 0.05 (raw
    //    in-the-box clip), tone up -- the real amp does the documented re-amp.
    // dOut = measured chord-burst parity compensation per preset.
    {
        Preset& ms = p->presets[11][0];   // March Stabs
        ms.vals[HF_NAIL_POS] = 6.0f; ms.vals[HF_CAB_POS] = 7.0f;
        ms.vals[HF_NAIL_DRIVE] = 0.55f; ms.vals[HF_NAIL_LEVEL] = 0.5f;
        ms.vals[HF_OUT_LEVEL] += 1.9f;
        Preset& ww = p->presets[11][1];   // World Went Away
        ww.vals[HF_RB_BLEND] = 0.34f; ww.vals[HF_RB_GAIN] = 0.60f;
        ww.vals[HF_NAIL_DRIVE] = 0.78f;
        ww.vals[HF_OUT_LEVEL] += 0.2f;
        Preset& bc = p->presets[11][2];   // Broken Crush
        bc.vals[HF_DR_ENABLE] = 1.0f; bc.vals[HF_DR_MODEL] = 4.0f;   // Grunge DS
        bc.vals[HF_DR_DRIVE] = 0.45f; bc.vals[HF_DR_TONE] = 0.55f; bc.vals[HF_DR_LEVEL] = 0.5f;
        bc.vals[HF_OUT_LEVEL] += -1.3f;
        Preset& cm = p->presets[11][3];   // Con Molars
        cm.vals[HF_NAIL_TEXTURE] = 0.05f; cm.vals[HF_NAIL_TONE] = 0.60f;

        // Round 2 (rev 87, user ear-pass): March Stabs "weak -- more gain and
        // volume" (hotter amp + harder Zoom stage, +1.5 dB ON TOP of the
        // natural +1.2 the gain adds -- intentionally louder, not parity);
        // Broken Crush "noisey" (gate raised to the measured rig floor: -40
        // closes at -44, above the -45 hands-off peak; DS stage trimmed so it
        // feeds the bitcrusher less hiss -- level-neutral, -0.06 measured);
        // Con Molars "not much lowend, could be gainier" (drive .8, amp gain
        // .58, bass .62, cab lowcut 75). World Went Away = USER-APPROVED
        // ("perfect") -- DO NOT RETUNE.
        ms.vals[HF_AMP_GAIN] = 0.45f; ms.vals[HF_NAIL_DRIVE] = 0.80f;
        ms.vals[HF_NAIL_LEVEL] = 0.55f; ms.vals[HF_OUT_LEVEL] += 1.5f;
        bc.vals[HF_GT_THRESH] = -40.0f; bc.vals[HF_DR_DRIVE] = 0.38f;
        bc.vals[HF_DR_LEVEL] = 0.42f;
        cm.vals[HF_NAIL_DRIVE] = 0.80f; cm.vals[HF_AMP_GAIN] = 0.58f;
        cm.vals[HF_AMP_BASS] = 0.62f; cm.vals[HF_CAB_LOWCUT] = 75.0f;

        // Round 3 (rev 88, user): March Stabs "still needs more volume" -->
        // +2.5 dB more out_level (+4.0 total over the rev-86 parity point).
        // Broken Crush "still too much white noise" --> the hash tamed at the
        // SOURCE: crusher texture .5 -> .30 (less SR-decimation/bit hash),
        // cab-defeat tilt darker (tone .38), cab highcut 9000 -> 7500.
        // Level-neutral (0.00 measured); the Broke-era grit character stays.
        ms.vals[HF_OUT_LEVEL] += 2.5f;
        bc.vals[HF_NAIL_TEXTURE] = 0.30f; bc.vals[HF_NAIL_TONE] = 0.38f;
        bc.vals[HF_CAB_HIGHCUT] = 7500.0f;

        // Round 4 (rev 89, user: "tweak the eqs... move it around if it sounds
        // better or is more accurate; con molars is still missing something"):
        //  - March Stabs: the -6/-4/-6 low scoop was part of the "weak" -- eased
        //    to -3/-2/-2 with the stab bite kept (+4 @1.6k); nets +1.0 dB after
        //    a -2.4 comp (the user asked for volume twice; some of the EQ's
        //    loudness gain is kept deliberately).
        //  - Broken Crush: the old +3 @3.2k was RE-boosting the very hiss band
        //    the rev-88 fix tamed -- now 0; aggression moved into Wish-era mids
        //    (+1 @800 / +3 @1.6k). Fully re-leveled (-1.3).
        //  - Con Molars: THE MISSING PIECE = the mode's internal +5 dB @1.9k
        //    mid-bite, lost when its double cab-sim was zeroed (rev 86).
        //    Restored in-chain (+4 @1.6k, +2 @800) and the EQ MOVES PRE-AMP
        //    (nail 4 -> eq 5 -> amp 6 -> cab 7): it now shapes the digital clip
        //    INTO the JCM800, the documented Reaktor-then-reamp order -- which
        //    also adds the wanted grind in exactly that band. Nets +0.3 dB.
        ms.vals[HF_EQ_100] = -3; ms.vals[HF_EQ_200] = -2; ms.vals[HF_EQ_400] = -2;
        ms.vals[HF_EQ_800] = 1;  ms.vals[HF_EQ_1K6] = 4; ms.vals[HF_EQ_3K2] = 2;
        ms.vals[HF_OUT_LEVEL] += -2.4f;
        bc.vals[HF_EQ_100] = -3; bc.vals[HF_EQ_400] = -2; bc.vals[HF_EQ_800] = 1;
        bc.vals[HF_EQ_1K6] = 3;  bc.vals[HF_EQ_3K2] = 0;
        bc.vals[HF_OUT_LEVEL] += -1.3f;
        cm.vals[HF_EQ_POS] = 5.0f; cm.vals[HF_AMP_POS] = 6.0f; cm.vals[HF_CAB_POS] = 7.0f;
        cm.vals[HF_EQ_100] = -2; cm.vals[HF_EQ_200] = 0; cm.vals[HF_EQ_400] = 0;
        cm.vals[HF_EQ_800] = 2;  cm.vals[HF_EQ_1K6] = 4; cm.vals[HF_EQ_3K2] = 1;
        cm.vals[HF_OUT_LEVEL] += -0.5f;

        // USER FINAL (rev 90, 2026-07-30, store-diffed on-device -- DO NOT
        // RETUNE): the user's full creative reworks of all three, baked
        // verbatim as ABSOLUTE values (overrides every earlier round):
        //  - March Stabs: DIRECT/no-cab (TDS console sound), octave-up layer
        //    at pos 2, Nail back pre-amp, EQ pre-amp, inverted Clean Meanie
        //    B rig (+4 dB, blend .24), hot master, out -9.6.
        //  - Broken Crush: amp -> Diamond Plate, 76 ms tape slap, octave-up,
        //    Nail eased way down, Mid-Boost-based EQ at pos 8, Cali V B rig
        //    (inverted, cab2 on), and a MID-CHAIN Gate 2 at pos 7 (thresh
        //    -39.8) cleaning up after the crushers; cab -> @greenback.
        //  - Con Molars: starved Fuzz Zachary front end (bias .09, sustain
        //    .24), Nail drive .13 with texture back up, amp gain .28 -- the
        //    whole chain re-balanced around the FF spit.
        {
        ms.vals[HF_OUT_LEVEL] = -7.8f; ms.vals[HF_CP_POS] = 6.0f;
        ms.vals[HF_FZ_POS] = 7.0f; ms.vals[HF_DR_POS] = 8.0f;
        ms.vals[HF_AMP_POS] = 4.0f; ms.vals[HF_AMP_GAIN] = 0.4825f;
        ms.vals[HF_AMP_BASS] = 0.6625f; ms.vals[HF_AMP_MID] = 0.595f;
        ms.vals[HF_AMP_TREBLE] = 0.4975f; ms.vals[HF_AMP_PRESENCE] = 0.2f;
        ms.vals[HF_AMP_MASTER] = 0.765f; ms.vals[HF_CAB_POS] = 9.0f;
        ms.vals[HF_CAB_ENABLE] = 0.0f; ms.vals[HF_MD_POS] = 10.0f;
        ms.vals[HF_DL_POS] = 11.0f; ms.vals[HF_RV_POS] = 12.0f;
        ms.vals[HF_WH_POS] = 13.0f; ms.vals[HF_OC_POS] = 2.0f;
        ms.vals[HF_OC_ENABLE] = 1.0f; ms.vals[HF_OC_UP] = 0.425f;
        ms.vals[HF_OC_DOWN] = 0.1075f; ms.vals[HF_NAIL_POS] = 3.0f;
        ms.vals[HF_NAIL_DRIVE] = 0.66f; ms.vals[HF_NAIL_TONE] = 0.41f;
        ms.vals[HF_NAIL_TEXTURE] = 0.375f; ms.vals[HF_NAIL_LEVEL] = 0.51f;
        ms.vals[HF_EQ_POS] = 5.0f; ms.vals[HF_RB_ENABLE] = 1.0f;
        ms.vals[HF_RB_AMP] = 0.0f; ms.vals[HF_RB_GAIN] = 0.7325f;
        ms.vals[HF_RB_BASS] = 0.3675f; ms.vals[HF_RB_MID] = 0.5875f;
        ms.vals[HF_RB_TREBLE] = 0.625f; ms.vals[HF_RB_BLEND] = 0.2375f;
        ms.vals[HF_RB_LEVEL] = 4.02f; ms.vals[HF_RB_POL] = 1.0f;
        bc.vals[HF_OUT_LEVEL] = -16.8f; bc.vals[HF_CP_POS] = 10.0f;
        bc.vals[HF_FZ_POS] = 11.0f; bc.vals[HF_DR_POS] = 3.0f;
        bc.vals[HF_DR_DRIVE] = 0.1975f; bc.vals[HF_DR_TONE] = 0.7225f;
        bc.vals[HF_DR_LEVEL] = 0.4075f; bc.vals[HF_AMP_MODEL] = 12.0f;
        bc.vals[HF_AMP_GAIN] = 0.2875f; bc.vals[HF_AMP_BASS] = 0.265f;
        bc.vals[HF_AMP_MID] = 0.5975f; bc.vals[HF_AMP_TREBLE] = 0.63f;
        bc.vals[HF_AMP_MASTER] = 0.715f; bc.vals[HF_MD_POS] = 12.0f;
        bc.vals[HF_DL_POS] = 9.0f; bc.vals[HF_DL_ENABLE] = 1.0f;
        bc.vals[HF_DL_TYPE] = 1.0f; bc.vals[HF_DL_TIME] = 75.9625f;
        bc.vals[HF_DL_FEEDBACK] = 0.1764f; bc.vals[HF_DL_MIX] = 0.265f;
        bc.vals[HF_DL_WIDTH] = 0.1325f; bc.vals[HF_RV_POS] = 13.0f;
        bc.vals[HF_WH_POS] = 14.0f; bc.vals[HF_OC_POS] = 2.0f;
        bc.vals[HF_OC_ENABLE] = 1.0f; bc.vals[HF_OC_UP] = 0.4875f;
        bc.vals[HF_OC_DOWN] = 0.0225f; bc.vals[HF_AMP_FR_CHANNEL] = 2.0f;
        bc.vals[HF_NAIL_DRIVE] = 0.22f; bc.vals[HF_NAIL_TONE] = 0.6825f;
        bc.vals[HF_NAIL_TEXTURE] = 0.33f; bc.vals[HF_NAIL_LEVEL] = 0.38f;
        bc.vals[HF_EQ_POS] = 8.0f; bc.vals[HF_EQ_PRESET] = 4.0f;
        bc.vals[HF_EQ_100] = 4.0f; bc.vals[HF_EQ_200] = -2.0f;
        bc.vals[HF_EQ_400] = -5.0f; bc.vals[HF_EQ_800] = -2.0f;
        bc.vals[HF_EQ_1K6] = 2.0f; bc.vals[HF_EQ_3K2] = 4.0f;
        bc.vals[HF_EQ_LEVEL] = 0.66f; bc.vals[HF_DR2_POS] = 15.0f;
        bc.vals[HF_RB_ENABLE] = 1.0f; bc.vals[HF_RB_AMP] = 11.0f;
        bc.vals[HF_RB_GAIN] = 0.32f; bc.vals[HF_RB_BASS] = 0.3125f;
        bc.vals[HF_RB_BLEND] = 0.275f; bc.vals[HF_RB_LEVEL] = 1.2f;
        bc.vals[HF_RB_POL] = 1.0f; bc.vals[HF_RB_MV_MODE] = 7.0f;
        bc.vals[HF_RB_CAB2ON] = 1.0f; bc.vals[HF_GT2_POS] = 7.0f;
        bc.vals[HF_GT2_ENABLE] = 1.0f; bc.vals[HF_GT2_THRESH] = -39.8f;
        bc.vals[HF_GT2_ATTACK] = 0.1f; bc.vals[HF_GT2_RELEASE] = 328.4f;
        cm.vals[HF_OUT_LEVEL] = -16.8f; cm.vals[HF_CP_POS] = 7.0f;
        cm.vals[HF_FZ_POS] = 2.0f; cm.vals[HF_FZ_ENABLE] = 1.0f;
        cm.vals[HF_FZ_PEDAL] = 3.0f; cm.vals[HF_FZ_SUSTAIN] = 0.235f;
        cm.vals[HF_FZ_BIAS] = 0.0875f; cm.vals[HF_FZ_INPUTTRIM] = 0.2925f;
        cm.vals[HF_FZ_GETEMP] = 0.185f; cm.vals[HF_DR_POS] = 8.0f;
        cm.vals[HF_AMP_POS] = 5.0f; cm.vals[HF_AMP_GAIN] = 0.2775f;
        cm.vals[HF_AMP_BASS] = 0.515f; cm.vals[HF_CAB_POS] = 6.0f;
        cm.vals[HF_MD_POS] = 9.0f; cm.vals[HF_DL_POS] = 10.0f;
        cm.vals[HF_RV_POS] = 11.0f; cm.vals[HF_WH_POS] = 12.0f;
        cm.vals[HF_OC_POS] = 13.0f; cm.vals[HF_NAIL_POS] = 3.0f;
        cm.vals[HF_NAIL_DRIVE] = 0.13f; cm.vals[HF_NAIL_TONE] = 0.7225f;
        cm.vals[HF_NAIL_TEXTURE] = 0.5675f; cm.vals[HF_EQ_POS] = 4.0f;
            std::strncpy(bc.irPath, "@greenback", kPathMax-1); bc.irPath[kPathMax-1]='\0';
        }
    }

    // JCM800 stage-2 cap re-level (rev 85, 2026-07-30 evens session): the cap
    // 2.0 -> 1.4 fix (h2@223 landing on both knob-documented captures) costs
    // 0.4-0.9 dB on SOME JCM800 presets -- measured per preset at both caps
    // (chord-burst RMS); only >0.3 dB shifts compensated. The immune six
    // (Skye No Mod, WWA, Con Molars, March Stabs, PIJ, KOF) have dual-rig or
    // fuzz-dominated chains that mask the stage-2 contribution.
    {
        struct Lvl { int bank, slot; float d; };
        static const Lvl kJcmRelevel[] = {
            {0,1,+0.6f},   // Crunch
            {6,0,+0.6f},   // Skye Crusher
            {6,2,+0.7f},   // Skye Soar
            {8,0,+0.4f},   // Bridge Vibe
            {12,0,+0.8f},  // Quarter-Tone Lead
            {13,3,+0.9f},  // Grunge Drop
        };
        for (const Lvl& lv : kJcmRelevel) {
            Preset& pr = p->presets[lv.bank][lv.slot];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += lv.d;
        }
    }

    // ── padrive main-rig fix re-level (2026-07-31): applying the per-amp PA
    // drive to the main rig (was Rig-B-only) un-over-drives EVH/Rockerverb/Vox,
    // which drops their level. Measured on-device before/after; the delta is
    // baked back so loudness parity holds -- the LESS-saturated tone (EVH swell
    // back, less fizz) is the intended change. 0/3 Lead measured 0 (skipped).
    {
        struct Lvl { int bank, slot; float d; };
        static const Lvl kPadriveRelevel[] = {
            {0,2,+4.3f},  {1,0,+1.4f},  {1,3,+3.5f},  {2,0,+1.4f},  {2,1,+0.3f},
            {2,2,+0.3f},  {2,3,+0.3f},  {5,3,+0.3f},  {7,2,+2.0f},  {7,3,+1.8f},
            {9,1,+4.4f},  {10,0,+2.2f}, {10,1,+0.5f}, {10,2,+2.8f}, {10,3,+1.5f},
            {12,3,+1.9f}, {16,0,+3.5f}, {16,1,+3.2f},
        };
        for (const Lvl& lv : kPadriveRelevel) {
            Preset& pr = p->presets[lv.bank][lv.slot];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += lv.d;
        }
    }

    // ── USER bank-0 reworks (rev 103, 2026-07-31, store-diffed on-device --
    // DO NOT RETUNE): the user re-dialed the stock Clean/Crunch/Rhythm/Lead
    // AFTER the padrive main-rig fix (a strong accept signal -- Rhythm is an
    // EVH preset, rebuilt around the corrected, un-over-driven EVH):
    //  - Clean: more amp gain (.68) + a touch more drive level.
    //  - Crunch: SIR #34 mod ON + more bass -- the new JCM800 hot-rod.
    //  - Rhythm: full rebuild -- chain reordered (Drive->Amp->Cab->Reverb),
    //    comp parked+bypassed, gain .665 / master .5775 (much hotter power
    //    section into the corrected EVH), mids/treble eased, input phase
    //    normal. out_level kept at the padrive-releveled value (user didn't
    //    touch it).
    //  - Lead: input phase normal.
    {
        Preset& cl = p->presets[0][0];
        cl.vals[HF_DR_LEVEL] = 0.5925f; cl.vals[HF_AMP_GAIN] = 0.6825f;
        Preset& cr = p->presets[0][1];
        cr.vals[HF_AMP_BASS] = 0.405f;  cr.vals[HF_AMP_SIR34] = 1.0f;
        Preset& rh = p->presets[0][2];
        rh.vals[HF_IT_PHASE] = 0.0f;    rh.vals[HF_CP_POS] = 6.0f;
        rh.vals[HF_CP_ENABLE] = 0.0f;   rh.vals[HF_FZ_POS] = 7.0f;
        rh.vals[HF_DR_POS] = 2.0f;      rh.vals[HF_DR_DRIVE] = 0.04f;
        rh.vals[HF_AMP_POS] = 3.0f;     rh.vals[HF_AMP_GAIN] = 0.665f;
        rh.vals[HF_AMP_BASS] = 0.52f;   rh.vals[HF_AMP_MID] = 0.6225f;
        rh.vals[HF_AMP_TREBLE] = 0.65f; rh.vals[HF_AMP_MASTER] = 0.5775f;
        rh.vals[HF_CAB_POS] = 4.0f;     rh.vals[HF_MD_POS] = 9.0f;
        rh.vals[HF_DL_POS] = 10.0f;     rh.vals[HF_RV_POS] = 5.0f;
        rh.vals[HF_WH_POS] = 11.0f;     rh.vals[HF_OC_POS] = 12.0f;
        rh.vals[HF_CP_BYPASS] = 1.0f;   rh.vals[HF_NAIL_POS] = 13.0f;
        rh.vals[HF_EQ_POS] = 8.0f;
        Preset& ld = p->presets[0][3];
        ld.vals[HF_IT_PHASE] = 0.0f;
    }

        // Periphery + Gojira EVH presets REWORKED to specific songs on the new EVH
        // (rev 105, 2026-08-03, user request). All EVH Red + Green Man (TS) tight boost.
        // First pass -- user refines by ear on-device; out_level LEFT for re-measure.
        //   Flatliner       -> Periphery "Flatline"  (II-era: bright, tight, articulate)
        //   Prayer Djent    -> Periphery "Prayer Position" (Hail Stan: tighter/modern/hot)
        //   Castaway Groove -> Gojira "Silvera" (Magma: mid-forward, thick, percussive)
        // Both Periphery stay RHYTHM (kept their parallel clean blend); Gojira darker/thicker.
        {
            // (rev 106, 2026-08-03) hiss tame: the bright/hot rework amplified the Red
            // channel's HF input-noise into audible hiss (measured -32 dBFS 6-8k band,
            // evh_hiss). Pulled treble/presence/TS-tone down + cab highcut way in (7.8k->
            // 6.3k) -- kills the >6k hiss while keeping the 2-5k djent chug/bite.
            Preset& fl = p->presets[7][2];   // Flatliner -> "Flatline"
            fl.vals[HF_GT_THRESH] = -40.0f; fl.vals[HF_GT_RELEASE] = 150.0f;
            fl.vals[HF_DR_DRIVE] = 0.0f;   fl.vals[HF_DR_TONE] = 0.66f; fl.vals[HF_DR_LEVEL] = 0.72f;
            fl.vals[HF_AMP_GAIN] = 0.62f;  fl.vals[HF_AMP_BASS] = 0.28f; fl.vals[HF_AMP_MID] = 0.60f;
            fl.vals[HF_AMP_TREBLE] = 0.60f; fl.vals[HF_AMP_PRESENCE] = 0.50f; fl.vals[HF_AMP_MASTER] = 0.42f;
            fl.vals[HF_AMP_SAG] = 0.22f;   fl.vals[HF_CAB_LOWCUT] = 95.0f; fl.vals[HF_CAB_HIGHCUT] = 6300.0f;
            fl.vals[HF_RB_BLEND] = 0.18f;

            Preset& pd = p->presets[7][3];   // Prayer Djent -> "Prayer Position"
            pd.vals[HF_GT_THRESH] = -40.0f; pd.vals[HF_GT_RELEASE] = 140.0f;
            pd.vals[HF_DR_DRIVE] = 0.05f;  pd.vals[HF_DR_TONE] = 0.66f; pd.vals[HF_DR_LEVEL] = 0.74f;
            pd.vals[HF_AMP_GAIN] = 0.62f;  pd.vals[HF_AMP_BASS] = 0.26f; pd.vals[HF_AMP_MID] = 0.63f;
            pd.vals[HF_AMP_TREBLE] = 0.58f; pd.vals[HF_AMP_PRESENCE] = 0.48f; pd.vals[HF_AMP_MASTER] = 0.44f;
            pd.vals[HF_AMP_SAG] = 0.20f;   pd.vals[HF_CAB_LOWCUT] = 100.0f; pd.vals[HF_CAB_HIGHCUT] = 6300.0f;
            pd.vals[HF_RB_BLEND] = 0.22f;

            Preset& cg = p->presets[12][3];  // Castaway Groove -> Gojira "Silvera"
            cg.vals[HF_GT_THRESH] = -42.0f; cg.vals[HF_GT_RELEASE] = 150.0f;
            cg.vals[HF_DR_DRIVE] = 0.05f;  cg.vals[HF_DR_TONE] = 0.52f; cg.vals[HF_DR_LEVEL] = 0.95f;
            cg.vals[HF_AMP_GAIN] = 0.56f;  cg.vals[HF_AMP_BASS] = 0.42f; cg.vals[HF_AMP_MID] = 0.66f;
            cg.vals[HF_AMP_TREBLE] = 0.55f; cg.vals[HF_AMP_PRESENCE] = 0.48f; cg.vals[HF_AMP_MASTER] = 0.72f;
            cg.vals[HF_AMP_SAG] = 0.22f;   cg.vals[HF_AMP_RESONANCE] = 0.35f;
            cg.vals[HF_CAB_LOWCUT] = 90.0f; cg.vals[HF_CAB_HIGHCUT] = 7000.0f;
        }
        // Brown Sound '84 = USER's on-device dial-in on the OVERVOLT variac,
        // baked VERBATIM from the store (store-diffed 2026-08-03; amp pulled to a
        // real setting -- gain .655/bass .5/mid .65/treble .705/pres .63/master
        // .85/sag .75/VolII .30, IT boost OFF, EP-3 clean (drive 0), EQ off, more
        // tape echo). Placed LAST so it wins over the speaker-drive loop. DO NOT RETUNE.
        {
            static const float kBS84[HF_N_PORTS] = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -19.200001f,
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 1.0f, -48.0f, 0.1f, 140.0f, 268.700012f,
        6.0f, 9.0f, 0.0f, 0.0f, -20.0f, 1.0f, 5.0f, 5.0f,
        3.0f, 0.0f, 10.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f,
        0.65f, 0.5f, 0.5f, 0.4f, 3.0f, 1.0f, 8.0f, 0.0f,
        0.5025f, 0.5525f, 1.0f, 0.3f, 4.0f, 1.0f, 10.0f, 0.655f,
        0.5f, 0.6525f, 0.705f, 0.6275f, 0.8475f, 0.7475f, 0.0f, 0.0f,
        0.5f, 0.0f, 0.0f, 1.0f, 0.55f, 0.18f, 0.33f, 0.62f,
        0.42f, 0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 5.0f, 1.0f, 85.0f, 8000.0f, 1.0f, 2.0f, 1.0f,
        8.0f, 0.205f, 0.35f, 0.5025f, 0.5f, 6.0f, 1.0f, 4.0f,
        235.882507f, 0.0196f, 0.1775f, 0.5f, 0.003f, 0.001f, 10.0f, 1.0f,
        0.0f, 0.0f, 0.3f, 7.0f, 1.0f, 15.0f, 1.601f, 0.3f,
        0.0f, 0.022475f, 0.14f, 11.0f, 0.0f, 0.0f, 0.4f, 0.7f,
        0.5f, 0.6f, 0.8f, 12.0f, 0.0f, 0.0f, 0.5f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 13.0f,
        0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 0.0f,
        5.0f, 0.0f, 2.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.3f, 0.360628f, 0.090113f, 1.0f, 0.12f,
        0.35f, 7.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,
        8.0f, 0.0f, 5.0f, -1.0f, 0.0f, 1.0f, 3.0f, 4.0f,
        5.0f, 0.12f, 0.0f, 0.08f, 0.3f, 1.0f, 0.0f, 1.0f,
        0.5f, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 14.0f, 0.0f,
        0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 10.0f, 0.7525f, 0.985f, 1.0f, 1.0f, 1.0f, 0.7f,
        0.5025f, 0.0f, 0.0f, 0.0f, 80.0f, 16000.0f, 0.5075f, 1.02f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 7.0f, 0.0f, 0.0f, 2.0f,
        0.0f, 0.1175f, 0.0f, 1.0f, 1.0f, 0.55f, 0.18f, 0.33f,
        0.62f, 0.42f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.12f, 0.35f, 1.0f, 0.0f, 2.0f, 0.0f, 15.0f,
        0.0f, -60.0f, 2.0f, 120.0f, 250.0f, 8.0f, 0.0f, 16.0f,
        0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f, 3.0f, 0.0f,
        0.0f, 17.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f, 0.65f,
        0.5f, 0.5f, 0.4f, 1.0f, 0.0f, 18.0f, 0.0f, 2.0f,
        0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 19.0f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 20.0f, 0.0f, 0.0f, 250.0f, 0.4f, 0.15f, 0.5f,
        0.003f, 0.001f, 10.0f, 1.0f, 0.0f, 0.0f, 0.3f, 0.0f,
        5.0f, 0.0f, 21.0f, 0.0f, 10.0f, 1.5f, 0.3f, 0.0f,
        0.8f, 0.15f, 0.0f, 0.0f, 0.5f, 0.0f, 22.0f, 0.0f,
        0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f, 0.0f, 23.0f,
        0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 24.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 1.0f, 0.15f, 0.35f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f,
            };
            Preset& bs = p->presets[17][1];
            std::memcpy(bs.vals, kBS84, sizeof(bs.vals));
            std::strncpy(bs.irPath,  "@greenback", kPathMax-1); bs.irPath[kPathMax-1]='\0';
            std::strncpy(bs.ir2Path, "@greenback", kPathMax-1); bs.ir2Path[kPathMax-1]='\0';
            bs.ampNamPath[0]=bs.drNamPath[0]=bs.cabNamPath[0]=bs.amp2NamPath[0]=bs.dr2NamPath[0]='\0';
        }
        // Berlin Wall Pulse (b4/1) = user's Gilmour "Wall/Pulse" preset baked VERBATIM
        // from the store, with the DRIVE FIXED: New Dawn (Life Pedal) is not a Gilmour
        // pedal -> Dear Rodent Boy (ProCo RAT), his actual OD. Preserves the user's preset
        // (name + Hiwatt + ambience) across the reseed. (rev 106, 2026-08-03, user.)
        {
            static const float kBWP[HF_N_PORTS] = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -20.299999f,
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 4.0f, 1.0f, 1.0f, -60.0f, 2.0f, 120.0f, 250.0f,
        8.0f, 2.0f, 1.0f, 0.0f, -20.0f, 1.0f, 5.0f, 5.0f,
        3.0f, 1.0f, 3.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f,
        0.65f, 0.5f, 0.5f, 0.4f, 4.0f, 1.0f, 1.0f, 0.15f,
        0.6f, 0.55f, 1.0f, 0.3f, 5.0f, 1.0f, 7.0f, 0.2f,
        0.5f, 0.45f, 0.6f, 0.6f, 0.7f, 0.25f, 0.0f, 0.0f,
        0.5f, 0.0f, 0.0f, 1.0f, 0.55f, 0.18f, 0.33f, 0.62f,
        0.42f, 0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 6.0f, 1.0f, 85.0f, 11000.0f, 1.0f, 7.0f, 1.0f,
        3.0f, 0.12f, 0.4f, 0.4f, 0.4f, 8.0f, 1.0f, 3.0f,
        380.0f, 0.5f, 0.4f, 0.4f, 0.003f, 0.001f, 10.0f, 1.0f,
        0.1f, 0.05f, 0.2f, 9.0f, 1.0f, 10.0f, 1.4f, 0.3f,
        0.0f, 0.8f, 0.1f, 10.0f, 0.0f, 0.0f, 0.4f, 0.7f,
        0.5f, 0.6f, 0.8f, 11.0f, 0.0f, 0.0f, 0.5f, 1.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 12.0f,
        0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 0.0f,
        5.0f, 0.0f, 2.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.15f, 1.0f, 0.12f,
        0.35f, 7.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,
        6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.1f, 0.25f, 1.0f, 0.0f, 1.0f,
        0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 14.0f, 0.0f,
        0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.7f,
        0.3f, 0.0f, 0.0f, 0.0f, 80.0f, 16000.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 7.0f, 0.0f, 0.0f, 2.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.55f, 0.18f, 0.33f,
        0.62f, 0.42f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.12f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 15.0f,
        0.0f, -60.0f, 2.0f, 120.0f, 250.0f, 8.0f, 0.0f, 16.0f,
        0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f, 3.0f, 0.0f,
        0.0f, 17.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f, 0.65f,
        0.5f, 0.5f, 0.4f, 1.0f, 0.0f, 18.0f, 0.0f, 2.0f,
        0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 19.0f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 20.0f, 0.0f, 0.0f, 250.0f, 0.4f, 0.15f, 0.5f,
        0.003f, 0.001f, 10.0f, 1.0f, 0.0f, 0.0f, 0.3f, 0.0f,
        5.0f, 0.0f, 21.0f, 0.0f, 10.0f, 1.5f, 0.3f, 0.0f,
        0.8f, 0.15f, 0.0f, 0.0f, 0.5f, 0.0f, 22.0f, 0.0f,
        0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f, 0.0f, 23.0f,
        0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 24.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.35f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f,
            };
            Preset& bwp = p->presets[4][1];
            std::memcpy(bwp.vals, kBWP, sizeof(bwp.vals));
            bwp.used = true;
            std::snprintf(bwp.name, sizeof(bwp.name), "Berlin Wall Pulse");
            bwp.vals[HF_DR_MODEL] = 2.0f;   // New Dawn (Life Pedal, 1) -> Dear Rodent Boy / RAT (2)
            bwp.vals[HF_DR_DRIVE] = 0.35f; bwp.vals[HF_DR_TONE] = 0.45f; bwp.vals[HF_DR_LEVEL] = 0.60f;
            std::strncpy(bwp.irPath, "@hiwatt", kPathMax-1); bwp.irPath[kPathMax-1]='\0';
            bwp.ampNamPath[0]=bwp.drNamPath[0]=bwp.cabNamPath[0]=bwp.amp2NamPath[0]=bwp.ir2Path[0]=bwp.dr2NamPath[0]='\0';
        }
        // Solar Monolith (b6/3, was "Wizard's Doom") -> SUNN O))) drone doom (rev 107,
        // 2026-08-03, user). New Dawn (the EQD Life Pedal -- literally built with Sunn O)))
        // for this sound) into the cranked Doom Daddy (Sunn Model T, series-linked) = a
        // massive dark saturated monolith. Fuzz OUT / drive IN (pos 4, already pre-amp),
        // huge sub, dark top, deep sag, Full speaker drive, Hex Ambient bloom wash, gate
        // opened so it drones. out_level LEFT for on-device re-measure.
        {
            // Solar Monolith = USER's on-device dial-in baked VERBATIM (store-diffed rev
            // 108, 2026-08-03): less drive (.14) but more octave (.43), brighter (tone .62/
            // highcut 8030), PARALLEL link, Sunn Vol II pushed .82, gate -43. DO NOT RETUNE.
            static const float kSM[HF_N_PORTS] = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -22.700001f,
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 4.0f, 1.0f, 1.0f, -43.400002f, 3.0f, 200.0f, 400.0f,
        8.0f, 2.0f, 0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f,
        3.0f, 0.0f, 3.0f, 0.0f, 0.0f, 4.0f, 0.62f, 0.38f,
        0.4f, 0.5f, 0.5f, 0.4f, 4.0f, 1.0f, 1.0f, 0.1375f,
        0.625f, 0.6725f, 1.0f, 0.425f, 5.0f, 1.0f, 3.0f, 0.68f,
        0.775f, 0.45f, 0.32f, 0.32f, 0.78f, 0.62f, 0.0f, 0.0f,
        0.8175f, 1.0f, 0.0f, 1.0f, 0.55f, 0.18f, 0.33f, 0.62f,
        0.42f, 0.5f, 0.0f, 1.0f, 0.825f, 0.3975f, 0.33f, 1.0f,
        1.0f, 6.0f, 1.0f, 45.0f, 8030.0f, 1.0f, 7.0f, 0.0f,
        0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 8.0f, 0.0f, 0.0f,
        250.0f, 0.4f, 0.15f, 0.5f, 0.003f, 0.001f, 10.0f, 1.0f,
        0.0f, 0.0f, 0.3f, 9.0f, 1.0f, 20.0f, 4.0f, 0.4f,
        0.0f, 0.8f, 0.32f, 10.0f, 0.0f, 0.0f, 0.4f, 0.7f,
        0.5f, 0.6f, 0.8f, 11.0f, 0.0f, 0.0f, 0.5f, 1.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 12.0f,
        0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 0.0f,
        5.0f, 0.0f, 2.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.374779f, 0.133917f, 1.0f, 0.12f,
        0.35f, 7.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,
        6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 1.0f, 2.0f, 1.0f,
        0.85f, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 14.0f, 0.0f,
        0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 10.0f, 0.42f, 0.605f, 0.5f, 0.5f, 0.5f, 0.7f,
        0.3f, 0.0f, 0.0f, 0.0f, 80.0f, 8390.0f, 0.2475f, 0.0f,
        1.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 6.0f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 7.0f, 0.0f, 0.0f, 2.0f,
        0.0f, 0.495f, 0.0f, 1.0f, 1.0f, 0.55f, 0.18f, 0.33f,
        0.62f, 0.42f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.152691f,
        1.0f, 0.12f, 0.35f, 1.0f, 0.0f, 2.0f, 1.0f, 15.0f,
        0.0f, -60.0f, 2.0f, 120.0f, 250.0f, 8.0f, 0.0f, 16.0f,
        0.0f, 0.0f, -18.0f, 1.0f, 5.0f, 5.0f, 3.0f, 0.0f,
        0.0f, 17.0f, 0.0f, 0.0f, 2.0f, 0.55f, 0.5f, 0.65f,
        0.5f, 0.5f, 0.4f, 1.0f, 0.0f, 18.0f, 0.0f, 2.0f,
        0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 19.0f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 20.0f, 0.0f, 0.0f, 250.0f, 0.4f, 0.15f, 0.5f,
        0.003f, 0.001f, 10.0f, 1.0f, 0.0f, 0.0f, 0.3f, 0.0f,
        5.0f, 0.0f, 21.0f, 0.0f, 10.0f, 1.5f, 0.3f, 0.0f,
        0.8f, 0.15f, 0.0f, 0.0f, 0.5f, 0.0f, 22.0f, 0.0f,
        0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f, 0.0f, 23.0f,
        0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 24.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.35f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f,
            };
            Preset& sm = p->presets[6][3];
            std::memcpy(sm.vals, kSM, sizeof(sm.vals));
            std::snprintf(sm.name, sizeof(sm.name), "Solar Monolith");
            std::strncpy(sm.irPath,  "@doom", kPathMax-1); sm.irPath[kPathMax-1]='\0';
            std::strncpy(sm.ir2Path, "@doom", kPathMax-1); sm.ir2Path[kPathMax-1]='\0';
            sm.ampNamPath[0]=sm.drNamPath[0]=sm.cabNamPath[0]=sm.amp2NamPath[0]=sm.dr2NamPath[0]='\0';

            // Regal Sustain (b1/0, Brian May / Vox "Chime Thirty") -- little tuning after the
            // Vox re-voice: tame the top (treble/presence/TS-tone), push the violin mids, a
            // touch more air. Treble booster (IT +6) + cranked AC30 + singing sag kept.
            Preset& rsus = p->presets[1][0];
            rsus.vals[HF_AMP_TREBLE] = 0.72f; rsus.vals[HF_AMP_MID] = 0.68f; rsus.vals[HF_AMP_PRESENCE] = 0.58f;
            rsus.vals[HF_DR_TONE] = 0.70f;    rsus.vals[HF_RV_MIX] = 0.16f;

            // Regal Solo (b10/3, Brian May lead) -- same top tame + violin mids; the 800 ms
            // Brighton-Rock delay upgraded to the EP-3 tape echo (warmer), a bit more space.
            Preset& rsol = p->presets[10][3];
            rsol.vals[HF_AMP_TREBLE] = 0.72f; rsol.vals[HF_AMP_MID] = 0.68f; rsol.vals[HF_AMP_PRESENCE] = 0.56f;
            rsol.vals[HF_DR_TONE] = 0.70f;    rsol.vals[HF_DL_TYPE] = 4.0f; rsol.vals[HF_DL_AGE] = 0.20f;
            rsol.vals[HF_DL_MIX] = 0.22f;     rsol.vals[HF_RV_MIX] = 0.22f;
        }

    // ── Green Man push restore (rev 114, 2026-08-21/22): the rev-109 pass
    // compensated OUTPUT loudness, but the gain-floor fix also boosted the
    // push INTO the amps for presets dialed around the old near-unity pedal
    // (user: "the TS is pushing too hard"). Both level laws are linear in the
    // knob, so scaling dr_level restores each preset's ORIGINAL amp push
    // exactly (factor ~0.54 for near-zero drives, ~0.8 at drive ~0.2); the
    // rev-109 out_level deltas are dropped with it (loudness follows push).
    // The pedal itself keeps the correct boost law for live dialing.
    {
        struct Ts { int bank, slot; float lvl; };
        static const Ts kTsPushRestore[] = {
            {0,0,0.47f},   // Clean       (drive .19: L .59 x0.80)
            {0,1,0.55f},   // Crunch      (drive .07: L 1.00 x0.55)
            {0,2,0.55f},   // Rhythm      (drive .04: L 1.00 x0.55)
            {0,3,0.40f},   // Lead        (drive .06: L .72 x0.55)
            {6,1,0.44f},   // Skye (No Mod) (drive .05: L .80 x0.55)
            {7,2,0.39f},   // Flatliner   (drive .00: L .72 x0.54)
            {7,3,0.41f},   // Prayer Djent (drive .05: L .74 x0.55)
            {12,3,0.52f},  // Castaway Groove (drive .05: L .95 x0.55)
            {14,3,0.41f},  // Duality Crush (drive .00: L .76 x0.54)
        };
        for (const Ts& t : kTsPushRestore) {
            Preset& pr = p->presets[t.bank][t.slot];
            if (pr.used) pr.vals[HF_DR_LEVEL] = t.lvl;
        }
        // Measured parity trims back to each preset's as-dialed loudness
        // (preset_diag, rev 115; targets = the pre-gain-floor-fix table, and
        // for 30/31 their round-11 managed state).
        struct Lvl { int bank, slot; float d; };
        static const Lvl kTsPushParity[] = {
            {0,1,+0.4f}, {0,2,+1.2f}, {0,3,+1.8f}, {6,1,+1.1f},
            {7,2,+2.7f}, {7,3,+1.2f}, {12,3,+0.8f}, {14,3,+2.8f},
        };
        for (const Lvl& lv : kTsPushParity) {
            Preset& pr = p->presets[lv.bank][lv.slot];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += lv.d;
        }
    }

    // ── Rev 116 (2026-08-22, user): drop the BLANKET "Lead Cut" EQ (the
    // rev-64/65 stock curve, identical -1/0/+1/+3/+4/+5 on 11 lead presets —
    // never ear-tuned per preset; its rising treble fights the FRFR voice +
    // corrected amps; "the ghost lead tones sound bad", Queen named too) and
    // the Periphery pair's custom EQs (user: "lose the eqs") + a little less
    // Periphery gain (0.62 -> 0.56). User-authored curves untouched: the NIN
    // bank, I Saw a Deer, Frayed Justice, Sister's Singer, and the nu-metal
    // de-boom curves.
    {
        static const int kEqDrop[][2] = {
            {1,0},   // Regal Sustain      (Queen)
            {2,3},   // Sermon Solo
            {3,1},   // Imperial Lead      (Ghost)
            {3,3},   // Cardinal Lead      (Ghost)
            {4,2},   // Numb Sustain
            {4,3},   // Gravity Lead
            {5,1},   // Hazy Solo
            {6,2},   // Skye Soar
            {10,3},  // Regal Solo         (Queen)
            {12,0},  // Quarter-Tone Lead
            {13,2},  // Spectrum Lead
            {7,2},   // Flatliner          (custom curve, user: lose it)
            {7,3},   // Prayer Djent       (custom curve, user: lose it)
        };
        for (const auto& e : kEqDrop) {
            Preset& pr = p->presets[e[0]][e[1]];
            if (pr.used) pr.vals[HF_EQ_ENABLE] = 0.0f;
        }
        p->presets[7][2].vals[HF_AMP_GAIN] = 0.56f;   // a little less gain (was 0.62)
        p->presets[7][3].vals[HF_AMP_GAIN] = 0.56f;
        // Measured parity trims for the removed boost curves (preset_diag,
        // rev 117; presets not listed measured within +-0.2 dB).
        struct Lvl { int bank, slot; float d; };
        static const Lvl kEqDropParity[] = {
            {1,0,+3.7f},   // Regal Sustain
            {3,1,+1.7f},   // Imperial Lead
            {4,3,+1.4f},   // Gravity Lead
            {5,1,+3.7f},   // Hazy Solo
            {6,2,+2.1f},   // Skye Soar
            {12,0,+1.0f},  // Quarter-Tone Lead
            {13,2,+4.3f},  // Spectrum Lead
        };
        for (const Lvl& lv : kEqDropParity) {
            Preset& pr = p->presets[lv.bank][lv.slot];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += lv.d;
        }
    }

    // ── Rev 118 (2026-08-22, user): Numb Sustain "sounds messy" + Desert
    // Robot "too much gain".
    // Numb Sustain rebuilt to the documented Pulse-era Comfortably Numb rig:
    // Muff (sustain eased .75->.62) -> NEW Tube Chauffeur (Butler Tube Driver,
    // Gilmour's staple — low drive, the singing smoother) -> Hiwatt. The mess
    // was TWO simultaneous delays + rotary + verb over a sustaining Muff:
    // dl2 (the rack delay) DROPPED, main delay to Gilmour's ~480 ms with less
    // feedback/mix, rotary (RA-200) and verb eased.
    {
        Preset& ns = p->presets[4][2];
        ns.vals[HF_FZ_SUSTAIN] = 0.62f;
        ns.vals[HF_DR_ENABLE]  = 1.0f;  ns.vals[HF_DR_POS]   = 4.0f;
        ns.vals[HF_DR_MODEL]   = 9.0f;  ns.vals[HF_DR_DRIVE] = 0.35f;
        ns.vals[HF_DR_TONE]    = 0.55f; ns.vals[HF_DR_LEVEL] = 0.60f;
        ns.vals[HF_DL2_ENABLE] = 0.0f;
        ns.vals[HF_DL_TIME]    = 480.0f; ns.vals[HF_DL_FEEDBACK] = 0.38f;
        ns.vals[HF_DL_MIX]     = 0.22f;
        ns.vals[HF_MD_MIX]     = 0.22f;
        ns.vals[HF_RV_MIX]     = 0.12f;
        // Desert Robot: the SD-1's level-law rework (round 2) restored its real
        // op-amp gain — this preset was dialed around the old quieter law and
        // now over-pushes the Backline. Ease the push + drive.
        Preset& dr = p->presets[9][2];
        dr.vals[HF_DR_DRIVE] = 0.52f;   // was 0.65
        dr.vals[HF_DR_LEVEL] = 0.58f;   // was 0.72
        // Rev 119: small measured parity drift after the rebuild (preset_diag).
        p->presets[0][2].vals[HF_OUT_LEVEL]  += 0.4f;
        p->presets[7][2].vals[HF_OUT_LEVEL]  += 0.5f;
        p->presets[7][3].vals[HF_OUT_LEVEL]  += 0.7f;
        p->presets[12][3].vals[HF_OUT_LEVEL] += 0.8f;
    
        // Rev 120 (2026-08-22, user: "Lead is dark and missing volume"): the
        // rev-114 push-restore factor over-corrected Lead — at drive .06 the
        // OLD law still had gain ~3 partially into the diodes (not unity), so
        // x0.55 pulled the push below the July dial-in. Corrected level + a
        // volume nudge; dr_level is the push/brightness lever if more wanted.
        p->presets[0][3].vals[HF_DR_LEVEL]  = 0.52f;   // was restored to .40
        p->presets[0][3].vals[HF_OUT_LEVEL] += 0.6f;   // the missing volume
    }

    // ── Rev 121/122 (2026-08-22): Solar Monolith = the USER'S on-device dial-in
    // (rev 122 = their RE-DIAL on the re-voiced Sunn: amp gain down, brights
    // OFF, master dimed, pedal drive up — the fixed amp needs no fighting.)
    // baked VERBATIM from the live store (store-diffed vs a fresh seed, 24
    // values). Their re-dial fights the Sunn's reported woofiness: hotter +
    // brighter (gain .8225, mid .6125, presence .5175, highcut 10.5k), tighter
    // lows (lowcut 60.8), close mic at the cap, Subtle spk drive, RIG B OFF,
    // less verb, louder out. DO NOT RETUNE.
    {
        Preset& sm = p->presets[6][3];
        sm.vals[HF_OUT_LEVEL]        = -18.96f;
        sm.vals[HF_DR_DRIVE]         = 0.735f;
        sm.vals[HF_DR_TONE]          = 0.3075f;
        sm.vals[HF_DR_LEVEL]         = 0.7325f;
        sm.vals[HF_DR_OCTAVE]        = 0.6325f;
        sm.vals[HF_AMP_GAIN]         = 0.6225f;
        sm.vals[HF_AMP_BASS]         = 0.58f;
        sm.vals[HF_AMP_MID]          = 0.63f;
        sm.vals[HF_AMP_TREBLE]       = 0.5725f;
        sm.vals[HF_AMP_PRESENCE]     = 0.5175f;
        sm.vals[HF_AMP_MASTER]       = 1.0f;
        sm.vals[HF_AMP_SUNN_VOL2]    = 0.8325f;
        sm.vals[HF_AMP_SUNN_BASS2]   = 0.695f;
        sm.vals[HF_AMP_SUNN_MID2]    = 0.59f;
        sm.vals[HF_AMP_SUNN_TREBLE2] = 0.575f;
        sm.vals[HF_AMP_SUNN_BRIGHT1] = 0.0f;   // brights OFF — the re-voiced amp has its own top
        sm.vals[HF_AMP_SUNN_BRIGHT2] = 0.0f;
        sm.vals[HF_CAB_LOWCUT]       = 60.8f;
        sm.vals[HF_CAB_HIGHCUT]      = 10505.0f;
        sm.vals[HF_RV_PREDELAY]      = 10.75f;
        sm.vals[HF_RV_MIX]           = 0.20f;
        sm.vals[HF_CAB_MICPOS]       = 0.1059f;
        sm.vals[HF_CAB_MICDIST]      = 0.04f;
        sm.vals[HF_CAB_SPKDRIVE]     = 1.0f;
        sm.vals[HF_RB_ENABLE]        = 0.0f;
        sm.vals[HF_RB_CAB2ON]        = 0.0f;
    }

    // ── Periphery rig-B low cut (rev 110, 2026-08-21): the djent parallel
    // clean blends' low-mids pump the inverted-polarity cancellation depth
    // (the mono "woosh"; isolated + measured by build-tools/woosh2.cpp —
    // 150 Hz recovers about half the rig-B flutter while keeping the layer's
    // attack clarity). User-adjustable in the Amp 2 Blend tab.
    p->presets[7][2].vals[HF_RB_LOCUT] = 150.0f;   // Flatliner
    p->presets[7][3].vals[HF_RB_LOCUT] = 150.0f;   // Prayer Djent
    // Measured parity restore for the removed low-mid energy (preset_diag).
    p->presets[7][2].vals[HF_OUT_LEVEL] += 1.2f;
    p->presets[7][3].vals[HF_OUT_LEVEL] += 1.6f;
    // Rev 112 (2026-08-21, user still heard the swoosh; woosh2 sweep): the
    // cancellation pump SCALES WITH BLEND — 0.10 measures at the rig-B-off
    // flutter floor on both presets while pol/locut-400 barely move it. The
    // layer becomes a subtle attack sheen; the user can zero it entirely if
    // it no longer earns its place on a mono rig.
    p->presets[7][2].vals[HF_RB_BLEND] = 0.10f;    // was 0.18
    p->presets[7][3].vals[HF_RB_BLEND] = 0.10f;    // was 0.22
    // Measured parity restore (less inverted cancellation = louder).
    p->presets[7][2].vals[HF_OUT_LEVEL] += -1.4f;
    p->presets[7][3].vals[HF_OUT_LEVEL] += -2.1f;

    // ── Rev 123 (2026-08-23, user: "NIN presets need some work since the
    // changes; March Stabs doesn't sound right"): each of the four was moved
    // by a LATER amp re-voice; compensated back toward its rev-90
    // user-approved operating point (loudness parity re-measured on-device):
    //  - March Stabs: the JCM800 runs DIRECT (no cab), so the HG round-2 PA
    //    bake (padrive 1.72) landed fully exposed -- master .765 -> .445 puts
    //    the PA back at the pre-bake operating point (.445 * 1.72 ~= .77).
    //  - World Went Away: the user-approved DARK Sunn wall brightened in the
    //    Sunn re-voice (evens exciter, air 3k -> 16k, +4.2 dB presence shelf,
    //    hotter input) -- B layer darkened back (treble .30 / presence .28)
    //    and gain .50 compensates the raised input drive.
    //  - Broken Crush: the Recto CH3-Modern capture fit (sat x1.84) nearly
    //    doubled the preamp saturation at gain .2875 -- gain .16 restores the
    //    voiced operating point on the now-accurate curve.
    //  - Con Molars: JCM800 master .82 with the HG PA bake pushed the FF-spit
    //    re-amp into mush -- master .50 (partial inverse, keeps some of the
    //    chosen HG character).
    {
        Preset& ms = p->presets[11][0];   // March Stabs
        ms.vals[HF_AMP_MASTER] = 0.445f;
        Preset& ww = p->presets[11][1];   // World Went Away
        ww.vals[HF_RB_TREBLE] = 0.30f; ww.vals[HF_RB_PRESENCE] = 0.28f;
        ww.vals[HF_RB_GAIN] = 0.50f;
        Preset& bc = p->presets[11][2];   // Broken Crush
        bc.vals[HF_AMP_GAIN] = 0.16f;
        Preset& cm = p->presets[11][3];   // Con Molars
        cm.vals[HF_AMP_MASTER] = 0.50f;
        // Parity measured (hexforge_meas @ forced -20 out, before/after): all
        // four deltas <= 0.17 dB -- the saturated chains self-normalize RMS
        // while the peaks drop 0.3-0.5 dB (less clipping, the intent). No
        // out_level compensation needed (0.3 dB bake threshold).
    }

    // Rockerverb gain-tracking parity (rev 136, 2026-09-04): trackLo 1.71
    // makes the Orange gain knob span its real loudness; two low-gain
    // presets re-leveled.
    {
        auto trim = [&](int b, int sl, float dB) {
            Preset& pr = p->presets[b][sl];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += dB;
        };
        trim(2, 0, +13.5f);  trim(2, 1, +2.6f);
    }

    // Vox AC30 the reference rig re-voice parity (rev 135, 2026-09-04): the W1 drive-
    // extension bake (span x1.8 gain-coupled, rail +18, honk voicing) shifted
    // nine Vox-carrying presets; trims to parity.
    {
        auto trim = [&](int b, int sl, float dB) {
            Preset& pr = p->presets[b][sl];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += dB;
        };
        trim(16, 0, -3.4f);  // Sweet Dispersion
        trim(10, 1, +2.4f);  trim(10, 3, +2.4f);   // Glide Wall / Regal Solo
        trim(9, 1, -2.0f);   // Glide Wall b10? (bank10B = index 9 slot 1)
        trim(1, 0, +1.9f);   // Come As Water (bank02A)
        trim(10, 0, +1.8f);  // March? (bank11A = index 10 slot 0)
        trim(10, 2, -1.6f);  // bank11C
        trim(14, 0, +1.3f);  // MoP (bank15A)
        trim(1, 3, -0.6f);   // bank02D
    }

    // Mark IV mode-7 the reference rig re-voice parity (rev 134, 2026-09-03): the config-L
    // bake lifted three mode-7 presets; trims to parity (placed with the
    // late blocks so nothing overwrites them).
    {
        auto trim = [&](int b, int sl, float dB) {
            Preset& pr = p->presets[b][sl];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += dB;
        };
        trim(13, 2, -3.4f);   // Colors Lead (bank14C, mode 7)
        trim(13, 1, -2.8f);   // Colors Rhythm (bank14B)
        trim(17, 0, -3.2f);   // Frayed Justice (bank18A - its rig B runs mode 7)
    }

    // Cali V Ch3 knob-law preservation (rev 133, 2026-09-03): the reference rig IIC+
    // probe re-lawed the Ch3 gain/treble knobs (MesaMarkV kCh3GainLaw/TrebLaw);
    // factory presets saved under the OLD (identity) law are inverse-remapped
    // so their SOUND is bit-preserved while the knobs now read like the real
    // dial. Programmatic: catches every Cali Ch3 preset incl. migrated-in
    // mode defaults and rig-B slots. (User-store slots get the same transform
    // via the blob v46 migration.)
    //
    // SCOPED TO FACTORY-OWNED SLOTS (fix 2026-09-04). This transform is NOT
    // idempotent, and seedFactoryPresets() re-runs on EVERY load whose stored
    // factoryRev is behind (hfDeserialize + the LV2 state loader). The original
    // sweep guarded only on pr.used, so a USER-saved Cali Ch3 preset in a
    // non-factory slot had its gain/treble re-inverted at every future rev
    // bump, ratcheting both knobs until they saturated at 1.0 — and could be
    // hit twice in one load (blob v46 migration, then this sweep). Factory
    // slots are memcpy'd fresh from the table immediately above, so exactly one
    // application is correct for them; user slots get theirs from the v46 blob
    // migration, as this block's own comment always said. Same rule as the
    // rev-72 JCM800 bump: never let a non-idempotent edit touch user slots.
    {
        auto isFactorySlot = [&](int b, int sl) {
            if (b == 0) return true;                       // Bank 1 stock four
            if (b == 17 || b == 18 || b == 19) return true; // banks 18/19/20, seeded inline above
            for (int i = 0; i < kFactoryExtraCount; ++i)
                if (kFactoryExtra[i].bank == b && kFactoryExtra[i].slot == sl) return true;
            return false;
        };
        auto invG = [](float g) { return std::min(g / 0.6f, g + 0.1f); };
        auto invT = [](float t) { return t <= 0.12f ? t / 0.24f : 0.5f + (t - 0.12f) / 0.8f; };
        for (int b = 0; b < kBanks; ++b) for (int sl = 0; sl < kSlots; ++sl) {
            Preset& pr = p->presets[b][sl];
            if (!pr.used || !isFactorySlot(b, sl)) continue;
            if (static_cast<int>(pr.vals[HF_AMP_MODEL] + 0.5f) == 11 && pr.vals[HF_AMP_MV_MODE] >= 5.5f) {
                pr.vals[HF_AMP_GAIN]   = std::min(1.0f, invG(pr.vals[HF_AMP_GAIN]));
                pr.vals[HF_AMP_TREBLE] = std::min(1.0f, invT(pr.vals[HF_AMP_TREBLE]));
            }
            if (static_cast<int>(pr.vals[HF_RB_AMP] + 0.5f) == 11 && pr.vals[HF_RB_MV_MODE] >= 5.5f) {
                pr.vals[HF_RB_GAIN]   = std::min(1.0f, invG(pr.vals[HF_RB_GAIN]));
                pr.vals[HF_RB_TREBLE] = std::min(1.0f, invT(pr.vals[HF_RB_TREBLE]));
            }
        }
    }

    // Drive-point restoration after the reference rig re-voices (rev 137, 2026-09-04,
    // overnight pass + user direction): factory presets were dialed against
    // the OLD models; each preset's measured THD@1k drive point is restored
    // on the re-voiced amps by re-dialing GAIN (the amp stays real, the
    // preset keeps its documented intent). Vox cleans floored at 0.12 (a
    // real TB dial; matching their old 4-8% THD needed absurd 0.03 knobs -
    // flagged for ears). Cardinal pair set to the USER's stated real dial
    // (gain 5-6/10): their 0.15 dial-in compensated for the old 2x-hot Ch3
    // law and rev-133 faithfully preserved the compensation - overridden on
    // user direction 2026-09-04. Quiet Drive left as-is (match failed).
    {
        auto setg = [&](int b, int sl, float g) {
            Preset& pr = p->presets[b][sl];
            if (pr.used) pr.vals[HF_AMP_GAIN] = g;
        };
        setg(1, 0, 0.101f); setg(1, 3, 0.12f);   // Regal Sustain / Come As Water (rev 138: ramped-rail rematch)
        setg(7, 2, 0.85f);  setg(7, 3, 0.71f);   // Flatliner / Prayer Djent
        setg(8, 0, 0.73f);  setg(9, 1, 0.12f);   // Bridge Vibe / Apache Echo
        setg(10, 0, 0.107f); setg(10, 1, 0.132f); // Innerspeaker / Glide Wall (rev 138)
        setg(10, 2, 0.229f); setg(10, 3, 0.099f); // Streets Chime / Regal Solo (rev 138)
        setg(12, 3, 0.85f); setg(13, 3, 0.70f);  // Castaway / Grunge Drop
        setg(14, 0, 0.63f); setg(14, 3, 0.99f);  // Plug-In Junior / Duality Crush
        setg(15, 2, 0.84f);                      // One Step Deeper
        setg(16, 0, 0.12f); setg(16, 1, 0.12f);  // Sweet Dispersion / Homesick Saucer
        setg(3, 2, 0.55f);  setg(3, 3, 0.55f);   // Cardinal Rhythm/Lead (USER: real dial 5-6)
        // loudness re-parity for the re-dials (golden-measured):
        auto trim = [&](int b, int sl, float dB) {
            Preset& pr = p->presets[b][sl];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += dB;
        };
        trim(16, 0, +3.9f);  trim(1, 3, +1.1f);  trim(9, 1, +1.0f);
        trim(3, 3, -0.8f);   trim(16, 1, +0.8f);
        // rev 138 (ramped-rail rematch) re-parity:
        trim(10, 3, -2.0f);  trim(10, 1, -1.8f);  trim(16, 0, +1.5f);
        trim(1, 0, -1.3f);   trim(10, 2, +1.3f);  trim(10, 0, +0.8f);  trim(9, 1, +0.6f);   // (1,0)=Regal Sustain — a P004/bank02A slot-map slip put this on Come As Water first
    }


    // Recto Modern-mode the reference rig re-voice parity (rev 131, 2026-09-03) — placed LAST: the NIN/nu-metal user-bake blocks assign ABSOLUTE out_levels above and would overwrite these trims: the config-Z
    // bake (chug peak, tightHP 60, sag x0.1, presence/mv laws) + Vintage
    // makeup x1.66 shifted six presets. Broken Crush PEAK-CAPPED (hit 2.26
    // after the bake; -5.5 lands ~1.2 - the rev-125 rule).
    {
        auto trim = [&](int b, int sl, float dB) {
            Preset& pr = p->presets[b][sl];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += dB;
        };
        trim(11, 2, -5.5f);   // Broken Crush (NIN, mode 7) - peak cap
        trim(15, 0, -1.7f);  trim(15, 1, -2.3f);  trim(15, 3, -1.3f);   // nu-metal Vintage lifts (rev 132: VB voicing pulled A/D back 0.8/1.2)
        trim(15, 2, +1.5f);  trim(14, 3, +1.1f);
    }





    // === REV 139/140: RESEARCH REBUILD, user-curated (2026-09-04) ===
    // Rev-139 rebuilt 53 presets from per-song rig research; the user A/B kept
    // GHOST (banks 3+4), the BTBAM Spectrum pair, Numb Sustain and Desert
    // Robot (QOTSA) -- rev 140 rolls the other 41 back for manual fixes.
    // User: "let's rethink all of the presets... research the presets for each
    // band and song that we're replicating and try creating them from scratch."
    // 8 web-research agents (per-song rig documentation, evidence-graded);
    // values authored against the reference-fitted amp laws + rev-137 drive points.
    // UNTOUCHED: Bank 1, NIN B/C/D, all Plexiglass presets, Solar Monolith /
    // Knights of Fuzz / Retro Poland / I Saw a Deer / Jungle Sleaze (user
    // builds), banks 19/20. Placed LAST: absolute values, overrides all
    // earlier rev blocks for these slots. out_levels re-measured (golden).
    {
        auto rb = [&](int b, int slot, const char* name, const char* ir) -> Preset& {
            Preset& pr = p->presets[b][slot];
            pr = p->presets[0][1];
            pr.used = true;
            std::snprintf(pr.name, sizeof(pr.name), "%s", name);
            pr.irPath[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0]
                = pr.amp2NamPath[0] = pr.ir2Path[0] = pr.dr2NamPath[0] = '\0';
            if (ir) { std::strncpy(pr.irPath, ir, kPathMax-1); pr.irPath[kPathMax-1] = '\0'; }
            static const int offs[] = { HF_CP_ENABLE, HF_FZ_ENABLE, HF_DR_ENABLE,
                HF_MD_ENABLE, HF_DL_ENABLE, HF_RV_ENABLE, HF_WH_ENABLE, HF_OC_ENABLE,
                HF_NAIL_ENABLE, HF_EQ_ENABLE, HF_DR2_ENABLE, HF_RB_ENABLE, HF_RB_CAB2ON,
                HF_GT2_ENABLE, HF_CP2_ENABLE, HF_FZ2_ENABLE, HF_NAIL2_ENABLE, HF_MD2_ENABLE,
                HF_DL2_ENABLE, HF_RV2_ENABLE, HF_WH2_ENABLE, HF_OC2_ENABLE, HF_EQ2_ENABLE };
            for (int o : offs) pr.vals[o] = 0.0f;
            // canonical chain slots (the donor may carry a user re-order)
            static const int posv[][2] = { {HF_GT_POS,1}, {HF_CP_POS,2}, {HF_FZ_POS,3},
                {HF_DR_POS,4}, {HF_AMP_POS,5}, {HF_CAB_POS,6}, {HF_MD_POS,7}, {HF_DL_POS,8},
                {HF_RV_POS,9}, {HF_WH_POS,10}, {HF_OC_POS,11}, {HF_NAIL_POS,12}, {HF_EQ_POS,13},
                {HF_DR2_POS,14}, {HF_GT2_POS,15}, {HF_CP2_POS,16}, {HF_FZ2_POS,17},
                {HF_NAIL2_POS,18}, {HF_MD2_POS,19}, {HF_DL2_POS,20}, {HF_RV2_POS,21},
                {HF_WH2_POS,22}, {HF_OC2_POS,23}, {HF_EQ2_POS,24} };
            for (auto& kv : posv) pr.vals[kv[0]] = (float)kv[1];
            pr.vals[HF_GT_ENABLE] = 1.0f;  pr.vals[HF_GT_ATTACK] = 2.0f;
            pr.vals[HF_GT_HOLD] = 120.0f;  pr.vals[HF_GT_RELEASE] = 250.0f;
            pr.vals[HF_GT_HYST] = 8.0f;
            pr.vals[HF_AMP_ENABLE] = 1.0f; pr.vals[HF_AMP_BYPASS] = 0.0f;
            pr.vals[HF_AMP_CHANNEL] = 0.0f; pr.vals[HF_AMP_RESONANCE] = 0.0f;
            pr.vals[HF_AMP_SAG] = 0.3f;   pr.vals[HF_AMP_SIR34] = 0.0f;
            pr.vals[HF_AMP_FR_CHANNEL] = 1.0f; pr.vals[HF_AMP_FR_FAT] = 0.0f;
            pr.vals[HF_AMP_FR_C45] = 0.0f; pr.vals[HF_AMP_FR_SAT] = 0.0f;
            pr.vals[HF_AMP_MV_MODE] = 0.0f; pr.vals[HF_AMP_MV_EQPRESET] = 0.0f;
            pr.vals[HF_AMP_MV_GEQ0] = 0.5f; pr.vals[HF_AMP_MV_GEQ1] = 0.5f;
            pr.vals[HF_AMP_MV_GEQ2] = 0.5f; pr.vals[HF_AMP_MV_GEQ3] = 0.5f;
            pr.vals[HF_AMP_MV_GEQ4] = 0.5f;
            pr.vals[HF_AMP_RC_MODE] = 4.0f; pr.vals[HF_AMP_RC_RECT] = 0.0f;
            pr.vals[HF_AMP_RC_VARIAC] = 0.0f;
            pr.vals[HF_AMP_PL_VARIAC] = 0.0f; pr.vals[HF_AMP_PL_VOL2] = 0.0f;
            pr.vals[HF_CAB_ENABLE] = 1.0f; pr.vals[HF_CAB_MIX] = 1.0f;
            pr.vals[HF_CAB_MICPOS] = 0.0f; pr.vals[HF_CAB_MICDIST] = 0.0f;
            pr.vals[HF_CAB_ROOMON] = 1.0f; pr.vals[HF_CAB_ROOMMIX] = 0.10f;
            pr.vals[HF_CAB_ROOMAMT] = 0.35f; pr.vals[HF_CAB_ROOMDENSE] = 1.0f;
            pr.vals[HF_CAB_SPKDRIVE] = 0.0f; pr.vals[HF_CAB_VOICE] = 0.0f;
            pr.vals[HF_RV_DENSITY] = 1.0f; pr.vals[HF_RV_PREDELAY] = 10.0f;
            pr.vals[HF_RV_DAMPING] = 0.3f; pr.vals[HF_RV_BLOOM] = 0.5f;
            pr.vals[HF_FZ_GVOL] = 1.0f;
            pr.vals[HF_IT_BOOST] = 0.0f; pr.vals[HF_IT_BOOSTAMT] = 0.0f;
            pr.vals[HF_IT_GAIN] = 0.0f; pr.vals[HF_IT_PHASE] = 0.0f;
            pr.vals[HF_OUT_DOUBLER] = 0.0f;
            pr.vals[HF_MD_OFFSET] = 0.0f; pr.vals[HF_MD_SHAPE] = 0.0f;
            pr.vals[HF_MD_WIDTH] = 0.5f; pr.vals[HF_DL_WIDTH] = 0.5f;
            pr.vals[HF_DL_AGE] = 0.35f; pr.vals[HF_DL_HEADS] = 10.0f;
            pr.vals[HF_DL_WOW] = 0.003f; pr.vals[HF_DL_FLUTTER] = 0.001f;
            pr.vals[HF_DL_DUCKING] = 0.0f; pr.vals[HF_DL_SYNC] = 0.0f;
            pr.vals[HF_EQ_LEVEL] = 0.0f;
            pr.vals[HF_OUT_LEVEL] = -16.0f;   // provisional; golden-measured below
            return pr;
        };
        {   // [3/A] Candlelit Clean
            // Meliora cleans: NO Fender documented -- the same Marshall+Orange pool at
            // low gain, Tele brightness. Rockerverb CLEAN channel, optical comp, light
            // chorus + plate (community-consensus dressing).
            Preset& q = rb(2, 0, "Candlelit Clean", "@factory");
            q.vals[HF_GT_THRESH] = -52.0f; q.vals[HF_CP_ENABLE] = 1.0f;
            q.vals[HF_CP_TYPE] = 0.0f; q.vals[HF_CP_THRESH] = -24.0f;
            q.vals[HF_CP_RATIO] = 1.0f; q.vals[HF_CP_MAKEUP] = 2.0f;
            q.vals[HF_AMP_MODEL] = 4.0f; q.vals[HF_AMP_CHANNEL] = 1.0f;
            q.vals[HF_AMP_GAIN] = 0.35f; q.vals[HF_AMP_BASS] = 0.45f;
            q.vals[HF_AMP_MID] = 0.5f; q.vals[HF_AMP_TREBLE] = 0.62f;
            q.vals[HF_AMP_PRESENCE] = 0.55f; q.vals[HF_AMP_MASTER] = 0.7f;
            q.vals[HF_MD_ENABLE] = 1.0f; q.vals[HF_MD_TYPE] = 0.0f; q.vals[HF_MD_RATE] = 0.35f;
            q.vals[HF_MD_DEPTH] = 0.45f; q.vals[HF_MD_MIX] = 0.25f;
            q.vals[HF_RV_ENABLE] = 1.0f; q.vals[HF_RV_TYPE] = 0.0f; q.vals[HF_RV_MIX] = 0.15f;
            q.vals[HF_RV_DECAY] = 1.8f; q.vals[HF_CAB_LOWCUT] = 80.0f;
            q.vals[HF_CAB_HIGHCUT] = 9000.0f; q.vals[HF_OUT_LEVEL] = -5.0f;
        }
        {   // [3/B] Sermon Crunch
            // Meliora rhythm: 12-amp Marshall+Orange wall, MID-gain (riff-led, mids
            // intact -- the wall is layering, not gain). Rockerverb dirty + doubler.
            Preset& q = rb(2, 1, "Sermon Crunch", "@factory");
            q.vals[HF_GT_THRESH] = -48.0f; q.vals[HF_AMP_MODEL] = 4.0f;
            q.vals[HF_AMP_GAIN] = 0.5f; q.vals[HF_AMP_BASS] = 0.5f; q.vals[HF_AMP_MID] = 0.55f;
            q.vals[HF_AMP_TREBLE] = 0.55f; q.vals[HF_AMP_PRESENCE] = 0.5f;
            q.vals[HF_AMP_MASTER] = 0.7f; q.vals[HF_AMP_SAG] = 0.3f;
            q.vals[HF_OUT_DOUBLER] = 1.0f; q.vals[HF_RV_ENABLE] = 1.0f;
            q.vals[HF_RV_TYPE] = 0.0f; q.vals[HF_RV_MIX] = 0.1f; q.vals[HF_RV_DECAY] = 1.4f;
            q.vals[HF_CAB_LOWCUT] = 80.0f; q.vals[HF_CAB_HIGHCUT] = 7800.0f;
            q.vals[HF_OUT_LEVEL] = -22.4f;
        }
        {   // [3/C] Sermon Rhythm
            // Cirice main riff: same wall, a step hotter.
            Preset& q = rb(2, 2, "Sermon Rhythm", "@factory");
            q.vals[HF_GT_THRESH] = -46.0f; q.vals[HF_AMP_MODEL] = 4.0f;
            q.vals[HF_AMP_GAIN] = 0.58f; q.vals[HF_AMP_BASS] = 0.52f;
            q.vals[HF_AMP_MID] = 0.55f; q.vals[HF_AMP_TREBLE] = 0.55f;
            q.vals[HF_AMP_PRESENCE] = 0.5f; q.vals[HF_AMP_MASTER] = 0.75f;
            q.vals[HF_AMP_SAG] = 0.3f; q.vals[HF_OUT_DOUBLER] = 1.0f;
            q.vals[HF_RV_ENABLE] = 1.0f; q.vals[HF_RV_TYPE] = 0.0f; q.vals[HF_RV_MIX] = 0.08f;
            q.vals[HF_RV_DECAY] = 1.4f; q.vals[HF_CAB_LOWCUT] = 80.0f;
            q.vals[HF_CAB_HIGHCUT] = 7800.0f; q.vals[HF_OUT_LEVEL] = -22.0f;
        }
        {   // [3/D] Sermon Solo
            // Pinnacle/Cirice solo voice: hotter channel from the same pool, neck-ish
            // warmth, mix delay + Lead Cut.
            Preset& q = rb(2, 3, "Sermon Solo", "@factory");
            q.vals[HF_GT_THRESH] = -46.0f; q.vals[HF_AMP_MODEL] = 4.0f;
            q.vals[HF_AMP_GAIN] = 0.62f; q.vals[HF_AMP_BASS] = 0.5f;
            q.vals[HF_AMP_MID] = 0.58f; q.vals[HF_AMP_TREBLE] = 0.5f;
            q.vals[HF_AMP_PRESENCE] = 0.48f; q.vals[HF_AMP_MASTER] = 0.75f;
            q.vals[HF_DL_ENABLE] = 1.0f; q.vals[HF_DL_TYPE] = 0.0f;
            q.vals[HF_DL_TIME] = 380.0f; q.vals[HF_DL_FEEDBACK] = 0.35f;
            q.vals[HF_DL_MIX] = 0.22f; q.vals[HF_RV_ENABLE] = 1.0f; q.vals[HF_RV_TYPE] = 0.0f;
            q.vals[HF_RV_MIX] = 0.12f; q.vals[HF_RV_DECAY] = 1.6f;
            q.vals[HF_CAB_LOWCUT] = 85.0f; q.vals[HF_CAB_HIGHCUT] = 7500.0f;
            q.vals[HF_EQ_ENABLE] = 1.0f; q.vals[HF_EQ_PRESET] = 5.0f;
            q.vals[HF_EQ_POS] = 13.0f; q.vals[HF_EQ_100] = -1.0f; q.vals[HF_EQ_200] = 0.0f;
            q.vals[HF_EQ_400] = 1.0f; q.vals[HF_EQ_800] = 3.0f; q.vals[HF_EQ_1K6] = 4.0f;
            q.vals[HF_EQ_3K2] = 5.0f; q.vals[HF_OUT_LEVEL] = -22.2f;
        }
        {   // [4/A] Imperial Rhythm
            // Impera rhythm (Akesson, documented): his Friedman BE-100 + Sugar Drive
            // (Klon) boost, 8-track packs -> doubler. Beardo BE ch BE.
            Preset& q = rb(3, 0, "Imperial Rhythm", "@factory");
            q.vals[HF_GT_THRESH] = -46.0f; q.vals[HF_DR_ENABLE] = 1.0f;
            q.vals[HF_DR_MODEL] = 5.0f; q.vals[HF_DR_DRIVE] = 0.2f; q.vals[HF_DR_TONE] = 0.55f;
            q.vals[HF_DR_LEVEL] = 0.7f; q.vals[HF_AMP_MODEL] = 6.0f;
            q.vals[HF_AMP_FR_CHANNEL] = 1.0f; q.vals[HF_AMP_GAIN] = 0.55f;
            q.vals[HF_AMP_BASS] = 0.5f; q.vals[HF_AMP_MID] = 0.55f;
            q.vals[HF_AMP_TREBLE] = 0.55f; q.vals[HF_AMP_PRESENCE] = 0.55f;
            q.vals[HF_AMP_MASTER] = 0.7f; q.vals[HF_OUT_DOUBLER] = 1.0f;
            q.vals[HF_CAB_SPKDRIVE] = 2.0f; q.vals[HF_CAB_LOWCUT] = 85.0f;
            q.vals[HF_CAB_HIGHCUT] = 7500.0f; q.vals[HF_OUT_LEVEL] = -18.8f;
        }
        {   // [4/B] Imperial Lead
            // Impera leads (documented STRONG): late-50s Plexi + DOD Preamp 250 'for
            // extra scream' (the Yngwie recipe). Plexi is user-reserved -> JCM800
            // platform, the 250 supplies the scream.
            Preset& q = rb(3, 1, "Imperial Lead", "@greenback");
            q.vals[HF_GT_THRESH] = -46.0f; q.vals[HF_DR_ENABLE] = 1.0f;
            q.vals[HF_DR_MODEL] = 7.0f; q.vals[HF_DR_DRIVE] = 0.45f;
            q.vals[HF_DR_TONE] = 0.55f; q.vals[HF_DR_LEVEL] = 0.75f;
            q.vals[HF_AMP_MODEL] = 1.0f; q.vals[HF_AMP_GAIN] = 0.62f;
            q.vals[HF_AMP_BASS] = 0.5f; q.vals[HF_AMP_MID] = 0.6f;
            q.vals[HF_AMP_TREBLE] = 0.55f; q.vals[HF_AMP_PRESENCE] = 0.55f;
            q.vals[HF_AMP_MASTER] = 0.75f; q.vals[HF_DL_ENABLE] = 1.0f;
            q.vals[HF_DL_TYPE] = 0.0f; q.vals[HF_DL_TIME] = 430.0f;
            q.vals[HF_DL_FEEDBACK] = 0.38f; q.vals[HF_DL_MIX] = 0.25f;
            q.vals[HF_RV_ENABLE] = 1.0f; q.vals[HF_RV_TYPE] = 0.0f; q.vals[HF_RV_MIX] = 0.1f;
            q.vals[HF_RV_DECAY] = 1.5f; q.vals[HF_CAB_SPKDRIVE] = 2.0f;
            q.vals[HF_CAB_LOWCUT] = 85.0f; q.vals[HF_CAB_HIGHCUT] = 7800.0f;
            q.vals[HF_EQ_ENABLE] = 1.0f; q.vals[HF_EQ_PRESET] = 5.0f;
            q.vals[HF_EQ_POS] = 13.0f; q.vals[HF_EQ_100] = -1.0f; q.vals[HF_EQ_200] = 0.0f;
            q.vals[HF_EQ_400] = 1.0f; q.vals[HF_EQ_800] = 3.0f; q.vals[HF_EQ_1K6] = 4.0f;
            q.vals[HF_EQ_3K2] = 5.0f; q.vals[HF_OUT_LEVEL] = -20.2f;
        }
        {   // [4/C] Cardinal Rhythm
            // Skeleta rhythm (SoS engineering feature): Rockman->JC-120+Plexi 80s sheen
            // core + HETFIELD's actual IIC+ heavy layer. Cali V IIC+ at the USER's real
            // dial (gain 5-6/10), comp+chorus = the Rockman sheen.
            Preset& q = rb(3, 2, "Cardinal Rhythm", "@factory");
            q.vals[HF_GT_THRESH] = -48.0f; q.vals[HF_CP_ENABLE] = 1.0f;
            q.vals[HF_CP_TYPE] = 0.0f; q.vals[HF_CP_THRESH] = -26.0f;
            q.vals[HF_CP_RATIO] = 1.0f; q.vals[HF_CP_MAKEUP] = 2.0f;
            q.vals[HF_AMP_MODEL] = 11.0f; q.vals[HF_AMP_MV_MODE] = 6.0f;
            q.vals[HF_AMP_GAIN] = 0.55f; q.vals[HF_AMP_BASS] = 0.3f;
            q.vals[HF_AMP_MID] = 0.45f; q.vals[HF_AMP_TREBLE] = 0.55f;
            q.vals[HF_AMP_PRESENCE] = 0.55f; q.vals[HF_AMP_MASTER] = 0.65f;
            q.vals[HF_AMP_MV_GEQ0] = 0.6f; q.vals[HF_AMP_MV_GEQ1] = 0.5f;
            q.vals[HF_AMP_MV_GEQ2] = 0.35f; q.vals[HF_AMP_MV_GEQ3] = 0.55f;
            q.vals[HF_AMP_MV_GEQ4] = 0.65f; q.vals[HF_MD_ENABLE] = 1.0f;
            q.vals[HF_MD_TYPE] = 0.0f; q.vals[HF_MD_RATE] = 0.35f; q.vals[HF_MD_DEPTH] = 0.5f;
            q.vals[HF_MD_MIX] = 0.3f; q.vals[HF_CAB_LOWCUT] = 80.0f;
            q.vals[HF_CAB_HIGHCUT] = 7800.0f; q.vals[HF_OUT_LEVEL] = -19.5f;
        }
        {   // [4/D] Cardinal Lead
            // Skeleta leads (SoS): vintage Boss OD-1 (14-pin) into the same pool --
            // Super Nova as the OD-1-family stand-in, IIC+ behind it, gain 5-6/10.
            Preset& q = rb(3, 3, "Cardinal Lead", "@factory");
            q.vals[HF_GT_THRESH] = -48.0f; q.vals[HF_DR_ENABLE] = 1.0f;
            q.vals[HF_DR_MODEL] = 6.0f; q.vals[HF_DR_DRIVE] = 0.4f; q.vals[HF_DR_TONE] = 0.5f;
            q.vals[HF_DR_LEVEL] = 0.7f; q.vals[HF_AMP_MODEL] = 11.0f;
            q.vals[HF_AMP_MV_MODE] = 6.0f; q.vals[HF_AMP_GAIN] = 0.58f;
            q.vals[HF_AMP_BASS] = 0.3f; q.vals[HF_AMP_MID] = 0.45f;
            q.vals[HF_AMP_TREBLE] = 0.55f; q.vals[HF_AMP_PRESENCE] = 0.6f;
            q.vals[HF_AMP_MASTER] = 0.65f; q.vals[HF_AMP_MV_GEQ0] = 0.6f;
            q.vals[HF_AMP_MV_GEQ1] = 0.5f; q.vals[HF_AMP_MV_GEQ2] = 0.4f;
            q.vals[HF_AMP_MV_GEQ3] = 0.6f; q.vals[HF_AMP_MV_GEQ4] = 0.65f;
            q.vals[HF_DL_ENABLE] = 1.0f; q.vals[HF_DL_TYPE] = 0.0f;
            q.vals[HF_DL_TIME] = 400.0f; q.vals[HF_DL_FEEDBACK] = 0.35f;
            q.vals[HF_DL_MIX] = 0.25f; q.vals[HF_CAB_LOWCUT] = 85.0f;
            q.vals[HF_CAB_HIGHCUT] = 7800.0f; q.vals[HF_EQ_ENABLE] = 1.0f;
            q.vals[HF_EQ_PRESET] = 5.0f; q.vals[HF_EQ_POS] = 13.0f; q.vals[HF_EQ_100] = -1.0f;
            q.vals[HF_EQ_200] = 0.0f; q.vals[HF_EQ_400] = 1.0f; q.vals[HF_EQ_800] = 3.0f;
            q.vals[HF_EQ_1K6] = 4.0f; q.vals[HF_EQ_3K2] = 5.0f; q.vals[HF_OUT_LEVEL] = -21.2f;
        }
        {   // [5/C] Numb Sustain
            // Comfortably Numb solo: RAM'S HEAD Muff (sust 70/tone 30-40/vol 55) into a
            // LOUD clean Hiwatt + RA-200 rotary LOW under it, MXR DDL ~370 ms low mix,
            // double-tracked -> doubler.
            Preset& q = rb(4, 2, "Numb Sustain", "@hiwatt");
            q.vals[HF_GT_THRESH] = -46.0f; q.vals[HF_FZ_ENABLE] = 1.0f;
            q.vals[HF_FZ_PEDAL] = 0.0f; q.vals[HF_FZ_MODE] = 1.0f;
            q.vals[HF_FZ_SUSTAIN] = 0.7f; q.vals[HF_FZ_TONE] = 0.35f;
            q.vals[HF_FZ_VOLUME] = 0.55f; q.vals[HF_FZ_GVOL] = 1.0f;
            q.vals[HF_AMP_MODEL] = 7.0f; q.vals[HF_AMP_GAIN] = 0.3f;
            q.vals[HF_AMP_BASS] = 0.5f; q.vals[HF_AMP_MID] = 0.6f;
            q.vals[HF_AMP_TREBLE] = 0.45f; q.vals[HF_AMP_PRESENCE] = 0.65f;
            q.vals[HF_AMP_MASTER] = 0.8f; q.vals[HF_MD_ENABLE] = 1.0f;
            q.vals[HF_MD_TYPE] = 5.0f; q.vals[HF_MD_RATE] = 0.35f; q.vals[HF_MD_DEPTH] = 0.5f;
            q.vals[HF_MD_MIX] = 0.25f; q.vals[HF_DL_ENABLE] = 1.0f; q.vals[HF_DL_TYPE] = 0.0f;
            q.vals[HF_DL_TIME] = 370.0f; q.vals[HF_DL_FEEDBACK] = 0.45f;
            q.vals[HF_DL_MIX] = 0.2f; q.vals[HF_OUT_DOUBLER] = 1.0f;
            q.vals[HF_CAB_MICPOS] = 0.25f; q.vals[HF_CAB_MICDIST] = 0.2f;
            q.vals[HF_CAB_LOWCUT] = 75.0f; q.vals[HF_CAB_HIGHCUT] = 8500.0f;
            q.vals[HF_EQ_ENABLE] = 1.0f; q.vals[HF_EQ_PRESET] = 5.0f;
            q.vals[HF_EQ_POS] = 13.0f; q.vals[HF_EQ_100] = -1.0f; q.vals[HF_EQ_200] = 0.0f;
            q.vals[HF_EQ_400] = 1.0f; q.vals[HF_EQ_800] = 3.0f; q.vals[HF_EQ_1K6] = 4.0f;
            q.vals[HF_EQ_3K2] = 5.0f; q.vals[HF_OUT_LEVEL] = -20.2f;
        }
        {   // [10/C] Desert Robot
            // QOTSA robot rhythm (documented: PEAVEY DECADE solid-state + Ampeg VT-40
            // layers, mid-forward, amp-first, NO pedal story): Backline Plus saturation
            // channel character, mids up, darker top.
            Preset& q = rb(9, 2, "Desert Robot", "@american-ob");
            q.vals[HF_GT_THRESH] = -46.0f; q.vals[HF_AMP_MODEL] = 9.0f;
            q.vals[HF_AMP_GAIN] = 0.55f; q.vals[HF_AMP_BASS] = 0.5f;
            q.vals[HF_AMP_MID] = 0.65f; q.vals[HF_AMP_TREBLE] = 0.45f;
            q.vals[HF_AMP_PRESENCE] = 0.45f; q.vals[HF_AMP_MASTER] = 0.7f;
            q.vals[HF_CAB_ROOMMIX] = 0.1f; q.vals[HF_CAB_LOWCUT] = 80.0f;
            q.vals[HF_CAB_HIGHCUT] = 7000.0f; q.vals[HF_OUT_LEVEL] = -25.6f;
        }
        {   // [14/B] Spectrum Rhythm
            // Colors (best-documented: Dual Rectifier family, the 'no mids' era EQ,
            // Jamie King): moved from Cali V Mark IV -> Diamond Plate Modern, mids
            // dumped, TS-808 habit boost, tight gate.
            Preset& q = rb(13, 1, "Spectrum Rhythm", "@factory");
            q.vals[HF_GT_THRESH] = -42.0f; q.vals[HF_DR_ENABLE] = 1.0f;
            q.vals[HF_DR_MODEL] = 0.0f; q.vals[HF_DR_DRIVE] = 0.15f; q.vals[HF_DR_TONE] = 0.6f;
            q.vals[HF_DR_LEVEL] = 0.7f; q.vals[HF_AMP_MODEL] = 12.0f;
            q.vals[HF_AMP_RC_MODE] = 7.0f; q.vals[HF_AMP_GAIN] = 0.75f;
            q.vals[HF_AMP_BASS] = 0.55f; q.vals[HF_AMP_MID] = 0.2f;
            q.vals[HF_AMP_TREBLE] = 0.55f; q.vals[HF_AMP_PRESENCE] = 0.5f;
            q.vals[HF_AMP_MASTER] = 0.7f; q.vals[HF_OUT_DOUBLER] = 1.0f;
            q.vals[HF_CAB_SPKDRIVE] = 2.0f; q.vals[HF_CAB_ROOMMIX] = 0.08f;
            q.vals[HF_CAB_LOWCUT] = 85.0f; q.vals[HF_CAB_HIGHCUT] = 7500.0f;
            q.vals[HF_OUT_LEVEL] = -25.0f;
        }
        {   // [14/C] Spectrum Lead
            // Colors lead voice: same Recto platform, TS pushed harder, delay + Lead Cut.
            Preset& q = rb(13, 2, "Spectrum Lead", "@factory");
            q.vals[HF_GT_THRESH] = -44.0f; q.vals[HF_DR_ENABLE] = 1.0f;
            q.vals[HF_DR_MODEL] = 0.0f; q.vals[HF_DR_DRIVE] = 0.3f; q.vals[HF_DR_TONE] = 0.6f;
            q.vals[HF_DR_LEVEL] = 0.75f; q.vals[HF_AMP_MODEL] = 12.0f;
            q.vals[HF_AMP_RC_MODE] = 7.0f; q.vals[HF_AMP_GAIN] = 0.78f;
            q.vals[HF_AMP_BASS] = 0.5f; q.vals[HF_AMP_MID] = 0.3f;
            q.vals[HF_AMP_TREBLE] = 0.55f; q.vals[HF_AMP_PRESENCE] = 0.55f;
            q.vals[HF_AMP_MASTER] = 0.7f; q.vals[HF_DL_ENABLE] = 1.0f;
            q.vals[HF_DL_TYPE] = 0.0f; q.vals[HF_DL_TIME] = 380.0f;
            q.vals[HF_DL_FEEDBACK] = 0.35f; q.vals[HF_DL_MIX] = 0.22f;
            q.vals[HF_CAB_SPKDRIVE] = 2.0f; q.vals[HF_CAB_LOWCUT] = 85.0f;
            q.vals[HF_CAB_HIGHCUT] = 7500.0f; q.vals[HF_EQ_ENABLE] = 1.0f;
            q.vals[HF_EQ_PRESET] = 5.0f; q.vals[HF_EQ_POS] = 13.0f; q.vals[HF_EQ_100] = -1.0f;
            q.vals[HF_EQ_200] = 0.0f; q.vals[HF_EQ_400] = 1.0f; q.vals[HF_EQ_800] = 3.0f;
            q.vals[HF_EQ_1K6] = 4.0f; q.vals[HF_EQ_3K2] = 5.0f; q.vals[HF_OUT_LEVEL] = -25.1f;
        }
    }
    // === END REV 139 ===


    // ═══ REV 141: USER DIAL-INS BAKED VERBATIM (2026-09-04) ═══
    // The user hand-fixed presets on the device after the rev-140 deploy and
    // asked for them to be baked. These are their EXACT on-device values,
    // store-diffed against a snapshot taken immediately after that deploy.
    // DO NOT RETUNE. Placed LAST so it wins over every earlier block —
    // including the rev-139 research rebuild — and so that the NEXT factory-
    // rev bump reseeds these slots to the user's sounds rather than over-
    // writing them (a rev bump is destructive to un-baked on-device edits).
    {
        auto S = [&](int b, int sl) -> Preset& { return p->presets[b][sl]; };
        {   // bank02-C  Nevermind Wall
            Preset& q = S(1, 2);
            q.vals[HF_DR_DRIVE] = 1.0f; q.vals[HF_DR_TONE] = 0.409999996f;
            q.vals[HF_DR_LEVEL] = 0.819999993f; q.vals[HF_AMP_GAIN] = 0.584999979f;
            q.vals[HF_AMP_MASTER] = 0.720000029f;
        }
        {   // bank03-B  Sermon Crunch
            Preset& q = S(2, 1);
            q.vals[HF_OUT_LEVEL] = -20.7600002f;
        }
        {   // bank03-C  Sermon Rhythm
            Preset& q = S(2, 2);
            q.vals[HF_OUT_LEVEL] = -17.1599998f; q.vals[HF_CP_POS] = 6.0f;
            q.vals[HF_FZ_POS] = 7.0f; q.vals[HF_DR_POS] = 2.0f; q.vals[HF_DR_ENABLE] = 1.0f;
            q.vals[HF_DR_DRIVE] = 0.0f; q.vals[HF_DR_LEVEL] = 0.667500019f;
            q.vals[HF_AMP_POS] = 3.0f; q.vals[HF_AMP_GAIN] = 0.457500011f;
            q.vals[HF_AMP_BASS] = 0.370000005f; q.vals[HF_AMP_MID] = 0.722500026f;
            q.vals[HF_AMP_TREBLE] = 0.584999979f; q.vals[HF_AMP_PRESENCE] = 0.574999988f;
            q.vals[HF_CAB_POS] = 4.0f; q.vals[HF_MD_POS] = 8.0f; q.vals[HF_DL_POS] = 9.0f;
            q.vals[HF_RV_POS] = 5.0f;
        }
        {   // bank03-D  Sermon Solo
            Preset& q = S(2, 3);
            q.vals[HF_CP_POS] = 8.0f; q.vals[HF_FZ_POS] = 9.0f; q.vals[HF_DR_POS] = 2.0f;
            q.vals[HF_DR_ENABLE] = 1.0f; q.vals[HF_DR_DRIVE] = 0.0f;
            q.vals[HF_DR_LEVEL] = 0.642499983f; q.vals[HF_AMP_POS] = 3.0f;
            q.vals[HF_AMP_GAIN] = 0.547500014f; q.vals[HF_CAB_POS] = 4.0f;
            q.vals[HF_MD_POS] = 10.0f; q.vals[HF_DL_POS] = 5.0f; q.vals[HF_RV_POS] = 6.0f;
            q.vals[HF_WH_POS] = 11.0f; q.vals[HF_OC_POS] = 12.0f; q.vals[HF_NAIL_POS] = 13.0f;
            q.vals[HF_EQ_POS] = 7.0f;
        }
        {   // bank04-A  Imperial Rhythm
            Preset& q = S(3, 0);
            q.vals[HF_OUT_LEVEL] = -18.6000004f; q.vals[HF_CP_POS] = 6.0f;
            q.vals[HF_FZ_POS] = 7.0f; q.vals[HF_DR_POS] = 2.0f;
            q.vals[HF_DR_DRIVE] = 0.224999994f; q.vals[HF_DR_LEVEL] = 0.732500017f;
            q.vals[HF_AMP_POS] = 3.0f; q.vals[HF_AMP_GAIN] = 0.694999993f;
            q.vals[HF_AMP_BASS] = 0.317499995f; q.vals[HF_AMP_MID] = 0.627499998f;
            q.vals[HF_AMP_TREBLE] = 0.617500007f; q.vals[HF_AMP_PRESENCE] = 0.517499983f;
            q.vals[HF_AMP_MASTER] = 0.639999986f; q.vals[HF_CAB_POS] = 4.0f;
            q.vals[HF_MD_POS] = 8.0f; q.vals[HF_DL_POS] = 9.0f; q.vals[HF_RV_POS] = 5.0f;
            q.vals[HF_RV_ENABLE] = 1.0f;
        }
        {   // bank04-B  Imperial Lead
            Preset& q = S(3, 1);
            q.vals[HF_OUT_LEVEL] = -17.5200005f; q.vals[HF_DR_DRIVE] = 0.212500006f;
            q.vals[HF_AMP_MODEL] = 6.0f; q.vals[HF_AMP_GAIN] = 0.42750001f;
            q.vals[HF_AMP_BASS] = 0.50999999f; q.vals[HF_AMP_MID] = 0.762499988f;
            q.vals[HF_AMP_TREBLE] = 0.610000014f; q.vals[HF_AMP_PRESENCE] = 0.472499996f;
            q.vals[HF_AMP_MASTER] = 0.704999983f; q.vals[HF_AMP_FR_SAT] = 1.0f;
            q.vals[HF_EQ_LEVEL] = -0.779999971f; q.vals[HF_EQ_BYPASS] = 1.0f;
        }
        {   // bank04-C  Cardinal Rhythm
            Preset& q = S(3, 2);
            q.vals[HF_OUT_LEVEL] = -18.7800007f; q.vals[HF_AMP_GAIN] = 0.582499981f;
            q.vals[HF_AMP_BASS] = 0.0f; q.vals[HF_AMP_MID] = 0.487500012f;
            q.vals[HF_AMP_TREBLE] = 0.600000024f; q.vals[HF_AMP_MV_GEQ0] = 0.5f;
            q.vals[HF_AMP_MV_GEQ2] = 0.547500014f; q.vals[HF_AMP_MV_GEQ3] = 0.519999981f;
            q.vals[HF_AMP_MV_GEQ4] = 0.555000007f;
        }
        {   // bank04-D  Cardinal Lead
            Preset& q = S(3, 3);
            q.vals[HF_DR_DRIVE] = 0.3125f; q.vals[HF_AMP_GAIN] = 0.379999995f;
            q.vals[HF_AMP_BASS] = 0.284999996f; q.vals[HF_AMP_TREBLE] = 0.514999986f;
            q.vals[HF_AMP_PRESENCE] = 0.532500029f; q.vals[HF_AMP_MV_GEQ2] = 0.467500001f;
            q.vals[HF_AMP_MV_GEQ3] = 0.584999979f;
        }
        {   // bank05-B  Berlin Wall Pulse
            Preset& q = S(4, 1);
            q.vals[HF_CP_ATTACK] = 3.7750001f; q.vals[HF_CP_RELEASE] = 6.0250001f;
            q.vals[HF_CP_MAKEUP] = 5.4000001f; q.vals[HF_FZ_POS] = 9.0f;
            q.vals[HF_DR_POS] = 3.0f; q.vals[HF_DR_MODEL] = 9.0f;
            q.vals[HF_DR_DRIVE] = 0.189999998f; q.vals[HF_DR_TONE] = 0.55250001f;
            q.vals[HF_DR_LEVEL] = 0.752499998f; q.vals[HF_AMP_GAIN] = 0.307500005f;
            q.vals[HF_AMP_MASTER] = 0.762499988f; q.vals[HF_MD_POS] = 4.0f;
            q.vals[HF_MD_MIX] = 0.402500004f; q.vals[HF_DL_POS] = 7.0f;
            q.vals[HF_RV_POS] = 8.0f; q.vals[HF_WH_POS] = 11.0f; q.vals[HF_OC_POS] = 12.0f;
            q.vals[HF_NAIL_POS] = 13.0f; q.vals[HF_EQ_POS] = 10.0f;
            q.vals[HF_CAB_SPKDRIVE] = 1.0f;
        }
        {   // bank05-D  Gravity Lead
            Preset& q = S(4, 3);
            q.vals[HF_DR_DRIVE] = 0.354999989f; q.vals[HF_DR_LEVEL] = 0.430000007f;
        }
        {   // bank06-C  Little Feather
            Preset& q = S(5, 2);
            q.vals[HF_OUT_LEVEL] = -10.5f;
        }
        {   // bank07-D  Solar Monolith
            Preset& q = S(6, 3);
            q.vals[HF_OUT_LEVEL] = -17.8799992f;
        }
        {   // bank08-C  Flatliner
            Preset& q = S(7, 2);
            q.vals[HF_DR_LEVEL] = 0.495000005f; q.vals[HF_AMP_GAIN] = 0.305000007f;
            q.vals[HF_AMP_BASS] = 0.204999998f;
        }
        {   // bank08-D  Prayer Djent
            Preset& q = S(7, 3);
            q.vals[HF_OUT_LEVEL] = -9.96000004f; q.vals[HF_DR_DRIVE] = 0.0125000002f;
            q.vals[HF_DR_TONE] = 0.644999981f; q.vals[HF_DR_LEVEL] = 0.670000017f;
            q.vals[HF_AMP_GAIN] = 0.262499988f; q.vals[HF_AMP_BASS] = 0.337500006f;
        }
        {   // bank09-A  Bridge Vibe
            Preset& q = S(8, 0);
            q.vals[HF_AMP_GAIN] = 0.670000017f;
        }
        {   // bank10-A  Surf Splash
            Preset& q = S(9, 0);
            q.vals[HF_OUT_LEVEL] = -2.4000001f; q.vals[HF_AMP_GAIN] = 0.25999999f;
            q.vals[HF_RV_DECAY] = 3.67475009f; q.vals[HF_RV_DAMPING] = 0.358875006f;
        }
        {   // bank10-D  Moondust Glam
            Preset& q = S(9, 3);
            q.vals[HF_FZ_SUSTAIN] = 0.567499995f; q.vals[HF_FZ_BIAS] = 0.647499979f;
            q.vals[HF_FZ_INPUTTRIM] = 0.392500013f; q.vals[HF_AMP_GAIN] = 0.435000002f;
            q.vals[HF_AMP_MID] = 0.547500014f; q.vals[HF_AMP_TREBLE] = 0.582499981f;
            q.vals[HF_AMP_PRESENCE] = 0.495000005f; q.vals[HF_AMP_PL_VOL2] = 0.465000004f;
        }
        {   // bank11-D  Regal Solo
            Preset& q = S(10, 3);
            q.vals[HF_CP_POS] = 8.0f; q.vals[HF_FZ_POS] = 9.0f; q.vals[HF_DR_POS] = 2.0f;
            q.vals[HF_AMP_POS] = 3.0f; q.vals[HF_AMP_GAIN] = 0.152500004f;
            q.vals[HF_AMP_PRESENCE] = 0.502499998f; q.vals[HF_CAB_POS] = 4.0f;
            q.vals[HF_MD_POS] = 10.0f; q.vals[HF_DL_POS] = 5.0f;
            q.vals[HF_DL_TIME] = 510.744995f; q.vals[HF_RV_POS] = 6.0f;
            q.vals[HF_WH_POS] = 11.0f; q.vals[HF_OC_POS] = 12.0f; q.vals[HF_NAIL_POS] = 13.0f;
            q.vals[HF_EQ_POS] = 7.0f; q.vals[HF_EQ_ENABLE] = 1.0f;
        }
        {   // bank12-A  March Stabs
            Preset& q = S(11, 0);
            q.vals[HF_GT_THRESH] = -46.0f; q.vals[HF_CP_POS] = 7.0f; q.vals[HF_FZ_POS] = 8.0f;
            q.vals[HF_DR_POS] = 9.0f; q.vals[HF_AMP_GAIN] = 0.462500006f;
            q.vals[HF_AMP_BASS] = 0.57249999f; q.vals[HF_AMP_MID] = 0.5625f;
            q.vals[HF_AMP_TREBLE] = 0.625f; q.vals[HF_AMP_PRESENCE] = 0.675000012f;
            q.vals[HF_AMP_MASTER] = 0.725000024f; q.vals[HF_CAB_POS] = 5.0f;
            q.vals[HF_CAB_ENABLE] = 1.0f; q.vals[HF_CAB_HIGHCUT] = 9155.0f;
            q.vals[HF_CAB_MIX] = 0.247500002f; q.vals[HF_OC_UP] = 0.354999989f;
            q.vals[HF_OC_DOWN] = 0.0700000003f; q.vals[HF_OC_BYPASS] = 1.0f;
            q.vals[HF_NAIL_DRIVE] = 0.790000021f; q.vals[HF_NAIL_TONE] = 0.492500007f;
            q.vals[HF_NAIL_TEXTURE] = 0.0f; q.vals[HF_CAB_MICPOS] = 0.141287997f;
            q.vals[HF_CAB_MICDIST] = 0.058823999f; q.vals[HF_EQ_POS] = 6.0f;
            q.vals[HF_RB_GAIN] = 0.472499996f;
            std::strncpy(q.irPath, "@doom", kPathMax-1); q.irPath[kPathMax-1] = 0;
        }
        {   // bank13-D  Castaway Groove
            Preset& q = S(12, 3);
            q.vals[HF_OUT_LEVEL] = -17.1599998f; q.vals[HF_DR_DRIVE] = 0.0f;
            q.vals[HF_DR_LEVEL] = 0.882499993f; q.vals[HF_AMP_GAIN] = 0.344999999f;
            q.vals[HF_AMP_MID] = 0.625f; q.vals[HF_AMP_PRESENCE] = 0.5f;
        }
        {   // bank14-B  Spectrum Rhythm
            Preset& q = S(13, 1);
            q.vals[HF_OUT_LEVEL] = -16.6200008f; q.vals[HF_DR_DRIVE] = 0.0199999996f;
            q.vals[HF_DR_LEVEL] = 1.0f; q.vals[HF_AMP_GAIN] = 0.382499993f;
            q.vals[HF_AMP_BASS] = 0.0500000007f; q.vals[HF_AMP_MID] = 0.649999976f;
            q.vals[HF_AMP_TREBLE] = 0.699999988f; q.vals[HF_AMP_RC_VARIAC] = 1.0f;
            q.vals[HF_AMP_RC_RECT] = 1.0f;
        }
        {   // bank14-C  Spectrum Lead
            Preset& q = S(13, 2);
            q.vals[HF_OUT_LEVEL] = -19.6800003f;
        }
        {   // bank15-A  Plug-In Junior
            Preset& q = S(14, 0);
            q.vals[HF_FZ_SUSTAIN] = 0.605000019f; q.vals[HF_FZ_BIAS] = 0.3125f;
            q.vals[HF_AMP_GAIN] = 0.472499996f; q.vals[HF_RB_GAIN] = 0.234999999f;
        }
        {   // bank15-C  Retro Poland
            Preset& q = S(14, 2);
            q.vals[HF_DL_BYPASS] = 0.0f;
        }
        {   // bank16-A  Blind Squall
            Preset& q = S(15, 0);
            q.vals[HF_OUT_LEVEL] = -22.3999996f; q.vals[HF_AMP_GAIN] = 0.737500012f;
            q.vals[HF_AMP_BASS] = 0.492500007f;
        }
    }
    // Loudness parity after the JCM800 re-fit (audit 2026-09-04). ONLY presets
    // the user did NOT hand-dial are compensated: where a slot carries their own
    // baked level (March Stabs, Plug-In Junior) that choice is theirs and stands,
    // even though the amp under it changed — those want an ear check, not a trim.
    {
        auto trim = [&](int b, int sl, float dB) {
            Preset& pr = p->presets[b][sl];
            if (pr.used) pr.vals[HF_OUT_LEVEL] += dB;
        };
        // Vox span 1.8->1.3 (rev 143 noise pass) took these down with it. All
        // factory slots — none carry a user level, so re-levelling is safe.
        trim(1, 0, +1.53f);    // Regal Sustain
        trim(1, 3, +3.31f);    // Come As Water   (peak-capped: full +4.00 pushed it past its old peak)
        trim(9, 1, +2.57f);    // Apache Echo     (peak-capped)
        trim(10, 0, +3.62f);   // Innerspeaker Swirl (peak-capped)
        trim(10, 1, +0.84f);   // Glide Wall
        trim(10, 2, +3.56f);   // Streets Chime
        trim(16, 0, +5.05f);   // Sweet Dispersion
        trim(16, 1, +3.88f);   // Homesick Saucer

        // Vox noise fix (rev 142): dropping the +3 dB input drive took these six
        // Vox presets down with it. None are user-baked, so re-level them.
        trim(1, 3, +1.59f);    // Come As Water
        trim(9, 1, +1.69f);    // Apache Echo
        trim(10, 0, +1.78f);   // Innerspeaker Swirl
        trim(10, 2, +1.57f);   // Streets Chime
        trim(16, 0, +2.38f);   // Sweet Dispersion
        trim(16, 1, +1.53f);   // Homesick Saucer

        trim(6, 1, -0.69f);    // Skye (No Mod)
        trim(11, 1, -2.03f);   // World Went Away
        trim(14, 1, -1.16f);   // Knights of Fuzz
    }
    // ═══ END REV 141 ═══
}
static void psInitDefaults(HexForge* p) {
    seedFactoryPresets(p);
    p->curBank = 0; p->curSlot = 0;
    p->pendingRecall = true;   // a fresh instance starts on Bank 1 / A (Clean)
}

