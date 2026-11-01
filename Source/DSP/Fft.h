#pragma once
// Kural – small in-place radix-2 complex FFT (float), twiddles and bit-reversal precomputed.
// prepare() allocates; forward()/inverse() are real-time safe.
#include <cmath>
#include <complex>
#include <vector>

namespace kural::dsp
{
class Fft
{
public:
    using cpx = std::complex<float>;

    void prepare (int size)
    {
        n = size;
        levels = 0;
        while ((1 << levels) < n) ++levels;
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * 3.14159265358979323846 * i / n));
        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < levels; ++b) r |= ((i >> b) & 1) << (levels - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    int size() const noexcept { return n; }

    void forward (cpx* x) const noexcept { run (x, false); }
    // inverse without the 1/n scaling
    void inverse (cpx* x) const noexcept { run (x, true); }

private:
    void run (cpx* x, bool inv) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    cpx w = tw[(size_t) (j * step)];
                    if (inv) w = std::conj (w);
                    const cpx u = x[i + j], v = x[i + j + half] * w;
                    x[i + j] = u + v;
                    x[i + j + half] = u - v;
                }
        }
    }

    int n = 0, levels = 0;
    std::vector<cpx> tw;
    std::vector<int> rev;
};
} // namespace kural::dsp
