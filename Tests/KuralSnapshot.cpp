// Headless editor snapshot: runs the real processor over a synthetic sung phrase, lets the real
// editor pull the analysis frames, and renders the editor to a PNG. Used to check the layout, the
// pitch display and the Tamil logo shaping on CI machines without a DAW.
// usage: KuralSnapshot <out.png> [scale] [seconds]
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String outPath = argc > 1 ? juce::String (argv[1]) : "kural-snapshot.png";
    const float scale = argc > 2 ? (float) juce::String (argv[2]).getDoubleValue() : 1.0f;
    const double seconds = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 4.9;   // ends mid-note, so the live head of the line shows

    KuralProcessor p;
    const double fs = 48000.0;
    const int blk = 256;
    p.prepareToPlay (fs, blk);
    std::unique_ptr<KuralEditor> ed (dynamic_cast<KuralEditor*> (p.createEditor()));

    // a phrase: G3 – A3 – B3 – D4 with glides, vibrato on the long notes, a breath and a consonant gap
    struct Note { double start, len, midi; };
    const Note phrase[] = { { 0.2, 0.9, 55 }, { 1.1, 0.6, 57 }, { 1.7, 0.5, 59 }, { 2.4, 1.6, 62 }, { 4.3, 0.9, 59 } };
    auto midiAt = [&] (double t, bool& voiced)
    {
        voiced = false;
        for (size_t i = 0; i < std::size (phrase); ++i)
        {
            const auto& n = phrase[i];
            if (t < n.start || t >= n.start + n.len) continue;
            voiced = true;
            double m = n.midi;
            const double into = t - n.start;
            if (i > 0 && into < 0.08 && phrase[i - 1].start + phrase[i - 1].len >= n.start - 1e-6)
                m = phrase[i - 1].midi + (n.midi - phrase[i - 1].midi) * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * into / 0.08));
            if (into > 0.3) m += 0.35 * std::min (1.0, (into - 0.3) / 0.3) * std::sin (2.0 * juce::MathConstants<double>::pi * 5.4 * t);
            return m;
        }
        return 0.0;
    };

    juce::AudioBuffer<float> buf (2, blk);
    juce::MidiBuffer midi;
    juce::Random rng (2);
    double ph = 0.0;
    long long i0 = 0;
    const long long total = (long long) (seconds * fs);
    while (i0 < total)
    {
        for (int i = 0; i < blk; ++i)
        {
            const double t = (double) (i0 + i) / fs;
            bool voiced;
            const double m = midiAt (t, voiced);
            float v = 0.0f;
            if (voiced)
            {
                const double f = 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
                ph += 2.0 * juce::MathConstants<double>::pi * f / fs;
                double s = 0.0;
                for (int h = 1; h <= 12; ++h) s += std::sin (h * ph) / (h * 1.4);
                v = (float) (0.18 * s);
            }
            else if (t > 4.05 && t < 4.25)
                v = 0.05f * (rng.nextFloat() * 2.0f - 1.0f);      // an "s": unvoiced, must leave a gap
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        p.processBlock (buf, midi);
        i0 += blk;
        ed->pumpForSnapshot (blk / fs);
    }
    for (int k = 0; k < 120; ++k) ed->pumpForSnapshot (1.0 / 60.0);   // let the view settle (the audio clock does not move)

    ed->setSize (juce::roundToInt (KuralEditor::baseW * scale), juce::roundToInt (KuralEditor::baseH * scale));
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::printf ("%s %s (%d x %d)\n", ok ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return ok ? 0 : 1;
}
