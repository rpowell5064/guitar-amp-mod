// Round-trip test for the Hex Forge preset-blob port migration. Verifies
// migratePorts() preserves every pre-existing port and defaults the inserted
// ones, for the seams that matter today:
//   * v23 -> v24: [HF_IT_LOAD, HF_AMP_PAMP_COUPL] must default to {0, 0}.
//   * v19 -> v22: the Diamond Plate ports [HF_AMP_RC_MODE..HF_AMP_RC_RECT]
//     must default to {7 CH3 Modern, 0 Bold, 0 Silicon} — plain zero-fill
//     would recall CH1 Clean into old boards — plus the MT15 pair + v22 pair.
//   * v13 -> v22: the full multi-gap walk (shimmer + Cali V + md_offset +
//     NAM trims + Recto + MT15 + voice/doubler) still lands every old value.
// Only needs the generated port enum — no LV2/NAM deps.
//
// Build (from repo root, or via the build-tools CMake target):
//   cl /std:c++17 /EHsc /I lv2/hexforge build-tools/hexforge_migrate_test.cpp
#include "hexforge_ports.h"
#include <cstdint>
#include <cstring>
#include <cstdio>

// ── Contiguity guards, copied verbatim from hexforge_plugin.cpp ───────────────
static_assert(HF_OC_INTERVAL == HF_OC_MICRO + 1 && HF_OC_MICRO == HF_MD_DIV + 1 && HF_OC_INTERVAL < HF_SW_A,
              "octave shimmer ports must be contiguous, after tempo-sync and before the commands");
static_assert(HF_AMP_RC_MODE == HF_CAB_ROOMAMT + 1 && HF_AMP_RC_VARIAC == HF_AMP_RC_MODE + 1
              && HF_AMP_RC_RECT == HF_AMP_RC_MODE + 2,
              "Recto ports must be contiguous after the cab room");
static_assert(HF_AMP_MT_MODE == HF_AMP_RC_RECT + 1 && HF_AMP_MT_BRIGHT == HF_AMP_MT_MODE + 1,
              "MT15 ports must be contiguous after the Recto block");
static_assert(HF_CAB_VOICE == HF_AMP_MT_BRIGHT + 1 && HF_OUT_DOUBLER == HF_CAB_VOICE + 1,
              "cab voice + doubler must be contiguous");
static_assert(HF_EQ_POS == HF_OUT_DOUBLER + 1 && HF_EQ_BYPASS == HF_EQ_POS + 10,
              "EQ block ports must be contiguous");
static_assert(HF_IT_LOAD == HF_EQ_BYPASS + 1 && HF_AMP_PAMP_COUPL == HF_IT_LOAD + 1,
              "fidelity ports must be contiguous");
static_assert(HF_RV_DENSITY == HF_AMP_PAMP_COUPL + 1, "reverb density after fidelity pair");
static_assert(HF_RV_TYPE == HF_RV_DENSITY + 1 && HF_CAB_ROOMDENSE == HF_RV_TYPE + 1,
              "v26 ports must be contiguous");
static_assert(HF_RV_BLOOM == HF_CAB_ROOMDENSE + 1, "v27 port must be contiguous");
static_assert(HF_CAB_SPKDRIVE == HF_RV_BLOOM + 1,
              "v28 port must be contiguous");
static_assert(HF_MD_SHAPE == HF_CAB_SPKDRIVE + 1,
              "v29 port must be contiguous");
static_assert(HF_FZ_GVOL == HF_MD_SHAPE + 1 && HF_QUALITY == HF_FZ_GVOL + 1
              && HF_DR_ECO == HF_QUALITY + 1 && HF_DR2_POS == HF_DR_ECO + 1
              && HF_DR2_BYPASS == HF_DR2_POS + 9 && HF_RB_ENABLE == HF_DR2_BYPASS + 1
              && HF_RB_POL == HF_RB_ENABLE + 16 && HF_RB_RESONANCE == HF_RB_POL + 1
              && HF_RB_CABSPKDRIVE == HF_RB_RESONANCE + 44 && HF_RB_CAB2ON == HF_RB_CABSPKDRIVE + 1,
              "params: Drive 2, Rig B core, the 45-port parity family, Cab 2 presence (v37)");
static_assert(HF_GT2_POS == HF_RB_CAB2ON + 1 && HF_GT2_BYPASS == HF_GT2_POS + 7
              && HF_CP2_POS == HF_GT2_BYPASS + 1 && HF_FZ2_POS == HF_CP2_POS + 10
              && HF_NAIL2_POS == HF_FZ2_POS + 12 && HF_MD2_POS == HF_NAIL2_POS + 8
              && HF_DL2_POS == HF_MD2_POS + 12 && HF_RV2_POS == HF_DL2_POS + 17
              && HF_WH2_POS == HF_RV2_POS + 12 && HF_OC2_POS == HF_WH2_POS + 9
              && HF_EQ2_POS == HF_OC2_POS + 8 && HF_EQ2_BYPASS == HF_EQ2_POS + 10
              && HF_RB_NAM_GAIN == HF_EQ2_BYPASS + 1 && HF_RB_NAM_VOL == HF_RB_NAM_GAIN + 1,
              "v38 X2 clone families, then the v39 Amp 2 NAM trims");
