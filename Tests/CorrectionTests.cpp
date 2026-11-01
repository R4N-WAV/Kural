// Pitch correction: the decision logic on known pitch tracks, then the whole lead path (detector →
// corrector → PSOLA) on synthetic sung vowels, measured with the detector on the output.
// Expected values come from the definition of each control, not from the code under test.
#include "TestSupport.h"
#include "DSP/HarmonyEngine.h"
#include "DSP/PitchCorrector.h"

using namespace kural::test;
using namespace kural::dsp;

namespace
{
constexpr float hop = 0.005f;

// runs a pitch track (MIDI per 5 ms frame) through a corrector; returns the corrected pitch per frame
std::vector<float> correct (const CorrectionParams& p, const std::function<float (float)>& lead, float seconds)
{
    PitchCorrector c;
    c.setParams (p);
    std::vector<float> out;
    for (float t = 0.0f; t < seconds; t += hop)
    {
        const float m = lead (t);
        out.push_back (m + c.update (m, hop));
    }
    return out;
}

float maxDev (const std::vector<float>& v, size_t from, float ref)
{
    float d = 0.0f;
    for (size_t i = from; i < v.size(); ++i) d = std::max (d, std::abs (v[i] - ref));
    return d;
}

float mean (const std::vector<float>& v, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += v[i];
    return (float) (s / (double) (to - from));
}

// processed lead through the full engine; returns the detector's frames on the output
std::vector<PitchFrame> leadThroughEngine (const std::vector<float>& x, double fs, const LeadParams& lp, std::vector<float>* audio = nullptr)
{
    HarmonyEngine e;
    e.prepare (fs);
    e.setLead (lp);
    std::vector<float> L (x.size(), 0.0f), lead (x.size(), 0.0f);
    for (size_t i = 0; i < x.size(); i += 256)
    {
        const int n = (int) std::min<size_t> (256, x.size() - i);
        e.process (&x[i], &L[i], nullptr, &lead[i], n, [] (const PitchFrame&, const HarmonyEngine&) {});
    }
    const auto lat = (size_t) e.latency();
    std::vector<float> y (x.size(), 0.0f);
    for (size_t i = lat; i < x.size(); ++i) y[i - lat] = lead[i];
    if (audio != nullptr) *audio = y;
    return analyse (y, fs);
}

// mean and peak-to-peak of the voiced output pitch (MIDI) between two times
void pitchStats (const std::vector<PitchFrame>& fr, double fs, double t0, double t1, double& meanMidi, double& p2p)
{
    double s = 0.0, lo = 1e9, hi = -1e9;
    int n = 0;
    for (const auto& f : fr)
    {
        const double t = (double) f.centreSample / fs;
        if (t < t0 || t > t1 || f.voicing != Voicing::voiced || f.f0Hz <= 0.0f) continue;
        const double m = hzToMidi (f.f0Hz);
        s += m; lo = std::min (lo, m); hi = std::max (hi, m); ++n;
    }
    meanMidi = n > 0 ? s / n : 0.0;
    p2p = n > 0 ? hi - lo : 0.0;
}
} // namespace

