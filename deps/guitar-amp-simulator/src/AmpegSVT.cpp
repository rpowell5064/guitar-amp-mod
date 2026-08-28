#include "AmpegSVT.h"
#include <cmath>
#include <algorithm>

using TC = TriodeComponent;

namespace {
// Stiff SS-rectified supply: the SVT barely breathes (cf. MT15 0.100/0.08 —
// the SVT is stiffer still; six 6550s off giant filter caps).
constexpr float kSagTauS  = 0.120f;
constexpr float kSagDepth = 0.06f;

// Per-stage input gains (fit levers — locked against the DI captures via
// nam_compare; the cascade stays polite, the PA row carries the growl).
// Tempered 2026-08-28 after amp_knobcheck: 8x pot + 1.2/1.3 ran 12.5% THD at
// noon — an SVT Channel 1 is far cleaner; growl belongs to the 6550 PA row.
constexpr float kG1 = 1.0f, kG2 = 1.1f, kMakeup = 0.54f;   // 0.30 × the measured x1.81 capture-loudness alignment (SVT CLEAN fit, 2026-08-28)
// The 6550 wall: the real fixed-bias push-pull ceiling is razor-kneed — the
// SVT CLEAN capture jumps 3.7% -> 17.5% THD between -18 and -12 dBFS in.
// softLimit's sharp knee (linear to .95) models it better than the PA's
// smooth shaper, so the wall is staged HERE (MT15 satDrive pattern) and the
// PA row keeps a moderate paDrive for texture, not the ceiling.
constexpr float kSatDrive = 2.6f, kSatInv = 1.0f / kSatDrive;
}  // namespace

constexpr float AmpegSVT::kMidCenters[3];

void AmpegSVT::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;
    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.stage1.prepare(oversampledFs_, TC::kFenderV1);
        c.stage2.prepare(oversampledFs_, TC::kFenderV2);
        c.stageDrv.prepare(oversampledFs_, TC::kMarshallV4);   // driver/CF into the PI
        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * kSagTauS));
    }
    recalcFilters();
    reset();
}

void AmpegSVT::recalcFilters() noexcept {
    if (oversampledFs_ <= 0.0) return;
    // Asymmetric knob laws (Ampeg-published ranges, 0.5 = flat). The bass shelf
    // corner sits at 100 Hz so "±12 dB measured at 40 Hz" also MOVES the audible
    // 60-120 Hz body (a literal 40 Hz corner left the knob nearly inert — the
    // 2026-08-28 amp_knobcheck 1.5 dB swing finding).
    const double bassDb = (double(bass_)   - 0.5) * 2.0 * 12.0;                     // ±12 (at 40 Hz)
    const double midDb  = mid_    >= 0.5f ? (double(mid_)    - 0.5) * 2.0 * 10.0
                                          : (double(mid_)    - 0.5) * 2.0 * 20.0;   // +10/−20
    const double trebDb = treble_ >= 0.5f ? (double(treble_) - 0.5) * 2.0 * 15.0
                                          : (double(treble_) - 0.5) * 2.0 * 20.0;   // +15/−20
    const double presDb = (double(presence_) - 0.5) * 2.0 * 4.0;                    // gentle ±4 @ 3 kHz
    for (auto& c : ch_) {
        c.inHP.setCoeffs(Filters::highpass1pole(25.0, oversampledFs_));
        // Ultra switches: identity when off (0 dB shelves/peaks).
        c.ultraLoPk.setCoeffs(Filters::peaking(500.0, ultraLo_ ? -10.0 : 0.0, 0.9, oversampledFs_));
        c.ultraLoSh.setCoeffs(Filters::lowshelf(40.0,  ultraLo_ ?   2.0 : 0.0, oversampledFs_));
        c.ultraHiSh.setCoeffs(Filters::highshelf(8000.0, ultraHi_ ? 9.0 : 0.0, oversampledFs_));
        c.bassSh.setCoeffs(Filters::lowshelf(100.0, bassDb, oversampledFs_));
        c.midPk.setCoeffs(Filters::peaking(kMidCenters[midFreq_], midDb, 0.9, oversampledFs_));
        c.trebSh.setCoeffs(Filters::highshelf(4000.0, trebDb, oversampledFs_));
        c.presSh.setCoeffs(Filters::highshelf(3000.0, presDb, oversampledFs_));
        c.airLP.setCoeffs(Filters::lowpass(12000.0, 0.707, oversampledFs_));
        // Capture-fit EQ (SVT CLEAN real-DI fit, 2026-08-28 — nam_compare deltas
        // at matched THD: dark 50/80 (−4.5/−3.4), plump 200-315 (+1.2), dark
        // 1.2k..8k (−4.6/−7.9/−9.5/−7.5/−14)):
        c.fit[0].setCoeffs(Filters::lowshelf (  90.0,  5.0, oversampledFs_));
        c.fit[1].setCoeffs(Filters::peaking  ( 240.0, -2.0, 1.0, oversampledFs_));
        c.fit[2].setCoeffs(Filters::highshelf(2000.0,  8.0, oversampledFs_));
        c.fit[3].setCoeffs(Filters::peaking  (1800.0,  3.0, 0.9, oversampledFs_));
        c.fit[4].setCoeffs(Filters::highshelf(7000.0,  5.5, oversampledFs_));
        c.dcBlk.setCoeffs(Filters::highpass(8.0, 0.707, oversampledFs_));   // below the 22 Hz OT corner
    }
}

