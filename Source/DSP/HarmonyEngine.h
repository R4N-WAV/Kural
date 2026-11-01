#pragma once
// Kural – the vocal engine: one vocal in; a processed lead (tuned, transposed, formant-shifted) and
// up to 16 harmony voices out (stereo). The dry signal is the caller's.
//
//   input (mono) ─► PitchDetector ─► PitchCorrector (lead: tune) + HarmonyPlanner (harmonies)
//                └► PitchMarker (shared analysis marks) ─► lead PsolaVoice ─► lead (mono)
//                                                       └► PsolaVoice × N ─► pan ─► L/R
//
// Harmonies follow the corrected lead: the lead's correction is added to every harmony's shift.
// The analysis runs once and is shared; each voice only overlap-adds grains. Output is delayed by
// latency() samples (constant); the caller delays the dry signal by the same amount.
// Plain C++17: prepare() allocates, process() is real-time safe and bounded.
#include <array>
#include <cmath>
#include <cstdint>
#include "Harmony.h"
#include "PitchCorrector.h"
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
    // naturalness (0 … 1): every harmony voice gets its own slow pitch drift (up to ±9 cents) and its
    // own small timing offset (5 … 25 ms), from deterministic per-voice random walks, so a stack sounds
    // like several singers rather than copies of one. 0 = exact copies.
    float naturalness = 0.3f;
};

// Duet: one more copy of the (corrected) lead, loosely timed and slightly detuned, beside it
struct DuetParams
{
    bool on = false;
    float gain = 0.7f;               // linear
    float spread = 0.5f;             // 0 … 1: timing, detune and stereo distance from the lead
};

struct LeadParams
{
    CorrectionParams tune;           // pitch correction
    float transposeSemis = 0.0f;     // pitch shift of the lead (−24 … 24)
    float formantSemis = 0.0f;       // formant shift of the lead ("throat"); 0 = preserved
    bool active() const noexcept { return tune.on || std::abs (transposeSemis) > 1.0e-3f || std::abs (formantSemis) > 1.0e-3f; }
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
    void setLead (const LeadParams& p) noexcept { lead = p; corrector.setParams (p.tune); }
    void setVoice (int index, const HarmonyVoiceParams& v) noexcept { if (index >= 0 && index < maxVoices) voiceParams[(size_t) index] = v; }
    void setDuet (const DuetParams& d) noexcept { duet = d; }

    // mono in, harmonies added to outL/outR (which may be the caller's buffers, already holding dry);
    // leadOut (optional, mono) receives the processed lead, or zeros while the lead is not active
    template <typename OnFrame>
    void process (const float* in, float* outL, float* outR, float* leadOut, int n, OnFrame&& onFrame) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            detector.process (&in[i], 1, [&] (const PitchFrame& f) { handleFrame (f); onFrame (f, *this); });
            marker.push (in[i]);
            if (leadOut != nullptr)
            {
                // the lead voice runs while active and for a short tail after, so switching is smooth
                if (lead.active()) { if (! leadRunning) { leadVoice.reset (marker); leadRunning = true; } leadIdle = 0; }
                else if (leadRunning && ++leadIdle > marker.latency() * 2) leadRunning = false;
                leadOut[i] = leadRunning ? leadVoice.next (marker) : 0.0f;
            }
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
                const float y = humanDelay (st, voices[(size_t) v].next (marker)) * st.fade * params.harmonyGain;
                st.gainL += (st.panL * vp.gain - st.gainL) * smoothCoef;
                st.gainR += (st.panR * vp.gain - st.gainR) * smoothCoef;
                l += y * st.gainL;
                r += y * st.gainR;
            }
            // duet: a second singer on the lead's line
            if (duet.on || duetState.fade > 0.0f)
            {
                auto& st = duetState;
                if (! st.running) { duetVoice.reset (marker); st.running = true; }
                st.fade += ((duet.on ? 1.0f : 0.0f) - st.fade) * fadeCoef;
                if (! duet.on && st.fade < 1.0e-4f) { st.fade = 0.0f; st.running = false; }
                const float y = humanDelay (st, duetVoice.next (marker)) * st.fade * duet.gain;
                st.gainL += (st.panL - st.gainL) * smoothCoef;
                st.gainR += (st.panR - st.gainR) * smoothCoef;
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
    float leadShift() const noexcept { return leadSemis; }      // correction + transpose applied to the lead
    float leadCorrection() const noexcept { return corrector.currentShift(); }
    bool leadActive() const noexcept { return lead.active(); }
    float voiceShift (int v) const noexcept { return state[(size_t) v].shift; }
    bool voiceActive (int v) const noexcept { return voiceParams[(size_t) v].on; }

private:
    void handleFrame (const PitchFrame& f) noexcept;
    void updateVoiceSettings() noexcept;

    PitchDetector detector;
    PitchMarker marker;
    HarmonyPlanner planner;
    PitchCorrector corrector;
    PsolaVoice leadVoice;
    LeadParams lead;
    bool leadRunning = false;
    int leadIdle = 0;
    float leadSemis = 0.0f;
    std::array<PsolaVoice, maxVoices> voices;
    std::array<HarmonyVoiceParams, maxVoices> voiceParams {};
    HarmonyEngineParams params;

    struct VoiceState
    {
        bool running = false;
        float fade = 0.0f, shift = 0.0f;
        float panL = 0.7071f, panR = 0.7071f, gainL = 0.0f, gainR = 0.0f;
        // naturalness: random walks for drift (cents) and timing (ms), and the voice's own delay line
        std::uint32_t rng = 1;
        float driftTarget = 0.0f, drift = 0.0f, timeTarget = 0.0f, timeMs = 0.0f, walkLeft = 0.0f;
        std::array<float, 8192> delay {};          // 25 ms at up to 192 kHz
        int w = 0;
        float delaySmp = 0.0f;
    };
    std::array<VoiceState, maxVoices> state {};
    VoiceState duetState;
    PsolaVoice duetVoice;
    DuetParams duet;

    void walk (VoiceState&, float dt, float amount, float baseMs, float rangeMs, float cents) noexcept;
    float humanDelay (VoiceState& st, float x) noexcept
    {
        // fractional read from the voice's own delay line; the delay glides (a few samples per frame)
        st.delay[(size_t) st.w] = x;
        const float d = std::min (st.delaySmp, (float) st.delay.size() - 4.0f);
        const float pos = (float) st.w - d;
        const int i0 = (int) std::floor (pos);
        const float t = pos - (float) i0;
        const int m = (int) st.delay.size() - 1;
        const float y = st.delay[(size_t) (i0 & m)] * (1.0f - t) + st.delay[(size_t) ((i0 + 1) & m)] * t;
        st.w = (st.w + 1) & m;
        st.delaySmp += (st.timeMs * 0.001f * (float) fs - st.delaySmp) * delayGlide;
        return y;
    }
    float delayGlide = 0.0f;
    double fs = 48000.0;
    float fadeCoef = 0.0f, smoothCoef = 0.0f, hopSec = 0.005f;
    float lastLead = 0.0f;
    bool lastVoiced = false;
    int silentFrames = 0;
};
} // namespace kural::dsp
