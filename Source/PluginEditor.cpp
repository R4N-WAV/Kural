#include "PluginEditor.h"
#include <cmath>

using namespace kural;

namespace
{
// layout, design units
const juce::Rectangle<int> screenR { 24, 92, 992, 330 };
constexpr int harmonyY = 436, stripY = 478, stripW = 236, stripH = 216, stripGap = 16;
const juce::Rectangle<float> meterR { 92.0f, 750.0f, 250.0f, 8.0f };
juce::Rectangle<int> stripR (int v) { return { 24 + v * (stripW + stripGap), stripY, stripW, stripH }; }
}

// =================================================================================== editor
KuralEditor::KuralEditor (KuralProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    const auto& T = ui::theme();
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    content.setSize (baseW, baseH);
    content.addAndMakeVisible (pitchView);
    pitchView.setBounds (screenR);

    // ---- key, scale, harmonies on/off
    setupCombo (keyBox, keyAtt, ids::key, { 152, harmonyY, 72, 28 }, "Key of the song: harmonies follow its scale");
    setupCombo (scaleBox, scaleAtt, ids::scale, { 232, harmonyY, 178, 28 },
                "Scale: intervals are counted along it, so a third is major or minor as the key requires");
    harmonyButton.setClickingTogglesState (true);
    harmonyButton.setTooltip ("All harmony voices on or off (the lead vocal stays)");
    content.addAndMakeVisible (harmonyButton);
    harmonyButton.setBounds (898, harmonyY, 118, 28);
    harmonyAtt = std::make_unique<ButtonAtt> (proc.apvts, ids::harmonyOn, harmonyButton);

    // ---- voice strips
    for (int v = 0; v < registeredVoices; ++v)
    {
        auto& s = strips[(size_t) v];
        const auto r = stripR (v);
        const auto col = ui::voiceColour (v);
        s.on.setButtonText ("VOICE " + juce::String (v + 1));
        s.on.setClickingTogglesState (true);
        s.on.setColour (juce::TextButton::buttonOnColourId, col);
        s.on.setTooltip ("Voice " + juce::String (v + 1) + " on or off");
        content.addAndMakeVisible (s.on);
        s.on.setBounds (r.getX() + 12, r.getY() + 12, 100, 26);
        s.onAtt = std::make_unique<ButtonAtt> (proc.apvts, ids::voiceId (v, "on"), s.on);

        setupCombo (s.mode, s.modeAtt, ids::voiceId (v, "mode"), { r.getRight() - 116, r.getY() + 12, 104, 26 },
                    "Scale: the interval follows the key (a third is major or minor as needed). Semitones: a fixed distance");
        setupCombo (s.interval, s.intervalAtt, ids::voiceId (v, "interval"), { r.getX() + 12, r.getY() + 48, 140, 26 },
                    "Interval from the lead, counted along the scale");
        setupCombo (s.semis, s.semisAtt, ids::voiceId (v, "semis"), { r.getX() + 12, r.getY() + 48, 140, 26 },
                    "Fixed distance from the lead in semitones (ignores the key)");
        setupCombo (s.octave, s.octaveAtt, ids::voiceId (v, "octave"), { r.getRight() - 82, r.getY() + 48, 70, 26 },
                    "Octaves added to the interval");

        const int ky = r.getY() + 112;
        setupKnob (s.level, ids::voiceId (v, "level"), "LEVEL", { r.getX() + 10, ky, 54, 74 }, "Level of this voice (double-click: 0 dB)", col, 0.0);
        setupKnob (s.pan, ids::voiceId (v, "pan"), "PAN", { r.getX() + 64, ky, 54, 74 }, "Position in the stereo field (double-click: centre)", col, 0.0);
        setupKnob (s.formant, ids::voiceId (v, "formant"), "FORMANT", { r.getX() + 118, ky, 54, 74 },
                   "Moves the vocal tract resonances: up = smaller, brighter voice; down = bigger, darker (double-click: natural)", col, 0.0);
        setupKnob (s.fine, ids::voiceId (v, "cents"), "FINE", { r.getX() + 172, ky, 54, 74 },
                   "Fine detune in cents (double-click: 0)", col, 0.0);
        s.modeValue = proc.apvts.getRawParameterValue (ids::voiceId (v, "mode"));
        s.onValue = proc.apvts.getRawParameterValue (ids::voiceId (v, "on"));
    }

    // ---- bottom row
    const int by = 724;
    setupKnob (consonants, ids::consonants, "CONSONANTS", { 410, by, 72, 84 },
               "How much of the consonants and breaths the harmony voices carry (they are never pitch-shifted)", T.seaGlass, 30.0);
    setupKnob (glide, ids::glide, "GLIDE", { 490, by, 72, 84 }, "How quickly a voice moves to a new harmony note", T.seaGlass, 30.0);
    setupKnob (harmonyLevel, ids::harmonyLevel, "HARMONY", { 570, by, 72, 84 }, "Level of all harmony voices together (double-click: -3 dB)", T.seaGlass, -3.0);
    setupKnob (dryLevel, ids::dryLevel, "DRY", { 650, by, 72, 84 }, "Level of the original vocal (double-click: 0 dB)", T.seaGlass, 0.0);
    setupKnob (output, ids::output, "OUTPUT", { 772, by - 4, 88, 92 }, "Output level (double-click: 0 dB)", T.seaGlass, 0.0);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Pass the input through untouched (delayed by the same latency, crossfaded, no click)");
    content.addAndMakeVisible (bypassButton);
    bypassButton.setBounds (898, 748, 118, 34);
    bypassAtt = std::make_unique<ButtonAtt> (proc.apvts, ids::bypass, bypassButton);

    updateVoiceStrips();
    setResizable (true, true);
    setResizeLimits (baseW * 3 / 5, baseH * 3 / 5, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

KuralEditor::~KuralEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KuralEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& label, juce::Rectangle<int> bounds,
                             const juce::String& tip, juce::Colour accent, double resetValue)
{
    k.label = label;
    k.accent = accent;
    k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k.s.setTooltip (tip);
    k.s.setColour (juce::Slider::thumbColourId, accent);
    k.s.getProperties().set ("label", label);
    content.addAndMakeVisible (k.s);
    k.s.setBounds (bounds);
    k.att = std::make_unique<SliderAtt> (proc.apvts, paramId, k.s);
    k.s.setDoubleClickReturnValue (true, resetValue);
}

void KuralEditor::setupCombo (juce::ComboBox& box, std::unique_ptr<ComboAtt>& att, const juce::String& paramId,
                              juce::Rectangle<int> bounds, const juce::String& tip)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (paramId)))
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (prm))
            box.addItemList (choice->choices, 1);
        else
        {
            const int steps = prm->getNumSteps();
            for (int i = 0; i < steps; ++i)
                box.addItem (prm->getText ((float) i / (float) juce::jmax (1, steps - 1), 32), i + 1);
        }
    }
    box.setTooltip (tip);
    box.setJustificationType (juce::Justification::centredLeft);
    content.addAndMakeVisible (box);
    box.setBounds (bounds);
    att = std::make_unique<ComboAtt> (proc.apvts, paramId, box);
}

