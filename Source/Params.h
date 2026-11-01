#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Kural parameters.
//
// Rule: a parameter is registered only once the engine behind it exists (no control that does
// nothing). IDs are permanent: once released, an ID is never renamed or reused, so DAW sessions,
// automation and presets keep working across versions. New parameters get a higher version hint.
// The planned ID scheme for later phases is listed in ARCHITECTURE.md.
namespace kural::ids
{
inline constexpr auto output = "output";     // output gain, dB          (v1)
inline constexpr auto bypass = "bypass";     // host-linked bypass        (v1)
}

namespace kural
{
inline constexpr int stateVersion = 1;       // bumped when the saved-state layout changes
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
