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
// ── Last-position cursor (survives a restart) ─────────────────────────────────
// Written on every preset change (the same moment the LCD status file is written);
// read on load and preferred over the State's cursor, which is only as fresh as the
// last board save. A missing or invalid file, or one pointing at an empty slot,
// leaves the cursor as it was (slot A of that bank is tried first).
static void hfWriteCursorFile(HexForge* p) {
    hfplat::ensureConfigDir();
    const std::string path = hfBackupDir() + "/hexforge-cursor.txt";
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "%d %d\n", p->curBank, p->curSlot);
    std::fclose(f);
}
static void hfReadCursorFile(HexForge* p) {
    const std::string path = hfBackupDir() + "/hexforge-cursor.txt";
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return;
    int b = -1, sl = -1; const int n = std::fscanf(f, "%d %d", &b, &sl); std::fclose(f);
    if (n != 2 || b < 0 || b >= kBanks || sl < 0 || sl >= kSlots) return;
    if (!p->presets[b][sl].used) sl = 0;
    if (!p->presets[b][sl].used) return;
    p->curBank = b; p->curSlot = sl;
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
              HF_RB_SV_MIDFREQ  == HF_DR_B7K_BASS - 1,
              "v45: the Blue Liner six-port tail");
static_assert(HF_DR_B7K_BASS == HF_RB_SV_MIDFREQ + 1 && HF_DR_B7K_LOMID == HF_DR_B7K_BASS + 1 &&
              HF_DR_B7K_HIMID == HF_DR_B7K_LOMID + 1 && HF_DR_B7K_TREBLE == HF_DR_B7K_HIMID + 1 &&
              HF_DR_B7K_GRUNT == HF_DR_B7K_TREBLE + 1 && HF_DR_B7K_ATTACK == HF_DR_B7K_GRUNT + 1 &&
              HF_DR2_B7K_BASS == HF_DR_B7K_ATTACK + 1 && HF_DR2_B7K_ATTACK == HF_DR2_B7K_BASS + 5 &&
              HF_DR2_B7K_ATTACK == HF_CAB_MIC2TYPE - 1,
              "v49: the Helsinki Grind twelve-port tail");
