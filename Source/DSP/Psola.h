#pragma once
// Kural – real-time TD-PSOLA pitch shifting (time-domain, pitch-synchronous overlap-add).
//
// One PitchMarker is shared by all voices: it keeps the input history and places analysis marks
// one pitch period apart, each mark aligned to the same point of its period by cross-correlating
// with the previous period. The marks also carry an instantaneous voicing measure (how alike two
// consecutive periods are), which reacts at once to consonants, faster than the pitch detector.
//
// Each PsolaVoice rebuilds the signal from two-period Hann-windowed grains taken at the analysis
// marks and laid down at its own spacing (period / ratio). Grains keep the spectral envelope of the
// voice, so formants are preserved; resampling the grains (formantRatio) moves the formants
// independently of the pitch. Unvoiced material (consonants, breaths) is passed unshifted at its own
// level, so 16 voices do not stack 16 shifted copies of every "s".
//
// Latency: 2 × the longest period handled (fs / minHz) plus a small margin; constant, reported to
// the host. Periods longer than that are handled with clamped grains.
//
// Plain C++17. prepare() allocates; everything else is real-time safe and bounded.
#include <cstdint>
#include <vector>
#include "PitchDetector.h"

namespace kural::dsp
{
struct AnalysisMark
{
    std::int64_t pos = 0;       // input sample index of the mark
    float period = 0.0f;        // distance to the previous mark (samples)
    float similarity = 0.0f;    // normalised correlation with the previous period (−1…1)
    bool voiced = false;
};

class PitchMarker
{
public:
    // minHz sets the longest period handled and therefore the latency
    void prepare (double sampleRate, float minHz = 65.0f);
    void reset();

    // feed one input sample (call before the voices produce output for this sample)
    void push (float x) noexcept;
    // latest pitch analysis (from the shared PitchDetector)
    void onPitchFrame (const PitchFrame& f) noexcept;

    std::int64_t now() const noexcept { return tNow; }                  // index of the newest sample
    int latency() const noexcept { return latencySamples; }
    float maxPeriod() const noexcept { return pMax; }
    double sampleRate() const noexcept { return fs; }

    // input at an integer / fractional index (must lie within the kept history)
    float at (std::int64_t i) const noexcept { return ring[(size_t) (i & ringMask)]; }
    float interp (double pos) const noexcept;                            // 4-point cubic

    // the latest mark at or before `t`; returns false if none is kept
    bool markAtOrBefore (double t, AnalysisMark& out) const noexcept;
    // the mark that follows the one at `pos`, if it is already placed
    bool markAfter (std::int64_t pos, AnalysisMark& out) const noexcept;

private:
    void advance() noexcept;
    float correlate (std::int64_t a, std::int64_t b, int len, int stride) const noexcept;
    void addMark (std::int64_t pos, float period, float sim, bool voiced) noexcept;

    double fs = 48000.0;
    float pMax = 738.0f, pMin = 24.0f;
    int latencySamples = 0;
    std::vector<float> ring;
    std::int64_t ringMask = 0, tNow = -1;

    std::vector<AnalysisMark> marks;
    std::int64_t markCount = 0;                                          // total marks placed
    std::int64_t marksMask = 0;

    // pitch information from the detector
    float detPeriod = 0.0f;            // samples; 0 = none
    Voicing detVoicing = Voicing::silence;
    std::int64_t detTime = -1;
    float recentPeriod = 0.0f;         // last voiced period and when it was heard (for probing at onsets)
    std::int64_t recentTime = 0;
    float unvoicedPeriod = 480.0f;     // mark spacing in unvoiced material (~10 ms)
    int searchStride = 1;
};

struct PsolaVoiceSettings
{
    float semitones = 0.0f;        // pitch shift (glided by the voice)
    float formantSemis = 0.0f;     // formant shift; 0 = preserved
    float unvoicedGain = 0.3f;     // level of consonants/breaths in this voice (0…1)
    float glideMs = 30.0f;         // glide time for changes of `semitones`
};

class PsolaVoice
{
public:
    void prepare (const PitchMarker&);
    void reset (const PitchMarker&);
    void set (const PsolaVoiceSettings& s) noexcept { settings = s; }

    // output sample for input time tOut = marker.now() − marker.latency(); call once per sample
    float next (const PitchMarker&) noexcept;
    float currentSemitones() const noexcept { return semis; }

private:
    void addGrain (std::vector<float>& dest, const PitchMarker&, const AnalysisMark&, double ts, float formant, float gain) noexcept;

    PsolaVoiceSettings settings;
    // voiced and unvoiced grains are summed separately: the voiced part is level-matched to the
    // input (overlapping grains interfere differently for every waveform and interval, so no fixed
    // gain law keeps the loudness), the unvoiced part keeps its own level
    std::vector<float> acc, accU;
    float inPow = 0.0f, outPow = 0.0f, levelGain = 1.0f, powCoef = 0.0f, gainCoef = 0.0f;
    std::int64_t accMask = 0;
    double ts = 0.0;                   // next synthesis mark (input time)
    bool started = false;
    float semis = 0.0f, voicedMix = 0.0f;
    double fs = 48000.0;
    std::int64_t lastMarkPos = -1;
};
} // namespace kural::dsp
