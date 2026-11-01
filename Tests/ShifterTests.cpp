// Pitch shifters (TD-PSOLA and phase vocoder) on synthetic sung vowels whose pitch and formants are
// known. Real-vocal comparisons are made with tools/eval/eval_shift.py (VALIDATION.md).
#include "TestSupport.h"
#include "DSP/Fft.h"
#include "DSP/PhaseVocoder.h"
#include "DSP/Psola.h"

using namespace kural::test;
using namespace kural::dsp;

namespace
{
struct Shifted { std::vector<float> y; int latency = 0; };

Shifted psola (const std::vector<float>& x, double fs, float semis, float formant = 0.0f, float unv = 1.0f, int block = 1)
{
    PitchDetector det; det.prepare (fs);
    PitchMarker mk; mk.prepare (fs);
    PsolaVoice v; v.prepare (mk);
    v.set ({ semis, formant, unv, 0.0f });
    v.reset (mk);
    Shifted s;
    s.latency = mk.latency();
    s.y.resize (x.size());
    // feed in blocks to show the result does not depend on how the host cuts the audio
    for (size_t i = 0; i < x.size(); i += (size_t) block)
    {
        const size_t n = std::min ((size_t) block, x.size() - i);
        for (size_t k = i; k < i + n; ++k)
        {
            det.process (&x[k], 1, [&] (const PitchFrame& f) { mk.onPitchFrame (f); });
            mk.push (x[k]);
            s.y[k] = v.next (mk);
        }
    }
    return s;
}

Shifted pv (const std::vector<float>& x, double fs, float semis, float formant = 0.0f)
{
    PitchDetector det; det.prepare (fs);
    PvAnalysis a; a.prepare (fs);
    PvVoice v; v.prepare (a);
    v.set ({ semis, formant, 1.0f });
    Shifted s;
    s.latency = a.latency();
    s.y.resize (x.size());
    for (size_t k = 0; k < x.size(); ++k)
    {
        det.process (&x[k], 1, [&] (const PitchFrame& f) { a.onPitchFrame (f); });
        if (a.push (x[k])) v.synthesise (a);
        s.y[k] = v.next();
    }
    return s;
}

double rms (const std::vector<float>& v, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b && i < v.size(); ++i) e += (double) v[i] * v[i];
    return std::sqrt (e / (double) std::max<size_t> (1, b - a));
}

// best SNR of y against x over delays [d0, d1]
double alignedSnr (const std::vector<float>& x, const std::vector<float>& y, int d0, int d1, size_t from)
{
    double best = -1e9;
    for (int d = d0; d <= d1; ++d)
    {
        double num = 0.0, den = 0.0;
        for (size_t i = from; i + (size_t) d < y.size(); ++i)
        {
            const double e = y[i + (size_t) d] - x[i];
            num += (double) x[i] * x[i]; den += e * e;
        }
        best = std::max (best, 10.0 * std::log10 (num / std::max (den, 1e-30)));
    }
    return best;
}

// first-formant frequency: the vowel's spectrum is sampled at its harmonics (amplitude in dB) and the
// largest peak between 300 Hz and 1.4 kHz is located with parabolic interpolation
double firstFormant (const std::vector<float>& v, double fs, double f0, size_t from)
{
    Fft fft;
    const int N = 8192;
    fft.prepare (N);
    std::vector<Fft::cpx> buf ((size_t) N);
    std::vector<double> pw ((size_t) N / 2, 0.0);
    for (size_t s = from; s + (size_t) N < v.size(); s += (size_t) N / 2)
    {
        for (int i = 0; i < N; ++i)
            buf[(size_t) i] = { v[s + (size_t) i] * (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / N)), 0.0f };
        fft.forward (buf.data());
        for (int k = 0; k < N / 2; ++k) pw[(size_t) k] += std::norm (buf[(size_t) k]);
    }
    std::vector<double> h;                        // dB per harmonic
    for (int k = 1; k * f0 < 2000.0; ++k)
    {
        const int b = (int) std::lround (k * f0 * N / fs);
        double m = 0.0;
        for (int j = std::max (1, b - 3); j <= std::min (N / 2 - 1, b + 3); ++j) m = std::max (m, pw[(size_t) j]);
        h.push_back (10.0 * std::log10 (m + 1e-30));
    }
    int best = -1;
    for (int k = 1; k + 1 < (int) h.size(); ++k)
        if ((k + 1) * f0 >= 300.0 && (k + 1) * f0 <= 1400.0 && h[(size_t) k] > h[(size_t) k - 1] && h[(size_t) k] >= h[(size_t) k + 1]
            && (best < 0 || h[(size_t) k] > h[(size_t) best]))
            best = k;                             // the strongest local peak, not just the loudest harmonic
    if (best < 1) return 0.0;
    const double a = h[(size_t) best - 1], b = h[(size_t) best], c = h[(size_t) best + 1];
    const double den = a - 2.0 * b + c;
    const double off = std::abs (den) > 1e-9 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
    return (best + 1 + off) * f0;
}
} // namespace