void KuralEditor::updateVoiceStrips()
{
    for (auto& s : strips)
    {
        const bool semitones = s.modeValue != nullptr && s.modeValue->load() > 0.5f;
        s.interval.setVisible (! semitones);
        s.semis.setVisible (semitones);
        const bool on = s.onValue != nullptr && s.onValue->load() > 0.5f;
        const float a = on ? 1.0f : 0.45f;
        for (juce::Component* c : { (juce::Component*) &s.mode, (juce::Component*) &s.interval, (juce::Component*) &s.semis,
                                    (juce::Component*) &s.octave, (juce::Component*) &s.level.s, (juce::Component*) &s.pan.s,
                                    (juce::Component*) &s.formant.s, (juce::Component*) &s.fine.s })
            if (std::abs (c->getAlpha() - a) > 0.01f) c->setAlpha (a);
    }
}

void KuralEditor::paint (juce::Graphics& g) { g.fillAll (ui::theme().abyss); }

void KuralEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseW));
}

void KuralEditor::timerCallback()
{
    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    pumpForSnapshot (juce::jlimit (0.0, 0.1, t - lastTick));
    lastTick = t;
}

void KuralEditor::pumpForSnapshot (double dt)
{
    int n;
    while ((n = proc.popAnalysis (popBuf.data(), (int) popBuf.size())) > 0)
        pitchView.addFrames (popBuf.data(), n);
    pitchView.advance (proc.analysisClockSec(), dt);
    updateVoiceStrips();

    // input meter: instant attack, ~20 dB/s release
    const float in = proc.inputPeakDb.load (std::memory_order_relaxed);
    inMeterDb = in > inMeterDb ? in : juce::jmax (in, inMeterDb - 20.0f * (float) dt);
    content.repaint (meterR.expanded (4.0f, 10.0f).getSmallestIntegerContainer().withTrimmedLeft (-80).withTrimmedRight (-70));
}

