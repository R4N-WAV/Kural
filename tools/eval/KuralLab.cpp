// KuralLab: runs Kural's real DSP code offline over WAV files, for measurements and listening tests.
//
//   KuralLab analyse <in.wav> <frames.csv>
//       pitch detector frames: time_s, f0_hz, confidence, voicing (0 silence,1 unvoiced,2 uncertain,3 voiced), rms_db
//   KuralLab shift <psola|pv|tap> <semitones> <formant_semis> <unvoiced_gain> <in.wav> <out.wav>
//       one shifted voice (no dry signal); prints latency and processing time
//   KuralLab bench <psola|pv> <voices> <in.wav>
//       CPU time for N voices sharing one analysis (different intervals per voice)
//   KuralLab harmonize <key 0-11> <scale 0-7> <in.wav> <out.wav> [interval:pan ...]
//   KuralLab tune <key 0-11> <scale 0-7> <retune_ms> <vibrato 0-1> <humanize 0-1> <flex 0-1> <transpose> <formant> <in.wav> <out.wav>
//       the processed lead alone (pitch correction / transpose / formant), aligned with the input
//       the full harmony engine as the plugin runs it: dry (delayed by the latency) + voices, stereo.
//       Voices are given as scale steps and pan, e.g. 2:-0.35 -2:0.35 (default: third above / below)
//
// "tap" is the rotating-tap delay shifter of R4N ATELIER (Sonic-Lab, PitchModule, 50 ms grain),
// ported here as a baseline: it is not pitch-synchronous and does not preserve formants.
#include "DSP/HarmonyEngine.h"
#include "DSP/PhaseVocoder.h"
#include "DSP/PitchDetector.h"
#include "DSP/Psola.h"
#include "WavIO.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace kural::dsp;
using kural::tools::Wav;

