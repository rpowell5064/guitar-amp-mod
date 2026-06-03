#include "NamModel.h"
#include "GuitarAmpProcessor.h"
#include <get_dsp.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float rms(const float* buf, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += static_cast<double>(buf[i]) * buf[i];
    return static_cast<float>(std::sqrt(acc / n));
}

// Returns true on pass, false on fail.
static bool testModel(const std::string& path, const std::string& label,
                      double sampleRate = 44100.0) {
    std::printf("  %-30s  ", label.c_str());

    // Try loading directly through NAMCore to get the real error message.
    try {
        auto dsp = nam::get_dsp(std::filesystem::path(path));
        (void)dsp;
    } catch (const std::exception& e) {
        std::printf("FAIL  (NAMCore exception: %s)\n", e.what());
        return false;
    } catch (...) {
        std::printf("FAIL  (NAMCore unknown exception)\n");
        return false;
    }

    NamModel model;
    if (!model.loadFromFile(path)) {
        std::printf("FAIL  (NamModel::loadFromFile returned false)\n");
        return false;
    }

    model.reset(sampleRate, 256);

    // 10 blocks of a 110 Hz sine wave
    constexpr int kBlock = 256;
    std::vector<float> in(kBlock), out(kBlock);
    double phase = 0.0;
    float totalRms = 0.0f;

    for (int b = 0; b < 10; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            in[i] = 0.5f * static_cast<float>(std::sin(phase));
            phase += 2.0 * M_PI * 110.0 / sampleRate;
        }
        model.processBuffer(in.data(), out.data(), kBlock);
        if (b >= 2) totalRms += rms(out.data(), kBlock); // skip prewarm blocks
    }

    const float avgRms = totalRms / 8.0f;
    const bool  ok     = avgRms > 0.0f && std::isfinite(avgRms);
    std::printf("%-6s  RMS=%.5f  SR_expected=%.0f\n",
                ok ? "PASS" : "FAIL", avgRms, model.getExpectedSampleRate());
    return ok;
}

int main() {
    const std::string modelDir =
        "C:/Development/Projects/GuitarAmpSimulator/build"
        "/_deps/neuralampmodelercore-src/example_models/";

    std::printf("=== NamModel integration test ===\n\n");

    int passed = 0, total = 0;
    auto run = [&](const std::string& file, const std::string& label,
                   double sr = 44100.0) {
        ++total;
        if (testModel(modelDir + file, label, sr)) ++passed;
    };

    run("lstm.nam",                  "LSTM",              48000.0);
    run("wavenet.nam",               "WaveNet");
    run("wavenet_a1_standard.nam",   "WaveNet A1 standard");
    run("wavenet_a2_max.nam",        "WaveNet A2 max");
    run("wavenet_condition_dsp.nam", "WaveNet condition");
    run("slimmable_wavenet.nam",     "Slimmable WaveNet");
    run("slimmable_container.nam",   "Slimmable container");
    run("my_model.nam",              "my_model");

    std::printf("\n%d / %d passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