// ================================================================================== content
void KuralEditor::Content::renderBackground (float scale)
{
    const auto& T = ui::theme();
    const int W = juce::roundToInt (baseW * scale), H = juce::roundToInt (baseH * scale);
    bgCache = juce::Image (juce::Image::RGB, juce::jmax (1, W), juce::jmax (1, H), true);
    bgScale = scale;
    juce::Graphics g (bgCache);
    g.addTransform (juce::AffineTransform::scale (scale));

    // depth: light from the surface above, fading into the abyss
    juce::ColourGradient depth (T.surface.withMultipliedBrightness (0.8f), 0.0f, -80.0f, T.abyss, 0.0f, (float) baseH, false);
    depth.addColour (0.3, T.deep);
    g.setGradientFill (depth);
    g.fillAll();

    // faint caustics: interference of two slow wave fields, thresholded into bright filaments.
    // Static decoration of the surface only (it never moves and is not data).
    {
        const int cw = baseW / 4, ch = baseH / 4;
        juce::Image caus (juce::Image::ARGB, cw, ch, true);
        juce::Image::BitmapData bd (caus, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
            {
                const float fx = (float) x * 0.11f, fy = (float) y * 0.13f;
                const float a = std::sin (fx + 1.7f * std::sin (fy * 0.7f)) + std::sin (fy * 1.1f + 1.3f * std::sin (fx * 0.6f + 0.8f));
                const float b = std::sin (fx * 0.8f - fy * 0.5f + 2.0f * std::sin (fx * 0.31f));
                float v = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (a + 0.6f * b) * 0.9f);
                v = v * v * v;
                const float fade = juce::jlimit (0.0f, 1.0f, 1.0f - (float) y / (float) ch * 1.6f);   // only near the surface
                bd.setPixelColour (x, y, T.voice.withAlpha (0.055f * v * fade));
            }
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (caus, juce::Rectangle<float> (0.0f, 0.0f, (float) baseW, (float) baseH));
    }

    // ---- header: the R4N lockup (Tamil name in Catamaran, Latin name, "by R4N")
    const auto& f = ed.fonts;
    const auto& tamil = ui::tamilName();
    const auto logoFont = f.logo (800, 46.0f);
    const float wTamil = juce::GlyphArrangement::getStringWidth (logoFont, tamil);
    g.setColour (T.pearl);
    g.setFont (logoFont);
    g.drawText (tamil, juce::Rectangle<float> (24.0f, 14.0f, wTamil + 8.0f, 64.0f), juce::Justification::centredLeft);
    const float lx = 24.0f + wTamil + 22.0f;
    g.setColour (T.pearl.withAlpha (0.25f));
    g.fillRect (lx - 11.0f, 30.0f, 1.0f, 34.0f);
    {
        juce::GlyphArrangement ga;
        float x = lx;
        for (auto ch : juce::String ("KURAL"))
        {
            const juce::String s = juce::String::charToString (ch);
            ga.addLineOfText (f.ui (600, 17.0f), s, x, 46.0f);
            x += juce::GlyphArrangement::getStringWidth (f.ui (600, 17.0f), s) + 3.5f;
        }
        g.setColour (T.label);
        ga.draw (g);
    }
    g.setColour (T.pearlDim);
    g.setFont (f.logo (600, 15.0f));
    g.drawText ("by R4N", juce::Rectangle<float> (lx, 50.0f, 120.0f, 20.0f), juce::Justification::centredLeft);

    // honest build status, right-aligned in the header
    g.setFont (f.ui (500, 13.0f));
    g.setColour (T.dim);
    g.drawText (ui::u8 ("v" KURAL_VERSION_STRING "  ·  development build  ·  " ) + juce::String (registeredVoices)
                    + ui::u8 (" harmony voices  ·  PSOLA"),
                juce::Rectangle<float> (520.0f, 34.0f, 496.0f, 20.0f), juce::Justification::centredRight);

    // ---- screen frame
    ui::drawPearlFrame (g, screenR.toFloat().expanded (0.5f), 6.0f);

    // ---- harmony section
    g.setColour (T.label);
    g.setFont (f.ui (700, 15.0f));
    juce::GlyphArrangement title;
    {
        float x = 24.0f;
        for (auto ch : juce::String ("HARMONY"))
        {
            const juce::String s = juce::String::charToString (ch);
            title.addLineOfText (f.ui (700, 15.0f), s, x, (float) harmonyY + 19.0f);
            x += juce::GlyphArrangement::getStringWidth (f.ui (700, 15.0f), s) + 2.5f;
        }
        title.draw (g);
    }
    g.setColour (T.dim);
    g.setFont (f.ui (600, 12.0f));
    g.drawText ("KEY", juce::Rectangle<float> (118.0f, (float) harmonyY, 30.0f, 28.0f), juce::Justification::centredRight);
    for (int v = 0; v < registeredVoices; ++v)
    {
        const auto r = stripR (v).toFloat();
        g.setColour (T.deep.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (T.pearl.withAlpha (0.10f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour (ui::voiceColour (v).withAlpha (0.8f));
        g.fillRoundedRectangle (r.getX() + 12.0f, r.getY() - 1.0f, r.getWidth() - 24.0f, 2.0f, 1.0f);   // the voice's colour thread
        g.setColour (T.dim);
        g.setFont (f.ui (600, 11.0f));
        const char* labels[] = { "LEVEL", "PAN", "FORMANT", "FINE" };
        for (int k = 0; k < 4; ++k)
            g.drawText (labels[k], juce::Rectangle<float> (r.getX() + 10.0f + 54.0f * (float) k, r.getY() + 94.0f, 54.0f, 16.0f),
                        juce::Justification::centred);
    }

    // ---- bottom row
    g.setColour (T.label.withAlpha (0.9f));
    g.setFont (f.ui (600, 12.0f));
    g.drawText ("INPUT", juce::Rectangle<float> (24.0f, meterR.getY() - 6.0f, 60.0f, 20.0f), juce::Justification::centredLeft);
    g.setColour (T.dim);
    g.setFont (f.ui (600, 11.0f));
    const std::pair<const char*, juce::Rectangle<float>> knobLabels[] = {
        { "CONSONANTS", { 410.0f, 708.0f, 72.0f, 16.0f } }, { "GLIDE", { 490.0f, 708.0f, 72.0f, 16.0f } },
        { "HARMONY", { 570.0f, 708.0f, 72.0f, 16.0f } },    { "DRY", { 650.0f, 708.0f, 72.0f, 16.0f } },
        { "OUTPUT", { 772.0f, 704.0f, 88.0f, 16.0f } } };
    for (const auto& [text, rr] : knobLabels) g.drawText (text, rr, juce::Justification::centred);
    g.setColour (T.screen);
    g.fillRoundedRectangle (meterR, 4.0f);
    g.setColour (T.gridMajor);
    g.drawRoundedRectangle (meterR, 4.0f, 1.0f);
}

void KuralEditor::Content::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (bgCache.isNull() || std::abs (bgScale - scale) > 0.01f)
        renderBackground (scale);
    g.drawImage (bgCache, getLocalBounds().toFloat());
}

void KuralEditor::Content::paintOverChildren (juce::Graphics& g)
{
    const auto& T = ui::theme();
    const auto& f = ed.fonts;

    // input level (peak, dBFS, scale −60 … 0)
    const float db = ed.inMeterDb;
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    if (frac > 0.0f)
    {
        auto r = meterR.reduced (1.5f);
        juce::ColourGradient grad (T.voiceGlow.withAlpha (0.7f), r.getX(), 0, T.voice, r.getRight(), 0, false);
        if (db > -1.0f) grad = juce::ColourGradient (T.warn, r.getX(), 0, T.warn, r.getRight(), 0, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * frac), 2.5f);
    }
    g.setColour (T.label.withAlpha (0.85f));
    g.setFont (f.ui (500, 12.0f));
    g.drawText (db <= -119.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB",
                juce::Rectangle<float> (meterR.getRight() + 10.0f, meterR.getY() - 6.0f, 60.0f, 20.0f), juce::Justification::centredLeft);

    // processing facts, read from the processor (not hard-coded)
    const int lat = ed.proc.getLatencySamples();
    const double sr = ed.proc.currentSampleRate();
    g.setColour (T.dim);
    g.drawText ("Latency " + juce::String (lat) + ui::u8 (" samples (") + juce::String (1000.0 * lat / sr, 1) + ui::u8 (" ms)   ·   ")
                    + juce::String (sr / 1000.0, 1) + " kHz",
                juce::Rectangle<float> (24.0f, 770.0f, 380.0f, 18.0f), juce::Justification::centredLeft);
}

// ===================================================================================== look
KuralEditor::Look::Look (const ui::Fonts& f) : fonts (f)
{
    const auto& T = ui::theme();
    setColour (juce::PopupMenu::backgroundColourId, T.deep);
    setColour (juce::PopupMenu::textColourId, T.label);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, T.surface);
    setColour (juce::PopupMenu::highlightedTextColourId, T.pearl);
    setColour (juce::ComboBox::textColourId, T.label);
    setColour (juce::ComboBox::backgroundColourId, T.deep);
    setColour (juce::ComboBox::outlineColourId, T.pearl.withAlpha (0.2f));
    setColour (juce::ComboBox::arrowColourId, T.pearlDim);
    setColour (juce::TooltipWindow::textColourId, T.pearl);
}

