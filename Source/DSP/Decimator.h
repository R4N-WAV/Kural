#pragma once
// Kural – integer-factor decimator (windowed-sinc FIR low-pass, computed only at output samples).
// Plain C++17, no framework dependency. prepare() allocates; push() is real-time safe.
#include <cmath>
#include <vector>

namespace kural::dsp
{
class Decimator
{
public:
    // factor 1 = pass-through. cutoff is relative to the OUTPUT rate (0.5 = output Nyquist).
    void prepare (int decimationFactor, int tapsPerPhase = 16, double cutoffRelOut = 0.42)
    {
        factor = decimationFactor < 1 ? 1 : decimationFactor;
        if (factor == 1) { taps.clear(); hist.clear(); numTaps = 0; reset(); return; }

        numTaps = tapsPerPhase * factor + 1;                        // odd → integer group delay
        taps.assign ((size_t) numTaps, 0.0f);
        const double fc = cutoffRelOut / factor;                   // cycles per input sample
        const double mid = 0.5 * (numTaps - 1);
        double sum = 0.0;
        for (int i = 0; i < numTaps; ++i)
        {
            const double x = i - mid;
            const double sinc = 2 * i == numTaps - 1 ? 2.0 * fc : std::sin (2.0 * pi * fc * x) / (pi * x);
            // Blackman-Harris window: > 90 dB stop-band, enough for pitch analysis
            const double w = 2.0 * pi * i / (numTaps - 1);
            const double win = 0.35875 - 0.48829 * std::cos (w) + 0.14128 * std::cos (2 * w) - 0.01168 * std::cos (3 * w);
            taps[(size_t) i] = (float) (sinc * win);
            sum += sinc * win;
        }
        for (auto& t : taps) t = (float) (t / sum);
        hist.assign ((size_t) numTaps * 2, 0.0f);                  // doubled ring: contiguous reads
        reset();
    }

    void reset()
    {
        std::fill (hist.begin(), hist.end(), 0.0f);
        pos = 0;
        phase = 0;
    }

    // feeds one input sample; returns true (and writes out) when an output sample is produced
    bool push (float x, float& out) noexcept
    {
        if (factor == 1) { out = x; return true; }
        hist[(size_t) pos] = x;
        hist[(size_t) (pos + numTaps)] = x;
        pos = pos + 1 == numTaps ? 0 : pos + 1;
        if (++phase < factor) return false;
        phase = 0;
        const float* h = hist.data() + pos;                        // oldest .. newest
        float acc = 0.0f;
        for (int i = 0; i < numTaps; ++i) acc += h[i] * taps[(size_t) i];
        out = acc;
        return true;
    }

    int getFactor() const noexcept { return factor; }
    // delay of the filter in input samples
    double groupDelayInput() const noexcept { return factor == 1 ? 0.0 : 0.5 * (numTaps - 1); }

private:
    static constexpr double pi = 3.14159265358979323846;
    int factor = 1, numTaps = 0, pos = 0, phase = 0;
    std::vector<float> taps, hist;
};
} // namespace kural::dsp
