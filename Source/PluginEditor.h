#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/PitchView.h"
#include "UI/Theme.h"

// Kural editor, direction "Shallows" (A2). All drawing happens in design units (baseW × baseH) inside
// one content component that is scaled as a whole when the window is resized, so every size looks
// the same and stays sharp at any display scale.
//
//   header        logo (குரல் by R4N), presets (browse, save, A/B), key, scale, bypass
//   meters        input (left), output (right)
//   display well  live pitch: lead as heard, lead as sung, every harmony voice (real engine data)
//   tune          on, natural / hard, retune speed, vibrato kept, humanize, flex-tune
//   lower left    HARMONY page (4 voices + harmony level, consonants, glide, naturalness)
//                 EFFECTS page (tone, dynamics, mutate, delay, reverb, stereo)
//   lower right   pitch & throat pad, duet, lead pitch / throat, dry, output
//   status bar    sample rate, latency, CPU, version — read from the processor
// Every control is attached to a real parameter; nothing is decorative.
class KuralEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KuralEditor (KuralProcessor&);
    ~KuralEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1100, baseH = 700;

    // test hook (snapshot tool): pull pending analysis and advance the view by dt without a timer
    void pumpForSnapshot (double dtSec);
    void showPage (int page);          // 0 = harmony, 1 = effects

private:
    void timerCallback() override;
    void refreshDynamic();
    void showPresetMenu();
    void savePresetDialog();

    struct Look : public juce::LookAndFeel_V4
    {
        explicit Look (const kural::ui::Fonts& f);
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override { return fonts.ui (500, 14.0f); }
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::Font getPopupMenuFont() override { return fonts.ui (500, 15.0f); }
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
        void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
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

    // pitch (vertical) and throat (horizontal) for the lead, on one pad
    class ThroatPad : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ThroatPad (KuralProcessor&, const kural::ui::Fonts&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
    private:
        void setFrom (juce::Point<float>);
        juce::RangedAudioParameter* pitch;
        juce::RangedAudioParameter* throat;
        std::unique_ptr<juce::ParameterAttachment> pitchAtt, throatAtt;
        float pitchV = 0.0f, throatV = 0.0f;
        const kural::ui::Fonts& fonts;
    };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider s;
        std::unique_ptr<SliderAtt> att;
    };
    enum class KnobSize { big, small, mini };
    void knob (Knob&, const juce::String& paramId, const juce::String& label, juce::Rectangle<int>, KnobSize,
               const juce::String& tip, int page = -1);
    void combo (juce::ComboBox&, std::unique_ptr<ComboAtt>&, const juce::String& paramId, juce::Rectangle<int>, bool dark,
                const juce::String& tip, int page = -1);
    void toggle (juce::TextButton&, std::unique_ptr<ButtonAtt>&, const juce::String& paramId, juce::Rectangle<int>,
                 const juce::String& style, const juce::String& tip, int page = -1);
    void place (juce::Component&, juce::Rectangle<int>, int page);

    struct VoiceCard
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
    ThroatPad pad { proc, fonts };
    juce::TooltipWindow tooltips { this, 700 };

    // header
    juce::TextButton prevPreset { juce::String::fromUTF8 ("\xe2\x97\x80") }, nextPreset { juce::String::fromUTF8 ("\xe2\x96\xb6") },
                     presetName, savePreset { "Save" }, slotA { "A" }, slotB { "B" }, bypassButton { "BYPASS" };
    juce::ComboBox keyBox, scaleBox;
    std::unique_ptr<ComboAtt> keyAtt, scaleAtt;
    std::unique_ptr<ButtonAtt> bypassAtt;

    // tune
    juce::TextButton tuneOn, naturalButton { "Natural" }, hardButton { "Hard" };
    std::unique_ptr<ButtonAtt> tuneOnAtt;
    Knob retune, vibrato, humanize, flex;

    // lower left: pages
    juce::TextButton tabHarmony { "HARMONY" }, tabEffects { "EFFECTS" }, harmonyOn;
    std::unique_ptr<ButtonAtt> harmonyOnAtt;
    std::array<VoiceCard, kural::registeredVoices> cards;
    Knob harmonyLevel, consonants, glide, naturalness;
    Knob lowCut, presence, air, compression, tube, mutateMix, delayMix, delayTime, delayFeedback, reverbMix, reverbSize, reverbDecay,
         reverbDamping, width;
    juce::TextButton mutateOn, pingPong { "PING-PONG" };
    std::unique_ptr<ButtonAtt> mutateOnAtt, pingPongAtt;
    juce::ComboBox mutateRatio, delaySync;
    std::unique_ptr<ComboAtt> mutateRatioAtt, delaySyncAtt;
    std::vector<std::pair<juce::Component*, int>> paged;   // component, page
    int page = 0;
    int cardMask = -1;

    // lower right
    juce::TextButton duetOn;
    std::unique_ptr<ButtonAtt> duetOnAtt;
    Knob duetLevel, duetSpread, leadPitch, leadFormant, dryLevel, output;

    std::vector<KuralProcessor::AnalysisFrame> popBuf = std::vector<KuralProcessor::AnalysisFrame> (2048);
    float inMeterDb = -120.0f, outMeterDb = -120.0f;
    double lastTick = 0.0;
    juce::String shownPreset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuralEditor)
};
