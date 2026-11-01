#include "PitchCorrector.h"
#include <algorithm>
#include <cmath>

namespace kural::dsp
{
float PitchCorrector::update (float m, float dt) noexcept
{
    if (! params.on) { shift = 0.0f; return 0.0f; }
    dt = std::max (dt, 1.0e-4f);

    // vibrato centre: running mean over ~180 ms (one vibrato cycle, so the vibrato averages out
    // instead of leaking into the correction); on a real jump (more than a semitone from the centre:
    // a new note, not vibrato) the history restarts, so a leap is not corrected towards the old note
    const int len = std::clamp ((int) std::lround (0.18f / dt), 1, histMax);
    const bool jumped = ! started || std::abs (m - centre) > 1.0f;
    if (jumped)
    {
        for (int i = 0; i < len; ++i) hist[i] = m;
        histCount = len; histPos = 0; histSum = (double) m * len; started = true;
    }
    else
    {
        if (histCount < len) { hist[(histPos + histCount) % len] = m; ++histCount; histSum += m; }
        else { histSum += (double) m - hist[histPos]; hist[histPos] = m; histPos = (histPos + 1) % len; }
    }
    centre = (float) (histSum / histCount);

    // the note: nearest scale note to the centre, with 0.2 st hysteresis, so neither vibrato nor a
    // voice sitting between two notes makes it flip
    const float decide = centre;
    const int cand = params.scale.noteOfDegree (params.scale.nearestDegree (decide));
    if (current == noNote || jumped) { current = cand; held = 0.0f; noteCentre = centre; }
    else if (cand != current && std::abs (decide - (float) cand) + 0.2f < std::abs (decide - (float) current))
    {
        current = cand;
        held = 0.0f;
        noteCentre = centre;
    }
    held += dt;

    // the note's own centre, for natural correction: the mean pitch since the note began (forgetting
    // over ~0.4 s on long notes). Correcting this keeps vibrato of any rate, scoops and drift within
    // the note, and puts the note as a whole in tune (as "pitch centre" correction in note editors)
    noteCentre += (m - noteCentre) * std::max (dt / (held + dt), dt / 0.4f);

    // target: flatten → note − pitch; natural → note − the note's centre (motion kept)
    const float v = std::clamp (params.vibrato, 0.0f, 1.0f);
    float target = (float) current - m + v * (m - noteCentre);

    // flex-tune: only notes the singer is already close to are corrected
    if (params.flex > 0.0f)
    {
        const float radius = 0.55f - 0.5f * std::clamp (params.flex, 0.0f, 1.0f);   // 55 c … 5 c
        const float off = std::abs (noteCentre - (float) current);
        const float w = std::clamp ((radius - off) / (0.5f * radius), 0.0f, 1.0f);
        target *= w;
    }
    target *= std::clamp (params.amount, 0.0f, 1.0f);

    // retune speed, lengthened on held notes by humanize
    const float hum = 1.0f + 4.0f * std::clamp (params.humanize, 0.0f, 1.0f) * std::clamp ((held - 0.2f) / 0.3f, 0.0f, 1.0f);
    const float rt = std::clamp (params.retuneMs, 0.0f, 400.0f) * 0.001f * hum;
    if (rt <= 1.0e-4f) shift = target;
    else shift += (target - shift) * (1.0f - std::exp (-dt / rt));
    return shift;
}
} // namespace kural::dsp