static_assert(HF_CAB_MIC2TYPE == HF_DR2_B7K_ATTACK + 1 && HF_CAB_MIC2POS == HF_CAB_MIC2TYPE + 1 &&
              HF_CAB_MIC2DIST == HF_CAB_MIC2POS + 1 && HF_CAB_MIC2LVL == HF_CAB_MIC2DIST + 1 &&
              HF_CAB_MIC2ALIGN == HF_CAB_MIC2LVL + 1 && HF_CAB_MIC2POL == HF_CAB_MIC2ALIGN + 1 &&
              HF_RB_CABMIC2TYPE == HF_CAB_MIC2POL + 1 && HF_RB_CABMIC2POL == HF_RB_CABMIC2TYPE + 5 &&
              HF_RB_CABMIC2POL == HF_SW_A - 1,
              "v50: the Cab Mic 2 twelve-port tail ends the param range");
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
    // v49: Helsinki Grind (B7K) twelve-port tail: EQ knobs at NOON, Grunt Raw (1), Attack Flat (0), Drive 1 then Drive 2.
    static const float b7kdef[12] = {0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f};
    const bool b7kGap = (srcVer < 49);
    const int b7kAt = HF_DR_B7K_BASS, b7kEnd = HF_DR_B7K_BASS + 12;
    // v50: Cab Mic 2 twelve-port tail: Type Off, Pos/Dist 0, Blend 0.35, Align/Polarity off; Cab 1 then Rig B.
    static const float m2def[12] = {0.0f, 0.0f, 0.0f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.0f, 0.0f};
    const bool m2Gap = (srcVer < 50);
    const int m2At = HF_CAB_MIC2TYPE, m2End = HF_CAB_MIC2TYPE + 12;

    // v47 (2026-09-05): out_phase appended at the tail. Gap-fills to 1.0, NOT 0 —
    // output invert is ON by default (real amps invert; the reference rig ships inverted),
    // so an old blob must come back inverted too or the same preset would sum
    // differently before and after the upgrade.
    const bool opGap = (srcVer < 47);
    const int  opAt  = HF_OUT_PHASE;
    // v48 (2026-09-09): amp_evhcomp lab toggle at the tail; default 0 = the
    // shipped capture-fitted EVH (bit-identical).
    const bool ecGap = (srcVer < 48);
    const int  ecAt  = HF_AMP_EVHCOMP;
    const bool dynGap = (srcVer < 51);   // v51: amp_dynload global tail toggle, OFF
    const int  dynAt  = HF_AMP_DYNLOAD;
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
        else if (b7kGap && i >= b7kAt && i < b7kEnd)     vals[i] = b7kdef[i - b7kAt]; // v49 Helsinki Grind panel: noon / Raw / Flat
        else if (m2Gap && i >= m2At && i < m2End)        vals[i] = m2def[i - m2At];   // v50 Cab Mic 2: Off, blend 0.35
        else if (opGap && i == opAt)                     vals[i] = 1.0f;             // v47 output phase INVERTED (matches real amps / the reference rig)
        else if (ecGap && i == ecAt)                     vals[i] = 0.0f;             // v48 component-EVH lab toggle OFF
        else if (dynGap && i == dynAt)                   vals[i] = 0.0f;             // v51 dynamic-load lab toggle OFF
        else                                             vals[i] = old[o++];
    }
}
static void seedFactoryPresets(HexForge* p);   // fwd decl (defined after the factory table)
// kFactoryTableVer (the layout the generated rows were captured at) now lives in
// hexforge_factory_presets.h, written by the generator that owns the rows.
static void hfSerialize(HexForge* p, std::vector<uint8_t>& blob) {
    auto putBytes = [&](const void* d, size_t n){ const uint8_t* b=(const uint8_t*)d; blob.insert(blob.end(), b, b+n); };
    auto putU32   = [&](uint32_t v){ putBytes(&v, 4); };
    auto putPath  = [&](const char* s){ uint32_t len=(uint32_t)std::strlen(s); putU32(len); putBytes(s, len); };
    putU32(51); putU32(kBanks); putU32(kSlots); putU32(HF_N_PORTS); putU32(kFactoryRev);   // v51: + amp_dynload tail; v50: + the Cab Mic 2 twelve-port tail; v49: + the Helsinki Grind twelve-port tail (B7K panel); v46: Cali Ch3 value transform (no layout change); v45: + the Blue Liner (SVT) six-port tail; v44: + rb_locut; v43: + SIR #34 pair; v42: + EP-3 Age pair; v41: + Plexi Variac; v40: + eco x4 + Drive 2 NAM; v39: + Amp 2 NAM + Cab 2 IR paths; v38: + X2 clone families; v37: + Cab 2 presence; v31: + 14 CPU meter outputs (tail, pre-MIDI); v30: + fuzz guitar vol; v29: + tremolo shape; v28: + speaker drive; v27: + ambient bloom; v26: + reverb type / room density; v25: + reverb density
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
    if (ver < 2 || ver > 51) return false;   // 51 = amp_dynload tail (50 = Cab Mic 2 twelve-port tail)
    const bool migrateOutDb = (ver == 2);
    const bool needMigrate  = (ver < 51);  // ...v51 dynamic-load tail
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
    // Land where the player left off: the saved cursor (bank + slot) is restored, so a
    // restart picks up the bank they were on. Bank 1 / A remains the default only for a
    // fresh instance with no saved state (psInitDefaults). A cursor that points at an
    // empty slot falls back to slot A of that bank, then to Bank 1 / A.
    uint32_t cb=0, cs=0; getU32(cb); getU32(cs);
    int rb = static_cast<int>(cb) & (kBanks - 1), rs = static_cast<int>(cs) & (kSlots - 1);
    if (!p->presets[rb][rs].used) rs = 0;
    if (!p->presets[rb][rs].used) { rb = 0; rs = 0; }
    p->curBank = rb; p->curSlot = rs;
    if (factoryRev < kFactoryRev) seedFactoryPresets(p);   // refresh updated factory slots (user slots kept)
    hfReadCursorFile(p);   // the last position, if fresher than the State's cursor
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
// User rigs mirror (2026-09-22): hexforge-rigs.json beside the preset backup.
static void hfWriteRigs(HexForge* p) {
    std::string dir = hfBackupDir();
    hfplat::ensureConfigDir();
    const std::string path = dir + "/hexforge-rigs.json";
    const std::string tmp  = path + ".tmp";
    FILE* f = hfplat::fopenBinary(tmp.c_str(), "wb");
    if (!f) return;
    std::fwrite(p->rigsJson, 1, std::strlen(p->rigsJson), f);
    std::fclose(f);
    std::rename(tmp.c_str(), path.c_str());
}
static void hfLoadRigs(HexForge* p) {
    const std::string path = hfBackupDir() + "/hexforge-rigs.json";
    FILE* f = hfplat::fopenBinary(path.c_str(), "rb");
    if (!f) return;
    const size_t n = std::fread(p->rigsJson, 1, HexForge::kRigsMax - 1, f);
    std::fclose(f);
    p->rigsJson[n] = '\0';
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
    hfWriteCursorFile(p);   // remember the last position across a restart
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
// Write the built-in factory presets over their slots (leaves every non-factory
// slot untouched — so user presets survive). Used both for a fresh instance AND
// to REFRESH factory slots when a saved store predates the current kFactoryRev
// (otherwise updated factory presets in the binary never reach an existing user —
// the persisted .dat / pedalboard state would keep overriding them forever).
//
// 2026-09-24 REWORK: the whole set — Bank 1 included — is generated by
// build-tools/gen_hexforge_presets.py (+ preset_rework.py) as complete rows at the
// CURRENT layout, loudness-levelled from on-device measurements. The layered
// history this function used to carry (the v3 Bank-1 rows, the v30 generated
// table, the dual-rig seeds, the rev-139 research rebuild, the rev-141 user bake
// and ~150 revisions of level trims) is gone: every one of those decisions is
// either baked into the captured base (build-tools/preset_base.json) or restated
// in the rework layer, and the git history keeps the old text.
static void seedFactoryPresets(HexForge* p) {
    for (int i = 0; i < kFactoryExtraCount; ++i) {
        const HfFactoryPreset& fp = kFactoryExtra[i];
        if (fp.bank < 0 || fp.bank >= kBanks || fp.slot < 0 || fp.slot >= kSlots) continue;
        Preset& pr = p->presets[fp.bank][fp.slot];
        pr.used = true;
        std::snprintf(pr.name, sizeof(pr.name), "%s", fp.name);
        std::memcpy(pr.vals, fp.vals, sizeof(pr.vals));
        migratePorts(pr.vals, kFactoryTableVer);   // a no-op while the table is generated at the current layout
        pr.irPath[0] = pr.ir2Path[0] = pr.ampNamPath[0] = pr.drNamPath[0] = pr.cabNamPath[0] = pr.amp2NamPath[0] = pr.dr2NamPath[0] = '\0';
        if (fp.cabIr  && fp.cabIr[0])  { std::strncpy(pr.irPath,  fp.cabIr,  kPathMax - 1); pr.irPath[kPathMax - 1]  = '\0'; }
        if (fp.cabIr2 && fp.cabIr2[0]) { std::strncpy(pr.ir2Path, fp.cabIr2, kPathMax - 1); pr.ir2Path[kPathMax - 1] = '\0'; }
    }
}
static void psInitDefaults(HexForge* p) {
    seedFactoryPresets(p);
    p->curBank = 0; p->curSlot = 0;
    hfReadCursorFile(p);       // ...or the last position, when one was saved
    p->pendingRecall = true;   // a fresh instance starts on Bank 1 / A (Clean) unless a last position exists
}

