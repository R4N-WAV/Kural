// Harmony planning: scale construction, scale-aware intervals, hysteresis, expression carried over.
// Expected notes are written out by hand from music theory, not computed by the code under test.
#include "TestSupport.h"
#include "DSP/Harmony.h"

using namespace kural::test;
using namespace kural::dsp;

namespace
{
const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
std::string noteName (int m) { return std::string (names[((m % 12) + 12) % 12]) + std::to_string (m / 12 - 1); }

// the harmony note (no expression) for a lead note, interval in scale steps
int harmonyNote (const Scale& s, int leadNote, int steps, int octave = 0)
{
    HarmonyPlanner p;
    p.setScale (s);
    p.update ((float) leadNote);
    return (int) std::lround (p.target ((float) leadNote, { IntervalMode::scaleSteps, steps, octave, 0.0f }).noteMidi);
}

bool expectSeries (const Scale& s, const std::vector<int>& leads, int steps, const std::vector<int>& expected, std::string& got)
{
    bool ok = true;
    got.clear();
    for (size_t i = 0; i < leads.size(); ++i)
    {
        const int h = harmonyNote (s, leads[i], steps);
        got += noteName (leads[i]) + "->" + noteName (h) + " ";
        ok = ok && h == expected[i];
    }
    return ok;
}
} // namespace

