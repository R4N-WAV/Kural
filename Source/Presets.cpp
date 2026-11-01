#include "Presets.h"
#include "Params.h"
#include <deque>

namespace kural
{
namespace
{
// interval choice index = scale steps + 6
constexpr float third_up = 8, third_dn = 4, fourth_up = 9, fifth_up = 10, fifth_dn = 2, sixth_up = 11, unison = 6;
const juce::String v (int voice, const char* suffix) { return ids::voiceId (voice, suffix); }
}

const std::vector<PresetManager::Factory>& PresetManager::factory()
{
    // Every preset is a set of real parameter values. Categories follow how a vocal effects plugin is
    // browsed: tuning, harmony, choir, doubling, transforming the voice, creative effects.
    static const std::vector<Factory> f = [] {
        std::vector<Factory> p;
        auto voices = [] (std::initializer_list<std::tuple<int, float, float, float, float, float>> vs)
        {
            // voice, interval index, octave, formant, level dB, pan; voices not listed are switched off
            std::vector<std::pair<const char*, float>> out;
            static std::deque<std::string> keep;                      // ids must outlive the table (deque: stable addresses)
            std::array<bool, 4> on {};
            for (auto [voice, iv, oct, fmt, lvl, pan] : vs)
            {
                on[(size_t) voice] = true;
                for (auto [suf, val] : std::initializer_list<std::pair<const char*, float>> {
                         { "on", 1.0f }, { "mode", 0.0f }, { "interval", iv }, { "octave", oct }, { "formant", fmt }, { "level", lvl }, { "pan", pan } })
                {
                    keep.push_back (v (voice, suf).toStdString());
                    out.push_back ({ keep.back().c_str(), val });
                }
            }
            for (int i = 0; i < 4; ++i)
                if (! on[(size_t) i]) { keep.push_back (v (i, "on").toStdString()); out.push_back ({ keep.back().c_str(), 0.0f }); }
            return out;
        };
        auto join = [] (std::vector<std::pair<const char*, float>> a, std::vector<std::pair<const char*, float>> b)
        { a.insert (a.end(), b.begin(), b.end()); return a; };
        const std::vector<std::pair<const char*, float>> noHarm { { ids::harmonyOn, 0.0f } };

        p.push_back ({ "Default", "Init", {} });

        // ---- natural tuning (notes put in tune, the singer's motion kept)
        p.push_back ({ "Glass Shallows", "Natural Tune", join (noHarm, { { ids::tuneOn, 1 }, { ids::retune, 25 }, { ids::tuneVibrato, 100 },
                       { ids::humanize, 30 }, { ids::eqLowCut, 80 }, { ids::eqPresence, 1.5f }, { ids::reverbMix, 14 } }) });
        p.push_back ({ "Studio Polish", "Natural Tune", join (noHarm, { { ids::tuneOn, 1 }, { ids::retune, 40 }, { ids::tuneVibrato, 100 },
                       { ids::humanize, 50 }, { ids::flexTune, 30 }, { ids::compAmount, 40 }, { ids::eqLowCut, 90 }, { ids::eqPresence, 2 }, { ids::eqAir, 2 } }) });
        p.push_back ({ "Gentle Drift", "Natural Tune", join (noHarm, { { ids::tuneOn, 1 }, { ids::retune, 80 }, { ids::tuneVibrato, 100 },
                       { ids::humanize, 60 }, { ids::flexTune, 60 } }) });

        // ---- hard tuning (the stepped, robotic sound)
        p.push_back ({ "Hard Tide", "Hard Tune", join (noHarm, { { ids::tuneOn, 1 }, { ids::retune, 0 }, { ids::tuneVibrato, 0 }, { ids::compAmount, 50 },
                       { ids::eqLowCut, 100 }, { ids::eqPresence, 3 }, { ids::eqAir, 3 }, { ids::delayMix, 12 }, { ids::delaySync, 2 }, { ids::reverbMix, 12 } }) });
        p.push_back ({ "Robot Current", "Hard Tune", join (noHarm, { { ids::tuneOn, 1 }, { ids::retune, 0 }, { ids::tuneVibrato, 0 }, { ids::mutateOn, 1 },
                       { ids::mutateRatio, 2 }, { ids::mutateMix, 35 }, { ids::compAmount, 40 } }) });
        p.push_back ({ "Chrome Reef", "Hard Tune", join (noHarm, { { ids::tuneOn, 1 }, { ids::retune, 5 }, { ids::tuneVibrato, 0 }, { ids::tubeDrive, 40 },
                       { ids::compAmount, 55 }, { ids::delayMix, 20 }, { ids::delaySync, 3 }, { ids::delayFeedback, 40 } }) });
        p.push_back ({ "Snap Back", "Hard Tune", join (noHarm, { { ids::tuneOn, 1 }, { ids::retune, 10 }, { ids::tuneVibrato, 20 }, { ids::eqPresence, 2 } }) });

        // ---- harmony
        p.push_back ({ "Thirds Up", "Harmony", join ({ { ids::tuneOn, 1 }, { ids::retune, 30 } }, voices ({ { 0, third_up, 0, 0, -2, -0.3f } })) });
        p.push_back ({ "Two-Part Tide", "Harmony", join ({ { ids::tuneOn, 1 }, { ids::retune, 30 }, { ids::reverbMix, 15 } },
                       voices ({ { 0, third_up, 0, 0, 0, -0.4f }, { 1, third_dn, 0, 0, 0, 0.4f } })) });
        p.push_back ({ "Open Fifths", "Harmony", join ({ { ids::naturalness, 40 }, { ids::reverbMix, 18 } },
                       voices ({ { 0, fifth_up, 0, 0, -2, -0.5f }, { 1, unison, -1, 1.5f, -4, 0.5f } })) });
        p.push_back ({ "Gospel Stack", "Harmony", join ({ { ids::tuneOn, 1 }, { ids::retune, 35 }, { ids::naturalness, 50 }, { ids::reverbMix, 25 } },
                       voices ({ { 0, third_up, 0, 0, -1, -0.5f }, { 1, fifth_up, 0, 0, -3, 0.5f }, { 2, third_dn, 0, 0, -3, -0.25f }, { 3, unison, -1, 2, -6, 0.25f } })) });
        p.push_back ({ "Sixths Sweet", "Harmony", voices ({ { 0, sixth_up, 0, 0, -2, -0.4f }, { 1, third_up, 0, 0, -3, 0.4f } }) });
        p.push_back ({ "Octave Bloom", "Harmony", join ({ { ids::naturalness, 35 } },
                       voices ({ { 0, unison, 1, -3, -6, -0.35f }, { 1, unison, -1, 2.5f, -4, 0.35f } })) });
        p.push_back ({ "Fourth Line", "Harmony", voices ({ { 0, fourth_up, 0, 0, -2, 0.3f } }) });

        // ---- choir
        p.push_back ({ "Harbour Choir", "Choir", join ({ { ids::naturalness, 80 }, { ids::reverbMix, 35 }, { ids::reverbDecay, 60 }, { ids::width, 140 }, { ids::harmonyLevel, -2 } },
                       voices ({ { 0, third_up, 0, 0, 0, -0.6f }, { 1, fifth_up, 0, 0, -2, 0.6f }, { 2, third_dn, 0, 0, -2, -0.3f }, { 3, unison, -1, 2, -5, 0.3f } })) });
        p.push_back ({ "Cathedral Tide", "Choir", join ({ { ids::naturalness, 70 }, { ids::reverbMix, 50 }, { ids::reverbDecay, 85 }, { ids::reverbSize, 85 }, { ids::width, 160 } },
                       voices ({ { 0, fifth_up, 0, 0, -2, -0.7f }, { 1, unison, -1, 2, -3, 0.7f }, { 2, unison, 1, -3, -8, 0.0f } })) });
        p.push_back ({ "Small Ensemble", "Choir", join ({ { ids::naturalness, 60 }, { ids::reverbMix, 20 } },
                       voices ({ { 0, third_up, 0, 0.5f, -2, -0.45f }, { 1, third_dn, 0, -0.5f, -2, 0.45f }, { 2, unison, 0, 0.7f, -6, 0.2f } })) });

        // ---- doubling
        p.push_back ({ "Wide Double", "Doubler", join (noHarm, { { ids::duetOn, 1 }, { ids::duetSpread, 60 }, { ids::duetLevel, -3 }, { ids::width, 150 } }) });
        p.push_back ({ "Tight Double", "Doubler", join (noHarm, { { ids::duetOn, 1 }, { ids::duetSpread, 20 }, { ids::duetLevel, -6 } }) });
        p.push_back ({ "Shoulder to Shoulder", "Doubler", join ({ { ids::duetOn, 1 }, { ids::duetSpread, 45 }, { ids::naturalness, 60 } },
                       voices ({ { 0, third_up, 0, 0, -4, -0.5f } })) });

        // ---- transforming the voice (pitch and throat)
        p.push_back ({ "Deep Sea Giant", "Transform", join (noHarm, { { ids::leadPitch, -7 }, { ids::leadFormant, -5 }, { ids::reverbMix, 20 }, { ids::eqLowCut, 40 } }) });
        p.push_back ({ "Little Shell", "Transform", join (noHarm, { { ids::leadPitch, 7 }, { ids::leadFormant, 5 } }) });
        p.push_back ({ "Sunken Throat", "Transform", join (noHarm, { { ids::leadFormant, -4 } }) });
        p.push_back ({ "Bright Siren", "Transform", join (noHarm, { { ids::leadPitch, 2 }, { ids::leadFormant, 4 }, { ids::eqAir, 3 } }) });
        p.push_back ({ "Octave Under", "Transform", join ({ { ids::leadPitch, -12 } }, voices ({ { 0, unison, 1, -2, -8, 0.0f } })) });

        // ---- creative effects
        p.push_back ({ "Ring of Coral", "Creative", join (noHarm, { { ids::mutateOn, 1 }, { ids::mutateRatio, 3 }, { ids::mutateMix, 60 },
                       { ids::delayMix, 25 }, { ids::delaySync, 2 }, { ids::delayPingPong, 1 } }) });
        p.push_back ({ "Lighthouse Echo", "Creative", join (noHarm, { { ids::delayMix, 35 }, { ids::delaySync, 5 }, { ids::delayFeedback, 45 }, { ids::reverbMix, 25 } }) });
        p.push_back ({ "Underwater Radio", "Creative", join (noHarm, { { ids::eqLowCut, 400 }, { ids::eqPresence, 9 }, { ids::eqAir, -9 }, { ids::tubeDrive, 70 }, { ids::compAmount, 70 } }) });
        p.push_back ({ "Tube Swell", "Creative", join (noHarm, { { ids::tubeDrive, 60 }, { ids::compAmount, 50 }, { ids::reverbMix, 15 } }) });
        p.push_back ({ "Ghost Wave", "Creative", join ({ { ids::reverbMix, 70 }, { ids::reverbDecay, 90 }, { ids::reverbDamping, 30 } },
                       voices ({ { 0, unison, 1, -2, -9, 0.0f } })) });
        return p;
    }();
    return f;
}

namespace { juce::File& testFolder() { static juce::File f; return f; } }
void PresetManager::setUserFolderForTests (const juce::File& f) { testFolder() = f; }

juce::File PresetManager::userFolder()
{
    if (testFolder() != juce::File()) return testFolder();
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("R4N").getChildFile ("Kural").getChildFile ("Presets");
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    refresh();
    slots[0] = snapshot();
}

void PresetManager::refresh()
{
    presets.clear();
    for (const auto& f : factory()) presets.add ({ f.name, f.category, true, {} });
    const auto dir = userFolder();
    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*.kuralpreset");
        files.sort();
        for (const auto& f : files)
            if (auto xml = juce::XmlDocument::parse (f))
                if (xml->hasTagName ("KuralPreset"))
                    presets.add ({ xml->getStringAttribute ("name", f.getFileNameWithoutExtension()), "User", false, f });
    }
}