static_assert(HF_FZ_ECO == HF_RB_NAM_VOL + 1 && HF_NAIL_ECO == HF_FZ_ECO + 1
              && HF_FZ2_ECO == HF_NAIL_ECO + 1 && HF_NAIL2_ECO == HF_FZ2_ECO + 1
              && HF_DR2_NAM_GAIN == HF_NAIL2_ECO + 1 && HF_DR2_NAM_VOL == HF_DR2_NAM_GAIN + 1,
              "v40: eco x4 + Drive 2 NAM trims");
static_assert(HF_AMP_PL_VARIAC == HF_DR2_NAM_VOL + 1 && HF_RB_PL_VARIAC == HF_AMP_PL_VARIAC + 1,
              "v41: the Plexi Variac pair");
static_assert(HF_DL_AGE == HF_RB_PL_VARIAC + 1 && HF_DL2_AGE == HF_DL_AGE + 1,
              "v42: the EP-3 Age pair");
static_assert(HF_AMP_SIR34 == HF_DL2_AGE + 1 && HF_RB_SIR34 == HF_SW_A - 1,
              "v43: the SIR #34 pair ends the param range");

// ── migratePorts, copied verbatim from hexforge_plugin.cpp (v28) ──────────────
// v31 inserted 14 CPU-meter outputs at HF_CPU_GT (before HF_MIDI_IN): indices at/after
// the insertion shift +14 and the region itself zero-fills. Tail checks written pre-v31
// route through these.
static bool inCpuGap(int i) { return i >= HF_CPU_GT && i < HF_CPU_GT + 26; }   // 14 meters + cpu_dr2 + cpu_rigb + 10 X2 meters
static int  preCpu(int i)   { return i >= HF_CPU_GT + 26 ? i - 26 : i; }

