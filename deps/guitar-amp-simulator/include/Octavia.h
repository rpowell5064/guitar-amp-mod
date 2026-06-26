#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// Octavia — octave-up fuzz (Tycobrahe / Roger Mayer voicing; the "Purple Haze" solo
// sound). A silicon fuzz feeds a full-wave rectifier: |x| folds the negative half up,
// doubling the fundamental into a ringing octave-up that sings on single notes (neck
// pickup, tone rolled back) and turns gnarly / ring-modulated on chords. Lower drive
// keeps the pre-rectifier wave sine-ish so the octave rings cleanly; higher drive
// squares it for the thick intermodulated fuzz.
//
// Params: "drive"/"sustain" [0,1] fuzz into rectifier, "tone" [0,1] post LP,
// "level"/"volume" [0,1] output.
class Octavia final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                         noexcept override;
    void  advanceSmoothing()                              noexcept override;
    float processSample(float x, int ch)                  noexcept override;
    void  setParameter(const std::string& id, float value) noexcept override;
    float getParameter(const std::string& id) const        noexcept override;

    const char* modelName() const noexcept override { return "Octavia"; }

private:
    double fs_ = 0.0;
    float drive_ = 0.7f, tone_ = 0.5f, level_ = 0.6f;

    LinearSmoother driveSmooth_, levelSmooth_;
    float driveCur_ = 0.7f, levelCur_ = 0.6f;

    struct ChannelState {
        BiquadFilter inputHP;   // thin the lows feeding the rectifier
        BiquadFilter octHP;     // strip rectifier DC
        BiquadFilter toneLP;    // post tone
    };
    std::array<ChannelState, kMaxCh> ch_;

    void recalcFilters() noexcept;
};