juce::ValueTree PresetManager::snapshot() const { return apvts.copyState(); }

void PresetManager::applyDefaults()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (r->getParameterID() != ids::bypass)
            {
                r->beginChangeGesture();
                r->setValueNotifyingHost (r->getDefaultValue());
                r->endChangeGesture();
            }
}

void PresetManager::apply (const juce::ValueTree& state)
{
    // every parameter is set: from the state when present, else its default
    for (auto* p : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (r->getParameterID() == ids::bypass) continue;
            float v01 = r->getDefaultValue();
            const auto child = state.getChildWithProperty ("id", r->getParameterID());
            if (child.isValid() && child.hasProperty ("value"))
                v01 = r->convertTo0to1 ((float) child.getProperty ("value"));
            r->beginChangeGesture();
            r->setValueNotifyingHost (v01);
            r->endChangeGesture();
        }
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= presets.size()) return false;
    const auto& info = presets.getReference (index);
    if (info.factory)
    {
        applyDefaults();
        for (const auto& f : factory())
            if (info.name == f.name)
                for (auto [id, val] : f.values)
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
                    {
                        r->beginChangeGesture();
                        r->setValueNotifyingHost (r->convertTo0to1 (val));
                        r->endChangeGesture();
                    }
    }
    else
    {
        auto xml = juce::XmlDocument::parse (info.file);
        if (xml == nullptr || ! xml->hasTagName ("KuralPreset")) return false;
        auto* stateXml = xml->getChildByName (apvts.state.getType());
        if (stateXml == nullptr) return false;
        apply (juce::ValueTree::fromXml (*stateXml));
    }
    current = index;
    loadedState = snapshot();
    return true;
}

