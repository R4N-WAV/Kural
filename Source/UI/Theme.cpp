#include "Theme.h"
#include "KuralBinaryData.h"

namespace
{
juce::Colour hex (juce::uint32 rgb) { return juce::Colour (0xff000000u | rgb); }

juce::Typeface::Ptr loadTypeface (const char* name)
{
    int size = 0;
    if (auto* data = KuralBinaryData::getNamedResource (name, size))
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    return {};
}
}

const kural::ui::Theme& kural::ui::theme()
{
    static const Theme t = []
    {
        Theme o;
        // depth: from the sea floor (near-black navy) up to the lit water just under the surface
        o.abyss = hex (0x02080d);   o.deep = hex (0x051520);   o.shelf = hex (0x0a2230);  o.surface = hex (0x123646);
        o.screen = hex (0x030c13);  o.gridMinor = hex (0x0c1d28);   o.gridMajor = hex (0x16303e);
        // pearl: warm off-white with a faint green cast, the role Anbu's gold plays
        o.pearl = hex (0xe6ece4);   o.pearlDim = hex (0x8fa3a3);    o.label = hex (0xc9d6d2);  o.dim = hex (0x5f7a82);
        // the voice: muted cyan core, sea-glass accents
        o.voice = hex (0xbfeee9);   o.voiceGlow = hex (0x3fb3b8);   o.seaGlass = hex (0x7cc8b4);
        o.warn = hex (0xd98a7a);
        o.chassis = hex (0xd6e1e4);  o.panel = hex (0xeef3f3);  o.panelLine = hex (0xa9bcc2);  o.cardLine = hex (0xc3d1d5);
        o.ink = hex (0x102532);      o.muted = hex (0x4d6370);  o.well = hex (0x0f2a3a);       o.wellTop = hex (0x16405a);
        o.wellText = hex (0x8fb0be); o.accent = hex (0x1d4a66); o.led = hex (0x6fb4cf);
        o.knobFace = hex (0xd9e9ee); o.knobTrack = hex (0xb8c8cd); o.aqua = hex (0xa9dcea);
        return o;
    }();
    return t;
}

const juce::String& kural::ui::tamilName()
{
    // க U+0B95, ு U+0BC1, ர U+0BB0, ல U+0BB2, ் U+0BCD
    static const juce::String s = juce::String (juce::CharPointer_UTF8 ("\xe0\xae\x95\xe0\xaf\x81\xe0\xae\xb0\xe0\xae\xb2\xe0\xaf\x8d"));
    return s;
}

kural::ui::Fonts::Fonts()
{
    c600 = loadTypeface ("catamaran600_ttf");
    c800 = loadTypeface ("catamaran800_ttf");
    b500 = loadTypeface ("barlow500_ttf");
    b600 = loadTypeface ("barlow600_ttf");
    b700 = loadTypeface ("barlow700_ttf");
}

juce::Font kural::ui::Fonts::logo (int weight, float height) const
{
    auto tf = weight >= 800 ? c800 : c600;
    if (tf == nullptr) return juce::Font (juce::FontOptions().withHeight (height));
    return juce::Font (juce::FontOptions (tf).withHeight (height));
}

juce::Font kural::ui::Fonts::ui (int weight, float height) const
{
    auto tf = weight >= 700 ? b700 : weight >= 600 ? b600 : b500;
    if (tf == nullptr) return juce::Font (juce::FontOptions().withHeight (height));
    return juce::Font (juce::FontOptions (tf).withHeight (height));
}

void kural::ui::drawPearlFrame (juce::Graphics& g, juce::Rectangle<float> r, float radius, float alpha)
{
    const auto& T = theme();
    g.setColour (T.pearl.withAlpha (0.16f * alpha));
    g.drawRoundedRectangle (r, radius, 1.0f);
    // a brighter thread along the top edge, as light catching it from above
    juce::ColourGradient top (T.pearl.withAlpha (0.0f), r.getX(), r.getY(), T.pearl.withAlpha (0.0f), r.getRight(), r.getY(), false);
    top.addColour (0.5, T.pearl.withAlpha (0.42f * alpha));
    g.setGradientFill (top);
    g.fillRect (r.getX() + radius, r.getY() - 0.5f, r.getWidth() - 2.0f * radius, 1.0f);
}