void shifterTests()
{
    const double fs = 48000.0;

    // 1. unshifted PSOLA rebuilds the input (it is the reference point for every voice)
    {
        double worst = 1e9;
        for (double f0 : { 98.0, 220.0, 523.3 })
        {
            auto x = vowel (fs, 1.2, [f0] (double) { return f0; }, 'a');
            auto s = psola (x, fs, 0.0f);
            worst = std::min (worst, alignedSnr (x, s.y, s.latency, s.latency + (int) (fs / 65.0), (size_t) (fs * 0.4)));
        }
        report (worst > 50.0, fmt ("PSOLA unshifted rebuilds the input: worst aligned SNR %.1f dB (98 / 220 / 523 Hz)", worst));
    }

    // 2. PSOLA pitch accuracy and level across the harmony range, low to high voices
    {
        double worstMed = 0.0, worstLvl = 0.0;
        int gross = 0;
        for (double f0 : { 110.0, 220.0, 440.0 })
        {
            auto x = vowel (fs, 1.2, [f0] (double) { return f0; }, 'a');
            for (float st : { -12.0f, -7.0f, -5.0f, 3.0f, 4.0f, 7.0f, 12.0f })
            {
                if (f0 * std::exp2 (st / 12.0) < 62.0) continue;         // below the detector's range
                auto s = psola (x, fs, st);
                const double tgt = f0 * std::exp2 (st / 12.0);
                auto sc = score (analyse (s.y, fs), fs, [tgt] (double) { return tgt; }, 0.3);
                worstMed = std::max (worstMed, sc.medianCents);
                gross += sc.gross + (sc.voiced < 100 ? 1000 : 0);
                worstLvl = std::max (worstLvl, std::abs (20.0 * std::log10 (rms (s.y, (size_t) (fs * 0.5), x.size()) / rms (x, (size_t) (fs * 0.5), x.size()))));
            }
        }
        report (worstMed < 2.0 && gross == 0 && worstLvl < 1.0,
                fmt ("PSOLA -12...+12 st on 110/220/440 Hz vowels: worst median error %.2f c, gross %.0f, worst level change %.2f dB",
                     worstMed, gross, worstLvl));
    }

    // 3. PSOLA follows vibrato and glides: the output tracks input × ratio frame by frame
    {
        auto f0 = [] (double t) { return 196.0 * std::exp2 ((0.4 * std::sin (2.0 * pi * 5.5 * t) + (t > 0.8 ? 2.0 * std::min (1.0, (t - 0.8) / 0.15) : 0.0)) / 12.0); };
        auto x = vowel (fs, 1.6, f0, 'a');
        auto s = psola (x, fs, 4.0f);
        const double lat = (s.latency + fs / 196.0 * 0.5) / fs;          // nominal latency + about half a period
        auto sc = score (analyse (s.y, fs), fs, [&] (double t) { return f0 (t - lat) * std::exp2 (4.0 / 12.0); }, 0.3);
        report (sc.medianCents < 12.0 && sc.gross <= 6, fmt ("PSOLA +4 st on a vibrato line with a glide: median %.1f c, gross %.0f of %.0f frames",
                                                         sc.medianCents, sc.gross, sc.voiced));
    }

    // 4. formants: kept in place when preserved, moved by the set amount when asked (first formant)
    {
        const double f0 = 196.0;
        auto x = vowel (fs, 1.4, [f0] (double) { return f0; }, 'a');
        const size_t from = (size_t) (fs * 0.3);
        const double ref = firstFormant (x, fs, f0, from);
        const double up7 = firstFormant (psola (x, fs, 7.0f).y, fs, f0 * std::exp2 (7.0 / 12.0), from) / ref;
        const double dn7 = firstFormant (psola (x, fs, -7.0f).y, fs, f0 * std::exp2 (-7.0 / 12.0), from) / ref;
        const double fUp = firstFormant (psola (x, fs, 0.0f, 5.0f).y, fs, f0, from) / ref;
        const double fDn = firstFormant (psola (x, fs, 0.0f, -5.0f).y, fs, f0, from) / ref;
        const double e5 = std::exp2 (5.0 / 12.0);
        report (std::abs (up7 - 1.0) < 0.10 && std::abs (dn7 - 1.0) < 0.10 && std::abs (fUp / e5 - 1.0) < 0.10 && std::abs (fDn * e5 - 1.0) < 0.10,
                fmt ("PSOLA first formant (input %.0f Hz): pitch +7 st x%.3f, -7 st x%.3f (kept); formant +5 st x%.3f, -5 st x%.3f (expected x1.335 / x0.749)",
                     ref, up7, dn7, fUp, fDn));
    }

    // 5. consonants: noise passes unshifted at the voice's unvoiced level, never louder
    {
        auto n = noise (fs, 1.0, -20.0, 5);
        auto full = psola (n, fs, 7.0f, 0.0f, 1.0f), quiet = psola (n, fs, 7.0f, 0.0f, 0.25f);
        const double gFull = 20.0 * std::log10 (rms (full.y, (size_t) (fs * 0.3), n.size()) / rms (n, (size_t) (fs * 0.3), n.size()));
        const double gQuiet = 20.0 * std::log10 (rms (quiet.y, (size_t) (fs * 0.3), n.size()) / rms (n, (size_t) (fs * 0.3), n.size()));
        report (std::abs (gFull) < 1.0 && std::abs (gQuiet - 20.0 * std::log10 (0.25)) < 1.5,
                fmt ("PSOLA unvoiced noise: %+.2f dB at level 1, %+.2f dB at level 0.25 (expected -12.04)", gFull, gQuiet));
    }

    // 6. block size does not change the output
    {
        auto x = vowel (fs, 0.6, [] (double t) { return 180.0 + 60.0 * t; }, 'i');
        auto a = psola (x, fs, 5.0f, 0.0f, 0.3f, 1), b = psola (x, fs, 5.0f, 0.0f, 0.3f, 4096);
        bool same = true;
        for (size_t i = 0; i < x.size() && same; ++i) same = a.y[i] == b.y[i];
        report (same, "PSOLA output identical with block sizes 1 and 4096");
    }

    // 7. latency is what is reported, at every sample rate (2 periods of 65 Hz + 16 samples)
    {
        bool ok = true;
        for (double r : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            PitchMarker m; m.prepare (r);
            ok = ok && m.latency() == (int) std::ceil (2.0 * r / 65.0) + 16;
        }
        PitchMarker m; m.prepare (48000.0);
        report (ok, fmt ("PSOLA latency at 48 kHz: %.0f samples (%.1f ms)", m.latency(), 1000.0 * m.latency() / 48000.0));
    }

    // 8. phase vocoder: rebuilds the input unshifted, lands on pitch when shifted
    {
        auto x = vowel (fs, 1.2, [] (double) { return 220.0; }, 'a');
        auto s0 = pv (x, fs, 0.0f);
        const double snr = alignedSnr (x, s0.y, s0.latency, s0.latency, (size_t) (fs * 0.4));
        double worst = 0.0;
        for (float st : { -7.0f, 4.0f, 7.0f })
        {
            const double tgt = 220.0 * std::exp2 (st / 12.0);
            worst = std::max (worst, score (analyse (pv (x, fs, st).y, fs), fs, [tgt] (double) { return tgt; }, 0.3).medianCents);
        }
        report (snr > 50.0 && worst < 3.0, fmt ("phase vocoder: unshifted SNR %.1f dB; -7/+4/+7 st worst median error %.2f c", snr, worst));
    }
}