bool PresetManager::next (int dir)
{
    if (presets.isEmpty()) return false;
    const int n = presets.size();
    return load (((current < 0 ? 0 : current + dir) % n + n) % n);
}

juce::String PresetManager::currentName() const
{
    return current >= 0 && current < presets.size() ? presets[current].name : juce::String ("Default");
}

bool PresetManager::isModified() const
{
    if (! loadedState.isValid()) return false;
    for (auto* p : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (r->getParameterID() == ids::bypass) continue;
            const auto child = loadedState.getChildWithProperty ("id", r->getParameterID());
            if (child.isValid() && std::abs ((float) child.getProperty ("value") - r->convertFrom0to1 (r->getValue())) > 1e-3f) return true;
        }
    return false;
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    const auto name = rawName.trim().isEmpty() ? juce::String ("My preset") : rawName.trim();
    const auto dir = userFolder();
    if (! dir.createDirectory()) return false;
    juce::XmlElement root ("KuralPreset");
    root.setAttribute ("formatVersion", formatVersion);
    root.setAttribute ("name", name);
    root.setAttribute ("pluginVersion", KURAL_VERSION_STRING);
    if (auto st = snapshot().createXml()) root.addChildElement (st.release());
    const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".kuralpreset");
    if (! root.writeTo (file)) return false;
    refresh();
    for (int i = 0; i < presets.size(); ++i)
        if (! presets[i].factory && presets[i].file == file) { current = i; break; }
    loadedState = snapshot();
    return true;
}

void PresetManager::selectSlot (int s)
{
    s = juce::jlimit (0, 1, s);
    if (s == activeSlot) return;
    slots[(size_t) activeSlot] = snapshot();
    if (slots[(size_t) s].isValid()) apply (slots[(size_t) s]);
    else slots[(size_t) s] = snapshot();
    activeSlot = s;
}

void PresetManager::copyAtoB()
{
    slots[(size_t) activeSlot] = snapshot();
    slots[(size_t) (1 - activeSlot)] = slots[(size_t) activeSlot].createCopy();
}
} // namespace kural
