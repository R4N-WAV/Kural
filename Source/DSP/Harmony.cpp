#include "Harmony.h"
#include <algorithm>

namespace kural::dsp
{
namespace
{
// masks relative to the root, bit n = n semitones above the root
constexpr std::uint16_t maskOf (std::initializer_list<int> pcs)
{
    std::uint16_t m = 0;
    for (int p : pcs) m = (std::uint16_t) (m | (1u << p));
    return m;
}
constexpr std::uint16_t kMasks[] = {
    maskOf ({ 0, 2, 4, 5, 7, 9, 11 }),        // major
    maskOf ({ 0, 2, 3, 5, 7, 8, 10 }),        // natural minor
    maskOf ({ 0, 2, 3, 5, 7, 8, 11 }),        // harmonic minor
    maskOf ({ 0, 2, 3, 5, 7, 9, 11 }),        // melodic minor (ascending form)
    maskOf ({ 0, 2, 4, 7, 9 }),               // major pentatonic
    maskOf ({ 0, 3, 5, 7, 10 }),              // minor pentatonic
    maskOf ({ 0, 3, 5, 6, 7, 10 }),           // blues (minor blues hexatonic)
    0x0FFF,                                   // chromatic
};

int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int mod (int a, int b) { const int r = a % b; return r < 0 ? r + b : r; }
}

const char* scaleName (ScaleType t)
{
    switch (t)
    {
        case ScaleType::major:           return "Major";
        case ScaleType::naturalMinor:    return "Natural minor";
        case ScaleType::harmonicMinor:   return "Harmonic minor";
        case ScaleType::melodicMinor:    return "Melodic minor";
        case ScaleType::majorPentatonic: return "Major pentatonic";
        case ScaleType::minorPentatonic: return "Minor pentatonic";
        case ScaleType::blues:           return "Blues";
        case ScaleType::chromatic:       return "Chromatic";
        case ScaleType::custom:          return "Custom";
        case ScaleType::count:           break;
    }
    return "";
}

Scale Scale::make (int root, ScaleType type, std::uint16_t customMask)
{
    Scale s;
    s.root = mod (root, 12);
    s.mask = type == ScaleType::custom ? (std::uint16_t) ((customMask & 0x0FFF) | 1u)   // the root is always in
                                       : kMasks[std::min ((int) type, (int) ScaleType::chromatic)];
    for (int i = 0; i < 12; ++i)
        if (s.mask & (1u << i)) s.offsets[(size_t) s.count++] = (std::int8_t) i;
    return s;
}

int Scale::size() const noexcept { return count; }

bool Scale::contains (int midiNote) const noexcept
{
    return (mask >> mod (midiNote - root, 12)) & 1u;
}

int Scale::noteOfDegree (int degree) const noexcept
{
    const int oct = floorDiv (degree, count);
    return root + 12 * oct + offsets[(size_t) mod (degree, count)];
}

int Scale::nearestDegree (float midi) const noexcept
{
    // degree just below or at `midi`, then compare with the one above
    const int below = (int) std::floor (midi);
    int oct = floorDiv (below - root, 12);
    const int pc = mod (below - root, 12);
    int idx = -1;
    for (int i = count - 1; i >= 0; --i)
        if (offsets[(size_t) i] <= pc) { idx = i; break; }
    if (idx < 0) { idx = count - 1; --oct; }
    const int dLow = oct * count + idx;
    const int dHigh = dLow + 1;
    const float a = midi - (float) noteOfDegree (dLow), b = (float) noteOfDegree (dHigh) - midi;
    return b < a ? dHigh : dLow;
}

int HarmonyPlanner::update (float leadMidi, float dtSec) noexcept
{
    if (current == noDegree)
    {
        current = scale.nearestDegree (leadMidi);
        centre = leadMidi;
        dwell = 0.0f; dwellDegree = noDegree;
        return current;
    }
    centre += (leadMidi - centre) * (1.0f - std::exp (-std::max (0.0f, dtSec) / centreTau));

    // arrival: settled close to another scale note
    const int nearRaw = scale.nearestDegree (leadMidi);
    if (nearRaw != current && std::abs (leadMidi - (float) scale.noteOfDegree (nearRaw)) < arrivalSemis)
    {
        if (nearRaw != dwellDegree) { dwellDegree = nearRaw; dwell = 0.0f; }
        dwell += dtSec;
        if (dwell >= arrivalSec)
        {
            current = nearRaw;
            centre = leadMidi;          // the centre jumps with the note, so it does not drag behind
            dwell = 0.0f; dwellDegree = noDegree;
            return current;
        }
    }
    else
    {
        dwell = 0.0f; dwellDegree = noDegree;
    }

    // centre: the vibrato centre has clearly moved to another note
    const int nearCentre = scale.nearestDegree (centre);
    if (nearCentre != current)
    {
        const float dCur = std::abs (centre - (float) scale.noteOfDegree (current));
        const float dNew = std::abs (centre - (float) scale.noteOfDegree (nearCentre));
        if (dCur - dNew > 2.0f * hysteresis) current = nearCentre;
    }
    return current;
}

HarmonyPlanner::Target HarmonyPlanner::target (float leadMidi, const VoiceInterval& iv) const noexcept
{
    Target t;
    const int deg = current == noDegree ? scale.nearestDegree (leadMidi) : current;
    const int leadNote = scale.noteOfDegree (deg);
    t.expression = leadMidi - (float) leadNote;
    int note = iv.mode == IntervalMode::scaleSteps ? scale.noteOfDegree (deg + iv.steps) : leadNote + iv.steps;
    note += 12 * iv.octave;
    t.noteMidi = (float) note + iv.cents * 0.01f;
    return t;
}
} // namespace kural::dsp
