#pragma once
// Kural – musical harmony planning: key/scale model and scale-aware intervals.
//
// The planner turns the detected pitch of the lead vocal into a target pitch for each harmony voice.
// Intervals are counted in scale degrees, not semitones: a "third above" in C major is E over C but
// F over D, and the right note in A harmonic minor. The singer's expression (vibrato, scoops, the
// distance from the note) is carried over unchanged, so a harmony follows the lead instead of being
// quantised. The planner works on pitches (MIDI note numbers as floats); the pitch shifter turns
// "target − detected" into a ratio.
//
// Plain C++17, no allocation, real-time safe.
#include <array>
#include <cmath>
#include <cstdint>

namespace kural::dsp
{
enum class ScaleType : std::uint8_t
{
    major = 0, naturalMinor, harmonicMinor, melodicMinor, majorPentatonic, minorPentatonic, blues, chromatic, custom,
    count
};

const char* scaleName (ScaleType);

// A scale: root pitch class (0 = C … 11 = B) and a 12-bit mask of the pitch classes it contains,
// counted from the root (bit 0 = root).
struct Scale
{
    int root = 0;
    std::uint16_t mask = 0;

    static Scale make (int root, ScaleType type, std::uint16_t customMask = 0x0AB5);
    int size() const noexcept;                         // notes per octave
    bool contains (int midiNote) const noexcept;

    // degree index ↔ note: degree 0 is the root in octave −1 (MIDI 0…11), counting upwards through
    // every scale note; negative degrees are below MIDI 0
    int noteOfDegree (int degree) const noexcept;
    int nearestDegree (float midi) const noexcept;     // nearest scale note (ties go down)

private:
    std::array<std::int8_t, 12> offsets {};            // pitch-class offsets from the root, ascending
    int count = 0;
};

// How a voice's interval is defined
enum class IntervalMode : std::uint8_t
{
    scaleSteps = 0,   // diatonic: steps along the scale (+2 = a third above in a 7-note scale)
    semitones         // fixed: a constant number of semitones (parallel harmony)
};

struct VoiceInterval
{
    IntervalMode mode = IntervalMode::scaleSteps;
    int steps = 2;            // scale steps (scaleSteps mode) or semitones (semitones mode)
    int octave = 0;           // added octaves
    float cents = 0.0f;       // fine offset
};

// Planner state for the lead pitch: decides which scale note the singer is on. Two rules, so that
// real note changes are followed quickly while vibrato is not mistaken for a note change:
//   arrival  the lead has settled within `arrivalSemis` of a scale note for `arrivalSec`
//   centre   a smoothed lead (vibrato centre, ~100 ms) crosses the midpoint between two scale notes by
//            more than the hysteresis
// The first note after a reset is taken at once.
class HarmonyPlanner
{
public:
    void setScale (const Scale& s) noexcept { if (s.root != scale.root || s.mask != scale.mask) { scale = s; reset(); } }
    const Scale& getScale() const noexcept { return scale; }
    void setHysteresis (float semis) noexcept { hysteresis = semis; }
    void reset() noexcept { current = noDegree; dwell = 0.0f; dwellDegree = noDegree; }

    // feed the detected pitch of the lead (voiced frames only), dt = time since the previous call;
    // returns the scale degree in use. Call reset() after a long gap (a new phrase).
    int update (float leadMidi, float dtSec = 0.005f) noexcept;
    int currentDegree() const noexcept { return current; }
    bool hasNote() const noexcept { return current != noDegree; }

    // result for one voice: target = note + expression. `note` changes in steps (glide it);
    // `expression` = lead − its scale note, continuous (apply it as is).
    struct Target
    {
        float noteMidi = 0.0f;
        float expression = 0.0f;
        float midi() const noexcept { return noteMidi + expression; }
        // the shift relative to the lead in semitones: what the pitch shifter applies
        float shiftFrom (float leadMidi) const noexcept { return midi() - leadMidi; }
    };
    Target target (float leadMidi, const VoiceInterval&) const noexcept;

    static constexpr int noDegree = -1000000;

private:
    Scale scale = Scale::make (0, ScaleType::major);
    int current = noDegree;
    float hysteresis = 0.3f;        // semitones beyond the midpoint before the centre rule changes note
    float centre = 0.0f;            // smoothed lead
    float dwell = 0.0f;             // time the lead has been settled on dwellDegree
    int dwellDegree = noDegree;
    static constexpr float centreTau = 0.10f, arrivalSemis = 0.25f, arrivalSec = 0.025f;
};

inline float hzToMidi (float hz) noexcept { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
inline float midiToHz (float m) noexcept { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }
} // namespace kural::dsp
