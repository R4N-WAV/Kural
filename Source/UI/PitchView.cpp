#include "PitchView.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using kural::dsp::Voicing;

namespace
{
constexpr float rightPad = 40.0f;    // "now" sits a little in from the edge so the line's head is fully visible
const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
float midiOf (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
}

juce::Colour kural::ui::voiceColour (int v)
{
    // sea-glass family: distinct from each other and from the lead's pale cyan, none of them neon
    static const juce::Colour c[] = { juce::Colour (0xff7cc8b4), juce::Colour (0xff8fb4e8), juce::Colour (0xffd9c08a), juce::Colour (0xffc79fd6) };
    return c[((v % 4) + 4) % 4];
}

kural::ui::PitchView::PitchView (const Fonts& f) : fonts (f)
{
    hist.resize (4096);
    setOpaque (true);
}

void kural::ui::PitchView::addFrames (const KuralProcessor::AnalysisFrame* frames, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const auto& a = frames[i];
        Frame f { a.timeSec, a.f0Hz > 0.0f ? midiOf (a.f0Hz) : 0.0f, a.confidence, a.rmsDb, a.voicing, a.activeVoices, {} };
        for (int v = 0; v < shownVoices; ++v) f.shift[(size_t) v] = a.shift[(size_t) v];
        if (count > 0 && f.t < hist[(head + hist.size() - 1) % hist.size()].t - 0.5)
            count = 0;                               // the host restarted playback: start a new trace
        hist[head] = f;
        head = (head + 1) % hist.size();
        count = std::min (count + 1, hist.size());
    }
}

void kural::ui::PitchView::advance (double nowSec, double dt)
{
    now = nowSec;
    // follow the singer: centre on the median of the last 2 s of voiced pitch, eased
    std::vector<float> recent;
    recent.reserve (400);
    for (size_t k = 0; k < count; ++k)
    {
        const auto& f = hist[(head + hist.size() - 1 - k) % hist.size()];
        if (f.t < now - 2.0) break;
        if (f.v == Voicing::voiced) recent.push_back (f.midi);
    }
    if (recent.size() > 20)
    {
        std::nth_element (recent.begin(), recent.begin() + (long) recent.size() / 2, recent.end());
        targetCentre = recent[recent.size() / 2];
    }
    // keep the active harmony voices on screen: widen the range and centre between the extremes
    float lo = 0.0f, hi = 0.0f;
    if (count > 0)
    {
        const auto& f = hist[(head + hist.size() - 1) % hist.size()];
        for (int v = 0; v < shownVoices; ++v)
            if (f.active & (1u << v)) { lo = std::min (lo, f.shift[(size_t) v]); hi = std::max (hi, f.shift[(size_t) v]); }
    }
    const float targetRange = juce::jlimit (26.0f, 44.0f, (hi - lo) + 16.0f);
    const float ease = 1.0f - std::exp (-(float) dt / 0.6f);
    centreMidi += (targetCentre + 0.5f * (lo + hi) - centreMidi) * ease;
    rangeSemis += (targetRange - rangeSemis) * ease;
    if (std::abs (rangeSemis - targetRange) < 0.01f) rangeSemis = targetRange;
    repaint();
}

float kural::ui::PitchView::yForMidi (float midi) const
{
    const float h = (float) getHeight();
    return h * 0.5f - (midi - centreMidi) * (h / rangeSemis);
}

void kural::ui::PitchView::paintGrid (juce::Graphics& g)
{
    const auto& T = theme();
    const float w = (float) getWidth(), h = (float) getHeight();

    // screen: a column of water, slightly lit from above
    juce::ColourGradient bg (T.shelf.withAlpha (0.55f).overlaidWith (T.screen.withAlpha (0.6f)), 0, 0, T.screen, 0, h, false);
    g.setGradientFill (bg);
    g.fillAll();

    const int lo = (int) std::floor (centreMidi - rangeSemis * 0.5f) - 1, hi = (int) std::ceil (centreMidi + rangeSemis * 0.5f) + 1;
    for (int m = lo; m <= hi; ++m)
    {
        const float y = std::round (yForMidi ((float) m)) + 0.5f;
        const int pc = ((m % 12) + 12) % 12;
        const bool isC = pc == 0, natural = std::strlen (noteNames[pc]) == 1;
        g.setColour (isC ? T.gridMajor : T.gridMinor.withAlpha (natural ? 1.0f : 0.55f));
        g.fillRect (52.0f, y - 0.5f, w - 52.0f, isC ? 1.0f : 0.8f);
        if (natural)
        {
            g.setColour (isC ? T.label.withAlpha (0.85f) : T.dim);
            g.setFont (fonts.ui (isC ? 600 : 500, 12.0f));
            const juce::String name = juce::String (noteNames[pc]) + (isC ? juce::String (m / 12 - 1) : juce::String());
            g.drawText (name, juce::Rectangle<float> (10.0f, y - 8.0f, 34.0f, 16.0f), juce::Justification::centredRight);
        }
    }
    // seconds ticks along the bottom
    g.setFont (fonts.ui (500, 11.0f));
    for (int s = 1; s < (int) spanSec; ++s)
    {
        const float x = (w - rightPad) - (float) s / (float) spanSec * (w - rightPad - 52.0f);
        g.setColour (T.gridMinor);
        g.fillRect (x, h - 10.0f, 1.0f, 6.0f);
        g.setColour (T.dim.withAlpha (0.8f));
        g.drawText ("-" + juce::String (s) + " s", juce::Rectangle<float> (x - 30.0f, h - 26.0f, 60.0f, 14.0f), juce::Justification::centred);
    }
}

