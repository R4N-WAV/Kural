#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Kural presets.
//
// A preset is the full parameter state (every parameter, so loading one never leaves a setting from
// the previous sound behind) plus a name and a category. Factory presets are defined in code
// (Presets.cpp) as differences from the defaults and cannot be overwritten; user presets are XML
// files (".kuralpreset") in the user's documents folder, with a format version so later versions
// can read them. Everything here runs on the message thread; the audio thread only sees parameter
// values change, as with host automation.
//
// A/B: two complete snapshots of the state; switching copies the other one in.
namespace kural
{
struct PresetInfo
{
    juce::String name, category;
    bool factory = true;
    juce::File file;           // user presets only
};

class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    static constexpr int formatVersion = 1;
    static juce::File userFolder();
    static void setUserFolderForTests (const juce::File& f);

    const juce::Array<PresetInfo>& list() const noexcept { return presets; }
    void refresh();                                   // re-reads the user folder
    bool load (int index);
    bool next (int direction);                        // ±1, wraps
    int currentIndex() const noexcept { return current; }
    juce::String currentName() const;
    bool saveUser (const juce::String& name);         // writes or replaces a user preset
    bool isModified() const;                          // the state differs from the loaded preset

    // A/B compare
    void selectSlot (int slot);                       // 0 = A, 1 = B
    int slot() const noexcept { return activeSlot; }
    void copyAtoB();

    // factory definitions: id → value in the parameter's own units
    struct Factory { const char* name; const char* category; std::vector<std::pair<const char*, float>> values; };
    static const std::vector<Factory>& factory();

private:
    void applyDefaults();
    void apply (const juce::ValueTree& state);
    juce::ValueTree snapshot() const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::Array<PresetInfo> presets;
    int current = -1;
    juce::ValueTree loadedState;
    std::array<juce::ValueTree, 2> slots;
    int activeSlot = 0;
};
} // namespace kural
