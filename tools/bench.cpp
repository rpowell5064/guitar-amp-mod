// Per-effect DSP benchmark for the Hex Chain suite.
// Times each real engine block at 48 kHz / 128-frame blocks and reports the
// fraction of ONE CPU core it uses (RT% = cpu_time / audio_time * 100).
// Also runs each block into SILENCE after priming, to expose denormal slowdowns
// in feedback paths (delay / reverb / echorec / power-amp).
//
// Build (host/native only):  cmake -B build -DGUITARAMP_BUILD_TOOLS=ON
//                            cmake --build build --target bench
// Run:  ./build/tools/bench
#include "AmpBlockExtended.h"
#include "AmpBlock.h"
#include "PowerAmpProcessor.h"
#include "CabinetBlock.h"
#include "DefaultCabIR.h"
#include "OverdriveBlock.h"
#include "OverdriveFactory.h"
#include "OversamplingWrapper.h"
#include "EHXBigMuff.h"
#include "DelayBlock.h"
#include "DelayFactory.h"
#include "PlateReverbBlock.h"
#include "CompressorBlock.h"
#include "NoiseGateBlock.h"
#include "ModulationBlock.h"
#include "DenormalGuard.h"

#include <chrono>
#include <cstdio>
#include <cmath>
#include <vector>
#include <functional>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr double FS = 48000.0;
static constexpr int    NB = 128;          // JACK buffer on the pi-Stomp
static const double BLOCK_MS = 1000.0 * NB / FS;   // 2.667 ms budget per block

using Proc = std::function<void(float**, float**, int, int)>;

// Time `blocks` process() calls; fill input with sine (or silence). Returns RT%.
static double timeProc(const Proc& proc, int nCh, int blocks, bool silent) {
    std::vector<float> l(NB), r(NB), ol(NB), orr(NB);
    float* ins[2]  = { l.data(),  r.data()  };
    float* outs[2] = { ol.data(), orr.data() };
    double ph = 0.0; const double dph = 2.0 * M_PI * 220.0 / FS;
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < NB; ++i) {
            float s = 0.0f;
            if (!silent) { s = 0.25f * std::sin(ph); ph += dph; if (ph > 2*M_PI) ph -= 2*M_PI; }
            l[i] = s; r[i] = s;
        }
        proc(ins, outs, NB, nCh);
    }
    auto t1 = std::chrono::steady_clock::now();
    const double cpu   = std::chrono::duration<double>(t1 - t0).count();
    const double audio = (double)blocks * NB / FS;
    return cpu / audio * 100.0;
}

static const int kWarm = (int)(FS * 0.25 / NB);   // 0.25 s warm-up
static const int kRun  = (int)(FS * 4.0  / NB);   // 4 s measurement

static void run(const char* name, int nCh, const Proc& proc, bool testSilence) {
    timeProc(proc, nCh, kWarm, false);                 // warm up (prime buffers)
    const double sig = timeProc(proc, nCh, kRun, false);
    if (testSilence) {
        const double sil = timeProc(proc, nCh, kRun, true);
        printf("  %-22s %6.2f%%   %6.2f%%   %s\n", name, sig, sil,
               sil > sig * 1.5 ? "<-- DENORMAL SLOWDOWN" : "");
    } else {
        printf("  %-22s %6.2f%%      -\n", name, sig);
    }
}

