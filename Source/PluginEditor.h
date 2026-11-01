#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/PitchView.h"
#include "UI/Theme.h"

// Kural editor. All drawing happens in design units (baseW × baseH) inside one content component
// that is scaled as a whole when the window is resized, so every size looks the same and stays
// sharp at any display scale.
//
//   header      logo lockup, build status
//   screen      live pitch: the lead and every sounding harmony voice (real engine data)
//   harmony     key, scale, harmonies on/off; one strip per voice (on, interval, octave, level,
//               pan, formant, fine) in the voice's screen colour
//   bottom      input meter, latency; consonants, glide, harmony level, dry level, output; bypass
class KuralEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KuralEditor (KuralProcessor&);
    ~KuralEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1040, baseH = 820;

    // test hook (snapshot tool): pull pending analysis and advance the view by dt without a timer
    void pumpForSnapshot (double dtSec);

private:
    void timerCallback() override;
    void updateVoiceStrips();

    struct Look : public juce::LookAndFeel_V4
    {
        explicit Look (const kural::ui::Fonts& f);
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return fonts.ui (700, 13.0f); }
        juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
        void drawCornerResizer (juce::Graphics&, int w, int h, bool over, bool dragging) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override { return fonts.ui (600, 14.0f); }
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::Font getPopupMenuFont() override { return fonts.ui (500, 15.0f); }
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
        void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
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

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider s;
        std::unique_ptr<SliderAtt> att;
        juce::String label;
        juce::Colour accent;
    };
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& label, juce::Rectangle<int> bounds,
                    const juce::String& tip, juce::Colour accent, double resetValue);
    void setupCombo (juce::ComboBox&, std::unique_ptr<ComboAtt>&, const juce::String& paramId, juce::Rectangle<int> bounds,
                     const juce::String& tip);

    struct VoiceStrip
    {
        juce::TextButton on;
        std::unique_ptr<ButtonAtt> onAtt;
        juce::ComboBox mode, interval, semis, octave;
        std::unique_ptr<ComboAtt> modeAtt, intervalAtt, semisAtt, octaveAtt;
        Knob level, pan, formant, fine;
        std::atomic<float>* modeValue = nullptr;
        std::atomic<float>* onValue = nullptr;
    };

    KuralProcessor& proc;
    kural::ui::Fonts fonts;
    Look look { fonts };
    Content content { *this };
    kural::ui::PitchView pitchView { fonts };
    juce::TooltipWindow tooltips { this, 700 };

    juce::ComboBox keyBox, scaleBox;
    std::unique_ptr<ComboAtt> keyAtt, scaleAtt;
    juce::TextButton harmonyButton { "HARMONIES" }, bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAtt> harmonyAtt, bypassAtt;
    std::array<VoiceStrip, kural::registeredVoices> strips;
    Knob consonants, glide, harmonyLevel, dryLevel, output;

    std::vector<KuralProcessor::AnalysisFrame> popBuf = std::vector<KuralProcessor::AnalysisFrame> (2048);
    float inMeterDb = -120.0f;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuralEditor)
};
