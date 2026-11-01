// Pitch detection accuracy and robustness on synthetic signals whose true pitch is known exactly.
// These signals prove the algorithm and its edge cases; real recorded vocals are measured separately
// (tools/eval, VALIDATION.md).
#include "TestSupport.h"

using namespace kural::test;

void pitchTests()
{
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    // 1. steady sine tones over the vocal range, at every supported sample rate
    for (double fs : rates)
    {
        double worstMed = 0.0, worstP95 = 0.0, worstVoiced = 1.0;
        int gross = 0;
        for (double f : { 65.4, 82.4, 110.0, 196.0, 261.6, 440.0, 659.3, 880.0, 1046.5, 1318.5 })
        {
            auto x = sine (fs, 0.6, [f] (double) { return f; });
            auto s = score (analyse (x, fs), fs, [f] (double) { return f; });
            worstMed = std::max (worstMed, s.medianCents);
            worstP95 = std::max (worstP95, s.p95Cents);
            worstVoiced = std::min (worstVoiced, s.voiced / (double) std::max (1, s.frames));
            gross += s.gross;
        }
        report (worstMed < 2.0 && worstP95 < 5.0 && worstVoiced > 0.97 && gross == 0,
                fmt ("sine 65 Hz – 1.3 kHz @ %.1f kHz: worst median %.2f c, worst p95 %.2f c, min voiced %.1f %%, gross %.0f",
                     fs / 1000, worstMed, worstP95, worstVoiced * 100, gross));
    }

    // 2. synthetic sung vowels, low male to high female
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (char vw : { 'a', 'i' })
        {
            double worstMed = 0.0, worstP95 = 0.0, worstVoiced = 1.0;
            int gross = 0;
            for (double f : { 82.4, 110.0, 146.8, 220.0, 329.6, 523.3, 784.0, 1046.5 })
            {
                auto x = vowel (fs, 0.6, [f] (double) { return f; }, vw);
                auto s = score (analyse (x, fs), fs, [f] (double) { return f; });
                worstMed = std::max (worstMed, s.medianCents);
                worstP95 = std::max (worstP95, s.p95Cents);
                worstVoiced = std::min (worstVoiced, s.voiced / (double) std::max (1, s.frames));
                gross += s.gross;
            }
            report (worstMed < 3.0 && worstP95 < 8.0 && worstVoiced > 0.95 && gross == 0,
                    std::string ("vowel /") + vw + "/ "
                        + fmt ("82 Hz – 1 kHz @ %.1f kHz: worst median %.2f c, worst p95 %.2f c, min voiced %.1f %%, gross %.0f",
                               fs / 1000, worstMed, worstP95, worstVoiced * 100, gross));
        }

    // 3. vibrato (±50 cents at 5.5 Hz) – the detector must follow it, not average it away
    {
        const double fs = 48000.0;
        for (double base : { 130.8, 261.6, 523.3 })
        {
            auto f0 = [base] (double t) { return base * std::pow (2.0, 0.5 / 12.0 * std::sin (2.0 * pi * 5.5 * t)); };
            auto x = vowel (fs, 1.2, f0, 'a');
            auto s = score (analyse (x, fs), fs, f0);
            report (s.medianCents < 6.0 && s.p95Cents < 15.0 && s.gross == 0,
                    fmt ("vibrato ±50 c @ 5.5 Hz on %.1f Hz: median %.2f c, p95 %.2f c, gross %.0f", base, s.medianCents, s.p95Cents, s.gross));
        }
    }

    // 4. fast melodic movement: a 100 ms glide of a fifth, then a jump of an octave up and back
    {
        const double fs = 48000.0;
        auto f0 = [] (double t)
        {
            if (t < 0.3) return 220.0;
            if (t < 0.4) return 220.0 * std::pow (1.5, (t - 0.3) / 0.1);
            if (t < 0.7) return 330.0;
            if (t < 1.0) return 660.0;
            return 330.0;
        };
        auto x = vowel (fs, 1.3, f0, 'a');
        auto fr = analyse (x, fs);
        // ignore 40 ms around each step (the analysis window straddles two notes there)
        int octaveErrors = 0, n = 0;
        for (const auto& f : fr)
        {
            const double t = f.centreSample / fs;
            if (t < 0.06 || std::abs (t - 0.7) < 0.04 || std::abs (t - 1.0) < 0.04 || f.voicing != Voicing::voiced) continue;
            ++n;
            if (std::abs (cents (f.f0Hz, f0 (t))) > 600.0) ++octaveErrors;
        }
        report (octaveErrors == 0 && n > 150, fmt ("glide + octave jumps: %.0f voiced frames, %.0f octave errors", n, octaveErrors));
    }

    // 5. octave traps: strong 2nd harmonic, and a missing fundamental (perceived pitch = f0)
    {
        const double fs = 48000.0;
        for (double f : { 98.0, 150.0, 233.1, 392.0 })
        {
            auto strongH2 = harmonic (fs, 0.6, [f] (double) { return f; }, { 0.3, 1.0, 0.4, 0.2, 0.1 });
            auto a = score (analyse (strongH2, fs), fs, [f] (double) { return f; });
            auto missing = harmonic (fs, 0.6, [f] (double) { return f; }, { 0.0, 1.0, 0.8, 0.6, 0.4, 0.3 });
            auto b = score (analyse (missing, fs), fs, [f] (double) { return f; });
            report (a.gross == 0 && b.gross == 0 && a.voiced > 0 && b.voiced > 0,
                    fmt ("octave traps at %.1f Hz: strong H2 gross %.0f/%.0f, missing fundamental gross %.0f/%.0f",
                         f, a.gross, a.voiced, b.gross, b.voiced));
        }
    }

    // 6. noisy and breathy voices
    {
        const double fs = 48000.0;
        for (double snr : { 20.0, 12.0 })
        {
            int gross = 0, voiced = 0, frames = 0;
            for (double f : { 110.0, 220.0, 440.0 })
            {
                auto x = vowel (fs, 0.8, [f] (double) { return f; }, 'a');
                addNoise (x, snr, (unsigned) f);
                auto s = score (analyse (x, fs), fs, [f] (double) { return f; });
                gross += s.gross; voiced += s.voiced; frames += s.frames;
            }
            const double grossPct = 100.0 * gross / std::max (1, voiced), voicedPct = 100.0 * voiced / std::max (1, frames);
            report (grossPct < 1.0 && voicedPct > (snr > 15 ? 90.0 : 70.0),
                    fmt ("vowel + white noise at %.0f dB SNR: voiced %.1f %%, gross errors %.2f %%", snr, voicedPct, grossPct));
        }
        // breathy voices, judged over many noise realisations (a single seed once passed on Linux and
        // failed on Windows): aspiration noise at −20 and −14 dB below the voice
        for (double breathDb : { -20.0, -14.0 })
        {
            int gross = 0, voiced = 0, frames = 0;
            for (unsigned seed = 1; seed <= 12; ++seed)
                for (double f : { 110.0, 196.0, 392.0 })
                {
                    auto x = vowel (fs, 0.8, [f] (double) { return f; }, 'a', breathDb, seed);
                    auto s = score (analyse (x, fs), fs, [f] (double) { return f; });
                    gross += s.gross; voiced += s.voiced; frames += s.frames;
                }
            const double grossPct = 100.0 * gross / std::max (1, voiced), voicedPct = 100.0 * voiced / std::max (1, frames);
            report (grossPct < (breathDb < -15.0 ? 0.05 : 0.5) && voicedPct > 90.0,
                    fmt ("breathy vowel, aspiration %.0f dB, 36 noise realisations: voiced %.1f %%, gross errors %.3f %%", breathDb, voicedPct, grossPct));
        }

        // a real octave drop must be followed even with breath noise (the continuity guard must not
        // hold the old pitch): no frame may still read the old note 100 ms after the drop
        {
            int stuck = 0, n = 0;
            for (unsigned seed = 1; seed <= 12; ++seed)
                for (double hi : { 440.0, 262.0 })
                {
                    auto f0 = [hi] (double t) { return t < 0.4 ? hi : hi / 2.0; };
                    auto x = vowel (fs, 0.8, f0, 'a', -14.0, seed);
                    for (const auto& q : analyse (x, fs))
                    {
                        const double t = q.centreSample / fs;
                        if (t < 0.5 || q.voicing != Voicing::voiced) continue;
                        ++n;
                        if (std::abs (cents (q.f0Hz, hi)) < 100.0) ++stuck;
                    }
                }
            report (stuck == 0 && n > 500, fmt ("octave drop with breath noise followed: %.0f of %.0f frames still on the old note", stuck, n));
        }
    }

    // 7. unvoiced material and silence must not produce a pitch
    {
        const double fs = 48000.0;
        auto n = analyse (noise (fs, 1.0, -20.0), fs);
        int v = 0;
        for (const auto& f : n) v += f.voicing == Voicing::voiced;
        report (v < (int) n.size() / 50, fmt ("white noise (fricative-like): %.0f of %.0f frames voiced", v, (double) n.size()));

        std::vector<float> silence ((size_t) fs, 0.0f);
        auto s = analyse (silence, fs);
        int nonSilent = 0;
        for (const auto& f : s) nonSilent += f.voicing != Voicing::silence;
        report (nonSilent == 0 && ! s.empty(), fmt ("digital silence: %.0f of %.0f frames not silent", nonSilent, (double) s.size()));

        auto quiet = sine (fs, 0.5, [] (double) { return 220.0; }, 0.0005);   // −69 dBFS: below the gate
        int qv = 0;
        for (const auto& f : analyse (quiet, fs)) qv += f.voicing != Voicing::silence;
        report (qv == 0, fmt ("very quiet tone below the gate treated as silence (%.0f frames not silent)", qv));
    }

    // 8. results do not depend on the host block size
    {
        const double fs = 44100.0;
        auto x = vowel (fs, 0.5, [] (double t) { return 200.0 + 40.0 * t; }, 'i');
        auto a = analyse (x, fs, 1), b = analyse (x, fs, 37), c = analyse (x, fs, 4096);
        bool same = a.size() == b.size() && a.size() == c.size();
        for (size_t i = 0; same && i < a.size(); ++i)
            same = a[i].f0Hz == b[i].f0Hz && a[i].f0Hz == c[i].f0Hz && a[i].centreSample == c[i].centreSample && a[i].voicing == c[i].voicing;
        report (same, "identical frames with block sizes 1, 37 and 4096");
    }

    // 9. time stamps: a pitch step must be reported at the right time (within 10 ms)
    {
        const double fs = 48000.0;
        auto f0 = [] (double t) { return t < 0.5 ? 200.0 : 300.0; };
        auto fr = analyse (sine (fs, 1.0, f0), fs);
        double tStep = -1.0;
        for (const auto& f : fr)
            if (f.voicing == Voicing::voiced && f.f0Hz > 250.0) { tStep = f.centreSample / fs; break; }
        report (tStep > 0.49 && tStep < 0.51, fmt ("pitch step at 500 ms reported at %.1f ms", tStep * 1000.0));
    }

}