void harmonyTests()
{
    // 1. scale sizes and membership
    {
        const int sizes[] = { 7, 7, 7, 7, 5, 5, 6, 12 };
        bool ok = true;
        for (int t = 0; t < 8; ++t) ok = ok && Scale::make (0, (ScaleType) t).size() == sizes[t];
        const auto cmaj = Scale::make (0, ScaleType::major);
        ok = ok && cmaj.contains (60) && cmaj.contains (64) && ! cmaj.contains (61) && cmaj.contains (71) && ! cmaj.contains (70);
        const auto aHarm = Scale::make (9, ScaleType::harmonicMinor);     // A B C D E F G#
        ok = ok && aHarm.contains (68) && ! aHarm.contains (67) && aHarm.contains (65);
        report (ok, "scale sizes (7,7,7,7,5,5,6,12) and membership (C major, A harmonic minor)");
    }

    // 2. diatonic thirds above in C major: the interval changes between major and minor thirds
    {
        std::string got;
        const auto s = Scale::make (0, ScaleType::major);
        //                 C   D   E   F   G   A   B   C
        const bool ok = expectSeries (s, { 60, 62, 64, 65, 67, 69, 71, 72 }, 2, { 64, 65, 67, 69, 71, 72, 74, 76 }, got);
        report (ok, "C major, third above: " + got);
    }

    // 3. thirds below, fifths above, in G major (F# in the key)
    {
        std::string got;
        const auto s = Scale::make (7, ScaleType::major);
        //                      G   A   B   C   D   E   F#
        bool ok = expectSeries (s, { 67, 69, 71, 72, 74, 76, 78 }, -2, { 64, 66, 67, 69, 71, 72, 74 }, got);
        report (ok, "G major, third below: " + got);
        ok = expectSeries (s, { 67, 69, 71, 72, 74, 76, 78 }, 4, { 74, 76, 78, 79, 81, 83, 84 }, got);
        report (ok, "G major, fifth above (B -> F#, F# -> C): " + got);
    }

    // 4. harmonic and melodic minor and a pentatonic scale
    {
        std::string got;
        auto s = Scale::make (9, ScaleType::harmonicMinor);         // A B C D E F G#
        bool ok = expectSeries (s, { 69, 71, 72, 76, 64 }, 2, { 72, 74, 76, 80, 68 }, got);   // E -> G# (the leading tone)
        report (ok, "A harmonic minor, third above: " + got);
        s = Scale::make (9, ScaleType::melodicMinor);               // A B C D E F# G#
        ok = expectSeries (s, { 69, 74, 76 }, 2, { 72, 78, 80 }, got);
        report (ok, "A melodic minor, third above: " + got);
        s = Scale::make (0, ScaleType::majorPentatonic);            // C D E G A
        ok = expectSeries (s, { 60, 62, 64, 67, 69 }, 1, { 62, 64, 67, 69, 72 }, got);
        report (ok, "C major pentatonic, one step above: " + got);
    }

    // 5. octaves and fixed semitone intervals
    {
        HarmonyPlanner p;
        p.setScale (Scale::make (0, ScaleType::major));
        p.update (64.0f);
        const int oct = (int) std::lround (p.target (64.0f, { IntervalMode::scaleSteps, 0, 1, 0.0f }).noteMidi);
        const int octDown = (int) std::lround (p.target (64.0f, { IntervalMode::scaleSteps, 0, -1, 0.0f }).noteMidi);
        const int semis = (int) std::lround (p.target (64.0f, { IntervalMode::semitones, 3, 0, 0.0f }).noteMidi);
        const int tenth = (int) std::lround (p.target (64.0f, { IntervalMode::scaleSteps, 2, 1, 0.0f }).noteMidi);
        report (oct == 76 && octDown == 52 && semis == 67 && tenth == 79,
                "E4: octave up E5, octave down E3, +3 semitones G4, third + octave G5 (" + noteName (oct) + ", "
                    + noteName (octDown) + ", " + noteName (semis) + ", " + noteName (tenth) + ")");
    }

    // 6. expression is carried over: a lead 30 cents sharp with vibrato gives a harmony 30 cents sharp
    //    with the same vibrato, i.e. the shift (harmony − lead) stays exactly constant
    {
        HarmonyPlanner p;
        p.setScale (Scale::make (0, ScaleType::major));
        double maxDev = 0.0;
        for (int i = 0; i < 200; ++i)
        {
            const float lead = 62.3f + 0.45f * std::sin ((float) i * 0.2f);   // D4 +30 c, ±45 c vibrato
            p.update (lead);
            const auto t = p.target (lead, { IntervalMode::scaleSteps, 2, 0, 0.0f });   // F above D: minor third
            maxDev = std::max (maxDev, (double) std::abs (t.shiftFrom (lead) - 3.0f));
        }
        report (maxDev < 1e-4, fmt ("vibrato ±45 c around D4+30c: shift stays a minor third (max deviation %.5f semitones)", maxDev));
    }

    // 7. vibrato is not a note change: wide vibrato centred between two scale notes (C# in C major,
    //    ±65 cents, 5.5 Hz) must not flip the harmony; normal ±50 c vibrato on D must not either
    {
        const double dt = 0.005;
        for (int k = 0; k < 2; ++k)
        {
            const float centreNote = k == 0 ? 61.0f : 62.0f, depth = k == 0 ? 0.65f : 0.5f;
            HarmonyPlanner p;
            p.setScale (Scale::make (0, ScaleType::major));
            int flips = 0, last = -999;
            for (int i = 0; i < 600; ++i)       // 3 s
            {
                const float lead = centreNote + depth * (float) std::sin (2.0 * pi * 5.5 * i * dt);
                p.update (lead, (float) dt);
                if (i > 40 && last != -999 && p.currentDegree() != last) ++flips;   // after the first 200 ms
                last = p.currentDegree();
            }
            report (flips == 0, fmt (k == 0 ? "wide vibrato (±65 c) centred between C and D: %.0f harmony note changes in 3 s"
                                            : "normal vibrato (±50 c) on D: %.0f harmony note changes in 3 s", flips));
        }
    }

    // 7b. real note changes are followed fast: C -> D step, D -> E with a 60 ms scoop, and a leap
    {
        const double dt = 0.005;
        auto timeToFollow = [&] (std::function<float (double)> lead, int targetNote)
        {
            HarmonyPlanner p;
            p.setScale (Scale::make (0, ScaleType::major));
            for (int i = 0; i < 300; ++i)
            {
                const double t = i * dt;
                p.update (lead (t), (float) dt);
                if (t >= 0.5 && p.getScale().noteOfDegree (p.currentDegree()) == targetNote) return (t - 0.5) * 1000.0;
            }
            return 9999.0;
        };
        const double step = timeToFollow ([] (double t) { return t < 0.5 ? 60.0f : 62.0f; }, 62);
        const double scoop = timeToFollow ([] (double t) { return t < 0.5 ? 62.0f : t < 0.56 ? (float) (62.0 + 2.0 * (t - 0.5) / 0.06) : 64.0f; }, 64);
        const double leap = timeToFollow ([] (double t) { return t < 0.5 ? 60.0f : 69.0f; }, 69);
        report (step <= 40.0 && scoop <= 100.0 && leap <= 40.0,
                fmt ("note changes followed after: step %.0f ms, 60 ms scoop %.0f ms (from its start), leap %.0f ms", step, scoop, leap));
    }

    // 8. a chromatic passing note keeps the interval of the scale note it is nearest to
    {
        HarmonyPlanner p;
        p.setScale (Scale::make (0, ScaleType::major));
        p.update (64.0f);                                           // on E
        p.update (63.1f);                                           // D#-ish passing tone: still nearest E
        const auto t = p.target (63.1f, { IntervalMode::scaleSteps, 2, 0, 0.0f });
        report (std::abs (t.midi() - 66.1f) < 1e-4f, fmt ("passing tone D#4-10c over E: harmony moves in parallel (target %.2f)", t.midi()));
    }

    // 9. every key: a third above the root is a major third in major and a minor third in minor
    {
        bool ok = true;
        for (int root = 0; root < 12; ++root)
        {
            const int lead = 60 + root;
            ok = ok && harmonyNote (Scale::make (root, ScaleType::major), lead, 2) - lead == 4;
            ok = ok && harmonyNote (Scale::make (root, ScaleType::naturalMinor), lead, 2) - lead == 3;
            ok = ok && harmonyNote (Scale::make (root, ScaleType::major), lead, 4) - lead == 7;
            ok = ok && harmonyNote (Scale::make (root, ScaleType::major), lead - 12, 7) - lead == 0;   // 7 steps = octave
        }
        report (ok, "all 12 keys: major/minor third and fifth above the root, 7 steps = one octave");
    }

    // 10. negative degrees and very low/high notes stay consistent (no off-by-one at octave borders)
    {
        bool ok = true;
        const auto s = Scale::make (2, ScaleType::naturalMinor);    // D minor
        for (int d = -30; d < 100; ++d)
            ok = ok && s.nearestDegree ((float) s.noteOfDegree (d)) == d && s.contains (s.noteOfDegree (d))
                    && s.noteOfDegree (d + 1) > s.noteOfDegree (d);
        report (ok, "D minor: degree <-> note round trip from far below MIDI 0 to above MIDI 120");
    }

    // 11. custom scale: the root is always included
    {
        const auto s = Scale::make (0, ScaleType::custom, 0x0090);  // E and G only (bits 4, 7) + root
        report (s.size() == 3 && s.contains (60) && s.contains (64) && s.contains (67) && ! s.contains (62),
                "custom scale {E, G} becomes {C, E, G}");
    }
}
