// AmpFreqTestMain.cpp — standalone console entry point for frequency-response
// validation of all four amp models.
//
// Compile as a separate console target (not the JUCE plugin):
//   cmake -DBUILD_FREQ_TEST=ON ..
// or manually:
//   cl /std:c++17 /I..\include AmpFreqTestMain.cpp ^
//      ..\src\FenderDeluxeModel.cpp ..\src\JCM800Model.cpp ^
//      ..\src\EVH5150Model.cpp ..\src\Rockerverb50.cpp ^
//      ..\src\TriodeComponent.cpp ..\src\ToneStackComponent.cpp ^
//      ..\src\AmpFrequencyAnalyzer.cpp ^
//      ..\src\CathodeFollower.cpp ..\src\PhaseInverter.cpp ^
//      ..\src\PowerTubeStage.cpp ..\src\PowerSupplySag.cpp ^
//      ..\src\NegativeFeedbackLoop.cpp ..\src\PresenceResonanceNetwork.cpp ^
//      ..\src\OutputTransformerModel.cpp ..\src\SpeakerImpedanceModel.cpp ^
//      /Fe:AmpFreqTest.exe

#include "FenderDeluxeModel.h"
#include "JCM800Model.h"
#include "EVH5150Model.h"
#include "Rockerverb50.h"
#include "AmpFrequencyAnalyzer.h"
#include <cstdio>

// All amp models expect the OVERSAMPLED sample rate (4× native)
static constexpr double kNativeFs       = 44100.0;
static constexpr double kOversampledFs  = kNativeFs * 4.0;
static constexpr int    kMaxBlockSize   = 512;

static void setNoonParams(FenderDeluxeModel& m) {
    // AB763: no gain knob — "gain" maps to Volume
    m.setParameter("gain",     0.50f);   // Volume (noon)
    m.setParameter("bass",     0.50f);
    m.setParameter("treble",   0.50f);
    // No mid knob on the real AB763; the mid param is held at neutral
    m.setParameter("mid",      0.50f);
    m.setParameter("presence", 0.50f);
    m.setParameter("master",   0.70f);
    m.setParameter("sag",      0.35f);
}

static void setNoonParams(JCM800Model& m) {
    m.setParameter("gain",     0.50f);
    m.setParameter("bass",     0.50f);
    m.setParameter("mid",      0.50f);
    m.setParameter("treble",   0.50f);
    m.setParameter("presence", 0.50f);
    m.setParameter("master",   0.70f);
    m.setParameter("sag",      0.20f);
}

static void setNoonParams(EVH5150Model& m, bool redChannel) {
    m.setParameter("channel",   redChannel ? 1.0f : 0.0f);
    m.setParameter("gain",      0.55f);
    m.setParameter("bass",      0.50f);
    m.setParameter("mid",       0.40f);   // 5150 "V" scoop default
    m.setParameter("treble",    0.60f);
    m.setParameter("presence",  0.60f);
    m.setParameter("resonance", 0.50f);
    m.setParameter("master",    0.70f);
    m.setParameter("sag",       0.10f);
}

static void setNoonParams(Rockerverb50& m) {
    m.setParameter("gain",     0.55f);
    m.setParameter("bass",     0.50f);
    m.setParameter("mid",      0.55f);   // slightly mid-forward (Orange character)
    m.setParameter("treble",   0.50f);
    m.setParameter("presence", 0.50f);
    m.setParameter("master",   0.70f);
    m.setParameter("sag",      0.28f);
}

int main() {
    std::printf("═══════════════════════════════════════════════════════════════\n");
    std::printf("  Guitar Amp Simulator — Frequency Response Validation Suite\n");
    std::printf("  Oversampled rate: %.0f Hz (4× %.0f Hz native)\n",
                kOversampledFs, kNativeFs);
    std::printf("═══════════════════════════════════════════════════════════════\n");

    FenderDeluxeModel fender;
    JCM800Model       marshall;
    EVH5150Model      evhRed;
    Rockerverb50      orange;

    fender.prepare  (kOversampledFs, kMaxBlockSize);
    marshall.prepare(kOversampledFs, kMaxBlockSize);
    evhRed.prepare  (kOversampledFs, kMaxBlockSize);
    orange.prepare  (kOversampledFs, kMaxBlockSize);

    setNoonParams(fender);
    setNoonParams(marshall);
    setNoonParams(evhRed, true);   // Red channel
    setNoonParams(orange);

    // Run full suite: sweep + validate + print
    const int failures = AmpFrequencyAnalyzer::runSuite(
        fender, marshall, evhRed, orange, kOversampledFs);

    // ── EVH Blue channel separate test ───────────────────────────────────────
    std::printf("──────────────────────────────────────────────────────────────\n");
    std::printf("EVH 5150 III — Blue Channel (rhythm/crunch)\n");
    evhRed.reset();
    setNoonParams(evhRed, false);
    const auto blueResult = AmpFrequencyAnalyzer::sweep(
        evhRed, kOversampledFs, 0.10f);
    AmpFrequencyAnalyzer::print(blueResult, "EVH 5150 III — Blue");

    std::string blueErr;
    AmpFrequencyAnalyzer::validate(blueResult, AmpFrequencyAnalyzer::kEVHSpec, blueErr);
    if (!blueErr.empty())
        std::printf("  NOTE: Blue channel: %s (expected at lower gain)\n", blueErr.c_str());

    return (failures == 0) ? 0 : 1;
}