juce::Slider::SliderLayout KuralEditor::Look::getSliderLayout (juce::Slider& s)
{
    juce::Slider::SliderLayout l;
    l.sliderBounds = s.getLocalBounds();
    return l;
}

void KuralEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto& T = ui::theme();
    // the dial sits in the top square; the value is written underneath
    const float d = (float) juce::jmin (w, h - 16);
    const auto area = juce::Rectangle<float> ((float) x + 0.5f * ((float) w - d), (float) y, d, d).reduced (3.0f);
    const float r = area.getWidth() * 0.5f;
    const auto c = area.getCentre();
    const auto accent = s.findColour (juce::Slider::thumbColourId);

    // track and value arc, growing from the neutral point (0 dB, centre, 0 st) when it is inside the range
    const auto& range = s.getNormalisableRange();
    const double neutral = range.start < 0.0 && range.end > 0.0 ? 0.0 : range.start;
    const float zeroPos = (float) range.convertTo0to1 (juce::jlimit (range.start, range.end, neutral));
    const float aZero = a0 + zeroPos * (a1 - a0), aVal = a0 + pos * (a1 - a0);
    juce::Path track, arc;
    track.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, a0, a1, true);
    arc.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, juce::jmin (aZero, aVal), juce::jmax (aZero, aVal), true);
    g.setColour (T.gridMajor);
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // body: a dark pebble lit from above
    const float br = r - 7.0f;
    juce::ColourGradient body (T.surface, c.x, c.y - br, T.deep, c.x, c.y + br, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (T.pearl.withAlpha (0.18f));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);
    g.setColour (T.pearl);
    g.drawLine ({ c.getPointOnCircumference (br * 0.3f, aVal), c.getPointOnCircumference (br - 4.0f, aVal) }, 1.8f);

    g.setColour (T.label);
    g.setFont (fonts.ui (600, 12.0f));
    g.drawText (s.getTextFromValue (s.getValue()), juce::Rectangle<float> ((float) x - 6.0f, area.getBottom() + 1.0f, (float) w + 12.0f, 15.0f),
                juce::Justification::centred);
}

