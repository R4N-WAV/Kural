#include "HarmonyEngine.h"
#include <algorithm>
#include <cmath>

namespace kural::dsp
{
void HarmonyEngine::prepare (double sampleRate, float minHz)
{
    fs = sampleRate > 1000.0 ? sampleRate : 48000.0;
    detector.prepare (fs);
    marker.prepare (fs, minHz);
    for (auto& v : voices) v.prepare (marker);
    fadeCoef = 1.0f - (float) std::exp (-1.0 / (0.010 * fs));
    smoothCoef = 1.0f - (float) std::exp (-1.0 / (0.020 * fs));
    hopSec = detector.config().hopMs * 0.001f;
    reset();
}

void HarmonyEngine::reset()
{
    detector.reset();
    marker.reset();
    planner.reset();
    for (auto& v : voices) v.reset (marker);
    for (auto& s : state) s = VoiceState {};
    lastLead = 0.0f;
    lastVoiced = false;
    silentFrames = 0;
    updateVoiceSettings();
}

void HarmonyEngine::handleFrame (const PitchFrame& f) noexcept
{
    marker.onPitchFrame (f);
    lastVoiced = f.voicing == Voicing::voiced && f.f0Hz > 0.0f;
    if (lastVoiced)
    {
        lastLead = hzToMidi (f.f0Hz);
        planner.update (lastLead, hopSec);
        silentFrames = 0;
    }
    else if (++silentFrames * hopSec > 0.3f)
    {
        planner.reset();             // a new phrase: its first note is taken at once
    }
    updateVoiceSettings();
}

void HarmonyEngine::updateVoiceSettings() noexcept
{
    for (int v = 0; v < maxVoices; ++v)
    {
        auto& st = state[(size_t) v];
        const auto& vp = voiceParams[(size_t) v];
        // the shift only changes when the planner changes note: between changes the voice follows
        // the lead's own vibrato and slides at a constant interval
        if (planner.hasNote())
        {
            const auto t = planner.target (lastLead, vp.interval);
            st.shift = t.noteMidi - (float) planner.getScale().noteOfDegree (planner.currentDegree());
        }
        else if (vp.interval.mode == IntervalMode::semitones)
        {
            st.shift = (float) (vp.interval.steps + 12 * vp.interval.octave) + vp.interval.cents * 0.01f;
        }
        const float p = std::clamp (vp.pan, -1.0f, 1.0f);
        st.panL = std::cos ((p + 1.0f) * 0.25f * 3.14159265f);     // constant power
        st.panR = std::sin ((p + 1.0f) * 0.25f * 3.14159265f);
        voices[(size_t) v].set ({ st.shift, vp.formantSemis, params.unvoicedGain, params.glideMs });
    }
}
} // namespace kural::dsp
