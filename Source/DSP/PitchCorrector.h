#pragma once
// Kural – pitch correction ("tune"): decides how far to move the lead vocal, frame by frame.
//
// Works on the detected pitch (MIDI note numbers as floats, one value per analysis frame) and returns
// the correction in semitones that the lead's pitch shifter applies. Two behaviours, mixed by
// `vibrato`:
//
//   flatten   the shift follows (note − pitch): at retune 0 every wobble is removed and the voice
//             steps between notes (the hard, robotic tuning effect)
//   natural   the shift follows (note − the note's centre): the mean pitch since the note began,
//             forgetting over ~0.4 s. The note as a whole is put in tune while the singer's vibrato,
//             scoops and drift ride on top. (Note decisions use a faster 180 ms running mean — one
//             cycle of a typical vibrato — so vibrato never flips the note.) This is how editors in the style of Logic's
//             Flex Pitch keep a voice sounding sung: they correct each note's centre, not its motion.
//
// Retune speed is the time constant with which the shift follows its target (0 = at once).
// Humanize lengthens it on held notes (×1 … ×5 after the note has been held for 0.2 s), so long
// notes keep some life while short ones still snap. Flex-tune only corrects notes the singer is
// already close to: at 0 every note is corrected, at 1 only notes within ~5 cents.
// Unvoiced frames hold the shift (the shifter passes consonants unshifted anyway).
//
// Plain C++17, no allocation, real-time safe.
#include "Harmony.h"
#include <array>

namespace kural::dsp
{
struct CorrectionParams
{
    bool on = false;
    Scale scale = Scale::make (0, ScaleType::chromatic);
    float retuneMs = 50.0f;      // 0 … 400
    float humanize = 0.0f;       // 0 … 1
    float flex = 0.0f;           // 0 … 1
    float vibrato = 1.0f;        // 0 = flatten, 1 = natural (keep the singer's motion)
    float amount = 1.0f;         // 0 … 1: how much of the correction is applied
};

class PitchCorrector
{
public:
    void setParams (const CorrectionParams& p) noexcept
    {
        if (p.scale.root != params.scale.root || p.scale.mask != params.scale.mask) current = noNote;
        params = p;
    }
    const CorrectionParams& getParams() const noexcept { return params; }
    void reset() noexcept { current = noNote; shift = 0.0f; centre = 0.0f; noteCentre = 0.0f; held = 0.0f; started = false; histCount = 0; histPos = 0; histSum = 0.0; }

    // one voiced frame: detected lead pitch and the time since the previous frame; returns the shift
    float update (float leadMidi, float dtSec) noexcept;
    // an unvoiced frame: the shift is held; after a long gap call reset()
    float hold() const noexcept { return shift; }

    float currentShift() const noexcept { return params.on ? shift : 0.0f; }
    int currentNote() const noexcept { return current; }          // MIDI note in use, or noNote
    float centrePitch() const noexcept { return centre; }

    static constexpr int noNote = -1000;

private:
    CorrectionParams params;
    int current = noNote;
    float shift = 0.0f, centre = 0.0f, noteCentre = 0.0f, held = 0.0f;
    bool started = false;
    // running mean of the pitch (frames); sized for 180 ms at the shortest supported hop (2.5 ms)
    static constexpr int histMax = 80;
    float hist[histMax] {};
    int histCount = 0, histPos = 0;
    double histSum = 0.0;
};
} // namespace kural::dsp
