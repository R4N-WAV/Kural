#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "PluginProcessor.h"
#include "Theme.h"

// Live pitch display: the incoming vocal as a flowing line over a note grid, scrolling right to
// left. Everything drawn comes from the processor's analysis frames: line position = detected
// pitch, brightness = confidence, gaps = unvoiced/silence, the rings at the head = input level.
// Nothing is simulated; with no input the screen says so instead of animating.
// (Harmony voice lines will be drawn alongside the vocal line once the harmony engine exists.)
namespace kural::ui
{
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
    struct Frame { double t; float midi, conf, rmsDb; kural::dsp::Voicing v; };
    float yForMidi (float midi) const;
    void paintGrid (juce::Graphics&);
    void paintReadout (juce::Graphics&, const Frame* last);

    const Fonts& fonts;
    std::vector<Frame> hist;          // ring buffer
    size_t head = 0, count = 0;
    double now = 0.0;
    float centreMidi = 57.0f, targetCentre = 57.0f;   // A3 until a voice is heard
    float rangeSemis = 26.0f;
    juce::Image gridCache;
    float gridCentre = -1000.0f;
};
}