static void migratePorts(float* vals, uint32_t srcVer) noexcept {
    static const float vdef[5] = {0.0f, 1.0f, 0.0f, 0.0f, 4.0f};  // humbk,hbamt,hbmodel,boost,boostamt
    static const float ddef[4] = {1.0f, 0.0f, 0.0f, 0.3f};        // pattern,ducking,moddepth,modrate
    static const float wodef[13] = {10.0f, 0.0f, 0.0f, 0.4f, 0.7f, 0.5f, 0.6f, 0.8f,
                                    11.0f, 0.0f, 0.0f, 0.5f, 1.0f};
    const int itExisting = (srcVer < 4) ? 0 : (srcVer == 4) ? 2 : (srcVer == 5) ? 3 : 5;
    const int itAt = HF_IT_HUMBK + itExisting, itEnd = HF_IT_HUMBK + 5;
    const bool dlGap = (srcVer < 7);
    const int dlAt = HF_DL_PATTERN, dlEnd = HF_DL_PATTERN + 4;
    const bool woGap = (srcVer < 8);
    const int woAt = HF_WH_POS, woEnd = HF_WH_POS + 13;
    const bool byGap = (srcVer < 9);
    const int byAt = HF_GT_BYPASS, byEnd = HF_OC_BYPASS + 1;
    static const float naildef[8] = {12.0f, 0.0f, 2.0f, 0.6f, 0.5f, 0.4f, 0.5f, 0.0f};
    const bool nailGap = (srcVer < 12);
    const int nailAt = HF_NAIL_POS, nailEnd = HF_NAIL_POS + 8;
    static const float syncdef[4] = {0.0f, 5.0f, 0.0f, 2.0f};
    const bool syncGap = (srcVer < 13);
    const int syncAt = HF_DL_SYNC, syncEnd = HF_DL_SYNC + 4;
    static const float ocdef[2] = {0.0f, 0.0f};
    const bool ocGap = (srcVer < 14);
    const int ocAt = HF_OC_MICRO, ocEnd = HF_OC_MICRO + 2;
    const bool mvGap = (srcVer < 15);
    const int mvAt = HF_AMP_MV_MODE, mvEnd = HF_AMP_MV_MODE + 1;
    const bool gvGap = (srcVer < 16);
    const int gvAt = HF_AMP_MV_GEQ0, gvEnd = HF_AMP_MV_GEQ0 + 5;
    const bool eqGap = (srcVer < 17);
    const int eqAt = HF_AMP_MV_EQPRESET, eqEnd = HF_AMP_MV_EQPRESET + 1;
    const bool mdoGap = (srcVer < 18);
    const int mdoAt = HF_MD_OFFSET, mdoEnd = HF_MD_OFFSET + 1;
    const bool namGap = (srcVer < 19);
    const int namAt = HF_AMP_NAM_GAIN, namEnd = HF_AMP_NAM_GAIN + 6;
    static const float rcdef[3] = {7.0f, 0.0f, 0.0f};
    const bool rcGap = (srcVer < 20);
    const int rcAt = HF_AMP_RC_MODE, rcEnd = HF_AMP_RC_MODE + 3;
    static const float mtdef[2] = {2.0f, 0.0f};
    const bool mtGap = (srcVer < 21);
    const int mtAt = HF_AMP_MT_MODE, mtEnd = HF_AMP_MT_MODE + 2;
    // v22 appended cab voice + output doubler; defaults Room (0) / off (0).
    const bool cvGap = (srcVer < 22);
    const int cvAt = HF_CAB_VOICE, cvEnd = HF_CAB_VOICE + 2;
    static const float eqbdef[11] = {6.0f, 0,0,0,0,0,0,0,0,0,0};
    const bool eqbGap = (srcVer < 23);
    const int eqbAt = HF_EQ_POS, eqbEnd = HF_EQ_POS + 11;
    const bool fidGap = (srcVer < 24);
    const int fidAt = HF_IT_LOAD, fidEnd = HF_IT_LOAD + 2;
    const bool rdGap = (srcVer < 25);
    const int rdAt = HF_RV_DENSITY;
    const bool rtGap = (srcVer < 26);
    const int rtAt = HF_RV_TYPE, rtEnd = HF_RV_TYPE + 2;
    const bool blGap = (srcVer < 27);
    const int blAt = HF_RV_BLOOM;
    const bool spkGap = (srcVer < 28);
    const int spkAt = HF_CAB_SPKDRIVE;
    const bool shpGap = (srcVer < 29);
    const int shpAt = HF_MD_SHAPE;
    const bool gvGap2 = (srcVer < 30);
    const int gvAt2 = HF_FZ_GVOL;
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
    // v37 appended Cab 2 presence (rb_cab2on); default 0.
    const bool c2Gap = (srcVer < 37);
    const int c2At = HF_RB_CAB2ON;
    // v38 inserted the ten X2 clone families (107 ports) + 10 tail CPU meters.
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
    // v41 appended the Plexi Variac pair; default 0.
    const bool vrGap = (srcVer < 41);
    const int vrAt = HF_AMP_PL_VARIAC, vrEnd = HF_AMP_PL_VARIAC + 2;
    // v42 appended the EP-3 Echo Age pair; default 0.35 (non-zero).
    const bool agGap = (srcVer < 42);
    const int agAt = HF_DL_AGE, agEnd = HF_DL_AGE + 2;
    // v43 appended the SIR #34 mod toggles; default 0 (stock JCM800).
    const bool smGap = (srcVer < 43);
    const int smAt = HF_AMP_SIR34, smEnd = HF_AMP_SIR34 + 2;

    float old[HF_N_PORTS];
    std::memcpy(old, vals, sizeof(old));
    int o = 0;
    for (int i = 0; i < HF_N_PORTS; ++i) {
        if      (i >= itAt && i < itEnd)                 vals[i] = vdef[i - HF_IT_HUMBK];
        else if (dlGap && i >= dlAt && i < dlEnd)        vals[i] = ddef[i - dlAt];
        else if (woGap && i >= woAt && i < woEnd)        vals[i] = wodef[i - woAt];
        else if (byGap && i >= byAt && i < byEnd)        vals[i] = 0.0f;
        else if (nailGap && i >= nailAt && i < nailEnd)  vals[i] = naildef[i - nailAt];
        else if (syncGap && i >= syncAt && i < syncEnd)  vals[i] = syncdef[i - syncAt];
        else if (ocGap && i >= ocAt && i < ocEnd)        vals[i] = ocdef[i - ocAt];
        else if (mvGap && i >= mvAt && i < mvEnd)        vals[i] = 6.0f;
        else if (gvGap && i >= gvAt && i < gvEnd)        vals[i] = 0.5f;
        else if (eqGap && i >= eqAt && i < eqEnd)        vals[i] = 0.0f;
        else if (mdoGap && i >= mdoAt && i < mdoEnd)     vals[i] = 0.0f;
        else if (namGap && i >= namAt && i < namEnd)     vals[i] = 0.0f;
        else if (rcGap && i >= rcAt && i < rcEnd)        vals[i] = rcdef[i - rcAt];
        else if (mtGap && i >= mtAt && i < mtEnd)        vals[i] = mtdef[i - mtAt];
        else if (cvGap && i >= cvAt && i < cvEnd)        vals[i] = 0.0f;
        else if (eqbGap && i >= eqbAt && i < eqbEnd)     vals[i] = eqbdef[i - eqbAt];
        else if (fidGap && i >= fidAt && i < fidEnd)     vals[i] = 0.0f;
        else if (rdGap && i == rdAt)                     vals[i] = 0.0f;
        else if (rtGap && i >= rtAt && i < rtEnd)        vals[i] = 0.0f;
        else if (blGap && i == blAt)                     vals[i] = 0.5f;
        else if (spkGap && i == spkAt)                   vals[i] = 0.0f;
        else if (shpGap && i == shpAt)                   vals[i] = 0.0f;
        else if (gvGap2 && i == gvAt2)                   vals[i] = 1.0f;
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
        else if (x2Gap && i >= HF_CPU_GT2 && i <= HF_CPU_EQ2) vals[i] = 0.0f;         // X2 meters
        else if (rnGap && i >= rnAt && i < rnEnd)        vals[i] = 0.0f;              // Amp 2 NAM trims 0 dB
        else if (qwGap && i >= qwAt && i < qwEnd)        vals[i] = 0.0f;              // v40 eco/trims
        else if (vrGap && i >= vrAt && i < vrEnd)        vals[i] = 0.0f;              // v41 variac
        else if (agGap && i >= agAt && i < agEnd)        vals[i] = 0.35f;             // v42 EP-3 age
        else if (smGap && i >= smAt && i < smEnd)        vals[i] = 0.0f;              // v43 SIR #34 stock
        else                                             vals[i] = old[o++];
    }
}