void kural::ui::PitchView::paint (juce::Graphics& g)
{
    const auto& T = theme();
    // the grid only changes when the view re-centres by a visible amount
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (gridCache.isNull() || std::abs (gridCentre - centreMidi) > 0.02f || std::abs (gridRange - rangeSemis) > 0.02f
        || gridCache.getWidth() != juce::roundToInt (getWidth() * scale))
    {
        gridCache = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt (getWidth() * scale)),
                                 juce::jmax (1, juce::roundToInt (getHeight() * scale)), true);
        juce::Graphics gg (gridCache);
        gg.addTransform (juce::AffineTransform::scale (scale));
        paintGrid (gg);
        gridCentre = centreMidi;
        gridRange = rangeSemis;
    }
    g.drawImage (gridCache, getLocalBounds().toFloat());

    const float w = (float) getWidth(), x0 = 52.0f;
    auto xForT = [&] (double t) { return (w - rightPad) - (float) ((now - t) / spanSec) * (w - rightPad - x0); };

    // ---- gather the visible frames, oldest first
    std::vector<const Frame*> vis;
    vis.reserve (count);
    for (size_t k = count; k-- > 0;)
    {
        const auto* f = &hist[(head + hist.size() - 1 - k) % hist.size()];
        if (f->t >= now - spanSec && f->t <= now + 0.05) vis.push_back (f);
    }

    // ---- continuous voiced runs; harmony lines first (underneath), then the lead on top
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> ((int) x0, 0, getWidth() - (int) x0, getHeight()));
    const Frame* last = vis.empty() ? nullptr : vis.back();
    std::vector<std::pair<size_t, size_t>> runs;
    for (size_t i = 0; i < vis.size();)
    {
        if (vis[i]->v != Voicing::voiced && vis[i]->v != Voicing::uncertain) { ++i; continue; }
        size_t j = i + 1;
        while (j < vis.size()
               && (vis[j]->v == Voicing::voiced || vis[j]->v == Voicing::uncertain)
               && std::abs (vis[j]->midi - vis[j - 1]->midi) < 1.5f
               && vis[j]->t - vis[j - 1]->t < 0.03)
            ++j;
        if (j - i >= 2) runs.push_back ({ i, j });
        i = j;
    }
    for (int v = shownVoices - 1; v >= 0; --v)
        for (auto [i, j] : runs)
        {
            // split where the voice is off or changes note, so a harmony step shows as a step
            size_t a = i;
            while (a < j)
            {
                if (! (vis[a]->active & (1u << v))) { ++a; continue; }
                size_t b = a + 1;
                while (b < j && (vis[b]->active & (1u << v)) && std::abs (vis[b]->shift[(size_t) v] - vis[b - 1]->shift[(size_t) v]) < 0.5f) ++b;
                if (b - a >= 3) strokeRun (g, vis, a, b, v, xForT);   // skip 1–2 frame fragments at note changes
                a = b;
            }
        }
    for (auto [i, j] : runs) strokeRun (g, vis, i, j, -1, xForT);
    const bool anyVoiced = ! runs.empty();

    // ---- head of the line: rings whose size follows the input level (real data, no animation loop)
    if (last != nullptr && last->v == Voicing::voiced && now - last->t < 0.08)
    {
        const juce::Point<float> c (xForT (last->t), yForMidi (last->midi));
        const float lvl = juce::jlimit (0.0f, 1.0f, (last->rmsDb + 50.0f) / 44.0f);
        for (int r = 0; r < 3; ++r)
        {
            const float rad = 4.0f + lvl * (6.0f + 7.0f * (float) r);
            g.setColour (T.voice.withAlpha ((0.32f - 0.09f * (float) r) * (0.4f + 0.6f * lvl)));
            g.drawEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, 1.0f);
        }
        g.setColour (T.voice);
        g.fillEllipse (c.x - 2.6f, c.y - 2.6f, 5.2f, 5.2f);
    }
    g.restoreState();

    if (! anyVoiced)
    {
        g.setColour (T.dim);
        g.setFont (fonts.ui (500, 15.0f));
        g.drawText ("Waiting for a voice: play or sing into the track to see its pitch",
                    getLocalBounds().withTrimmedLeft ((int) x0), juce::Justification::centred);
    }
    paintReadout (g, last);
}