void correctionTests()
{
    const auto cmaj = Scale::make (0, ScaleType::major);

    // 1. hard tune (retune 0, flatten): a held note 40 cents flat with ±30 c vibrato comes out flat-lined on the note
    {
        CorrectionParams p; p.on = true; p.scale = cmaj; p.retuneMs = 0.0f; p.vibrato = 0.0f;
        const auto out = correct (p, [] (float t) { return 59.6f + 0.3f * std::sin (2.0f * 3.14159f * 5.5f * t); }, 2.0f);
        const float d = maxDev (out, 20, 60.0f);
        report (d < 0.001f, fmt ("hard tune: a note 40 c flat with +-30 c vibrato -> C4 exactly, no flips (max deviation %.4f st)", d));
    }

    // 2. natural (vibrato kept): centre moved onto the note, vibrato depth unchanged
    {
        CorrectionParams p; p.on = true; p.scale = cmaj; p.retuneMs = 0.0f; p.vibrato = 1.0f;
        const auto lead = [] (float t) { return 59.6f + 0.3f * std::sin (2.0f * 3.14159f * 5.5f * t); };
        const auto out = correct (p, lead, 3.0f);
        float lo = 1e9f, hi = -1e9f;
        for (size_t i = 200; i < out.size(); ++i) { lo = std::min (lo, out[i]); hi = std::max (hi, out[i]); }
        const float centre = mean (out, 200, out.size());
        report (std::abs (centre - 60.0f) < 0.03f && hi - lo > 0.5f && hi - lo < 0.7f,
                fmt ("natural tune: centre %.3f (expected 60), vibrato %.2f st peak to peak (expected 0.6)", centre, hi - lo));
    }

    // 3. retune speed: a step onto a note 30 c flat is corrected with the set time constant (63 % after retune ms)
    {
        CorrectionParams p; p.on = true; p.scale = cmaj; p.retuneMs = 100.0f; p.vibrato = 0.0f;
        const auto out = correct (p, [] (float) { return 63.7f; }, 1.0f);
        const float at100 = 64.0f - out[20];       // remaining error after 100 ms (frame 20)
        const float at500 = 64.0f - out[100];
        // after 100 ms, 37 % of the 0.3 st error remains; after 500 ms almost none
        report (std::abs (at100 - 0.3f * 0.368f) < 0.02f && at500 < 0.005f,
                fmt ("retune 100 ms: error after 100 ms %.3f st (expected 0.110), after 500 ms %.4f", at100, at500));
    }

    // 4. note changes: a C-D-E-F line sung 20 c sharp comes out on C, D, E, F (hard tune)
    {
        CorrectionParams p; p.on = true; p.scale = cmaj; p.retuneMs = 0.0f; p.vibrato = 0.0f;
        const int notes[] = { 60, 62, 64, 65 };
        const auto out = correct (p, [&] (float t) { return (float) notes[std::min (3, (int) (t / 0.3f))] + 0.2f; }, 1.2f);
        bool ok = true;
        std::string got;
        for (int k = 0; k < 4; ++k) { const float v = out[(size_t) (k * 60 + 40)]; ok = ok && std::abs (v - (float) notes[k]) < 0.01f; got += fmt ("%.2f ", v); }
        report (ok, "hard tune follows a C D E F line sung 20 c sharp: " + got);
    }

    // 5. scale: a sung C#4 in C major is pulled to a scale note; in chromatic it stays C#
    {
        CorrectionParams p; p.on = true; p.retuneMs = 0.0f; p.vibrato = 0.0f;
        p.scale = cmaj;
        const auto a = correct (p, [] (float) { return 61.1f; }, 0.3f);
        p.scale = Scale::make (0, ScaleType::chromatic);
        const auto b = correct (p, [] (float) { return 61.1f; }, 0.3f);
        report ((std::abs (a.back() - 62.0f) < 0.01f || std::abs (a.back() - 60.0f) < 0.01f) && std::abs (b.back() - 61.0f) < 0.01f,
                fmt ("C#4+10c: C major -> %.2f (a scale note), chromatic -> %.2f (C#)", a.back(), b.back()));
    }

    // 6. flex-tune: at 100 % a note 40 c off is left alone while a note 3 c off is corrected
    {
        CorrectionParams p; p.on = true; p.scale = cmaj; p.retuneMs = 0.0f; p.vibrato = 1.0f; p.flex = 1.0f;
        const auto far = correct (p, [] (float) { return 60.4f; }, 0.5f);
        const auto near = correct (p, [] (float) { return 60.03f; }, 0.5f);
        report (std::abs (far.back() - 60.4f) < 0.01f && std::abs (near.back() - 60.0f) < 0.01f,
                fmt ("flex 100 %%: 40 c off left at %.3f, 3 c off corrected to %.3f", far.back(), near.back()));
    }

    // 7. humanize: on a held note the correction is slower after 0.2 s (more of the singer's drift kept)
    {
        CorrectionParams p; p.on = true; p.scale = cmaj; p.retuneMs = 40.0f; p.vibrato = 0.0f;
        // a slow drift on a held note: 0 → +40 c over 1 s
        const auto lead = [] (float t) { return 64.0f + 0.4f * std::min (1.0f, std::max (0.0f, t - 0.5f)); };
        const auto plain = correct (p, lead, 1.5f);
        p.humanize = 1.0f;
        const auto human = correct (p, lead, 1.5f);
        const float ep = plain[280] - 64.0f, eh = human[280] - 64.0f;   // at 1.4 s
        report (eh > 2.0f * ep && eh > 0.02f, fmt ("humanize: drift left on a held note %.3f st vs %.3f without", eh, ep));
    }

    // 8. off: no correction at all
    {
        CorrectionParams p; p.on = false; p.scale = cmaj;
        const auto out = correct (p, [] (float) { return 59.6f; }, 0.5f);
        report (std::abs (out.back() - 59.6f) < 1e-6f, "tune off: pitch unchanged");
    }

    // ---- the whole lead path on audio
    const double fs = 48000.0;
    // 9. natural tune on a sung vowel 35 c flat of A3 with ±25 c vibrato: centre on A3, vibrato kept
    {
        const auto x = vowel (fs, 3.0, [] (double t) { return midiToHz (56.65f + 0.25f * (float) std::sin (2.0 * pi * 5.0 * t)); }, 'a');
        LeadParams lp; lp.tune.on = true; lp.tune.scale = cmaj; lp.tune.retuneMs = 20.0f; lp.tune.vibrato = 1.0f;
        // 56.65 is 35 c below A3 (57); the nearest C-major note is A3
        double m, p2p;
        pitchStats (leadThroughEngine (x, fs, lp), fs, 0.8, 2.8, m, p2p);
        double m0, p0;
        pitchStats (analyse (x, fs), fs, 0.8, 2.8, m0, p0);
        report (std::abs (m - 57.0) < 0.05 && std::abs (p2p - p0) < 0.12,
                fmt ("audio, natural: mean %.3f (expected 57.000), vibrato %.2f st p-p (input %.2f)", m, p2p, p0));
    }
    // 10. hard tune on the same vowel: on A3 and the vibrato flattened
    {
        const auto x = vowel (fs, 3.0, [] (double t) { return midiToHz (56.65f + 0.25f * (float) std::sin (2.0 * pi * 5.0 * t)); }, 'a');
        LeadParams lp; lp.tune.on = true; lp.tune.scale = cmaj; lp.tune.retuneMs = 0.0f; lp.tune.vibrato = 0.0f;
        double m, p2p;
        pitchStats (leadThroughEngine (x, fs, lp), fs, 0.8, 2.8, m, p2p);
        report (std::abs (m - 57.0) < 0.05 && p2p < 0.15, fmt ("audio, hard: mean %.3f (expected 57.000), wobble %.2f st p-p (input 0.50)", m, p2p));
    }
    // 11. transpose +5 and formant: pitch moves by 5 st exactly
    {
        const auto x = vowel (fs, 2.0, [] (double) { return 220.0; }, 'o');
        LeadParams lp; lp.transposeSemis = 5.0f;
        double m, p2p;
        pitchStats (leadThroughEngine (x, fs, lp), fs, 0.5, 1.8, m, p2p);
        report (std::abs (m - (57.0 + 5.0)) < 0.03, fmt ("audio, transpose +5: %.3f (expected 62.000)", m));
    }
    // 12. in tune already: natural tune leaves the pitch as it was, frame by frame
    {
        const auto x = vowel (fs, 2.0, [] (double t) { return midiToHz (60.0f + 0.2f * (float) std::sin (2.0 * pi * 5.5 * t)); }, 'e');
        LeadParams lp; lp.tune.on = true; lp.tune.scale = cmaj; lp.tune.retuneMs = 30.0f; lp.tune.vibrato = 1.0f;
        const auto fo = leadThroughEngine (x, fs, lp);
        const auto fi = analyse (x, fs);
        double s = 0.0; int n = 0;
        for (size_t k = 0; k < std::min (fo.size(), fi.size()); ++k)
        {
            const double t = (double) fi[k].centreSample / fs;
            if (t < 0.6 || t > 1.8 || fi[k].f0Hz <= 0.0f || fo[k].f0Hz <= 0.0f) continue;
            s += std::abs (cents (fo[k].f0Hz, fi[k].f0Hz)); ++n;
        }
        const double d = n > 0 ? s / n : 1e9;
        report (d < 4.0, fmt ("audio, in-tune voice through natural tune: pitch unchanged within %.1f c on average (< 4)", d));
    }
}