void AmpegSVT::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inHP.reset();
        c.ultraLoPk.reset(); c.ultraLoSh.reset(); c.ultraHiSh.reset();
        c.bassSh.reset(); c.midPk.reset(); c.trebSh.reset(); c.presSh.reset();
        for (auto& f : c.fit) f.reset();
        c.airLP.reset(); c.dcBlk.reset();
        c.stage1.reset(); c.stage2.reset(); c.stageDrv.reset();
        c.sagEnv = 0.0f;
    }
}

void AmpegSVT::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float AmpegSVT::processSample(float x, int chn) noexcept {
    auto& c = ch_[chn];
    // Volume law: NOT the high-gain 8k³ taper — a bass amp's usable range is
    // the lower half of the knob, and the cubic law collapsed it (golden diff
    // 2026-08-28: gain .25-.35 presets rendered near-silent, peak 0.02-0.11).
    // Floor + linear + cubic top: audible from the first notch, still opens up.
    const float knob = gainSmooth_.getCurrentValue();
    const float pot  = 0.25f + 2.0f * knob + 3.0f * knob * knob * knob;
    const float mv   = masterSmooth_.getCurrentValue();

    x = c.inHP.process(x);
    x = c.stage1.process(x * kG1) * 0.82f;
    // Ultra switches sit BETWEEN stages 1 and 2 in the real circuit — pre-gain,
    // so they interact with drive (Ultra-Lo pushes the stack harder at 40 Hz).
    x = c.ultraLoPk.process(x);
    x = c.ultraLoSh.process(x);
    x = c.ultraHiSh.process(x);

    x *= pot;
    x = c.stage2.process(x * kG2) * 0.82f;

    // Passive stack after the recovery stage (circuit position).
    x = c.bassSh.process(x);
    x = c.midPk.process(x);
    x = c.trebSh.process(x);
    x = c.presSh.process(x);

    // Driver into the (external) power stage; master rides its drive.
    x = c.stageDrv.process(x * (0.5f + 1.6f * mv)) * (0.80f * mv);
    x = c.dcBlk.process(x);

    // Minimal supply sag — SS rectifier.
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(x);
    x *= std::fmax(0.35f, 1.0f - sag_ * c.sagEnv * kSagDepth);

    x = softLimit(x * kSatDrive) * kSatInv * kMakeup;
    for (auto& f : c.fit) x = f.process(x);   // capture-fit EQ (see recalcFilters)
    x = c.airLP.process(x);
    return x;
}

void AmpegSVT::setParameter(const std::string& id, float value) noexcept {
    if (id == "ultralo") {
        const int v = value >= 0.5f ? 1 : 0;
        if (v != ultraLo_) { ultraLo_ = v; recalcFilters(); }
        return;
    }
    if (id == "ultrahi") {
        const int v = value >= 0.5f ? 1 : 0;
        if (v != ultraHi_) { ultraHi_ = v; recalcFilters(); }
        return;
    }
    if (id == "midfreq") {
        int v = static_cast<int>(value + 0.5f);
        v = v < 0 ? 0 : (v > 2 ? 2 : v);
        if (v != midFreq_) { midFreq_ = v; recalcFilters(); }
        return;
    }
    if      (id == "gain")   { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master") { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")    { sag_    = value; }
    else if (id == "bass")   { bass_   = value; recalcFilters(); }
    else if (id == "mid")    { mid_    = value; recalcFilters(); }
    else if (id == "treble") { treble_ = value; recalcFilters(); }
    else if (id == "presence") { presence_ = value; recalcFilters(); }
    // "channel": no-op — the SVT model is Channel 1 only.
}

float AmpegSVT::getParameter(const std::string& id) const noexcept {
    if (id == "ultralo") return static_cast<float>(ultraLo_);
    if (id == "ultrahi") return static_cast<float>(ultraHi_);
    if (id == "midfreq") return static_cast<float>(midFreq_);
    if (id == "gain")    return gain_;
    if (id == "presence") return presence_;
    if (id == "master")  return master_;
    if (id == "bass")    return bass_;
    if (id == "mid")     return mid_;
    if (id == "treble")  return treble_;
    if (id == "sag")     return sag_;
    return 0.0f;
}

float AmpegSVT::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
