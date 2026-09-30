#pragma once
// Minimal RIFF/WAVE reader, shared by the cabinet IR loader and the drum
// sampler. Extracted from cab_plugin.cpp so there is one parser rather than
// two that drift: a WAV reader is exactly the kind of code that grows a fix in
// one copy and not the other.
//
// Handles the formats real-world sample libraries and IR packs actually ship:
// 16 / 24 / 32-bit PCM and 32-bit float, mono or interleaved multi-channel
// (channels beyond the first two are discarded). Chunks other than fmt/data
// are skipped, which is what makes it survive the LIST/INFO and cue chunks
// that editors leave behind.
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace wavread {

// Reads `path` into L (and R when the file has 2+ channels; R is cleared for
// mono). Returns false on anything it cannot parse. outRate carries the file's
// OWN sample rate — callers are responsible for resampling or for playing back
// at the appropriate rate ratio.
inline bool readWav(const char* path, std::vector<float>& L, std::vector<float>& R,
                    uint32_t& outRate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    auto rd = [&](void* p, int n) { f.read(reinterpret_cast<char*>(p), n); };

    char riff[4]; rd(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) return false;
    uint32_t rsz; rd(&rsz, 4);
    char wave[4]; rd(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) return false;

    uint16_t fmt = 0, ch = 0, bits = 0; uint32_t sr = 0;
    std::vector<uint8_t> data;
    while (f) {
        char id[4]; rd(id, 4); uint32_t sz = 0; rd(&sz, 4);
        if (!f) break;
        if (std::strncmp(id, "fmt ", 4) == 0) {
            rd(&fmt, 2); rd(&ch, 2); rd(&sr, 4);
            uint32_t br; rd(&br, 4); uint16_t ba; rd(&ba, 2); rd(&bits, 2);
            if (sz > 16) f.seekg(sz - 16, std::ios::cur);
        } else if (std::strncmp(id, "data", 4) == 0) {
            data.resize(sz); f.read(reinterpret_cast<char*>(data.data()), sz);
        } else {
            f.seekg(sz, std::ios::cur);
        }
        if (sz & 1) f.seekg(1, std::ios::cur);   // chunks are word-aligned
    }
    if (ch == 0 || bits == 0 || data.empty()) return false;
    outRate = sr;

    const size_t bps    = bits / 8;
    const size_t frames = data.size() / (bps * ch);
    L.assign(frames, 0.0f);
    if (ch >= 2) R.assign(frames, 0.0f); else R.clear();

    const uint8_t* p = data.data();
    for (size_t i = 0; i < frames; ++i) {
        for (uint16_t c = 0; c < ch; ++c) {
            float s = 0.0f;
            if (fmt == 3 && bits == 32) { float v; std::memcpy(&v, p, 4); s = v; }
            else if (bits == 16)        { int16_t v; std::memcpy(&v, p, 2); s = v / 32768.0f; }
            else if (bits == 24)        { int32_t v = (p[0]) | (p[1] << 8) | (p[2] << 16);
                                          if (v & 0x800000) v |= ~0xFFFFFF;
                                          s = v / 8388608.0f; }
            else if (bits == 32)        { int32_t v; std::memcpy(&v, p, 4); s = v / 2147483648.0f; }
            if (c == 0)                 L[i] = s;
            else if (c == 1 && ch >= 2) R[i] = s;
            p += bps;
        }
    }
    return true;
}

} // namespace wavread
