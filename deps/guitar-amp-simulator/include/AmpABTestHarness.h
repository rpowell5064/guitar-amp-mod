#pragma once
#include "AudioBlock.h"
#include <atomic>
#include <string>
#include <cstring>

// Passthrough AudioBlock that captures audio at a tap point for offline analysis.
//
// Insert at any position in a signal chain (or in a dedicated test chain).
// Writes only the left channel to a fixed-size lock-free ring buffer.
// Single-producer (audio thread) / single-consumer (analysis thread) safe.
//
// Usage:
//   AmpABTestHarness tap;
//   tap.tapLabel = "preamp";
//   chain.insertBlockAt(2, &tap);          // after AmpBlock
//   tap.startCapture();
//   // … render audio …
//   tap.stopCapture();
//   std::vector<float> buf(tap.capturedCount());
//   tap.readCaptured(buf.data(), (int)buf.size());
class AmpABTestHarness final : public AudioBlock {
public:
    static constexpr int kBufLen = 65536;  // ~1.5 s at 44.1 kHz

    std::string tapLabel;

    AmpABTestHarness() { std::memset(ringBuf_, 0, sizeof(ringBuf_)); }

    void  prepare(double sr, int maxBlock, int numCh) override {
        sampleRate = sr; maxBlockSize = maxBlock; numChannels = numCh;
    }

    // Passthrough: always copies in → out regardless of capture state.
    void process(float** in, float** out, int numSamples, int numChannels) override {
        copyBlock(in, out, numSamples, numChannels);

        if (!capturing_.load(std::memory_order_acquire)) return;
        if (numChannels < 1) return;

        const int wPos = writePos_.load(std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i) {
            ringBuf_[(wPos + i) & (kBufLen - 1)] = in[0][i];
        }
        writePos_.store((wPos + numSamples) & (kBufLen - 1),
                        std::memory_order_release);
    }

    void setParameter(const std::string&, float) override {}
    float getParameter(const std::string&) const override { return 0.0f; }

    // ── Control (UI / test thread) ────────────────────────────────────────────

    void startCapture() noexcept {
        readPos_.store(writePos_.load(std::memory_order_acquire),
                       std::memory_order_release);
        capturing_.store(true, std::memory_order_release);
    }

    void stopCapture() noexcept {
        capturing_.store(false, std::memory_order_release);
    }

    bool isCapturing() const noexcept {
        return capturing_.load(std::memory_order_acquire);
    }

    // Samples available for reading.
    int capturedCount() const noexcept {
        const int w = writePos_.load(std::memory_order_acquire);
        const int r = readPos_.load(std::memory_order_relaxed);
        return (w - r + kBufLen) & (kBufLen - 1);
    }

    // Read up to maxSamples from the capture buffer into out.
    // Returns actual samples written. Advances the read pointer.
    int readCaptured(float* out, int maxSamples) noexcept {
        const int avail  = capturedCount();
        const int toRead = avail < maxSamples ? avail : maxSamples;
        int rPos = readPos_.load(std::memory_order_relaxed);
        for (int i = 0; i < toRead; ++i)
            out[i] = ringBuf_[(rPos + i) & (kBufLen - 1)];
        readPos_.store((rPos + toRead) & (kBufLen - 1), std::memory_order_release);
        return toRead;
    }

    void clearCapture() noexcept {
        readPos_.store(writePos_.load(std::memory_order_acquire),
                       std::memory_order_release);
    }

private:
    static_assert((kBufLen & (kBufLen - 1)) == 0, "kBufLen must be a power of 2");

    alignas(64) float ringBuf_[kBufLen]{};
    std::atomic<bool> capturing_{false};
    std::atomic<int>  writePos_{0};
    std::atomic<int>  readPos_{0};
};
