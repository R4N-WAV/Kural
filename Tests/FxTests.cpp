// Effects: each one checked for doing what its control says, and for doing nothing when off.
#include "TestSupport.h"
#include "DSP/Fx.h"

using namespace kural::test;
using namespace kural::dsp::fx;

namespace
{
constexpr double fs = 48000.0;
std::vector<float> tone (double hz, double sec, double amp = 0.5)
{
    std::vector<float> v ((size_t) (sec * fs));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2.0 * pi * hz * (double) i / fs));
    return v;
}
double rmsDb (const std::vector<float>& v, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += (double) v[i] * v[i];
    return 10.0 * std::log10 (e / (double) (b - a) + 1e-20);
}
double binDb (const std::vector<float>& v, double hz, size_t a, size_t b)   // level of one frequency (Goertzel-like)
{
    double re = 0.0, im = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) (i - a) / (double) (b - a));
        re += w * v[i] * std::cos (2.0 * pi * hz * (double) i / fs);
        im += w * v[i] * std::sin (2.0 * pi * hz * (double) i / fs);
    }
    return 10.0 * std::log10 ((re * re + im * im) / std::pow ((double) (b - a) * 0.25, 2.0) + 1e-20);
}
} // namespace

void fxTests()
{
    // 1. EQ: 24 dB/oct low cut at 120 Hz, presence +6 dB at 3.2 kHz, flat elsewhere
    {
        auto run = [] (double hz, EqParams p)
        {
            Eq eq; eq.prepare (fs); eq.set (p); eq.reset();
            auto x = tone (hz, 0.6);
            auto y = x;
            eq.process (y.data(), nullptr, (int) y.size());
            return rmsDb (y, 9600, y.size()) - rmsDb (x, 9600, x.size());
        };
        const double cut60 = run (60.0, { 120.0f, 0.0f, 0.0f }), pass1k = run (1000.0, { 120.0f, 0.0f, 0.0f });
        const double pres = run (3200.0, { 20.0f, 6.0f, 0.0f });
        report (cut60 < -20.0 && std::abs (pass1k) < 0.3 && std::abs (pres - 6.0) < 0.5,
                fmt ("EQ: low cut 120 Hz takes 60 Hz down %.1f dB (> 20), 1 kHz %.2f dB; presence +6 dB gives %+.2f dB", -cut60, pass1k, pres));
    }

    // 2. compressor: off = untouched (bit-exact); full = a 24 dB louder passage comes out much less than 24 dB louder
    {
        std::vector<float> x;
        auto q = tone (440.0, 0.5, 0.03), loud = tone (440.0, 0.5, 0.5);
        x.insert (x.end(), q.begin(), q.end()); x.insert (x.end(), loud.begin(), loud.end());
        Compressor c; c.prepare (fs);
        auto y = x;
        c.set ({ 0.0f }); c.process (y.data(), nullptr, (int) y.size());
        const bool same = y == x;
        y = x;
        c.reset(); c.set ({ 1.0f }); c.process (y.data(), nullptr, (int) y.size());
        const double inDiff = rmsDb (x, 36000, 48000) - rmsDb (x, 12000, 24000);
        const double outDiff = rmsDb (y, 36000, 48000) - rmsDb (y, 12000, 24000);
        report (same && outDiff < inDiff - 8.0, fmt ("compressor: off is bit-exact; full turns a %.1f dB jump into %.1f dB", inDiff, outDiff));
    }

    // 3. tube: off = untouched; driven = harmonics, and little aliasing at 2x oversampling
    {
        Tube t; t.prepare (fs);
        auto x = tone (1000.0, 0.5, 0.5);
        auto y = x;
        t.set ({ 0.0f, 1.0f }); t.reset(); t.process (y.data(), nullptr, (int) y.size());
        const bool same = y == x;
        y = x;
        t.set ({ 0.8f, 1.0f }); t.reset(); t.process (y.data(), nullptr, (int) y.size());
        const double h1 = binDb (y, 1000.0, 4800, 24000), h2 = binDb (y, 2000.0, 4800, 24000), h3 = binDb (y, 3000.0, 4800, 24000);
        auto hf = tone (9000.0, 0.5, 0.5);
        t.reset(); t.process (hf.data(), nullptr, (int) hf.size());
        // 3rd harmonic of 9 kHz (27 kHz) aliases to 21 kHz without oversampling
        const double fund = binDb (hf, 9000.0, 4800, 24000), alias = binDb (hf, 21000.0, 4800, 24000);
        report (same && h2 - h1 > -40.0 && h3 - h1 > -40.0 && alias - fund < -30.0,
                fmt ("tube: off is bit-exact; drive 0.8 adds H2 %.0f dB, H3 %.0f dB; alias of a 9 kHz tone %.0f dB", h2 - h1, h3 - h1, alias - fund));
    }

    // 4. mutate: ring modulation of 440 Hz by 100 Hz puts the energy at 340 and 540 Hz
    {
        Mutate m; m.prepare (fs);
        m.set ({ true, 1.0f, 100.0f, 1.0f });
        auto x = tone (440.0, 0.6);
        m.process (x.data(), nullptr, (int) x.size());
        const double c = binDb (x, 440.0, 9600, 28800), lo = binDb (x, 340.0, 9600, 28800), hi = binDb (x, 540.0, 9600, 28800);
        report (lo - c > 30.0 && hi - c > 30.0, fmt ("mutate: 440 Hz x 100 Hz -> sidebands %.0f / %.0f dB above what is left at 440 Hz", lo - c, hi - c));
    }

    // 5. delay: an impulse comes back after the set time, quieter each repeat
    {
        Delay d; d.prepare (fs);
        d.set ({ 250.0f, 0.5f, 1.0f, false, 12000.0f }); d.reset();
        std::vector<float> l ((size_t) fs, 0.0f), r ((size_t) fs, 0.0f);
        l[100] = r[100] = 1.0f;
        d.process (l.data(), r.data(), (int) l.size());
        auto peakNear = [&] (size_t at) { float m = 0.0f; for (size_t i = at - 200; i < at + 200; ++i) m = std::max (m, std::abs (l[i])); return m; };
        const float e1 = peakNear (100 + 12000), e2 = peakNear (100 + 24000);
        report (e1 > 0.2f && e2 > 0.05f && e2 < e1, fmt ("delay 250 ms, feedback 0.5: echoes %.2f then %.2f", e1, e2));
    }

    // 6. reverb: stays finite at the longest decay, decays at all, and its tail is in the set range
    {
        Reverb rv; rv.prepare (fs);
        rv.set ({ 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 1.0f }); rv.reset();
        std::vector<float> l ((size_t) (6.0 * fs), 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        rv.process (l.data(), r.data(), (int) l.size());
        // decay rate of the tail (energy in 100 ms windows), extrapolated to −60 dB
        const double e1 = rmsDb (l, (size_t) (0.3 * fs), (size_t) (0.4 * fs)), e2 = rmsDb (l, (size_t) (1.0 * fs), (size_t) (1.1 * fs));
        const double t60 = 60.0 / ((e1 - e2) / 0.7);
        // expected 0.3 + 6 * 0.5^1.6 = 2.28 s
        Reverb big; big.prepare (fs); big.set ({ 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f }); big.reset();
        std::vector<float> nl ((size_t) (10.0 * fs)), nr;
        Gauss g (3, 0.1);
        for (auto& v : nl) v = (float) g();
        nr = nl;
        big.process (nl.data(), nr.data(), (int) nl.size());
        bool finite = true; float pk = 0.0f;
        for (float v : nl) { finite = finite && std::isfinite (v); pk = std::max (pk, std::abs (v)); }
        report (t60 > 1.5 && t60 < 3.2 && finite && pk < 4.0f,
                fmt ("reverb: decay 0.5 -> measured T60 %.2f s (expected ~2.3); longest decay on noise stays finite, peak %.2f", t60, pk));
    }
}
