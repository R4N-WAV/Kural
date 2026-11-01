#pragma once
// Shared helpers for Kural's framework-free tests: check reporting, portable noise, synthetic
// signals with exactly known pitch (sines, harmonic tones, sung vowels with formants), and scoring
// of pitch frames against the truth.
#include "DSP/PitchDetector.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace kural::test
{
using namespace kural::dsp;
constexpr double pi = 3.14159265358979323846;
inline int failures = 0, checks = 0;

inline void report (bool ok, const std::string& what)
{
    ++checks;
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (! ok) ++failures;
}

inline double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

// Portable Gaussian noise. std::mt19937's output is fixed by the C++ standard, but
// std::normal_distribution is not: libstdc++ and MSVC produce different numbers from the same
// seed, which once made a breathy-voice test pass on Linux and fail on Windows. Box-Muller on raw
// mt19937 output gives the same noise on every platform.
struct Gauss
{
    explicit Gauss (unsigned seed, double sd) : rng (seed), sigma (sd) {}
    double operator()()
    {
        if (has) { has = false; return spare * sigma; }
        double u, v, r;
        do
        {
            u = 2.0 * ((rng() + 0.5) / 4294967296.0) - 1.0;
            v = 2.0 * ((rng() + 0.5) / 4294967296.0) - 1.0;
            r = u * u + v * v;
        } while (r >= 1.0 || r == 0.0);
        const double m = std::sqrt (-2.0 * std::log (r) / r);
        spare = v * m; has = true;
        return u * m * sigma;
    }
    std::mt19937 rng;
    double sigma, spare = 0.0;
    bool has = false;
};

// ---------------------------------------------------------------- signal generators
// f0 (t) gives the instantaneous pitch; every generator is phase-continuous

inline std::vector<float> sine (double fs, double seconds, std::function<double (double)> f0, double amp = 0.3)
{
    std::vector<float> v ((size_t) (fs * seconds));
    double ph = 0.0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        v[i] = (float) (amp * std::sin (ph));
        ph += 2.0 * pi * f0 (i / fs) / fs;
    }
    return v;
}

// additive harmonic tone with given harmonic amplitudes (index 0 = fundamental)
inline std::vector<float> harmonic (double fs, double seconds, std::function<double (double)> f0, std::vector<double> amps, double gain = 0.2)
{
    std::vector<float> v ((size_t) (fs * seconds));
    double ph = 0.0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double f = f0 (i / fs);
        double s = 0.0;
        for (size_t h = 0; h < amps.size(); ++h)
            if ((h + 1) * f < 0.45 * fs) s += amps[h] * std::sin ((h + 1) * ph);
        v[i] = (float) (gain * s);
        ph += 2.0 * pi * f / fs;
    }
    return v;
}

// a sung vowel: band-limited glottal-like pulse (harmonics falling at −12 dB/octave) through three
// formant resonators. "a" = 730/1090/2440 Hz, "i" = 270/2290/3010 Hz.
inline std::vector<float> vowel (double fs, double seconds, std::function<double (double)> f0, char which, double breathDb = -200.0, unsigned seed = 1)
{
    const double F[2][3] = { { 730, 1090, 2440 }, { 270, 2290, 3010 } };
    const double B[3] = { 90, 110, 160 };
    const int vi = which == 'i' ? 1 : 0;
    std::vector<float> v ((size_t) (fs * seconds));
    double ph = 0.0;
    double z[3][2] = {};
    Gauss nd (seed, 1.0);
    const double breath = std::pow (10.0, breathDb / 20.0);
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double f = f0 (i / fs);
        double src = 0.0;
        for (int h = 1; h * f < 0.45 * fs && h < 80; ++h) src += std::sin (h * ph) / (h * h);
        src += breath * nd();
        ph += 2.0 * pi * f / fs;
        double y = src;
        for (int k = 0; k < 3; ++k)                         // cascade of 2-pole resonators
        {
            const double r = std::exp (-pi * B[k] / fs), w = 2.0 * pi * F[vi][k] / fs;
            const double a1 = 2.0 * r * std::cos (w), a2 = -r * r, g = 1.0 - r;
            const double o = g * y + a1 * z[k][0] + a2 * z[k][1];
            z[k][1] = z[k][0]; z[k][0] = o; y = o;
        }
        v[i] = (float) y;
    }
    float peak = 0.0f;
    for (float s : v) peak = std::max (peak, std::abs (s));
    for (float& s : v) s *= 0.3f / std::max (peak, 1e-9f);
    return v;
}

inline std::vector<float> noise (double fs, double seconds, double rmsDb, unsigned seed = 7)
{
    std::vector<float> v ((size_t) (fs * seconds));
    Gauss nd (seed, std::pow (10.0, rmsDb / 20.0));
    for (auto& s : v) s = (float) nd();
    return v;
}

inline void addNoise (std::vector<float>& v, double snrDb, unsigned seed = 9)
{
    double e = 0.0;
    for (float s : v) e += (double) s * s;
    const double rms = std::sqrt (e / v.size());
    Gauss nd (seed, rms * std::pow (10.0, -snrDb / 20.0));
    for (auto& s : v) s += (float) nd();
}

// ---------------------------------------------------------------- analysis
inline std::vector<PitchFrame> analyse (const std::vector<float>& x, double fs, int block = 512, PitchDetector::Config cfg = {})
{
    PitchDetector d;
    d.prepare (fs, cfg);
    std::vector<PitchFrame> frames;
    frames.reserve (x.size() / 64 + 16);
    for (size_t i = 0; i < x.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, x.size() - i);
        d.process (x.data() + i, n, [&] (const PitchFrame& f) { frames.push_back (f); });
    }
    return frames;
}

struct Score
{
    int frames = 0, voiced = 0, gross = 0;   // gross: error > 50 cents
    double medianCents = 0.0, p95Cents = 0.0, maxCents = 0.0;
};

// compares voiced frames against the true pitch at each frame's centre time; skips the first `skip` s
inline Score score (const std::vector<PitchFrame>& fr, double fs, std::function<double (double)> truth, double skip = 0.06, double endSkip = 0.0, double duration = 1e9)
{
    Score s;
    std::vector<double> errs;
    for (const auto& f : fr)
    {
        const double t = f.centreSample / fs;
        if (t < skip || t > duration - endSkip) continue;
        ++s.frames;
        if (f.voicing != Voicing::voiced) continue;
        ++s.voiced;
        const double e = std::abs (cents (f.f0Hz, truth (t)));
        errs.push_back (e);
        if (e > 50.0) ++s.gross;
    }
    if (! errs.empty())
    {
        std::sort (errs.begin(), errs.end());
        s.medianCents = errs[errs.size() / 2];
        s.p95Cents = errs[(size_t) (errs.size() * 0.95)];
        s.maxCents = errs.back();
    }
    return s;
}

inline std::string fmt (const char* f, double a, double b = 0, double c = 0, double d = 0, double e = 0)
{
    char buf[256];
    std::snprintf (buf, sizeof buf, f, a, b, c, d, e);
    return buf;
}
} // namespace kural::test
