#pragma once
// Kural – the effects that follow the voices: tone (EQ), dynamics, colour (tube), Mutate (ring
// modulation) and space (delay, reverb). Each is small, framework-free and real-time safe after
// prepare(). Parameters are plain values set per block; changes are smoothed inside.
//
//   lead + harmonies ─► EQ ─► compressor ─► tube ─► mutate ─► (dry/space split) ─► delay ─► reverb ─► out
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kural::dsp::fx
{
// RBJ biquad, transposed direct form II
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() noexcept { z1 = z2 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void highPass (double fs, double f, double q) noexcept;
    void lowPass (double fs, double f, double q) noexcept;
    void bandPass (double fs, double f, double q) noexcept;          // constant 0 dB peak gain
    void peak (double fs, double f, double q, double dB) noexcept;
    void highShelf (double fs, double f, double dB) noexcept;
    void lowShelf (double fs, double f, double dB) noexcept;
};

// ---- tone: low cut, presence (peak ~3 kHz), air (shelf ~10 kHz)
struct EqParams { float lowCutHz = 80.0f; float presenceDb = 0.0f; float airDb = 0.0f; };
class Eq
{
public:
    void prepare (double fs);
    void reset();
    void set (const EqParams& p) noexcept { target = p; }
    void process (float* l, float* r, int n) noexcept;
private:
    void design() noexcept;
    double fs = 48000.0;
    EqParams target, current { -1.0f, -100.0f, -100.0f };
    std::array<Biquad, 2> hp1, hp2, pres, air;   // per channel; low cut is 24 dB/oct
    int counter = 0;
};

// ---- dynamics: feed-forward RMS compressor with soft knee and automatic make-up
struct CompParams { float amount = 0.0f; };   // 0 … 1: threshold, ratio and make-up move together
class Compressor
{
public:
    void prepare (double fs);
    void reset() { env = 0.0f; gainDb = 0.0f; }
    void set (const CompParams& p) noexcept { params = p; }
    void process (float* l, float* r, int n) noexcept;
    float reductionDb() const noexcept { return -gainDb; }   // for metering
private:
    double fs = 48000.0;
    CompParams params;
    float env = 0.0f, gainDb = 0.0f, atk = 0.0f, rel = 0.0f, envCoef = 0.0f;
};

// ---- tube: asymmetric soft clipping at 2× oversampling, pre-emphasis so it reads as warmth, not fuzz
struct TubeParams { float drive = 0.0f; float mix = 1.0f; };   // drive 0 … 1
class Tube
{
public:
    void prepare (double fs);
    void reset();
    void set (const TubeParams& p) noexcept { params = p; }
    void process (float* l, float* r, int n) noexcept;
private:
    float shape (float x, float k) const noexcept;
    double fs = 48000.0;
    TubeParams params;
    float driveS = 0.0f, mixS = 0.0f, smooth = 0.0f;
    std::array<std::array<Biquad, 4>, 2> up, down;             // 8th-order Butterworth low-pass at 2× rate
    std::array<Biquad, 2> dcBlock;
};

// ---- Mutate: ring modulation by a sine; follows the voice (frequency as a ratio of the lead pitch)
//      or a fixed frequency; the classic metallic / robot texture
struct MutateParams { bool on = false; float ratio = 1.5f; float fixedHz = 0.0f; float mix = 0.5f; };
class Mutate
{
public:
    void prepare (double fs);
    void reset() { phase = 0.0; mixS = 0.0f; }
    void set (const MutateParams& p) noexcept { params = p; }
    void setPitch (float hz) noexcept { if (hz > 0.0f) pitchHz = hz; }     // the lead's pitch (analysis frames)
    void process (float* l, float* r, int n) noexcept;
private:
    double fs = 48000.0, phase = 0.0;
    MutateParams params;
    float pitchHz = 220.0f, freqS = 330.0f, mixS = 0.0f;
};

// ---- delay: stereo, ping-pong optional, filtered feedback; time in ms (tempo sync by the host side)
struct DelayParams { float timeMs = 250.0f; float feedback = 0.3f; float mix = 0.0f; bool pingPong = true; float toneHz = 5000.0f; };
class Delay
{
public:
    void prepare (double fs, float maxMs = 2000.0f);
    void reset();
    void set (const DelayParams& p) noexcept { params = p; }
    void process (float* l, float* r, int n) noexcept;
private:
    double fs = 48000.0;
    DelayParams params;
    std::vector<float> bufL, bufR;
    int mask = 0, w = 0;
    float timeS = 0.0f, mixS = 0.0f;
    Biquad toneL, toneR, hpL, hpR;
};

// ---- reverb: 8-line feedback delay network (Householder), modulated, with pre-delay and damping
struct ReverbParams { float size = 0.5f; float decay = 0.5f; float damping = 0.5f; float mix = 0.0f; float preDelayMs = 20.0f; float width = 1.0f; };
class Reverb
{
public:
    void prepare (double fs);
    void reset();
    void set (const ReverbParams& p) noexcept { params = p; }
    void process (float* l, float* r, int n) noexcept;
private:
    static constexpr int N = 8;
    double fs = 48000.0;
    ReverbParams params;
    std::array<std::vector<float>, N> lines;
    std::array<int, N> masks {}, writes {}, baseLen {};
    std::array<float, N> lpState {}, modPhase {};
    std::vector<float> pre;
    int preMask = 0, preW = 0;
    std::array<std::vector<float>, 4> diff;   // input diffusion allpasses
    std::array<int, 4> diffLen {}, diffW {};
    float mixS = 0.0f;
};

// ---- stereo width (mid/side) for the processed signal
inline void width (float* l, float* r, int n, float w) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * w;
        l[i] = m + s;
        r[i] = m - s;
    }
}
} // namespace kural::dsp::fx
