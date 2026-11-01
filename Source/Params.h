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
// lead vocal: pitch correction, transpose, formant                                          (v3)
inline constexpr auto tuneOn = "tuneOn";           // pitch correction on/off (uses key/scale)
inline constexpr auto retune = "retune";           // retune speed, ms (0 = instant, hard tune)
inline constexpr auto tuneVibrato = "tuneVibrato"; // % of the singer's vibrato/motion kept (100 = natural)
inline constexpr auto humanize = "humanize";       // %, slower correction on held notes
inline constexpr auto flexTune = "flexTune";       // %, only correct notes the singer is close to
inline constexpr auto leadPitch = "leadPitch";     // lead transpose, semitones
inline constexpr auto leadFormant = "leadFormant"; // lead formant shift, semitones (0 = preserved)
// naturalness, duet, effects                                                                (v4)
inline constexpr auto naturalness = "naturalness"; // %, per-voice drift and timing of the harmonies
inline constexpr auto duetOn = "duetOn";
inline constexpr auto duetLevel = "duetLevel";     // dB
inline constexpr auto duetSpread = "duetSpread";   // %
inline constexpr auto eqLowCut = "eqLowCut";       // Hz
inline constexpr auto eqPresence = "eqPresence";   // dB
inline constexpr auto eqAir = "eqAir";             // dB
inline constexpr auto compAmount = "compAmount";   // %
inline constexpr auto tubeDrive = "tubeDrive";     // %
inline constexpr auto mutateOn = "mutateOn";
inline constexpr auto mutateRatio = "mutateRatio"; // choice: carrier as a multiple of the lead's pitch
inline constexpr auto mutateMix = "mutateMix";     // %
inline constexpr auto delayMix = "delayMix";       // %
inline constexpr auto delayTime = "delayTime";     // ms (when not synced)
inline constexpr auto delaySync = "delaySync";     // choice: Free, note values
inline constexpr auto delayFeedback = "delayFeedback"; // %
inline constexpr auto delayPingPong = "delayPingPong";
inline constexpr auto reverbMix = "reverbMix";     // %
inline constexpr auto reverbSize = "reverbSize";   // %
inline constexpr auto reverbDecay = "reverbDecay"; // %
inline constexpr auto reverbDamping = "reverbDamping"; // %
inline constexpr auto width = "width";             // %, stereo width of the output

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
juce::StringArray mutateRatioNames();
float mutateRatioValue (int index);
juce::StringArray delaySyncNames();
double delaySyncBeats (int index);      // 0 = free
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