int main() {
    int fails = 0;

    // ── v25 -> current: reverb type + cab room density (v26) + ambient bloom (v27)
    // + cab speaker drive (v28) + tremolo shape (v29) + fuzz guitar vol (v30)
    // all inserted at the end, in order.
    {
        const int npOld = HF_N_PORTS - 228;   // v25: 202 param + 26 cpu-region inserts
        float vals[HF_N_PORTS];
        for (int i = 0; i < HF_N_PORTS; ++i) vals[i] = 0.0f;
        for (int i = 0; i < npOld; ++i) vals[i] = static_cast<float>(i + 1);
        migratePorts(vals, 25);
        for (int i = 0; i < HF_RV_TYPE; ++i)
            if (vals[i] != static_cast<float>(i + 1)) {
                std::printf("FAIL: v25 pre-insert port %d = %g (want %d)\n", i, vals[i], i + 1); ++fails; break;
            }
        if (vals[HF_RV_TYPE] != 0.0f || vals[HF_CAB_ROOMDENSE] != 0.0f)
            { std::printf("FAIL: v26 defaults wrong\n"); ++fails; }
        if (vals[HF_RV_BLOOM] != 0.5f) { std::printf("FAIL: v27 bloom default wrong (%g)\n", vals[HF_RV_BLOOM]); ++fails; }
        if (vals[HF_CAB_SPKDRIVE] != 0.0f) { std::printf("FAIL: v28 spkdrive default wrong (%g)\n", vals[HF_CAB_SPKDRIVE]); ++fails; }
        if (vals[HF_MD_SHAPE] != 0.0f) { std::printf("FAIL: v29 shape default wrong (%g)\n", vals[HF_MD_SHAPE]); ++fails; }
        if (vals[HF_FZ_GVOL] != 1.0f) { std::printf("FAIL: v30 gvol default wrong (%g)\n", vals[HF_FZ_GVOL]); ++fails; }
        for (int i = HF_SW_A; i < HF_N_PORTS; ++i) {
            if (inCpuGap(i)) { if (vals[i] != 0.0f) { std::printf("FAIL: cpu gap port %d nonzero|", i); ++fails; break; } continue; }
            float want = (preCpu(i) - 202 < npOld) ? static_cast<float>((preCpu(i) - 202) + 1) : 0.0f;
            if (vals[i] != want) {
                std::printf("FAIL: v25 post-insert port %d = %g (want %g)\n", i, vals[i], want); ++fails; break;
            }
        }
        std::printf("v25->current: pre-insert preserved, all defaults, tail shifted +6\n");
    }

    // ── v27 -> current: speaker drive (v28) + shape (v29) + gvol (v30) at the end.
    {
        const int npOld = HF_N_PORTS - 225;   // v27: 199 param + 26 cpu
        float vals[HF_N_PORTS];
        for (int i = 0; i < HF_N_PORTS; ++i) vals[i] = 0.0f;
        for (int i = 0; i < npOld; ++i) vals[i] = static_cast<float>(i + 1);
        migratePorts(vals, 27);
        for (int i = 0; i < HF_CAB_SPKDRIVE; ++i)
            if (vals[i] != static_cast<float>(i + 1)) {
                std::printf("FAIL: v27 pre-insert port %d = %g (want %d)\n", i, vals[i], i + 1); ++fails; break;
            }
        if (vals[HF_CAB_SPKDRIVE] != 0.0f)
            { std::printf("FAIL: v28 spkdrive default wrong (%g)\n", vals[HF_CAB_SPKDRIVE]); ++fails; }
        if (vals[HF_MD_SHAPE] != 0.0f)
            { std::printf("FAIL: v29 shape default wrong (%g)\n", vals[HF_MD_SHAPE]); ++fails; }
        if (vals[HF_FZ_GVOL] != 1.0f)
            { std::printf("FAIL: v30 gvol default wrong (%g)\n", vals[HF_FZ_GVOL]); ++fails; }
        for (int i = HF_SW_A; i < HF_N_PORTS; ++i) {
            if (inCpuGap(i)) { if (vals[i] != 0.0f) { std::printf("FAIL: cpu gap port %d nonzero|", i); ++fails; break; } continue; }
            float want = (preCpu(i) - 199 < npOld) ? static_cast<float>((preCpu(i) - 199) + 1) : 0.0f;
            if (vals[i] != want) {
                std::printf("FAIL: v27 post-insert port %d = %g (want %g)\n", i, vals[i], want); ++fails; break;
            }
        }
        std::printf("v27->current: pre-insert preserved, all defaults, tail shifted +3\n");
    }

    // ── v28 -> current: tremolo shape (v29) + fuzz guitar vol (v30) at the end.
    {
        const int npOld = HF_N_PORTS - 224;   // v28: 198 param + 26 cpu
        float vals[HF_N_PORTS];
        for (int i = 0; i < HF_N_PORTS; ++i) vals[i] = 0.0f;
        for (int i = 0; i < npOld; ++i) vals[i] = static_cast<float>(i + 1);
        migratePorts(vals, 28);
        for (int i = 0; i < HF_MD_SHAPE; ++i)
            if (vals[i] != static_cast<float>(i + 1)) {
                std::printf("FAIL: v28 pre-insert port %d = %g (want %d)\n", i, vals[i], i + 1); ++fails; break;
            }
        if (vals[HF_MD_SHAPE] != 0.0f)
            { std::printf("FAIL: v29 shape default wrong (%g)\n", vals[HF_MD_SHAPE]); ++fails; }
        if (vals[HF_FZ_GVOL] != 1.0f)
            { std::printf("FAIL: v30 gvol default wrong (%g)\n", vals[HF_FZ_GVOL]); ++fails; }
        for (int i = HF_SW_A; i < HF_N_PORTS; ++i) {
            if (inCpuGap(i)) { if (vals[i] != 0.0f) { std::printf("FAIL: cpu gap port %d nonzero|", i); ++fails; break; } continue; }
            float want = (preCpu(i) - 198 < npOld) ? static_cast<float>((preCpu(i) - 198) + 1) : 0.0f;
            if (vals[i] != want) {
                std::printf("FAIL: v28 post-insert port %d = %g (want %g)\n", i, vals[i], want); ++fails; break;
            }
        }
        std::printf("v28->current: pre-insert preserved, both defaults, tail shifted +2\n");
    }

    // ── v29 -> v30: Fuzz Guitar Vol (roadmap #45) inserted at the very end, alone.
    // Its default is 1.0 (NOT zero) — full guitar volume = bit-identical voicing.
    {
        const int npOld = HF_N_PORTS - 223;   // v29: 197 param + 26 cpu
        float vals[HF_N_PORTS];
        for (int i = 0; i < HF_N_PORTS; ++i) vals[i] = 0.0f;
        for (int i = 0; i < npOld; ++i) vals[i] = static_cast<float>(i + 1);
        migratePorts(vals, 29);
        for (int i = 0; i < HF_FZ_GVOL; ++i)
            if (vals[i] != static_cast<float>(i + 1)) {
                std::printf("FAIL: v29 pre-insert port %d = %g (want %d)\n", i, vals[i], i + 1); ++fails; break;
            }
        if (vals[HF_FZ_GVOL] != 1.0f)
            { std::printf("FAIL: v30 gvol default wrong (%g)\n", vals[HF_FZ_GVOL]); ++fails; }
        for (int i = HF_SW_A; i < HF_N_PORTS; ++i) {
            if (inCpuGap(i)) { if (vals[i] != 0.0f) { std::printf("FAIL: cpu gap port %d nonzero|", i); ++fails; break; } continue; }
            float want = (preCpu(i) - 197 < npOld) ? static_cast<float>((preCpu(i) - 197) + 1) : 0.0f;
            if (vals[i] != want) {
                std::printf("FAIL: v29 post-insert port %d = %g (want %g)\n", i, vals[i], want); ++fails; break;
            }
        }
        std::printf("v29->v30: pre-insert preserved, gvol FULL default, tail shifted +1\n");
    }

    // ── v19 -> current: rc3 + mt2 + cv2 + EQ11 + fidelity2 + density1 + type/room2
    // + bloom1 + spkdrive1 + shape1 + gvol1 = 27 inserted. A v19 blob had HF_N_PORTS - 27.
    {
        const int npOld = HF_N_PORTS - 249;   // v19: 223 param + 26 cpu
        float vals[HF_N_PORTS];
        for (int i = 0; i < HF_N_PORTS; ++i) vals[i] = 0.0f;
        for (int i = 0; i < npOld; ++i) vals[i] = static_cast<float>(i + 1);   // sentinels

        migratePorts(vals, 19);

        for (int i = 0; i < HF_AMP_RC_MODE; ++i)
            if (vals[i] != static_cast<float>(i + 1)) {
                std::printf("FAIL: v19 pre-insert port %d = %g (want %d)\n", i, vals[i], i + 1); ++fails; break;
            }
        if (vals[HF_AMP_RC_MODE]   != 7.0f) { std::printf("FAIL: rc_mode not 7 (%g)\n",  vals[HF_AMP_RC_MODE]);   ++fails; }
        if (vals[HF_AMP_RC_VARIAC] != 0.0f) { std::printf("FAIL: rc_variac not 0 (%g)\n", vals[HF_AMP_RC_VARIAC]); ++fails; }
        if (vals[HF_AMP_RC_RECT]   != 0.0f) { std::printf("FAIL: rc_rect not 0 (%g)\n",   vals[HF_AMP_RC_RECT]);   ++fails; }
        if (vals[HF_AMP_MT_MODE]   != 2.0f) { std::printf("FAIL: v19 mt_mode not 2 (%g)\n",   vals[HF_AMP_MT_MODE]);   ++fails; }
        if (vals[HF_AMP_MT_BRIGHT] != 0.0f) { std::printf("FAIL: v19 mt_bright not 0 (%g)\n", vals[HF_AMP_MT_BRIGHT]); ++fails; }
        if (vals[HF_CAB_VOICE]   != 0.0f) { std::printf("FAIL: v19 cab_voice not 0 (%g)\n",   vals[HF_CAB_VOICE]);   ++fails; }
        if (vals[HF_OUT_DOUBLER] != 0.0f) { std::printf("FAIL: v19 out_doubler not 0 (%g)\n", vals[HF_OUT_DOUBLER]); ++fails; }
        if (vals[HF_EQ_POS]      != 6.0f) { std::printf("FAIL: v19 eq_pos not 6 (%g)\n",      vals[HF_EQ_POS]);      ++fails; }
        if (vals[HF_RV_BLOOM] != 0.5f) { std::printf("FAIL: v19 bloom default wrong (%g)\n", vals[HF_RV_BLOOM]); ++fails; }
        if (vals[HF_CAB_SPKDRIVE] != 0.0f) { std::printf("FAIL: v19 spkdrive default wrong (%g)\n", vals[HF_CAB_SPKDRIVE]); ++fails; }
        if (vals[HF_MD_SHAPE] != 0.0f) { std::printf("FAIL: v19 shape default wrong (%g)\n", vals[HF_MD_SHAPE]); ++fails; }
        if (vals[HF_FZ_GVOL] != 1.0f) { std::printf("FAIL: v19 gvol default wrong (%g)\n", vals[HF_FZ_GVOL]); ++fails; }
        // Command/status slots after the inserts: shifted up by 27.
        for (int i = HF_SW_A; i < HF_N_PORTS; ++i) {
            if (inCpuGap(i)) { if (vals[i] != 0.0f) { std::printf("FAIL: cpu gap port %d nonzero|", i); ++fails; break; } continue; }
            float want = (preCpu(i) - 223 < npOld) ? static_cast<float>((preCpu(i) - 223) + 1) : 0.0f;
            if (vals[i] != want) {
                std::printf("FAIL: v19 post-insert port %d = %g (want %g)\n", i, vals[i], want); ++fails; break;
            }
        }
        std::printf("v19->current: pre-insert preserved, all gap defaults, tail shifted +27\n");
    }

    // ── v13 -> v20: the full multi-gap walk. Count the inserted slots for a v13
    // source (oc 2 + mv 1 + geq 5 + eqpreset 1 + mdo 1 + nam 6 + rc 3 = 19) and
    // verify the first old value after each gap lands where the walk says.
    {
        const int inserted = 2 + 1 + 5 + 1 + 1 + 6 + 3 + 2 + 2 + 11 + 2 + 1 + 2 + 1 + 1 + 1 + 1 + 16 + 1 + 1 + 10 + 17 + 45 + 1 + 107 + 10 + 2 + 6 + 2 + 2 + 2;  // ... + v39 trims 2 + v40 6 + v41 variac 2 + v42 age 2 + v43 sir34 2
        const int npOld = HF_N_PORTS - inserted;
        float vals[HF_N_PORTS];
        for (int i = 0; i < HF_N_PORTS; ++i) vals[i] = 0.0f;
        for (int i = 0; i < npOld; ++i) vals[i] = static_cast<float>(i + 1);

        migratePorts(vals, 13);

        if (vals[HF_OC_MICRO] != 0.0f || vals[HF_OC_INTERVAL] != 0.0f)
            { std::printf("FAIL: v13 oc ports not defaulted\n"); ++fails; }
        if (vals[HF_AMP_MV_MODE] != 6.0f)
            { std::printf("FAIL: v13 mv_mode not 6 (%g)\n", vals[HF_AMP_MV_MODE]); ++fails; }
        for (int b = 0; b < 5; ++b)
            if (vals[HF_AMP_MV_GEQ0 + b] != 0.5f)
                { std::printf("FAIL: v13 geq%d not 0.5\n", b); ++fails; break; }
        if (vals[HF_AMP_RC_MODE] != 7.0f || vals[HF_AMP_RC_VARIAC] != 0.0f || vals[HF_AMP_RC_RECT] != 0.0f)
            { std::printf("FAIL: v13 rc ports not defaulted {7,0,0}\n"); ++fails; }
        if (vals[HF_AMP_MT_MODE] != 2.0f || vals[HF_AMP_MT_BRIGHT] != 0.0f)
            { std::printf("FAIL: v13 mt ports not defaulted {2,0}\n"); ++fails; }
        if (vals[HF_CAB_VOICE] != 0.0f || vals[HF_OUT_DOUBLER] != 0.0f)
            { std::printf("FAIL: v13 voice/doubler ports not defaulted {0,0}\n"); ++fails; }
        if (vals[HF_EQ_POS] != 6.0f || vals[HF_EQ_BYPASS] != 0.0f)
            { std::printf("FAIL: v13 EQ ports not defaulted\n"); ++fails; }
        if (vals[HF_IT_LOAD] != 0.0f || vals[HF_AMP_PAMP_COUPL] != 0.0f)
            { std::printf("FAIL: v13 fidelity ports not defaulted\n"); ++fails; }
        if (vals[HF_RV_DENSITY] != 0.0f)
            { std::printf("FAIL: v13 rv_density not defaulted\n"); ++fails; }
        if (vals[HF_RV_TYPE] != 0.0f || vals[HF_CAB_ROOMDENSE] != 0.0f)
            { std::printf("FAIL: v13 v26 ports not defaulted\n"); ++fails; }
        if (vals[HF_RV_BLOOM] != 0.5f) { std::printf("FAIL: v13 bloom default wrong (%g)\n", vals[HF_RV_BLOOM]); ++fails; }
        if (vals[HF_CAB_SPKDRIVE] != 0.0f) { std::printf("FAIL: v13 spkdrive default wrong (%g)\n", vals[HF_CAB_SPKDRIVE]); ++fails; }
        if (vals[HF_MD_SHAPE] != 0.0f) { std::printf("FAIL: v13 shape default wrong (%g)\n", vals[HF_MD_SHAPE]); ++fails; }
        if (vals[HF_FZ_GVOL] != 1.0f) { std::printf("FAIL: v13 gvol default wrong (%g)\n", vals[HF_FZ_GVOL]); ++fails; }
        // Every non-gap slot must hold consecutive sentinels in order (the walk is
        // order-preserving); just verify the LAST old value survived to the end.
        int o = 0; float lastSeen = -1.0f;
        for (int i = 0; i < HF_N_PORTS; ++i) {
            const bool gap = (i >= HF_OC_MICRO && i <= HF_OC_INTERVAL)
                          || (i >= HF_AMP_MV_MODE && i <= HF_AMP_MV_EQPRESET)
                          || (i == HF_MD_OFFSET)
                          || (i >= HF_AMP_NAM_GAIN && i <= HF_AMP_NAM_GAIN + 5)
                          || (i >= HF_AMP_RC_MODE && i <= HF_AMP_RC_RECT)
                          || (i >= HF_AMP_MT_MODE && i <= HF_AMP_MT_BRIGHT)
                          || (i >= HF_CAB_VOICE && i <= HF_OUT_DOUBLER)
                          || (i >= HF_EQ_POS && i <= HF_EQ_BYPASS)
                          || (i >= HF_IT_LOAD && i <= HF_AMP_PAMP_COUPL)
                          || (i == HF_RV_DENSITY)
                          || (i >= HF_RV_TYPE && i <= HF_CAB_ROOMDENSE)
                          || (i == HF_RV_BLOOM)
                          || (i == HF_CAB_SPKDRIVE)
                          || (i == HF_MD_SHAPE)
                          || (i == HF_FZ_GVOL)
                          || (i == HF_QUALITY)
                          || (i == HF_DR_ECO)
                          || (i >= HF_DR2_POS && i <= HF_DR2_BYPASS)
                          || (i >= HF_RB_ENABLE && i <= HF_RB_CABSPKDRIVE)
                          || (i == HF_RB_CAB2ON)
                          || (i >= HF_GT2_POS && i <= HF_EQ2_BYPASS)
                          || (i >= HF_RB_NAM_GAIN && i <= HF_RB_NAM_VOL)
                          || (i >= HF_FZ_ECO && i <= HF_DR2_NAM_VOL)
                          || (i >= HF_AMP_PL_VARIAC && i <= HF_RB_PL_VARIAC)
                          || (i >= HF_DL_AGE && i <= HF_DL2_AGE)
                          || (i >= HF_AMP_SIR34 && i <= HF_RB_SIR34)
                          || inCpuGap(i);
            if (gap) continue;
            const float want = (o < npOld) ? static_cast<float>(o + 1) : 0.0f;
            if (vals[i] != want) {
                std::printf("FAIL: v13 walk port %d = %g (want %g)\n", i, vals[i], want); ++fails; break;
            }
            if (vals[i] > 0.0f) lastSeen = vals[i];
            ++o;
        }
        if (lastSeen != static_cast<float>(npOld))
            { std::printf("FAIL: v13 last old value %g != %d\n", lastSeen, npOld); ++fails; }
        std::printf("v13->v26: all gaps defaulted, old values order-preserved\n");
    }

    // ── v37 -> v38: ONLY the X2 clone families insert; every old value keeps its
    // slot up to HF_GT2_POS, the clones take their parked defaults, tail shifts +119.
    {
        const int npOld = HF_N_PORTS - 131;   // 121 param (X2 107 + v39 2 + v40 6 + v41 2 + v42 2 + v43 2) + 10 cpu inserts
        float vals[HF_N_PORTS];
        for (int i = 0; i < HF_N_PORTS; ++i) vals[i] = 0.0f;
        for (int i = 0; i < npOld; ++i) vals[i] = static_cast<float>(i + 1);
        migratePorts(vals, 37);
        for (int i = 0; i < HF_GT2_POS; ++i)
            if (vals[i] != static_cast<float>(i + 1)) {
                std::printf("FAIL: v37 pre-insert port %d = %g (want %d)\n", i, vals[i], i + 1); ++fails; break;
            }
        if (vals[HF_GT2_POS] != 15.0f)  { std::printf("FAIL: gt2_pos not 15 (%g)\n",  vals[HF_GT2_POS]);  ++fails; }
        if (vals[HF_EQ2_POS] != 24.0f)  { std::printf("FAIL: eq2_pos not 24 (%g)\n",  vals[HF_EQ2_POS]);  ++fails; }
        if (vals[HF_FZ2_GVOL] != 1.0f)  { std::printf("FAIL: fz2_gvol not 1 (%g)\n",  vals[HF_FZ2_GVOL]); ++fails; }
        if (vals[HF_DL2_TIME] != 250.0f){ std::printf("FAIL: dl2_time not 250 (%g)\n",vals[HF_DL2_TIME]); ++fails; }
        if (vals[HF_GT2_ENABLE] != 0.0f || vals[HF_EQ2_ENABLE] != 0.0f)
            { std::printf("FAIL: X2 enables not 0\n"); ++fails; }
        if (vals[HF_RB_NAM_GAIN] != 0.0f || vals[HF_RB_NAM_VOL] != 0.0f)
            { std::printf("FAIL: v39 NAM trims not 0\n"); ++fails; }
        if (vals[HF_FZ_ECO] != 0.0f || vals[HF_DR2_NAM_VOL] != 0.0f)
            { std::printf("FAIL: v40 eco/trims not 0\n"); ++fails; }
        if (vals[HF_DL_AGE] != 0.35f || vals[HF_DL2_AGE] != 0.35f)
            { std::printf("FAIL: v42 EP-3 age not 0.35\n"); ++fails; }
        if (vals[HF_AMP_SIR34] != 0.0f || vals[HF_RB_SIR34] != 0.0f)
            { std::printf("FAIL: v43 SIR #34 not stock\n"); ++fails; }
        for (int i = HF_SW_A; i < HF_N_PORTS; ++i) {
            if (i >= HF_CPU_GT2 && i <= HF_CPU_EQ2) { if (vals[i] != 0.0f) { std::printf("FAIL: x2 cpu port %d nonzero\n", i); ++fails; break; } continue; }
            float want = (i >= HF_CPU_GT2 ? i - 131 : i - 121) < npOld
                       ? static_cast<float>((i >= HF_CPU_GT2 ? i - 131 : i - 121) + 1) : 0.0f;
            if (vals[i] != want) {
                std::printf("FAIL: v37 post-insert port %d = %g (want %g)\n", i, vals[i], want); ++fails; break;
            }
        }
        std::printf("v37->v42: pre-insert preserved, X2 parked defaults + NAM trims + EP-3 age, tail shifted +121\n");
    }

    std::printf("\nHF_N_PORTS=%d  HF_AMP_RC_MODE=%d  HF_CAB_VOICE=%d  HF_SW_A=%d\n",
                (int)HF_N_PORTS, (int)HF_AMP_RC_MODE, (int)HF_CAB_VOICE, (int)HF_SW_A);
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
