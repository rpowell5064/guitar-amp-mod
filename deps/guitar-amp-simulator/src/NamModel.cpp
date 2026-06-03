#include "NamModel.h"
#include <get_dsp.h>
#include <algorithm>
#include <filesystem>

NamModel::NamModel()  = default;
NamModel::~NamModel() = default;
NamModel::NamModel(NamModel&&) noexcept            = default;
NamModel& NamModel::operator=(NamModel&&) noexcept = default;

bool NamModel::loadFromFile(const std::string& path) noexcept {
    try {
        auto newDsp = nam::get_dsp(std::filesystem::path(path));
        if (!newDsp) return false;
        dsp = std::move(newDsp);
        return true;
    } catch (...) {
        dsp.reset();
        return false;
    }
}

void NamModel::reset(double sampleRate, int maxBlockSize) noexcept {
    if (!dsp || maxBlockSize <= 0) return;
    try {
        dsp->ResetAndPrewarm(sampleRate, maxBlockSize);
        inBuf.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    } catch (...) {}
}

void NamModel::processBuffer(const float* in, float* out, int numSamples) noexcept {
    if (!dsp) {
        if (in != out)
            std::copy(in, in + numSamples, out);
        return;
    }
    // Copy input to non-const scratch — NAM's process() takes float** (non-const).
    std::copy(in, in + numSamples, inBuf.data());
    float* inPtr = inBuf.data();
    // Write directly into the caller's output buffer to avoid an extra copy.
    dsp->process(&inPtr, &out, numSamples);
}

void NamModel::clear() noexcept { dsp.reset(); }

bool NamModel::isLoaded() const noexcept { return dsp != nullptr; }

double NamModel::getExpectedSampleRate() const noexcept {
    return dsp ? dsp->GetExpectedSampleRate() : -1.0;
}
