#include "Params.h"
#include "DSP/Harmony.h"

juce::String kural::ids::voiceId (int v, const char* suffix)
{
    return "v" + juce::String (v + 1).paddedLeft ('0', 2) + "_" + suffix;
}

juce::StringArray kural::intervalNames()
{
    return { "7th below", "6th below", "5th below", "4th below", "3rd below", "2nd below", "Unison",
             "2nd above", "3rd above", "4th above", "5th above", "6th above", "7th above" };
}

juce::StringArray kural::keyNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::StringArray kural::scaleNames()
{
    juce::StringArray s;
    for (int t = 0; t < (int) kural::dsp::ScaleType::custom; ++t) s.add (kural::dsp::scaleName ((kural::dsp::ScaleType) t));
    return s;
}

juce::AudioProcessorValueTreeState::ParameterLayout kural::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto db = [] (float v, int)
    {
        if (v <= -59.95f) return String ("-inf dB");
        const float r = std::round (v * 10.0f) / 10.0f;
        return (r > 0.0f ? "+" : "") + String (r, 1) + " dB";
    };
    auto num = [] (const String& t) { return t.retainCharacters ("-+0123456789.").getFloatValue(); };
    auto dbParam = [&] (const String& id, const String& name, float lo, float hi, float def, int version)
    {
        NormalisableRange<float> r (lo, hi, 0.1f);
        if (lo <= -59.0f) r.setSkewForCentre (-12.0f);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name, r, def,
                                                      AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)
                                                                                     .withValueFromStringFunction (num));
    };

    // ---- v1
    p.push_back (dbParam (ids::output, "Output", -24.0f, 12.0f, 0.0f, 1));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));

    // ---- v2: harmony
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::key, 2 }, "Key", keyNames(), 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 2 }, "Scale", scaleNames(), 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::harmonyOn, 2 }, "Harmonies", true));
    p.push_back (dbParam (ids::harmonyLevel, "Harmony Level", -60.0f, 6.0f, -3.0f, 2));
    p.push_back (dbParam (ids::dryLevel, "Dry Level", -60.0f, 6.0f, 0.0f, 2));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::consonants, 2 }, "Consonants",
                                                        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f,
                                                        AudioParameterFloatAttributes().withLabel ("%")
                                                            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })
                                                            .withValueFromStringFunction (num)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::glide, 2 }, "Glide",
                                                        NormalisableRange<float> (0.0f, 250.0f, 1.0f, 0.5f), 30.0f,
                                                        AudioParameterFloatAttributes().withLabel ("ms")
                                                            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })
                                                            .withValueFromStringFunction (num)));

    // ---- v2: voices 1–4
    struct Default { bool on; int interval; int octave; float pan; };
    const Default defaults[] = { { true, 8, 0, -0.35f }, { true, 4, 0, 0.35f }, { false, 10, 0, -0.6f }, { false, 6, -1, 0.6f } };
    for (int v = 0; v < registeredVoices; ++v)
    {
        const String n = "Voice " + String (v + 1) + " ";
        const auto& d = defaults[v];
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::voiceId (v, "on"), 2 }, n + "On", d.on));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::voiceId (v, "mode"), 2 }, n + "Mode",
                                                             StringArray { "Scale", "Semitones" }, 0));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::voiceId (v, "interval"), 2 }, n + "Interval",
                                                             intervalNames(), d.interval));
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::voiceId (v, "semis"), 2 }, n + "Semitones", -12, 12, 4,
                                                          AudioParameterIntAttributes().withStringFromValueFunction ([] (int s, int)
                                                              { return (s > 0 ? "+" : "") + String (s) + " st"; })));
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::voiceId (v, "octave"), 2 }, n + "Octave", -2, 2, d.octave,
                                                          AudioParameterIntAttributes().withStringFromValueFunction ([] (int o, int)
                                                              { return o == 0 ? String ("0 oct") : (o > 0 ? "+" : "") + String (o) + " oct"; })));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::voiceId (v, "cents"), 2 }, n + "Fine",
                                                            NormalisableRange<float> (-50.0f, 50.0f, 1.0f), 0.0f,
                                                            AudioParameterFloatAttributes().withLabel ("ct")
                                                                .withStringFromValueFunction ([] (float c, int) { return (c > 0 ? "+" : "") + String (roundToInt (c)) + " ct"; })
                                                                .withValueFromStringFunction (num)));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::voiceId (v, "formant"), 2 }, n + "Formant",
                                                            NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                            AudioParameterFloatAttributes().withLabel ("st")
                                                                .withStringFromValueFunction ([] (float f, int)
                                                                    { const float r = std::round (f * 10.0f) / 10.0f; return (r > 0 ? "+" : "") + String (r, 1) + " st"; })
                                                                .withValueFromStringFunction (num)));
        p.push_back (dbParam (ids::voiceId (v, "level"), n + "Level", -60.0f, 6.0f, 0.0f, 2));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::voiceId (v, "pan"), 2 }, n + "Pan",
                                                            NormalisableRange<float> (-1.0f, 1.0f, 0.01f), d.pan,
                                                            AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float x, int)
                                                                {
                                                                    const int pc = roundToInt (std::abs (x) * 100.0f);
                                                                    return pc == 0 ? String ("C") : String (pc) + (x < 0 ? " L" : " R");
                                                                })
                                                                .withValueFromStringFunction ([] (const String& t)
                                                                {
                                                                    const float amount = t.retainCharacters ("0123456789.").getFloatValue() / 100.0f;
                                                                    return t.containsIgnoreCase ("L") ? -amount : amount;
                                                                })));
    }
    return { p.begin(), p.end() };
}
