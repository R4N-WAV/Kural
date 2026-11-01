#pragma once
// Kural – monophonic vocal pitch detector.
//
// Method: the input (mono) is decimated to an analysis rate of roughly 20–24 kHz, then a YIN
// estimator (de Cheveigné & Kawahara 2002: difference function → cumulative-mean-normalised
// difference → absolute threshold → parabolic interpolation) runs every hop. On top of plain YIN:
//   * confidence = 1 − d'(τ*) and a four-state voicing decision (silence / unvoiced / uncertain / voiced)
//   * octave-continuity check: when the chosen lag is ~½ or ~2× the previous voiced lag and the lag
//     matching the previous pitch is almost as good, the continuous one wins (removes octave flips)
//   * frame time stamps that account for the analysis window and the decimation filter delay
//
// One detector serves all harmony voices (analysis is shared, never repeated per voice).
// Plain C++17. prepare() allocates; process() is real-time safe and bounded
// (at most one analysis per hop, O(W · τmax) per analysis).
#include <cmath>
#include <cstdint>
#include <vector>
#include "Decimator.h"

namespace kural::dsp
{
enum class Voicing : std::uint8_t { silence = 0, unvoiced = 1, uncertain = 2, voiced = 3 };

struct PitchFrame
{
    std::int64_t centreSample = 0;   // host-rate sample index (since reset) the estimate refers to
    float f0Hz = 0.0f;               // 0 when no usable estimate
    float confidence = 0.0f;         // 0..1 (1 − aperiodicity)
    float rmsDb = -120.0f;           // level of the analysed window, dBFS
    Voicing voicing = Voicing::silence;
};

struct PitchDetectorConfig
{
    float minHz = 60.0f;          // lowest pitch searched (B1 ≈ 62 Hz covers low male voices)
    float maxHz = 1500.0f;        // highest pitch searched (above soprano high C, F#6 ≈ 1480 Hz)
    float threshold = 0.15f;      // YIN absolute threshold on d'
    float uncertainLimit = 0.35f; // d' between threshold and this = "uncertain"
    float hopMs = 5.0f;           // analysis interval
    float silenceDb = -55.0f;     // window RMS below this = silence
};

class PitchDetector
{
public:
    using Config = PitchDetectorConfig;

    void prepare (double hostSampleRate, const Config& c = Config {});
    void reset();

    // feed mono host-rate samples; onFrame (const PitchFrame&) is called for every finished analysis
    template <typename OnFrame>
    void process (const float* x, int n, OnFrame&& onFrame) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            ++hostCount;
            float y;
            if (! dec.push (x[i], y)) continue;
            ring[(size_t) wpos] = y;
            ring[(size_t) (wpos + ringLen)] = y;
            wpos = wpos + 1 == ringLen ? 0 : wpos + 1;
            if (filled < frameLen) ++filled;
            if (++sinceHop >= hopLen && filled >= frameLen)
            {
                sinceHop = 0;
                onFrame (analyse());
            }
        }
    }

    double analysisRate() const noexcept { return fa; }
    int decimationFactor() const noexcept { return dec.getFactor(); }
    // how far (host samples) a frame's centre lies behind the newest sample when it is reported
    double reportDelaySamples() const noexcept { return centreBackHost; }
    const Config& config() const noexcept { return cfg; }

private:
    PitchFrame analyse() noexcept;

    Config cfg;
    Decimator dec;
    double fs = 48000.0, fa = 24000.0, centreBackHost = 0.0;
    int tauMin = 2, tauMax = 400, win = 400, frameLen = 800, hopLen = 120, ringLen = 1024;
    int wpos = 0, filled = 0, sinceHop = 0;
    std::int64_t hostCount = 0;
    float prevTau = 0.0f;            // lag of the last voiced frame (0 = none)
    int framesSinceVoiced = 1 << 20;
    std::vector<float> ring, frame, diff, cmnd;
};
} // namespace kural::dsp
