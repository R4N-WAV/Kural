#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>
#include "PluginProcessor.h"
#include "Theme.h"

// Live pitch display: the incoming vocal as a flowing line over a note grid, scrolling right to
// left. Everything drawn comes from the processor's analysis frames: line position = detected
// pitch, brightness = confidence, gaps = unvoiced/silence, the rings at the head = input level.
// Nothing is simulated; with no input the screen says so instead of animating.
// Harmony voices are drawn as thinner lines in their own colours at lead + the shift the engine
// actually applies (from the same frames), so the screen shows what is being sung by every voice.
namespace kural::ui
{
// colour of harmony voice v (0-based) on the screen and on its controls
juce::Colour voiceColour (int v);

class PitchView : public juce::Component
{
public:
    explicit PitchView (const Fonts&);

    // message thread: append frames popped from the processor, then advance the view
    void addFrames (const KuralProcessor::AnalysisFrame* frames, int n);
    void advance (double nowSec, double frameDtSec);

    void paint (juce::Graphics&) override;
    void resized() override { gridCache = {}; }

    double spanSec = 6.0;            // visible history

private:
    static constexpr int shownVoices = kural::registeredVoices;
    struct Frame
    {
        double t;
        float midi, conf, rmsDb;
        kural::dsp::Voicing v;
        std::uint16_t active;
        std::array<float, shownVoices> shift;
        float lead;          // shift applied to the lead (tune + transpose)
        bool leadOn;         // the processed lead is what is heard
    };
    float yForMidi (float midi) const;
    void paintGrid (juce::Graphics&);
    void paintReadout (juce::Graphics&, const Frame* last);
    // voice: −2 = the lead as sung (dashed, under), −1 = the lead as heard, 0… = harmony voices
    void strokeRun (juce::Graphics&, const std::vector<const Frame*>& vis, size_t i, size_t j, int voice,
                    const std::function<float (double)>& xForT);

    const Fonts& fonts;
    std::vector<Frame> hist;          // ring buffer
    size_t head = 0, count = 0;
    double now = 0.0;
    float centreMidi = 57.0f, targetCentre = 57.0f;   // A3 until a voice is heard
    float rangeSemis = 26.0f;
    juce::Image gridCache;
    float gridCentre = -1000.0f, gridRange = 0.0f;
};
}