void KuralEditor::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto& T = ui::theme();
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    const auto onCol = accent == juce::Colour() || accent == getDefaultLookAndFeel().findColour (juce::TextButton::buttonOnColourId)
                           ? T.seaGlass : accent;
    g.setColour (on ? onCol.withAlpha (down ? 0.42f : 0.28f) : T.deep.brighter (over ? 0.25f : 0.0f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? onCol : T.pearl.withAlpha (over ? 0.35f : 0.2f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
}

void KuralEditor::Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto& T = ui::theme();
    g.setColour (b.getToggleState() ? T.pearl : T.label.withAlpha (0.75f));
    g.setFont (fonts.ui (700, 13.0f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void KuralEditor::Look::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    g.setColour (ui::theme().pearl.withAlpha (over || dragging ? 0.5f : 0.18f));
    for (int i = 1; i <= 2; ++i)
    {
        const float d = (float) i * 4.0f;
        g.drawLine ((float) w - d, (float) h - 1.5f, (float) w - 1.5f, (float) h - d, 1.0f);
    }
}

void KuralEditor::Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& T = ui::theme();
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (T.deep.brighter (box.isMouseOver (true) ? 0.2f : 0.0f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (T.pearl.withAlpha (box.isMouseOver (true) ? 0.32f : 0.18f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 15.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (T.pearlDim);
    g.fillPath (arrow);
}

void KuralEditor::Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void KuralEditor::Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto& T = ui::theme();
    g.fillAll (T.deep);
    g.setColour (T.pearl.withAlpha (0.18f));
    g.drawRect (0, 0, w, h, 1);
}

void KuralEditor::Look::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto& T = ui::theme();
    g.fillAll (T.deep);
    g.setColour (T.pearl.withAlpha (0.25f));
    g.drawRect (0, 0, w, h, 1);
    g.setColour (T.pearl);
    g.setFont (fonts.ui (500, 14.0f));
    g.drawFittedText (text, juce::Rectangle<int> (8, 4, w - 16, h - 8), juce::Justification::centredLeft, 4);
}

juce::AudioProcessorEditor* KuralProcessor::createEditor()
{
    return new KuralEditor (*this);
}
