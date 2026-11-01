#pragma once
// Kural – the harmony engine: one vocal in, up to 16 harmony voices out (stereo), no dry signal.
//
//   input (mono) ─► PitchDetector ─► HarmonyPlanner (key/scale, one decision for all voices)
//                └► PitchMarker (shared analysis marks) ─► PsolaVoice × N ─► pan ─► L/R
//
// The analysis runs once and is shared; each voice only overlap-adds grains. Output is delayed by
// latency() samples (constant); the caller delays the dry signal by the same amount.
// Plain C++17: prepare() allocates, process() is real-time safe and bounded.
#include <array>
#include "Harmony.h"
#include "PitchDetector.h"
#include "Psola.h"

namespace kural::dsp
{
struct HarmonyVoiceParams
{
    bool on = false;
    VoiceInterval interval;          // scale steps or semitones, octave, cents
    float formantSemis = 0.0f;
    float gain = 1.0f;               // linear
    float pan = 0.0f;                // −1 … 1
};

struct HarmonyEngineParams
{
    Scale scale = Scale::make (0, ScaleType::major);
    float unvoicedGain = 0.3f;       // consonants and breaths in the harmonies (0 … 1)
    float glideMs = 30.0f;
    float harmonyGain = 1.0f;        // linear, all voices
};

class HarmonyEngine
{
public:
    static constexpr int maxVoices = 16;

    void prepare (double sampleRate, float minHz = 65.0f);
    void reset();
    int latency() const noexcept { return marker.latency(); }

    // parameters for the next block (copied; safe to call every block)
    void setParams (const HarmonyEngineParams& p) noexcept { params = p; planner.setScale (p.scale); }
    void setVoice (int index, const HarmonyVoiceParams& v) noexcept { if (index >= 0 && index < maxVoices) voiceParams[(size_t) index] = v; }

    // mono in, harmonies added to outL/outR (which may be the caller's buffers, already holding dry)
    template <typename OnFrame>
    void process (const float* in, float* outL, float* outR, int n, OnFrame&& onFrame) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            detector.process (&in[i], 1, [&] (const PitchFrame& f) { handleFrame (f); onFrame (f, *this); });
            marker.push (in[i]);
            float l = 0.0f, r = 0.0f;
            for (int v = 0; v < maxVoices; ++v)
            {
                auto& st = state[(size_t) v];
                const auto& vp = voiceParams[(size_t) v];
                // a voice fades in/out over ~10 ms when switched; silent voices cost nothing
                const float target = vp.on ? 1.0f : 0.0f;
                if (! vp.on && st.fade <= 0.0f) { st.running = false; continue; }
                if (! st.running) { voices[(size_t) v].reset (marker); st.running = true; }
                st.fade += (target - st.fade) * fadeCoef;
                if (! vp.on && st.fade < 1.0e-4f) st.fade = 0.0f;
                const float y = voices[(size_t) v].next (marker) * st.fade * params.harmonyGain;
                st.gainL += (st.panL * vp.gain - st.gainL) * smoothCoef;
                st.gainR += (st.panR * vp.gain - st.gainR) * smoothCoef;
                l += y * st.gainL;
                r += y * st.gainR;
            }
            outL[i] += l;
            if (outR != nullptr) outR[i] += r;
            else outL[i] += r;
        }
    }

    // state for displays (message thread may read copies of these through the caller)
    bool leadVoiced() const noexcept { return lastVoiced; }
    float leadMidi() const noexcept { return lastLead; }
    float voiceShift (int v) const noexcept { return state[(size_t) v].shift; }
    bool voiceActive (int v) const noexcept { return voiceParams[(size_t) v].on; }

private:
    void handleFrame (const PitchFrame& f) noexcept;
    void updateVoiceSettings() noexcept;

    PitchDetector detector;
    PitchMarker marker;
    HarmonyPlanner planner;
    std::array<PsolaVoice, maxVoices> voices;
    std::array<HarmonyVoiceParams, maxVoices> voiceParams {};
    HarmonyEngineParams params;

    struct VoiceState
    {
        bool running = false;
        float fade = 0.0f, shift = 0.0f;
        float panL = 0.7071f, panR = 0.7071f, gainL = 0.0f, gainR = 0.0f;
    };
    std::array<VoiceState, maxVoices> state {};
    double fs = 48000.0;
    float fadeCoef = 0.0f, smoothCoef = 0.0f, hopSec = 0.005f;
    float lastLead = 0.0f;
    bool lastVoiced = false;
    int silentFrames = 0;
};
} // namespace kural::dsp
