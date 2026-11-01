#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Kural parameters.
//
// Rule: a parameter is registered only once the engine behind it exists (no control that does
// nothing). IDs are permanent: once released, an ID is never renamed or reused, so DAW sessions,
// automation and presets keep working across versions. New parameters get a higher version hint.
// Voice parameters are numbered v01 … v16; this version registers voices 1–4 (the engine supports 16,
// the rest are registered with the 16-voice interface in Phase 4).
namespace kural::ids
{
inline constexpr auto output = "output";           // output gain, dB                      (v1)
inline constexpr auto bypass = "bypass";           // host-linked bypass                    (v1)
inline constexpr auto key = "key";                 // C … B                                  (v2)
inline constexpr auto scale = "scale";             // scale type                             (v2)
inline constexpr auto harmonyOn = "harmonyOn";     // all harmonies on/off (dry stays)       (v2)
inline constexpr auto harmonyLevel = "harmonyLevel"; // dB                                   (v2)
inline constexpr auto dryLevel = "dryLevel";       // dB                                     (v2)
inline constexpr auto consonants = "consonants";   // level of consonants/breaths in harmonies, % (v2)
inline constexpr auto glide = "glide";             // ms                                     (v2)

// per voice: "v01_on" etc.
inline constexpr const char* voiceSuffixes[] = { "on", "mode", "interval", "semis", "octave", "cents", "formant", "level", "pan" };
juce::String voiceId (int voiceIndex, const char* suffix);   // voiceIndex 0-based → "v01_level"
}

namespace kural
{
inline constexpr int stateVersion = 1;       // bumped when the saved-state layout changes
inline constexpr int registeredVoices = 4;   // voices with host parameters in this version
inline constexpr int intervalChoices = 13;   // "7th below" … "Unison" … "7th above": steps = index − 6

juce::StringArray intervalNames();
juce::StringArray keyNames();
juce::StringArray scaleNames();
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