int main() {
    DenormalGuard dg;   // mirror the real-time plugin: flush denormals
    printf("Hex Chain per-effect DSP cost — 48 kHz, %d-frame blocks (%.2f ms budget/block)\n",
           NB, BLOCK_MS);
    printf("RT%% = %% of ONE core. A serial chain sums these.\n\n");
    printf("  %-22s %7s   %7s\n", "effect", "signal", "silence");
    printf("  %-22s %7s   %7s\n", "------", "------", "-------");

    { OverdriveBlock d; d.prepare(FS,NB,1); d.setType(OverdriveType::TubeScreamer808);
      run("Drive: TS-808", 1, [&](float**i,float**o,int n,int c){d.process(i,o,n,c);}, false); }
    { OverdriveBlock d; d.prepare(FS,NB,1); d.setType(OverdriveType::LifePedal);
      run("Drive: Life Pedal", 1, [&](float**i,float**o,int n,int c){d.process(i,o,n,c);}, false); }
    { OverdriveBlock d; d.prepare(FS,NB,1); d.setType(OverdriveType::ProcoRAT);
      run("Drive: ProCo RAT", 1, [&](float**i,float**o,int n,int c){d.process(i,o,n,c);}, false); }
    { OversamplingWrapper f(std::make_unique<EHXBigMuff>()); f.prepare(FS,NB,1); f.setParameter("era",2);
      run("Fuzz (Muff)", 1, [&](float**i,float**o,int n,int c){f.process(i,o,n,c);}, false); }

    { AmpBlockExtended a; a.prepare(FS,NB,2); a.setAmpModel(AmpModel::FenderDeluxe);
      PowerAmpProcessor pa; pa.prepare(FS,NB,2); pa.setTubeType(TubeType::Tube_6L6GC);
      run("Amp: Fender + PA", 2, [&](float**i,float**o,int n,int c){ a.process(i,o,n,c); pa.process(o,o,n,c); }, true); }
    { AmpBlockExtended a; a.prepare(FS,NB,2); a.setAmpModel(AmpModel::MarshallJCM800);
      PowerAmpProcessor pa; pa.prepare(FS,NB,2); pa.setTubeType(TubeType::Tube_EL34);
      run("Amp: JCM800 + PA", 2, [&](float**i,float**o,int n,int c){ a.process(i,o,n,c); pa.process(o,o,n,c); }, true); }
    { AmpBlockExtended a; a.prepare(FS,NB,2); a.setAmpModel(AmpModel::EVH5150III);
      PowerAmpProcessor pa; pa.prepare(FS,NB,2); pa.setTubeType(TubeType::Tube_EL34);
      run("Amp: 5150 + power amp", 2, [&](float**i,float**o,int n,int c){ a.process(i,o,n,c); pa.process(o,o,n,c); }, true); }
    { AmpBlockExtended a; a.prepare(FS,NB,2); a.setAmpModel(AmpModel::OrangeRockerverb50);
      PowerAmpProcessor pa; pa.prepare(FS,NB,2); pa.setTubeType(TubeType::Tube_EL34);
      run("Amp: Rockerverb + PA", 2, [&](float**i,float**o,int n,int c){ a.process(i,o,n,c); pa.process(o,o,n,c); }, true); }
    { AmpBlockExtended a; a.prepare(FS,NB,2); a.setAmpModel(AmpModel::MesaDualRectifier);
      a.setParameter("mode", 7.0f);   // CH3 Modern = worst case (5 stages + DNR + post-clip voicing)
      PowerAmpProcessor pa; pa.prepare(FS,NB,2); pa.setTubeType(TubeType::Tube_6L6GC);
      run("Amp: Recto + PA", 2, [&](float**i,float**o,int n,int c){ a.process(i,o,n,c); pa.process(o,o,n,c); }, true); }
    // Sunn Model T: its PA is internal, so measure the amp block alone — in each
    // channel-link mode. Parallel/Series run BOTH preamp channels (≈2x triodes).
    // Silence column flags denormal/decay cost (relevant to "notes cut out").
    for (auto lk : { std::pair<const char*,float>{"Sunn Indep",   0.0f},
                     std::pair<const char*,float>{"Sunn Parallel",1.0f},
                     std::pair<const char*,float>{"Sunn Series",  2.0f} }) {
        AmpBlockExtended a; a.prepare(FS,NB,2); a.setAmpModel(AmpModel::SunnModelT);
        a.setParameter("channel_link", lk.second);
        a.setParameter("vol1",0.6f); a.setParameter("vol2",0.6f); a.setParameter("master",0.7f);
        char nm[48]; std::snprintf(nm,sizeof(nm),"Amp: %s", lk.first);
        run(nm, 2, [&](float**i,float**o,int n,int c){ a.process(i,o,n,c); }, true);
    }

    { CabinetBlock cb; cb.prepare(FS,NB,2); cb.setIR(DefaultCabIR::generate(FS));
      run("Cab (convolution)", 2, [&](float**i,float**o,int n,int c){cb.process(i,o,n,c);}, false); }

    { DelayBlock dl; dl.prepare(FS,NB,2); dl.setType(DelayType::Digital); dl.setParameter("feedback",0.5f); dl.setParameter("mix",0.5f);
      run("Delay: Digital", 2, [&](float**i,float**o,int n,int c){dl.process(i,o,n,c);}, true); }
    { DelayBlock dl; dl.prepare(FS,NB,2); dl.setType(DelayType::Tape); dl.setParameter("feedback",0.5f); dl.setParameter("mix",0.5f);
      run("Delay: Tape", 2, [&](float**i,float**o,int n,int c){dl.process(i,o,n,c);}, true); }
    { DelayBlock dl; dl.prepare(FS,NB,2); dl.setType(DelayType::Echorec); dl.setParameter("feedback",0.5f); dl.setParameter("mix",0.5f);
      run("Delay: Echorec", 2, [&](float**i,float**o,int n,int c){dl.process(i,o,n,c);}, true); }

    { PlateReverbBlock rv; rv.prepare(FS,NB,2); rv.setParameter("mix",0.5f);
      run("Reverb (plate)", 2, [&](float**i,float**o,int n,int c){rv.process(i,o,n,c);}, true); }
    { CompressorBlock cp; cp.prepare(FS,NB,1);
      run("Compressor", 1, [&](float**i,float**o,int n,int c){cp.process(i,o,n,c);}, false); }
    { NoiseGateBlock g; g.prepare(FS,NB,1);
      run("Gate", 1, [&](float**i,float**o,int n,int c){g.process(i,o,n,c);}, true); }
    { ModulationBlock m; m.prepare(FS,NB,2);
      run("Modulation", 2, [&](float**i,float**o,int n,int c){m.process(i,o,n,c);}, false); }

    printf("\n(silence column flags blocks that get SLOWER into silence = denormals)\n");
    return 0;
}
