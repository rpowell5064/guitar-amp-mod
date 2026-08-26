// Anagram/KosmOS build ONLY (HEXCHAIN_ANAGRAM): the mod-plugin-builder
// toolchain is GCC 9.4 and cannot compile NAM's C++20 wavenet
// ("std::atomic requires a trivially copyable type" in
// NAM/wavenet/slimmable.cpp). NamModel.cpp is dropped from GuitarAmpSim on
// this target and these no-op definitions stand in: every load fails, so
// every Neural path passes through untouched. gen_anagram.py strips the
// Neural entries from the Anagram TTLs to match.
#include "NamModel.h"

namespace nam { class DSP {}; }   // complete type for the unique_ptr member;
                                  // the real nam::DSP never exists in this build

NamModel::NamModel() = default;
NamModel::~NamModel() = default;
NamModel::NamModel(NamModel&&) noexcept = default;
NamModel& NamModel::operator=(NamModel&&) noexcept = default;

bool NamModel::loadFromFile(const std::string&) noexcept { return false; }
void NamModel::reset(double, int) noexcept {}
void NamModel::processBuffer(const float* in, float* out, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i) out[i] = in[i];   // unreachable (never loaded)
}
void NamModel::clear() noexcept {}
bool NamModel::isLoaded() const noexcept { return false; }
double NamModel::getExpectedSampleRate() const noexcept { return 48000.0; }