void kural::ui::PitchView::strokeRun (juce::Graphics& g, const std::vector<const Frame*>& vis, size_t i, size_t j, int voice,
                                      const std::function<float (double)>& xForT)
{
    const auto& T = theme();
    auto midiAt = [&] (size_t k) { return vis[k]->midi + (voice >= 0 ? vis[k]->shift[(size_t) voice] : 0.0f); };
    juce::Path p;
    float confSum = 0.0f;
    juce::Point<float> prev (xForT (vis[i]->t), yForMidi (midiAt (i)));
    p.startNewSubPath (prev);
    for (size_t k = i + 1; k < j; ++k)
    {
        const juce::Point<float> pt (xForT (vis[k]->t), yForMidi (midiAt (k)));
        p.quadraticTo (prev, (prev + pt) * 0.5f);      // midpoint smoothing: a continuous, fluid curve
        prev = pt;
        confSum += vis[k]->conf;
    }
    p.lineTo (prev);
    const float conf = confSum / (float) std::max<size_t> (1, j - i - 1);
    const float a = juce::jlimit (0.15f, 1.0f, (conf - 0.6f) / 0.35f);
    if (voice < 0)
    {
        g.setColour (T.voiceGlow.withAlpha (0.10f * a));
        g.strokePath (p, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (T.voiceGlow.withAlpha (0.22f * a));
        g.strokePath (p, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (T.voice.withAlpha (0.95f * a));
        g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        const auto c = voiceColour (voice);
        g.setColour (c.withAlpha (0.12f * a));
        g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.75f * a));
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void kural::ui::PitchView::paintReadout (juce::Graphics& g, const Frame* last)
{
    const auto& T = theme();
    auto area = juce::Rectangle<float> (68.0f, 12.0f, 260.0f, 52.0f);
    juce::String note = u8 ("–"), detail;
    juce::Colour col = T.dim;
    if (last != nullptr && now - last->t < 0.25)
    {
        switch (last->v)
        {
            case Voicing::voiced:
            case Voicing::uncertain:
            {
                const int m = (int) std::lround (last->midi);
                const int cents = (int) std::lround ((last->midi - (float) m) * 100.0f);
                note = juce::String (noteNames[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
                detail = (cents >= 0 ? "+" : "") + juce::String (cents) + u8 (" cents   ·   ")
                         + juce::String (juce::roundToInt (last->conf * 100.0f)) + " % confidence"
                         + (last->v == Voicing::uncertain ? u8 ("   ·   uncertain") : juce::String());
                col = last->v == Voicing::voiced ? T.pearl : T.pearlDim;
                break;
            }
            case Voicing::unvoiced: detail = "unvoiced (breath or consonant)"; break;
            case Voicing::silence:  detail = "silence"; break;
        }
    }
    g.setColour (col);
    g.setFont (fonts.ui (700, 30.0f));
    g.drawText (note, area.removeFromLeft (64.0f), juce::Justification::centredLeft);
    g.setColour (T.label.withAlpha (0.8f));
    g.setFont (fonts.ui (500, 13.0f));
    g.drawText (detail, area, juce::Justification::centredLeft);

    // the notes the harmony voices are singing right now (lead + applied shift)
    if (last != nullptr && now - last->t < 0.25 && (last->v == Voicing::voiced || last->v == Voicing::uncertain))
    {
        float x = (float) getWidth() - 24.0f;
        for (int v = shownVoices - 1; v >= 0; --v)
        {
            if (! (last->active & (1u << v))) continue;
            const int m = (int) std::lround (last->midi + last->shift[(size_t) v]);
            const juce::String n = juce::String (noteNames[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
            g.setFont (fonts.ui (700, 18.0f));
            const float wN = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), n) + 4.0f;
            x -= wN;
            g.setColour (voiceColour (v));
            g.drawText (n, juce::Rectangle<float> (x, 18.0f, wN, 22.0f), juce::Justification::centredLeft);
            g.setFont (fonts.ui (600, 11.0f));
            g.setColour (voiceColour (v).withAlpha (0.7f));
            g.drawText ("V" + juce::String (v + 1), juce::Rectangle<float> (x, 40.0f, wN, 14.0f), juce::Justification::centredLeft);
            x -= 16.0f;
        }
    }
}
