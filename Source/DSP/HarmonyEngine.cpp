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
    leadVoice.prepare (marker);
    duetVoice.prepare (marker);
    for (auto& st : state) st.delay.assign (8192, 0.0f);
    duetState.delay.assign (8192, 0.0f);
    delayGlide = 1.0f - (float) std::exp (-1.0 / (0.25 * fs));       // timing changes glide over ~250 ms
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
    // keep each voice's delay line (allocated in prepare), reset everything else
    auto fresh = [] (VoiceState& st, std::uint32_t seed)
    {
        auto buf = std::move (st.delay);
        st = VoiceState {};
        st.delay = std::move (buf);
        std::fill (st.delay.begin(), st.delay.end(), 0.0f);
        st.rng = seed;
    };
    for (int v = 0; v < maxVoices; ++v) fresh (state[(size_t) v], 0x9E3779B9u * (std::uint32_t) (v + 1));   // fixed seeds: renders repeat
    fresh (duetState, 0xA5A5F00Du);
    duetVoice.reset (marker);
    corrector.reset();
    leadVoice.reset (marker);
    leadRunning = false;
    leadIdle = 0;
    leadSemis = 0.0f;
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
        corrector.update (lastLead, hopSec);
        silentFrames = 0;
    }
    else if (++silentFrames * hopSec > 0.3f)
    {
        planner.reset();             // a new phrase: its first note is taken at once
        corrector.reset();
    }
    updateVoiceSettings();
}

void HarmonyEngine::walk (VoiceState& st, float dt, float amount, float baseMs, float rangeMs, float cents) noexcept
{
    // every 0.4 … 1.2 s a new random target; drift and timing ease towards it
    st.walkLeft -= dt;
    if (st.walkLeft <= 0.0f)
    {
        auto rnd = [&st]
        {
            st.rng ^= st.rng << 13; st.rng ^= st.rng >> 17; st.rng ^= st.rng << 5;
            return (float) (st.rng & 0xFFFFFF) / (float) 0xFFFFFF;            // 0 … 1
        };
        st.driftTarget = 2.0f * rnd() - 1.0f;
        st.timeTarget = rnd();
        st.walkLeft = 0.4f + 0.8f * rnd();
    }
    const float k = 1.0f - std::exp (-dt / 0.5f);
    st.drift += (st.driftTarget * cents * amount - st.drift) * k;
    st.timeMs += ((baseMs + rangeMs * st.timeTarget) * amount - st.timeMs) * k;
}

void HarmonyEngine::updateVoiceSettings() noexcept
{
    // the lead: correction + transpose; consonants pass at full level (it is the main vocal), the
    // shift changes every frame so it only needs a short glide against 5 ms steps
    const float corr = corrector.currentShift();
    leadSemis = corr + lead.transposeSemis;
    leadVoice.set ({ leadSemis, lead.formantSemis, 1.0f, 4.0f });

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
        // harmonies follow the corrected lead; naturalness adds each voice's own drift and timing
        walk (st, hopSec, std::clamp (params.naturalness, 0.0f, 1.0f), 5.0f, 20.0f, 9.0f);
        voices[(size_t) v].set ({ st.shift + corr + 0.01f * st.drift, vp.formantSemis, params.unvoicedGain, params.glideMs });
    }

    // duet: the lead's line, detuned a little and loosely timed (always some, spread scales it)
    const float sp = std::clamp (duet.spread, 0.0f, 1.0f);
    walk (duetState, hopSec, 0.3f + 0.7f * sp, 12.0f, 25.0f, 12.0f);
    const float dp = 0.25f + 0.6f * sp;                                     // to the right of the lead
    duetState.panL = std::cos ((dp + 1.0f) * 0.25f * 3.14159265f);
    duetState.panR = std::sin ((dp + 1.0f) * 0.25f * 3.14159265f);
    duetVoice.set ({ leadSemis + 0.01f * duetState.drift + 0.06f * sp, lead.formantSemis + 0.3f * sp, 0.6f, 20.0f });
}
} // namespace kural::dsp
