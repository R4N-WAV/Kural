#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Kural visual system. Same grammar as the R4N family (Anbu): a one-word Tamil logo in Catamaran
// with "by R4N" beneath, Barlow Semi Condensed for every label, thin accent hairlines, a dark
// analysis screen. Anbu's maroon plate and gold "zari" thread become deep water and a pearl
// thread here (pearl: the Gulf of Mannar pearl beds off the Tamil coast).
namespace kural::ui
{
struct Theme
{
    juce::Colour abyss, deep, shelf, surface;     // background depth layers, darkest → lightest
    juce::Colour screen, gridMinor, gridMajor;    // analysis screen
    juce::Colour pearl, pearlDim, label, dim;     // text and the pearl hairline
    juce::Colour voice, voiceGlow, seaGlass;      // the live vocal line, its glow, active accents
    juce::Colour warn;
};
const Theme& theme();

// string literals in this code base are UTF-8; juce::String (const char*) would read them as Latin-1
inline juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

// Unicode text of the logo: குரல் (ku-ra-l, "voice")
const juce::String& tamilName();

struct Fonts
{
    Fonts();
    juce::Font logo (int weight, float height) const;   // Catamaran 600/800 (Tamil + Latin)
    juce::Font ui (int weight, float height) const;     // Barlow Semi Condensed 500/600/700
    juce::Typeface::Ptr c600, c800, b500, b600, b700;
};

// the pearl hairline used to frame panels
void drawPearlFrame (juce::Graphics&, juce::Rectangle<float>, float cornerRadius, float alpha = 1.0f);
}
