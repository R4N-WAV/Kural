#pragma once
// Kural – phase-vocoder pitch shifting for harmonies (Laroche & Dolson, "New phase-vocoder techniques
// for real-time pitch shifting, chorusing, harmonizing and other exotic audio modifications",
// JAES 47(11), 1999), with spectral-envelope (cepstral) formant preservation.
//
// One PvAnalysis is shared by all voices: STFT (Hann, N ≈ 43 ms, hop N/4), magnitudes, phases, the
// spectral envelope and the spectral peaks. Each PvVoice moves every peak and the bins around it to
// ratio × its frequency, rotating their phases together (identity phase locking) by an angle that
// accumulates per peak, rescales the magnitudes by the envelope so formants stay put, and resynthesises
// with its own inverse FFT.
//
// Latency: N samples. Plain C++17; prepare() allocates, processing is real-time safe.
#include <cstdint>
#include <vector>
#include "Fft.h"
#include "PitchDetector.h"

namespace kural::dsp
{
class PvAnalysis
{
public:
    void prepare (double sampleRate);
    // push one sample; returns true when a new analysis frame is ready (every hop samples)
    bool push (float x) noexcept;
    void onPitchFrame (const PitchFrame& f) noexcept;

    int fftSize() const noexcept { return N; }
    int hop() const noexcept { return H; }
    int latency() const noexcept { return N; }
    double sampleRate() const noexcept { return fs; }

    // current frame
    std::vector<Fft::cpx> spec;            // N/2 + 1 bins
    std::vector<float> mag, logEnv, instBin;
    std::vector<int> peaks, region;       // peak bins; owning peak index per bin
    bool voiced = false;

private:
    void analyse() noexcept;
    void envelope() noexcept;

    double fs = 48000.0;
    int N = 2048, H = 512, filled = 0, sinceHop = 0, pos = 0;
    Fft fft;
    std::vector<float> ring, win, prevPhase;
    std::vector<Fft::cpx> work, cep;
    float detF0 = 0.0f;
    Voicing detVoicing = Voicing::silence;
};

struct PvVoiceSettings
{
    float semitones = 0.0f;
    float formantSemis = 0.0f;      // 0 = preserved
    float unvoicedGain = 0.3f;
};

class PvVoice
{
public:
    void prepare (const PvAnalysis&);
    void set (const PvVoiceSettings& s) noexcept { settings = s; }
    // call after PvAnalysis::push returned true
    void synthesise (const PvAnalysis&) noexcept;
    // one output sample per input sample (delayed by the analysis latency)
    float next() noexcept;

private:
    PvVoiceSettings settings;
    Fft fft;
    int N = 2048, H = 512, readPos = 0;
    std::vector<Fft::cpx> out;
    std::vector<float> ola, win, prevPeakBin, prevTheta, theta;
    int prevPeakCount = 0;
    float norm = 1.0f, gainMix = 1.0f;
};
} // namespace kural::dsp