namespace
{
// ---- ATELIER rotating-tap shifter (Sonic-Lab core/src/modules/EffectModules.cpp, PitchModule), mono,
// no feedback, no tone filter: two taps half a window apart, delay ramping at (1 − ratio), sin² cross-fade
struct RotatingTap
{
    std::vector<float> line;
    int w = 0;
    double ph = 0.0, W = 0.0, ratio = 1.0;
    void prepare (double fs, double semis, double windowMs = 50.0)
    {
        line.assign ((size_t) (fs * 0.32), 0.0f);
        W = std::max (16.0, windowMs * 0.001 * fs);
        ratio = std::pow (2.0, semis / 12.0);
    }
    float read (double delay) const
    {
        const double pos = (double) w - delay;
        const int n = (int) line.size();
        const double fl = std::floor (pos);
        const int i = (int) fl;
        const float t = (float) (pos - fl);
        auto at = [&] (int k) { return line[(size_t) (((k % n) + n) % n)]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
    float next (float x)
    {
        line[(size_t) w] = x;
        double y = 0.0;
        for (int t = 0; t < 2; ++t)
        {
            double p = ph + 0.5 * t;
            p -= std::floor (p);
            const double s = std::sin (3.14159265358979 * p);
            y += s * s * read (4.0 + p * W);
        }
        ph += (1.0 - ratio) / W;
        ph -= std::floor (ph);
        w = (w + 1) % (int) line.size();
        return (float) y;
    }
};

double secondsSince (std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
}

int usage()
{
    std::fprintf (stderr, "usage:\n  KuralLab analyse <in.wav> <frames.csv>\n"
                          "  KuralLab harmonize <key> <scale> <in.wav> <out.wav> [steps:pan ...]\n"
                          "  KuralLab shift <psola|pv|tap> <semitones> <formant_semis> <unvoiced_gain> <in.wav> <out.wav>\n"
                          "  KuralLab bench <psola|pv> <voices> <in.wav>\n");
    return 2;
}

bool load (const char* path, Wav& w)
{
    std::string err;
    if (! kural::tools::readWav (path, w, err)) { std::fprintf (stderr, "%s: %s\n", path, err.c_str()); return false; }
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) return usage();
    const std::string cmd = argv[1];

    if (cmd == "analyse" && argc == 4)
    {
        Wav in;
        if (! load (argv[2], in)) return 1;
        PitchDetector det;
        PitchDetector::Config cfg;
        // experiment hooks (tools only): KURAL_THRESHOLD, KURAL_OCTAVE_GUARD
        if (const char* e = std::getenv ("KURAL_THRESHOLD")) cfg.threshold = (float) std::atof (e);
        if (const char* e = std::getenv ("KURAL_OCTAVE_GUARD")) cfg.octaveGuard = (float) std::atof (e);
        det.prepare (in.sampleRate, cfg);
        FILE* f = std::fopen (argv[3], "w");
        if (! f) return 1;
        std::fprintf (f, "time_s,f0_hz,confidence,voicing,rms_db\n");
        det.process (in.mono.data(), (int) in.mono.size(), [&] (const PitchFrame& fr)
        {
            std::fprintf (f, "%.6f,%.4f,%.4f,%d,%.2f\n", (double) fr.centreSample / in.sampleRate, fr.f0Hz, fr.confidence,
                          (int) fr.voicing, fr.rmsDb);
        });
        std::fclose (f);
        return 0;
    }

    if (cmd == "shift" && argc == 8)
    {
        const std::string algo = argv[2];
        const float semis = (float) std::atof (argv[3]), formant = (float) std::atof (argv[4]), unv = (float) std::atof (argv[5]);
        Wav in;
        if (! load (argv[6], in)) return 1;
        const double fs = in.sampleRate;
        std::vector<float> out (in.mono.size(), 0.0f);
        int latency = 0;
        const auto t0 = std::chrono::steady_clock::now();
        if (algo == "psola")
        {
            PitchDetector det; det.prepare (fs);
            PitchMarker mk; mk.prepare (fs);
            PsolaVoice v; v.prepare (mk);
            PsolaVoiceSettings vs { semis, formant, unv, 0.0f };
            if (const char* e = std::getenv ("KURAL_PSOLA_BLEND")) vs.blendGrains = std::atoi (e) != 0;   // A/B hook
            v.set (vs);
            if (const char* e = std::getenv ("KURAL_PSOLA_CENTRE")) v.setGrainCentring (std::atoi (e) != 0);
            v.reset (mk);
            latency = mk.latency();
            for (size_t i = 0; i < in.mono.size(); ++i)
            {
                det.process (&in.mono[i], 1, [&] (const PitchFrame& fr) { mk.onPitchFrame (fr); });
                mk.push (in.mono[i]);
                out[i] = v.next (mk);
            }
        }
        else if (algo == "pv")
        {
            PitchDetector det; det.prepare (fs);
            PvAnalysis a; a.prepare (fs);
            PvVoice v; v.prepare (a);
            v.set ({ semis, formant, unv });
            latency = a.latency();
            for (size_t i = 0; i < in.mono.size(); ++i)
            {
                det.process (&in.mono[i], 1, [&] (const PitchFrame& fr) { a.onPitchFrame (fr); });
                if (a.push (in.mono[i])) v.synthesise (a);
                out[i] = v.next();
            }
        }
        else if (algo == "tap")
        {
            RotatingTap t;
            t.prepare (fs, semis);
            latency = (int) std::lround (4.0 + 0.5 * t.W);      // as ATELIER reports it
            for (size_t i = 0; i < in.mono.size(); ++i) out[i] = t.next (in.mono[i]);
        }
        else return usage();
        const double secs = secondsSince (t0);
        if (! kural::tools::writeWav (argv[7], out, fs)) { std::fprintf (stderr, "cannot write %s\n", argv[7]); return 1; }
        std::printf ("latency_samples=%d latency_ms=%.2f process_s=%.4f audio_s=%.3f\n", latency, 1000.0 * latency / fs, secs,
                     (double) in.mono.size() / fs);
        return 0;
    }

    if (cmd == "bench" && argc == 5)
    {
        const std::string algo = argv[2];
        const int voices = std::max (1, std::atoi (argv[3]));
        Wav in;
        if (! load (argv[4], in)) return 1;
        const double fs = in.sampleRate;
        const float intervals[] = { 4, 7, -5, 12, -12, 3, 9, -8, 2, -3, 5, -7, 16, -9, 10, 14 };
        double sink = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        if (algo == "psola")
        {
            PitchDetector det; det.prepare (fs);
            PitchMarker mk; mk.prepare (fs);
            std::vector<PsolaVoice> vs ((size_t) voices);
            for (int k = 0; k < voices; ++k) { vs[(size_t) k].prepare (mk); vs[(size_t) k].set ({ intervals[k % 16], 0.0f, 0.3f, 30.0f }); vs[(size_t) k].reset (mk); }
            for (size_t i = 0; i < in.mono.size(); ++i)
            {
                det.process (&in.mono[i], 1, [&] (const PitchFrame& fr) { mk.onPitchFrame (fr); });
                mk.push (in.mono[i]);
                for (auto& v : vs) sink += v.next (mk);
            }
        }
        else if (algo == "pv")
        {
            PitchDetector det; det.prepare (fs);
            PvAnalysis a; a.prepare (fs);
            std::vector<PvVoice> vs ((size_t) voices);
            for (int k = 0; k < voices; ++k) { vs[(size_t) k].prepare (a); vs[(size_t) k].set ({ intervals[k % 16], 0.0f, 0.3f }); }
            for (size_t i = 0; i < in.mono.size(); ++i)
            {
                det.process (&in.mono[i], 1, [&] (const PitchFrame& fr) { a.onPitchFrame (fr); });
                const bool ready = a.push (in.mono[i]);
                for (auto& v : vs) { if (ready) v.synthesise (a); sink += v.next(); }
            }
        }
        else return usage();
        const double secs = secondsSince (t0), audio = (double) in.mono.size() / fs;
        std::printf ("algo=%s voices=%d cpu_percent_one_core=%.2f (checksum %.3g)\n", algo.c_str(), voices, 100.0 * secs / audio, sink);
        return 0;
    }
    if (cmd == "harmonize" && argc >= 6)
    {
        Wav in;
        if (! load (argv[4], in)) return 1;
        const double fs = in.sampleRate;
        HarmonyEngine e;
        e.prepare (fs);
        HarmonyEngineParams ep;
        ep.scale = Scale::make (std::atoi (argv[2]), (ScaleType) std::clamp (std::atoi (argv[3]), 0, 7));
        ep.harmonyGain = std::pow (10.0f, -3.0f / 20.0f);
        if (const char* v = std::getenv ("KURAL_NATURAL")) ep.naturalness = (float) std::atof (v);   // 0 … 1
        e.setParams (ep);
        if (const char* v = std::getenv ("KURAL_DUET")) e.setDuet ({ true, 0.7f, (float) std::atof (v) });   // spread 0 … 1
        LeadParams lp;
        if (const char* v = std::getenv ("KURAL_TUNE"))                                                 // natural | hard
        {
            lp.tune.on = true;
            lp.tune.scale = ep.scale;
            const bool hard = std::string (v) == "hard";
            lp.tune.retuneMs = hard ? 0.0f : 25.0f;
            lp.tune.vibrato = hard ? 0.0f : 1.0f;
        }
        e.setLead (lp);
        std::vector<std::pair<int, float>> vs;
        for (int a = 6; a < argc; ++a)
        {
            const std::string spec = argv[a];
            const auto c = spec.find (':');
            vs.push_back ({ std::atoi (spec.substr (0, c).c_str()), c == std::string::npos ? 0.0f : (float) std::atof (spec.substr (c + 1).c_str()) });
        }
        if (vs.empty()) vs = { { 2, -0.35f }, { -2, 0.35f } };
        if (vs.size() == 1 && vs[0].first == 99) vs.clear();         // "99" = no harmony voices (lead / duet only)
        for (size_t v = 0; v < vs.size() && v < (size_t) HarmonyEngine::maxVoices; ++v)
        {
            HarmonyVoiceParams p;
            p.on = true;
            p.interval.steps = vs[v].first;
            p.pan = vs[v].second;
            e.setVoice ((int) v, p);
        }
        const int lat = e.latency();
        std::vector<float> L (in.mono.size(), 0.0f), R (in.mono.size(), 0.0f), lead (in.mono.size(), 0.0f);
        e.process (in.mono.data(), L.data(), R.data(), lead.data(), (int) in.mono.size(), [] (const PitchFrame&, const HarmonyEngine&) {});
        std::vector<float> st (in.mono.size() * 2);
        for (size_t i = 0; i < in.mono.size(); ++i)
        {
            const float dry = lp.active() ? lead[i] : i >= (size_t) lat ? in.mono[i - (size_t) lat] : 0.0f;
            st[2 * i] = dry + L[i];
            st[2 * i + 1] = dry + R[i];
        }
        if (! kural::tools::writeWavStereo (argv[5], st, fs)) return 1;
        std::printf ("latency_samples=%d voices=%zu\n", lat, vs.size());
        return 0;
    }
    if (cmd == "tune" && argc >= 12)
    {
        Wav in;
        if (! load (argv[10], in)) return 1;
        const double fs = in.sampleRate;
        HarmonyEngine e;
        e.prepare (fs);
        LeadParams lp;
        lp.tune.on = true;
        lp.tune.scale = Scale::make (std::atoi (argv[2]), (ScaleType) std::clamp (std::atoi (argv[3]), 0, 7));
        lp.tune.retuneMs = (float) std::atof (argv[4]);
        lp.tune.vibrato = (float) std::atof (argv[5]);
        lp.tune.humanize = (float) std::atof (argv[6]);
        lp.tune.flex = (float) std::atof (argv[7]);
        lp.transposeSemis = (float) std::atof (argv[8]);
        lp.formantSemis = (float) std::atof (argv[9]);
        e.setLead (lp);
        const int lat = e.latency();
        std::vector<float> L (in.mono.size(), 0.0f), lead (in.mono.size(), 0.0f);
        const auto t0 = std::chrono::steady_clock::now();
        e.process (in.mono.data(), L.data(), nullptr, lead.data(), (int) in.mono.size(), [] (const PitchFrame&, const HarmonyEngine&) {});
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        // remove the latency so the output lines up with the input
        std::vector<float> out (in.mono.size(), 0.0f);
        for (size_t i = (size_t) lat; i < lead.size(); ++i) out[i - (size_t) lat] = lead[i];
        if (! kural::tools::writeWav (argv[11], out, fs)) return 1;
        std::printf ("latency_samples=%d process_s=%.4f audio_s=%.4f\n", lat, sec, (double) in.mono.size() / fs);
        return 0;
    }
    return usage();
}
