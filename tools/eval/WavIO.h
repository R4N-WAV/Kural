#pragma once
// Minimal WAV reader/writer for Kural's offline tools (no framework).
// Reads RIFF/WAVE PCM 16/24/32-bit and IEEE float 32/64, mixes to mono. Writes float32 mono.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace kural::tools
{
struct Wav
{
    double sampleRate = 0.0;
    std::vector<float> mono;
};

inline bool readWav (const std::string& path, Wav& out, std::string& err)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) { err = "cannot open " + path; return false; }
    std::vector<unsigned char> d;
    unsigned char buf[1 << 16];
    size_t n;
    while ((n = std::fread (buf, 1, sizeof buf, f)) > 0) d.insert (d.end(), buf, buf + n);
    std::fclose (f);
    auto u16 = [&] (size_t p) { return (unsigned) d[p] | ((unsigned) d[p + 1] << 8); };
    auto u32 = [&] (size_t p) { return (std::uint32_t) d[p] | ((std::uint32_t) d[p + 1] << 8) | ((std::uint32_t) d[p + 2] << 16) | ((std::uint32_t) d[p + 3] << 24); };
    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0) { err = "not a WAV file"; return false; }
    unsigned fmt = 0, ch = 0, bits = 0;
    std::uint32_t sr = 0;
    size_t p = 12, dataPos = 0, dataLen = 0;
    while (p + 8 <= d.size())
    {
        const std::uint32_t len = u32 (p + 4);
        if (std::memcmp (d.data() + p, "fmt ", 4) == 0)
        {
            fmt = u16 (p + 8); ch = u16 (p + 10); sr = u32 (p + 12); bits = u16 (p + 22);
            if (fmt == 0xFFFE && len >= 40) fmt = u16 (p + 32);          // WAVE_FORMAT_EXTENSIBLE: sub-format
        }
        else if (std::memcmp (d.data() + p, "data", 4) == 0) { dataPos = p + 8; dataLen = std::min<size_t> (len, d.size() - dataPos); }
        p += 8 + len + (len & 1);
    }
    if (! dataPos || ! ch || ! sr) { err = "missing fmt or data chunk"; return false; }
    const size_t bps = bits / 8, frames = dataLen / (bps * ch);
    out.sampleRate = sr;
    out.mono.assign (frames, 0.0f);
    for (size_t i = 0; i < frames; ++i)
    {
        double s = 0.0;
        for (unsigned c = 0; c < ch; ++c)
        {
            const size_t q = dataPos + (i * ch + c) * bps;
            double v = 0.0;
            if (fmt == 3 && bits == 32) { float x; std::memcpy (&x, &d[q], 4); v = x; }
            else if (fmt == 3 && bits == 64) { double x; std::memcpy (&x, &d[q], 8); v = x; }
            else if (bits == 16) v = (std::int16_t) u16 (q) / 32768.0;
            else if (bits == 24) { std::int32_t x = (std::int32_t) ((d[q] << 8) | (d[q + 1] << 16) | (d[q + 2] << 24)) >> 8; v = x / 8388608.0; }
            else if (bits == 32) v = (std::int32_t) u32 (q) / 2147483648.0;
            else { err = "unsupported sample format"; return false; }
            s += v;
        }
        out.mono[i] = (float) (s / ch);
    }
    return true;
}

inline bool writeWav (const std::string& path, const std::vector<float>& x, double sampleRate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    auto w32 = [&] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
    const std::uint32_t bytes = (std::uint32_t) (x.size() * 4);
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (3); w16 (1); w32 ((std::uint32_t) sampleRate);
    w32 ((std::uint32_t) sampleRate * 4); w16 (4); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    std::fwrite (x.data(), 4, x.size(), f);
    std::fclose (f);
    return true;
}
} // namespace kural::tools
