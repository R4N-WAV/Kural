#include "Params.h"

juce::AudioProcessorValueTreeState::ParameterLayout kural::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto db = [] (float v, int)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return (r > 0.0f ? "+" : "") + String (r, 1) + " dB";
    };
    auto num = [] (const String& t) { return t.retainCharacters ("-+0123456789.").getFloatValue(); };

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, 1 }, "Output",
                                                        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                        AudioParameterFloatAttributes().withLabel ("dB")
                                                                                       .withStringFromValueFunction (db)
                                                                                       .withValueFromStringFunction (num)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    return { p.begin(), p.end() };
}
