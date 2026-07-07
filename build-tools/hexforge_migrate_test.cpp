// Round-trip test for the v13 -> v14 Hex Forge preset-blob migration (the Octave
// microtonal shimmer insert). Verifies migratePorts() preserves every pre-existing
// port and defaults the two new ones (oc_micro, oc_interval) to 0. Only needs the
// generated port enum — no LV2/NAM deps.
//
// Build (from repo root):
//   cl /std:c++17 /EHsc /I lv2/hexforge build-tools/hexforge_migrate_test.cpp
#include "hexforge_ports.h"
#include <cstdint>
#include <cstring>
#include <cstdio>

// ── Contiguity guards, copied verbatim from hexforge_plugin.cpp ───────────────
static_assert(HF_OC_INTERVAL == HF_OC_MICRO + 1 && HF_OC_MICRO == HF_MD_DIV + 1 && HF_OC_INTERVAL < HF_SW_A,
              "octave shimmer ports must be contiguous, after tempo-sync and before the commands");

// ── migratePorts, copied verbatim from hexforge_plugin.cpp (v14) ──────────────
static void migratePorts(float* vals, uint32_t srcVer) noexcept {
    static const float vdef[5] = {0.0f, 1.0f, 0.0f, 0.0f, 4.0f};
    static const float ddef[4] = {1.0f, 0.0f, 0.0f, 0.3f};
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
        else                                             vals[i] = old[o++];
    }
}

int main() {
    int fails = 0;

    // Simulate a v13 blob: it had HF_N_PORTS - 2 params (no oc_micro/oc_interval).
    // The deserializer memcpy's them into a zeroed HF_N_PORTS array (tail = 0).
    const int npOld = HF_N_PORTS - 2;
    float vals[HF_N_PORTS];
    for (int i = 0; i < HF_N_PORTS; ++i) vals[i] = 0.0f;
    for (int i = 0; i < npOld; ++i) vals[i] = static_cast<float>(i + 1);   // sentinels 1..npOld

    migratePorts(vals, 13);

    // Ports before the insert: unchanged.
    for (int i = 0; i < HF_OC_MICRO; ++i)
        if (vals[i] != static_cast<float>(i + 1)) {
            std::printf("FAIL: pre-insert port %d = %g (want %d)\n", i, vals[i], i + 1); ++fails; break;
        }
    // The two new ports default to 0.
    if (vals[HF_OC_MICRO]    != 0.0f) { std::printf("FAIL: oc_micro not defaulted (%g)\n", vals[HF_OC_MICRO]); ++fails; }
    if (vals[HF_OC_INTERVAL] != 0.0f) { std::printf("FAIL: oc_interval not defaulted (%g)\n", vals[HF_OC_INTERVAL]); ++fails; }
    // Ports after the insert: shifted up by 2 (old value that sat 2 slots earlier).
    for (int i = HF_OC_INTERVAL + 1; i < HF_N_PORTS; ++i) {
        float want = (i - 2 < npOld) ? static_cast<float>((i - 2) + 1) : 0.0f;
        if (vals[i] != want) {
            std::printf("FAIL: post-insert port %d = %g (want %g)\n", i, vals[i], want); ++fails; break;
        }
    }
    std::printf("v13->v14: pre-insert preserved, oc ports=0, tail shifted +2\n");

    // A v3 blob (fresh factory rows) must also insert the oc gap (3 < 14).
    float v3[HF_N_PORTS];
    for (int i = 0; i < HF_N_PORTS; ++i) v3[i] = 7.0f;
    migratePorts(v3, 3);
    if (v3[HF_OC_MICRO] != 0.0f || v3[HF_OC_INTERVAL] != 0.0f) {
        std::printf("FAIL: v3 migrate did not default oc ports\n"); ++fails;
    }
    std::printf("v3->v14: oc ports defaulted\n");

    std::printf("\nHF_N_PORTS=%d  HF_OC_MICRO=%d  HF_OC_INTERVAL=%d  HF_SW_A=%d\n",
                (int)HF_N_PORTS, (int)HF_OC_MICRO, (int)HF_OC_INTERVAL, (int)HF_SW_A);
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
