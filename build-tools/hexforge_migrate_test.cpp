// Round-trip test for the Hex Forge preset-blob port migration. Verifies
// migratePorts() preserves every pre-existing port and defaults the inserted
// ones, for the seams that matter today:
//   * v19 -> v20: the Diamond Plate (Dual Rectifier) ports [HF_AMP_RC_MODE..
//     HF_AMP_RC_RECT] must default to {7 CH3 Modern, 0 Bold, 0 Silicon} —
//     plain zero-fill would recall CH1 Clean into old boards.
//   * v13 -> v20: the full multi-gap walk (shimmer + Cali V + md_offset +
//     NAM trims + Recto) still lands every old value in its new slot.
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
              && HF_AMP_RC_RECT == HF_AMP_RC_MODE + 2 && HF_AMP_RC_RECT == HF_SW_A - 1,
              "Recto ports must be contiguous, after the cab room and right before the commands");

// ── migratePorts, copied verbatim from hexforge_plugin.cpp (v20) ──────────────
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
        else                                             vals[i] = old[o++];
    }
}

int main() {
    int fails = 0;

    // ── v19 -> v20: only the 3 Recto ports are inserted (at the very end of the
    // preset params). A v19 blob had HF_N_PORTS - 3 params; the deserializer
    // memcpy's them into a zeroed array (tail = 0).
    {
        const int npOld = HF_N_PORTS - 3;
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
        // Command/status slots after the insert: shifted up by 3.
        for (int i = HF_AMP_RC_RECT + 1; i < HF_N_PORTS; ++i) {
            float want = (i - 3 < npOld) ? static_cast<float>((i - 3) + 1) : 0.0f;
            if (vals[i] != want) {
                std::printf("FAIL: v19 post-insert port %d = %g (want %g)\n", i, vals[i], want); ++fails; break;
            }
        }
        std::printf("v19->v20: pre-insert preserved, rc defaults {7,0,0}, tail shifted +3\n");
    }

    // ── v13 -> v20: the full multi-gap walk. Count the inserted slots for a v13
    // source (oc 2 + mv 1 + geq 5 + eqpreset 1 + mdo 1 + nam 6 + rc 3 = 19) and
    // verify the first old value after each gap lands where the walk says.
    {
        const int inserted = 2 + 1 + 5 + 1 + 1 + 6 + 3;
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
        // Every non-gap slot must hold consecutive sentinels in order (the walk is
        // order-preserving); just verify the LAST old value survived to the end.
        int o = 0; float lastSeen = -1.0f;
        for (int i = 0; i < HF_N_PORTS; ++i) {
            const bool gap = (i >= HF_OC_MICRO && i <= HF_OC_INTERVAL)
                          || (i >= HF_AMP_MV_MODE && i <= HF_AMP_MV_EQPRESET)
                          || (i == HF_MD_OFFSET)
                          || (i >= HF_AMP_NAM_GAIN && i <= HF_AMP_NAM_GAIN + 5)
                          || (i >= HF_AMP_RC_MODE && i <= HF_AMP_RC_RECT);
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
        std::printf("v13->v20: all gaps defaulted, old values order-preserved\n");
    }

    std::printf("\nHF_N_PORTS=%d  HF_AMP_RC_MODE=%d  HF_SW_A=%d\n",
                (int)HF_N_PORTS, (int)HF_AMP_RC_MODE, (int)HF_SW_A);
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
