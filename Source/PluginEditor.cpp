#include "PluginEditor.h"
#include <cmath>

using namespace kural;

namespace
{
// layout, design units
const juce::Rectangle<int> screenR { 24, 92, 992, 440 };
const juce::Rectangle<int> knobR { 790, 548, 84, 84 };
const juce::Rectangle<int> bypassR { 898, 574, 118, 34 };
const juce::Rectangle<float> meterR { 92.0f, 584.0f, 300.0f, 8.0f };
}

KuralEditor::KuralEditor (KuralProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    addAndMakeVisible (content);
    content.setSize (baseW, baseH);
    content.addAndMakeVisible (pitchView);
    pitchView.setBounds (screenR);

    outputKnob.setLookAndFeel (&look);
    outputKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    outputKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    outputKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    outputKnob.setTooltip ("Output level (double-click: 0 dB)");
    content.addAndMakeVisible (outputKnob);
    outputKnob.setBounds (knobR);
    outputAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids::output, outputKnob);
    outputKnob.setDoubleClickReturnValue (true, 0.0);

    bypassButton.setLookAndFeel (&look);
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Pass the input through untouched (crossfaded, no click)");
    content.addAndMakeVisible (bypassButton);
    bypassButton.setBounds (bypassR);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::bypass, bypassButton);

    setLookAndFeel (&look);
    setResizable (true, true);
    setResizeLimits (baseW * 3 / 4, baseH * 3 / 4, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

KuralEditor::~KuralEditor()
{
    setLookAndFeel (nullptr);
    outputKnob.setLookAndFeel (nullptr);
    bypassButton.setLookAndFeel (nullptr);
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

    // input meter: instant attack, ~20 dB/s release
    const float in = proc.inputPeakDb.load (std::memory_order_relaxed);
    inMeterDb = in > inMeterDb ? in : juce::jmax (in, inMeterDb - 20.0f * (float) dt);
    content.repaint (meterR.expanded (4.0f, 10.0f).getSmallestIntegerContainer().withTrimmedLeft (-80));
}

// ------------------------------------------------------------------------------------- content
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
    depth.addColour (0.35, T.deep);
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
                float v = 1.0f - std::abs (a + 0.6f * b) * 0.9f;
                v = juce::jlimit (0.0f, 1.0f, v);
                v = v * v * v;
                const float fade = juce::jlimit (0.0f, 1.0f, 1.0f - (float) y / (float) ch * 1.25f);   // only near the surface
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
    juce::GlyphArrangement ga;
    {
        // letter-spaced wordmark
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
    g.drawText (ui::u8 ("v" KURAL_VERSION_STRING "  ·  pitch analysis preview  ·  harmony voices not built yet"),
                juce::Rectangle<float> (520.0f, 34.0f, 496.0f, 20.0f), juce::Justification::centredRight);

    // ---- frames
    ui::drawPearlFrame (g, screenR.toFloat().expanded (0.5f), 6.0f);
    g.setColour (T.label.withAlpha (0.9f));
    g.setFont (f.ui (600, 12.0f));
    g.drawText ("INPUT", juce::Rectangle<float> (24.0f, meterR.getY() - 6.0f, 60.0f, 20.0f), juce::Justification::centredLeft);
    g.drawText ("OUTPUT", juce::Rectangle<float> ((float) knobR.getX() - 70.0f, (float) knobR.getCentreY() - 10.0f, 62.0f, 20.0f),
                juce::Justification::centredRight);
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
                juce::Rectangle<float> (meterR.getRight() + 10.0f, meterR.getY() - 6.0f, 70.0f, 20.0f), juce::Justification::centredLeft);

    // processing facts, read from the processor (not hard-coded)
    g.setColour (T.dim);
    g.drawText ("Latency " + juce::String (ed.proc.getLatencySamples()) + ui::u8 (" samples   ·   ")
                    + juce::String (ed.proc.currentSampleRate() / 1000.0, 1) + " kHz",
                juce::Rectangle<float> (24.0f, 606.0f, 400.0f, 18.0f), juce::Justification::centredLeft);
}

// ------------------------------------------------------------------------------------- look
juce::Slider::SliderLayout KuralEditor::Look::getSliderLayout (juce::Slider& s)
{
    juce::Slider::SliderLayout l;
    l.sliderBounds = s.getLocalBounds();
    return l;
}

void KuralEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto& T = ui::theme();
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();

    // track and value arc (from 0 dB: the arc grows to either side of the neutral point)
    const auto& range = s.getNormalisableRange();
    const float zeroPos = (float) range.convertTo0to1 (0.0);
    const float aZero = a0 + zeroPos * (a1 - a0), aVal = a0 + pos * (a1 - a0);
    juce::Path track, arc;
    track.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, a0, a1, true);
    arc.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, juce::jmin (aZero, aVal), juce::jmax (aZero, aVal), true);
    g.setColour (T.gridMajor);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (T.seaGlass);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // body: a dark pebble lit from above
    const float br = r - 10.0f;
    juce::ColourGradient body (T.surface, c.x, c.y - br, T.deep, c.x, c.y + br, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (T.pearl.withAlpha (0.18f));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);
    const auto tip = c.getPointOnCircumference (br - 6.0f, aVal), base = c.getPointOnCircumference (br * 0.35f, aVal);
    g.setColour (T.pearl);
    g.drawLine ({ base, tip }, 2.0f);

    g.setColour (T.label);
    g.setFont (fonts.ui (600, 13.0f));
    g.drawText (s.getTextFromValue (s.getValue()), juce::Rectangle<float> (c.x - 40.0f, c.y + br + 2.0f, 80.0f, 16.0f).translated (0.0f, -2.0f),
                juce::Justification::centred);
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

void KuralEditor::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto& T = ui::theme();
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? T.seaGlass.withAlpha (down ? 0.45f : 0.32f) : T.deep.brighter (over ? 0.25f : 0.0f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? T.seaGlass : T.pearl.withAlpha (over ? 0.35f : 0.2f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
}

void KuralEditor::Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto& T = ui::theme();
    g.setColour (b.getToggleState() ? T.pearl : T.label.withAlpha (0.85f));
    g.setFont (fonts.ui (700, 13.0f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

juce::AudioProcessorEditor* KuralProcessor::createEditor()
{
    return new KuralEditor (*this);
}
