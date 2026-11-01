#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/PitchView.h"
#include "UI/Theme.h"

// Kural editor. All drawing happens in design units (baseW × baseH) inside one content component
// that is scaled as a whole when the window is resized, so every size looks the same and stays
// sharp at any display scale.
class KuralEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KuralEditor (KuralProcessor&);
    ~KuralEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1040, baseH = 640;

    // test hook (snapshot tool): pull pending analysis and advance the view by dt without a timer
    void pumpForSnapshot (double dtSec);

private:
    void timerCallback() override;

    struct Look : public juce::LookAndFeel_V4
    {
        explicit Look (const kural::ui::Fonts& f) : fonts (f) {}
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return fonts.ui (700, 13.0f); }
        juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
        void drawCornerResizer (juce::Graphics&, int w, int h, bool over, bool dragging) override;
        const kural::ui::Fonts& fonts;
    };

    class Content : public juce::Component
    {
    public:
        explicit Content (KuralEditor& e) : ed (e) { setOpaque (true); }
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
    private:
        void renderBackground (float scale);
        KuralEditor& ed;
        juce::Image bgCache;
        float bgScale = 0.0f;
    };

    KuralProcessor& proc;
    kural::ui::Fonts fonts;
    Look look { fonts };
    Content content { *this };
    kural::ui::PitchView pitchView { fonts };
    juce::Slider outputKnob;
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    std::vector<KuralProcessor::AnalysisFrame> popBuf = std::vector<KuralProcessor::AnalysisFrame> (2048);
    float inMeterDb = -120.0f;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuralEditor)
};
