#include "PluginEditor.h"
#include <cmath>

using namespace kural;

namespace
{
// ---- layout, design units
const juce::Rectangle<int> wellR { 52, 62, 650, 318 }, tuneR { 714, 62, 330, 318 }, lowerR { 52, 392, 650, 270 },
                           throatR { 714, 392, 330, 270 }, statusR { 0, 674, 1100, 26 };
const juce::Rectangle<float> inMeterR { 22.0f, 80.0f, 10.0f, 570.0f }, outMeterR { 1062.0f, 80.0f, 8.0f, 570.0f };

// harmony page: voice cards and the column of global harmony controls
juce::Rectangle<int> cardR (int v) { return { lowerR.getX() + 14 + v * 120, lowerR.getY() + 46, 112, 214 }; }
const juce::Rectangle<int> harmonyColR { lowerR.getX() + 500, lowerR.getY() + 46, 136, 214 };
// effects page: 3 × 2 modules
juce::Rectangle<int> moduleR (int i)
{
    const int w = 202, h = 104;
    return { lowerR.getX() + 14 + (i % 3) * (w + 8), lowerR.getY() + 46 + (i / 3) * (h + 8), w, h };
}
const char* moduleNames[] = { "TONE", "DYNAMICS & COLOUR", "MUTATE", "DELAY", "REVERB", "STEREO" };

juce::String upperSpaced (const juce::String& s) { return s.toUpperCase(); }
}

// ======================================================================================== editor
KuralEditor::KuralEditor (KuralProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    content.setSize (baseW, baseH);
    content.addAndMakeVisible (pitchView);
    pitchView.setBounds (wellR);

    // ---- header: presets, key, scale, bypass
    for (auto* b : { &prevPreset, &nextPreset, &presetName })
    {
        b->getProperties().set ("style", "well");
        content.addAndMakeVisible (*b);
    }
    prevPreset.setBounds (190, 8, 30, 32);
    presetName.setBounds (220, 8, 260, 32);
    nextPreset.setBounds (480, 8, 30, 32);
    prevPreset.setTooltip ("Previous preset");
    nextPreset.setTooltip ("Next preset");
    presetName.setTooltip ("Browse presets by category");
    prevPreset.onClick = [this] { proc.presets.next (-1); refreshDynamic(); };
    nextPreset.onClick = [this] { proc.presets.next (1); refreshDynamic(); };
    presetName.onClick = [this] { showPresetMenu(); };
    savePreset.getProperties().set ("style", "chip");
    savePreset.setTooltip ("Save the current sound as a user preset");
    savePreset.onClick = [this] { savePresetDialog(); };
    content.addAndMakeVisible (savePreset);
    savePreset.setBounds (518, 8, 52, 32);
    for (auto* b : { &slotA, &slotB })
    {
        b->getProperties().set ("style", "chip");
        b->setClickingTogglesState (false);
        b->setTooltip ("Compare two settings: changes are kept separately in A and B");
        content.addAndMakeVisible (*b);
    }
    slotA.setBounds (578, 8, 32, 32);
    slotB.setBounds (610, 8, 32, 32);
    slotA.onClick = [this] { proc.presets.selectSlot (0); refreshDynamic(); };
    slotB.onClick = [this] { proc.presets.selectSlot (1); refreshDynamic(); };

    combo (keyBox, keyAtt, ids::key, { 740, 8, 58, 32 }, false, "Key of the song: tuning and harmonies follow its scale");
    combo (scaleBox, scaleAtt, ids::scale, { 852, 8, 146, 32 }, false,
           "Scale: tuning snaps to its notes; harmony intervals are counted along it (a third is major or minor as the key requires)");
    toggle (bypassButton, bypassAtt, ids::bypass, { 1006, 8, 80, 32 }, "chip",
            "Pass the input through untouched (delayed by the same latency, crossfaded, no click)");

    // ---- tune
    toggle (tuneOn, tuneOnAtt, ids::tuneOn, { tuneR.getX() + 14, tuneR.getY() + 12, 20, 20 }, "led", "Pitch correction on or off");
    for (auto* b : { &naturalButton, &hardButton })
    {
        b->getProperties().set ("style", "segment");
        content.addAndMakeVisible (*b);
    }
    naturalButton.setBounds (tuneR.getRight() - 14 - 140, tuneR.getY() + 9, 70, 26);
    hardButton.setBounds (tuneR.getRight() - 14 - 70, tuneR.getY() + 9, 70, 26);
    naturalButton.setTooltip ("Natural: puts each note in tune and keeps the singer's vibrato, scoops and drift");
    hardButton.setTooltip ("Hard: instant correction with the vibrato flattened (the stepped, robotic sound)");
    auto setParam = [this] (const char* id, float v)
    {
        if (auto* r = proc.apvts.getParameter (id)) { r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 (v)); r->endChangeGesture(); }
    };
    naturalButton.onClick = [this, setParam]
    {
        setParam (ids::tuneOn, 1.0f);
        setParam (ids::tuneVibrato, 100.0f);
        if (proc.apvts.getRawParameterValue (ids::retune)->load() < 5.0f) setParam (ids::retune, 25.0f);
        refreshDynamic();
    };
    hardButton.onClick = [this, setParam]
    {
        setParam (ids::tuneOn, 1.0f);
        setParam (ids::tuneVibrato, 0.0f);
        setParam (ids::retune, 0.0f);
        refreshDynamic();
    };
    knob (retune, ids::retune, "RETUNE SPEED", { tuneR.getX() + 14, tuneR.getY() + 48, 300, 128 }, KnobSize::big,
          "How quickly notes are pulled into tune. 0 ms = instantly (hard tune); slower keeps more of the slide into each note");
    knob (vibrato, ids::tuneVibrato, "VIBRATO KEPT", { tuneR.getX() + 14, tuneR.getY() + 206, 96, 98 }, KnobSize::small,
          "How much of the singer's own motion stays: 100 % corrects each note's centre and keeps vibrato and drift; 0 % flattens it");
    knob (humanize, ids::humanize, "HUMANIZE", { tuneR.getX() + 117, tuneR.getY() + 206, 96, 98 }, KnobSize::small,
          "Slower correction on held notes, so long notes keep some life while short ones still snap");
    knob (flex, ids::flexTune, "FLEX-TUNE", { tuneR.getX() + 220, tuneR.getY() + 206, 96, 98 }, KnobSize::small,
          "Only correct notes the singer is already close to; expressive notes further off are left alone");

    // ---- lower left: page tabs
    for (auto* b : { &tabHarmony, &tabEffects })
    {
        b->getProperties().set ("style", "tab");
        content.addAndMakeVisible (*b);
    }
    tabHarmony.setBounds (lowerR.getX() + 14, lowerR.getY() + 10, 96, 26);
    tabEffects.setBounds (lowerR.getX() + 112, lowerR.getY() + 10, 96, 26);
    tabHarmony.onClick = [this] { showPage (0); };
    tabEffects.onClick = [this] { showPage (1); };
    toggle (harmonyOn, harmonyOnAtt, ids::harmonyOn, { lowerR.getRight() - 34, lowerR.getY() + 13, 20, 20 }, "led",
            "All harmony voices on or off (the lead vocal stays)", 0);

    // harmony page: voice cards
    for (int v = 0; v < registeredVoices; ++v)
    {
        auto& c = cards[(size_t) v];
        const auto r = cardR (v);
        const juce::String n = "Voice " + juce::String (v + 1);
        toggle (c.on, c.onAtt, ids::voiceId (v, "on"), { r.getX() + 8, r.getY() + 8, 18, 18 }, "led", n + " on or off", 0);
        combo (c.mode, c.modeAtt, ids::voiceId (v, "mode"), { r.getX() + 8, r.getY() + 32, 96, 22 }, false,
               "Scale: the interval follows the key. Semitones: a fixed distance", 0);
        combo (c.interval, c.intervalAtt, ids::voiceId (v, "interval"), { r.getX() + 8, r.getY() + 58, 96, 24 }, true,
               "Interval from the lead, counted along the scale", 0);
        combo (c.semis, c.semisAtt, ids::voiceId (v, "semis"), { r.getX() + 8, r.getY() + 58, 96, 24 }, true,
               "Fixed distance from the lead in semitones (ignores the key)", 0);
        combo (c.octave, c.octaveAtt, ids::voiceId (v, "octave"), { r.getX() + 8, r.getY() + 86, 96, 22 }, false, "Octaves added to the interval", 0);
        knob (c.level, ids::voiceId (v, "level"), "LEVEL", { r.getX() + 6, r.getY() + 112, 50, 50 }, KnobSize::mini, n + " level", 0);
        knob (c.pan, ids::voiceId (v, "pan"), "PAN", { r.getX() + 56, r.getY() + 112, 50, 50 }, KnobSize::mini, n + " position in the stereo field", 0);
        knob (c.formant, ids::voiceId (v, "formant"), "FORMANT", { r.getX() + 6, r.getY() + 162, 50, 50 }, KnobSize::mini,
              "Vocal tract size of this voice: up = smaller, brighter; down = bigger, darker", 0);
        knob (c.fine, ids::voiceId (v, "cents"), "FINE", { r.getX() + 56, r.getY() + 162, 50, 50 }, KnobSize::mini, "Fine detune in cents", 0);
        c.modeValue = proc.apvts.getRawParameterValue (ids::voiceId (v, "mode"));
        c.onValue = proc.apvts.getRawParameterValue (ids::voiceId (v, "on"));
    }
    {
        const auto r = harmonyColR;
        knob (harmonyLevel, ids::harmonyLevel, "HARMONY", { r.getX(), r.getY() + 4, 68, 100 }, KnobSize::small, "Level of all harmony voices together", 0);
        knob (naturalness, ids::naturalness, "NATURAL", { r.getX() + 68, r.getY() + 4, 68, 100 }, KnobSize::small,
              "Each voice gets its own slow drift and timing, so the stack sounds like several singers, not copies of one", 0);
        knob (consonants, ids::consonants, "CONSONANTS", { r.getX(), r.getY() + 110, 68, 100 }, KnobSize::small,
              "How much of the consonants and breaths the harmony voices carry (never pitch-shifted)", 0);
        knob (glide, ids::glide, "GLIDE", { r.getX() + 68, r.getY() + 110, 68, 100 }, KnobSize::small, "How quickly a voice moves to a new harmony note", 0);
    }

    // effects page
    {
        auto at = [] (int m, int k, int w = 64) { const auto r = moduleR (m); return juce::Rectangle<int> (r.getX() + 4 + k * w, r.getY() + 22, w, 80); };
        knob (lowCut, ids::eqLowCut, "LOW CUT", at (0, 0), KnobSize::mini, "Removes rumble below this frequency (20 Hz = off)", 1);
        knob (presence, ids::eqPresence, "PRESENCE", at (0, 1), KnobSize::mini, "Forward or softer around 3 kHz, where words are understood", 1);
        knob (air, ids::eqAir, "AIR", at (0, 2), KnobSize::mini, "Breath and sheen above 10 kHz", 1);
        knob (compression, ids::compAmount, "COMPRESS", at (1, 0), KnobSize::mini, "Evens out the level: more = steadier and closer", 1);
        knob (tube, ids::tubeDrive, "TUBE", at (1, 1), KnobSize::mini, "Warm valve saturation (oversampled)", 1);
        toggle (mutateOn, mutateOnAtt, ids::mutateOn, { moduleR (2).getRight() - 26, moduleR (2).getY() + 4, 18, 18 }, "led",
                "Mutate on or off: ring modulation that follows the voice (metallic, robotic)", 1);
        combo (mutateRatio, mutateRatioAtt, ids::mutateRatio, { moduleR (2).getX() + 10, moduleR (2).getY() + 40, 70, 24 }, true,
               "Carrier as a multiple of the lead's pitch", 1);
        knob (mutateMix, ids::mutateMix, "MIX", at (2, 2), KnobSize::mini, "Amount of the ring-modulated sound", 1);
        knob (delayMix, ids::delayMix, "MIX", at (3, 0, 50), KnobSize::mini, "Delay level (0 = off)", 1);
        knob (delayTime, ids::delayTime, "TIME", at (3, 1, 50), KnobSize::mini, "Delay time when not synced to the tempo", 1);
        knob (delayFeedback, ids::delayFeedback, "REPEATS", at (3, 2, 50), KnobSize::mini, "How long the echoes keep repeating", 1);
        combo (delaySync, delaySyncAtt, ids::delaySync, { moduleR (3).getRight() - 54, moduleR (3).getY() + 30, 50, 22 }, true,
               "Sync the delay to the song's tempo (Free = use the time knob)", 1);
        toggle (pingPong, pingPongAtt, ids::delayPingPong, { moduleR (3).getRight() - 54, moduleR (3).getY() + 60, 50, 22 }, "chip",
                "Echoes alternate left and right", 1);
        pingPong.setButtonText ("L/R");
        knob (reverbMix, ids::reverbMix, "MIX", at (4, 0, 50), KnobSize::mini, "Reverb level (0 = off)", 1);
        knob (reverbSize, ids::reverbSize, "SIZE", at (4, 1, 50), KnobSize::mini, "Size of the space", 1);
        knob (reverbDecay, ids::reverbDecay, "DECAY", at (4, 2, 50), KnobSize::mini, "How long the tail lasts", 1);
        knob (reverbDamping, ids::reverbDamping, "DAMP", at (4, 3, 50), KnobSize::mini, "How quickly the highs fade in the tail", 1);
        knob (width, ids::width, "WIDTH", at (5, 0), KnobSize::mini, "Stereo width of the whole sound: 0 % mono, 100 % as is, 200 % wider", 1);
    }

    // ---- lower right: pitch & throat, duet, levels
    content.addAndMakeVisible (pad);
    pad.setBounds (throatR.getX() + 14, throatR.getY() + 40, 132, 132);
    pad.setTooltip ("Drag: up/down moves the lead's pitch, left/right its throat (formants). Double-click: back to centre");
    toggle (duetOn, duetOnAtt, ids::duetOn, { throatR.getX() + 162, throatR.getY() + 44, 18, 18 }, "led",
            "Duet: a second singer on the lead's line, loosely timed and slightly detuned");
    knob (duetLevel, ids::duetLevel, "LEVEL", { throatR.getX() + 158, throatR.getY() + 70, 78, 100 }, KnobSize::small, "Level of the duet voice");
    knob (duetSpread, ids::duetSpread, "SPREAD", { throatR.getX() + 238, throatR.getY() + 70, 78, 100 }, KnobSize::small,
          "How far apart the duet sings: timing, detune and stereo position");
    knob (leadPitch, ids::leadPitch, "PITCH", { throatR.getX() + 14, throatR.getY() + 190, 70, 72 }, KnobSize::mini, "Lead pitch shift in semitones");
    knob (leadFormant, ids::leadFormant, "THROAT", { throatR.getX() + 88, throatR.getY() + 190, 70, 72 }, KnobSize::mini,
          "Lead formant shift: up = smaller, brighter voice; down = bigger, darker");
    knob (dryLevel, ids::dryLevel, "LEAD", { throatR.getX() + 168, throatR.getY() + 190, 70, 72 }, KnobSize::mini, "Level of the lead vocal (dry or tuned)");
    knob (output, ids::output, "OUTPUT", { throatR.getX() + 246, throatR.getY() + 190, 70, 72 }, KnobSize::mini, "Output level");

    showPage (0);
    refreshDynamic();
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

void KuralEditor::place (juce::Component& c, juce::Rectangle<int> r, int pg)
{
    content.addAndMakeVisible (c);
    c.setBounds (r);
    if (pg >= 0) paged.push_back ({ &c, pg });
}

void KuralEditor::knob (Knob& k, const juce::String& paramId, const juce::String& label, juce::Rectangle<int> r, KnobSize size,
                        const juce::String& tip, int pg)
{
    k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k.s.setTooltip (tip);
    k.s.getProperties().set ("label", label);
    k.s.getProperties().set ("size", size == KnobSize::big ? "big" : size == KnobSize::small ? "small" : "mini");
    place (k.s, r, pg);
    k.att = std::make_unique<SliderAtt> (proc.apvts, paramId, k.s);
    if (auto* prm = proc.apvts.getParameter (paramId))
        k.s.setDoubleClickReturnValue (true, prm->getNormalisableRange().convertFrom0to1 (prm->getDefaultValue()));
}

void KuralEditor::combo (juce::ComboBox& box, std::unique_ptr<ComboAtt>& att, const juce::String& paramId, juce::Rectangle<int> r,
                         bool dark, const juce::String& tip, int pg)
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
    box.getProperties().set ("dark", dark);
    box.setJustificationType (juce::Justification::centredLeft);
    place (box, r, pg);
    att = std::make_unique<ComboAtt> (proc.apvts, paramId, box);
}

void KuralEditor::toggle (juce::TextButton& b, std::unique_ptr<ButtonAtt>& att, const juce::String& paramId, juce::Rectangle<int> r,
                          const juce::String& style, const juce::String& tip, int pg)
{
    b.setClickingTogglesState (true);
    b.getProperties().set ("style", style);
    b.setTooltip (tip);
    place (b, r, pg);
    att = std::make_unique<ButtonAtt> (proc.apvts, paramId, b);
}

void KuralEditor::showPage (int pg)
{
    page = juce::jlimit (0, 1, pg);
    for (auto& [c, p] : paged) c->setVisible (p == page);
    tabHarmony.setToggleState (page == 0, juce::dontSendNotification);
    tabEffects.setToggleState (page == 1, juce::dontSendNotification);
    refreshDynamic();
    content.repaint();
}

void KuralEditor::refreshDynamic()
{
    // voice cards: interval or semitone box by mode; switched-off voices dimmed (card backgrounds are
    // painted by the content, so it is repainted when any on/off state changes)
    int mask = proc.apvts.getRawParameterValue (ids::harmonyOn)->load() > 0.5f ? 1 : 0;
    for (size_t v = 0; v < cards.size(); ++v)
        if (cards[v].onValue != nullptr && cards[v].onValue->load() > 0.5f) mask |= 2 << v;
    if (mask != cardMask) { cardMask = mask; content.repaint (lowerR); }
    for (auto& c : cards)
    {
        const bool semitones = c.modeValue != nullptr && c.modeValue->load() > 0.5f;
        c.interval.setVisible (page == 0 && ! semitones);
        c.semis.setVisible (page == 0 && semitones);
        const bool on = c.onValue != nullptr && c.onValue->load() > 0.5f
                        && proc.apvts.getRawParameterValue (ids::harmonyOn)->load() > 0.5f;
        const float a = on ? 1.0f : 0.5f;
        for (juce::Component* comp : { (juce::Component*) &c.mode, (juce::Component*) &c.interval, (juce::Component*) &c.semis,
                                       (juce::Component*) &c.octave, (juce::Component*) &c.level.s, (juce::Component*) &c.pan.s,
                                       (juce::Component*) &c.formant.s, (juce::Component*) &c.fine.s })
            if (std::abs (comp->getAlpha() - a) > 0.01f) comp->setAlpha (a);
    }
    // tune style shows what the settings are, not a separate state
    const bool tuneIsOn = proc.apvts.getRawParameterValue (ids::tuneOn)->load() > 0.5f;
    const bool natural = proc.apvts.getRawParameterValue (ids::tuneVibrato)->load() >= 50.0f;
    naturalButton.setToggleState (tuneIsOn && natural, juce::dontSendNotification);
    hardButton.setToggleState (tuneIsOn && ! natural, juce::dontSendNotification);
    slotA.setToggleState (proc.presets.slot() == 0, juce::dontSendNotification);
    slotB.setToggleState (proc.presets.slot() == 1, juce::dontSendNotification);
    auto name = proc.presets.currentName() + (proc.presets.isModified() ? " *" : "");
    if (name != shownPreset)
    {
        shownPreset = name;
        presetName.setButtonText (name);
        const int i = proc.presets.currentIndex();
        presetName.getProperties().set ("category", i >= 0 ? proc.presets.list()[i].category : juce::String ("Init"));
        presetName.repaint();
    }
}

void KuralEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& list = proc.presets.list();
    juce::StringArray cats;
    for (const auto& p : list) cats.addIfNotAlreadyThere (p.category);
    for (const auto& c : cats)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < list.size(); ++i)
            if (list[i].category == c) sub.addItem (i + 1, list[i].name, true, i == proc.presets.currentIndex());
        menu.addSubMenu (c, sub);
    }
    menu.addSeparator();
    menu.addItem (10001, "Rescan user presets");
    menu.addItem (10002, "Show preset folder");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName), [this] (int r)
    {
        if (r == 10001) { proc.presets.refresh(); return; }
        if (r == 10002) { auto d = PresetManager::userFolder(); d.createDirectory(); d.startAsProcess(); return; }
        if (r > 0) { proc.presets.load (r - 1); refreshDynamic(); }
    });
}

void KuralEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name of the new user preset:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.presets.currentName(), {});
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result == 1) proc.presets.saveUser (w->getTextEditorContents ("name"));
        refreshDynamic();
    }), true);
}

void KuralEditor::paint (juce::Graphics& g) { g.fillAll (ui::theme().chassis); }

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
    refreshDynamic();

    // meters: instant attack, ~20 dB/s release
    auto ballistics = [dt] (float& m, float v) { m = v > m ? v : juce::jmax (v, m - 20.0f * (float) dt); };
    ballistics (inMeterDb, proc.inputPeakDb.load (std::memory_order_relaxed));
    ballistics (outMeterDb, proc.outputPeakDb.load (std::memory_order_relaxed));
    content.repaint (inMeterR.expanded (10.0f, 20.0f).getSmallestIntegerContainer());
    content.repaint (outMeterR.expanded (12.0f, 20.0f).getSmallestIntegerContainer());
    content.repaint (statusR);
    content.repaint (tuneR.getX() + 150, tuneR.getY() + 50, 170, 120);       // retune value text
}

// ======================================================================================= content
void KuralEditor::Content::renderBackground (float scale)
{
    const auto& T = ui::theme();
    const int W = juce::roundToInt (baseW * scale), H = juce::roundToInt (baseH * scale);
    bgCache = juce::Image (juce::Image::RGB, juce::jmax (1, W), juce::jmax (1, H), true);
    bgScale = scale;
    juce::Graphics g (bgCache);
    g.addTransform (juce::AffineTransform::scale (scale));

    // chassis: wet stone under a film of water; caustic light drifts across it (static)
    g.fillAll (T.chassis);
    {
        const int cw = baseW / 3, ch = baseH / 3;
        juce::Image caus (juce::Image::ARGB, cw, ch, true);
        juce::Image::BitmapData bd (caus, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
            {
                const float fx = (float) x * 0.085f, fy = (float) y * 0.11f;
                const float a = std::sin (fx + 1.7f * std::sin (fy * 0.7f)) + std::sin (fy * 1.1f + 1.3f * std::sin (fx * 0.6f + 0.8f));
                const float b = std::sin (fx * 0.8f - fy * 0.5f + 2.0f * std::sin (fx * 0.31f));
                const float v = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (a + 0.6f * b) * 0.9f);
                bd.setPixelColour (x, y, juce::Colours::white.withAlpha (0.22f * v * v * v));
            }
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (caus, juce::Rectangle<float> (0.0f, 0.0f, (float) baseW, (float) baseH));
    }

    // header strip and status bar
    g.setColour (T.panel.withAlpha (0.72f));
    g.fillRect (0, 0, baseW, 48);
    g.setColour (T.panelLine);
    g.fillRect (0, 47, baseW, 1);
    g.setColour (T.ink.withAlpha (0.05f));
    g.fillRect (statusR);
    g.setColour (T.panelLine);
    g.fillRect (statusR.getX(), statusR.getY(), statusR.getWidth(), 1);

    // the lockup: குரல் by R4N
    const auto& f = ed.fonts;
    const auto& tamil = ui::tamilName();
    const auto logoFont = f.logo (800, 28.0f);
    const float wTamil = juce::GlyphArrangement::getStringWidth (logoFont, tamil);
    g.setColour (T.ink);
    g.setFont (logoFont);
    g.drawText (tamil, juce::Rectangle<float> (16.0f, 4.0f, wTamil + 6.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (T.muted);
    g.setFont (f.ui (500, 13.0f));
    g.drawText ("by R4N", juce::Rectangle<float> (16.0f + wTamil + 10.0f, 17.0f, 60.0f, 18.0f), juce::Justification::centredLeft);

    // header labels
    g.setFont (f.ui (600, 11.0f));
    g.drawText ("KEY", juce::Rectangle<float> (708.0f, 8.0f, 28.0f, 32.0f), juce::Justification::centredRight);
    g.drawText ("SCALE", juce::Rectangle<float> (806.0f, 8.0f, 42.0f, 32.0f), juce::Justification::centredRight);

    // panels
    for (auto r : { tuneR, lowerR, throatR })
    {
        g.setColour (T.panel.withAlpha (0.8f));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (T.panelLine);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }
    // the display well
    g.setColour (T.well);
    g.fillRoundedRectangle (wellR.toFloat().expanded (1.0f), 8.0f);
    g.setColour (T.muted.withAlpha (0.6f));
    g.drawRoundedRectangle (wellR.toFloat().expanded (1.0f), 8.0f, 1.0f);

    // meters: wells
    for (auto r : { inMeterR, outMeterR })
    {
        g.setColour (T.well);
        g.fillRoundedRectangle (r, 3.0f);
    }
    g.setColour (T.muted);
    g.setFont (f.ui (600, 10.0f));
    g.drawText ("IN", juce::Rectangle<float> (12.0f, 62.0f, 30.0f, 14.0f), juce::Justification::centred);
    g.drawText ("OUT", juce::Rectangle<float> (1050.0f, 62.0f, 34.0f, 14.0f), juce::Justification::centred);

    // panel titles
    auto title = [&] (const juce::String& t, float x, float y)
    {
        g.setColour (T.ink);
        g.setFont (f.ui (700, 13.0f));
        g.drawText (t, juce::Rectangle<float> (x, y, 220.0f, 20.0f), juce::Justification::centredLeft);
    };
    title ("TUNE", (float) tuneR.getX() + 42.0f, (float) tuneR.getY() + 12.0f);
    title ("PITCH & THROAT", (float) throatR.getX() + 14.0f, (float) throatR.getY() + 12.0f);
    title ("DUET", (float) throatR.getX() + 186.0f, (float) throatR.getY() + 43.0f);
}

void KuralEditor::Content::paint (juce::Graphics& g)
{
    const auto& T = ui::theme();
    const auto& f = ed.fonts;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (bgCache.isNull() || std::abs (bgScale - scale) > 0.01f)
        renderBackground (scale);
    g.drawImage (bgCache, getLocalBounds().toFloat());

    // page-dependent card frames and labels (cheap; not cached)
    if (ed.page == 0)
    {
        for (int v = 0; v < registeredVoices; ++v)
        {
            const auto r = cardR (v).toFloat();
            const bool on = ed.cards[(size_t) v].onValue != nullptr && ed.cards[(size_t) v].onValue->load() > 0.5f
                            && ed.proc.apvts.getRawParameterValue (ids::harmonyOn)->load() > 0.5f;
            g.setColour (juce::Colour (0xfff6f8f8).withAlpha (on ? 1.0f : 0.5f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (T.cardLine);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (on ? T.ink : T.muted);
            g.setFont (f.ui (700, 11.5f));
            g.drawText ("VOICE " + juce::String (v + 1), juce::Rectangle<float> (r.getX() + 30.0f, r.getY() + 8.0f, 60.0f, 18.0f), juce::Justification::centredLeft);
            g.setColour (ui::voiceColour (v).darker (0.35f));
            g.fillRoundedRectangle (r.getRight() - 24.0f, r.getY() + 15.0f, 14.0f, 3.0f, 1.5f);
        }
        g.setColour (T.muted);
        g.setFont (f.ui (500, 12.0f));
        int onCount = 0;
        for (auto& c : ed.cards) onCount += c.onValue != nullptr && c.onValue->load() > 0.5f;
        g.drawText (juce::String (onCount) + " of 16 voices on", juce::Rectangle<float> ((float) lowerR.getRight() - 190.0f, (float) lowerR.getY() + 12.0f, 150.0f, 20.0f),
                    juce::Justification::centredRight);
    }
    else
    {
        for (int m = 0; m < 6; ++m)
        {
            const auto r = moduleR (m).toFloat();
            g.setColour (juce::Colour (0xfff6f8f8));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (T.cardLine);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (T.ink);
            g.setFont (f.ui (700, 11.0f));
            g.drawText (moduleNames[m], juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 4.0f, 180.0f, 16.0f), juce::Justification::centredLeft);
        }
        g.setColour (T.muted);
        g.setFont (f.ui (500, 11.0f));
        g.drawText ("RATIO", moduleR (2).toFloat().withTrimmedLeft (10.0f).withY ((float) moduleR (2).getY() + 22.0f).withHeight (16.0f),
                    juce::Justification::centredLeft);
        g.drawFittedText ("Effects run in this order: tone, dynamics, tube, mutate, delay, reverb, width.",
                          moduleR (5).withTrimmedLeft (74).withTrimmedRight (8).withTrimmedTop (28).withTrimmedBottom (8), juce::Justification::topLeft, 4);
    }

    // retune readout beside the big knob
    {
        const float v = ed.proc.apvts.getRawParameterValue (ids::retune)->load();
        const float x = (float) tuneR.getX() + 150.0f, y = (float) tuneR.getY() + 64.0f;
        g.setColour (T.muted);
        g.setFont (f.ui (600, 11.0f));
        g.drawText ("RETUNE SPEED", juce::Rectangle<float> (x, y, 160.0f, 16.0f), juce::Justification::centredLeft);
        g.setColour (T.ink);
        g.setFont (f.ui (600, 34.0f));
        g.drawText (juce::String (juce::roundToInt (v)) + " ms", juce::Rectangle<float> (x, y + 16.0f, 160.0f, 40.0f), juce::Justification::centredLeft);
        g.setColour (T.muted);
        g.setFont (f.ui (500, 12.5f));
        g.drawText (v < 1.0f ? "instant: hard tune" : v < 60.0f ? "fast, natural" : "slow, gentle",
                    juce::Rectangle<float> (x, y + 56.0f, 160.0f, 18.0f), juce::Justification::centredLeft);
    }
}

void KuralEditor::Content::paintOverChildren (juce::Graphics& g)
{
    const auto& T = ui::theme();
    const auto& f = ed.fonts;

    // meters (peak, dBFS, −60 … 0)
    auto meter = [&] (juce::Rectangle<float> r, float db)
    {
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (frac > 0.0f)
        {
            auto in = r.reduced (2.0f);
            auto lit = in.withTrimmedTop (in.getHeight() * (1.0f - frac));
            juce::ColourGradient grad (juce::Colour (0xffe9f6fa), 0, in.getY(), T.led, 0, in.getBottom(), false);
            if (db > -1.0f) grad = juce::ColourGradient (T.warn, 0, in.getY(), T.warn, 0, in.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (lit, 2.0f);
        }
        g.setColour (T.muted);
        g.setFont (f.ui (500, 10.0f));
        g.drawText (db <= -119.0f ? juce::String ("-inf") : juce::String (juce::roundToInt (db)),
                    juce::Rectangle<float> (r.getCentreX() - 18.0f, r.getBottom() + 4.0f, 36.0f, 14.0f), juce::Justification::centred);
    };
    meter (inMeterR, ed.inMeterDb);
    meter (outMeterR, ed.outMeterDb);

    // status bar: facts read from the processor
    const int lat = ed.proc.getLatencySamples();
    const double sr = ed.proc.currentSampleRate();
    g.setColour (T.muted);
    g.setFont (f.ui (500, 12.0f));
    const auto s = juce::String (sr / 1000.0, 1) + " kHz     latency " + juce::String (1000.0 * lat / sr, 1) + " ms (" + juce::String (lat)
                   + " samples)     CPU " + juce::String (100.0f * ed.proc.cpuLoad.load (std::memory_order_relaxed), 1) + " %";
    g.drawText (s, statusR.toFloat().withTrimmedLeft (16.0f), juce::Justification::centredLeft);
    g.drawText ("v" KURAL_VERSION_STRING, statusR.toFloat().withTrimmedRight (16.0f), juce::Justification::centredRight);
}

// ===================================================================================== throat pad
KuralEditor::ThroatPad::ThroatPad (KuralProcessor& p, const ui::Fonts& f) : fonts (f)
{
    pitch = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (ids::leadPitch));
    throat = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (ids::leadFormant));
    pitchAtt = std::make_unique<juce::ParameterAttachment> (*pitch, [this] (float v) { pitchV = v; repaint(); });
    throatAtt = std::make_unique<juce::ParameterAttachment> (*throat, [this] (float v) { throatV = v; repaint(); });
    pitchAtt->sendInitialUpdate();
    throatAtt->sendInitialUpdate();
}

void KuralEditor::ThroatPad::paint (juce::Graphics& g)
{
    const auto& T = ui::theme();
    const auto r = getLocalBounds().toFloat();
    g.setColour (T.well);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (T.aqua.withAlpha (0.15f));
    g.fillRect (r.getCentreX(), r.getY() + 6.0f, 1.0f, r.getHeight() - 12.0f);
    g.fillRect (r.getX() + 6.0f, r.getCentreY(), r.getWidth() - 12.0f, 1.0f);
    // the pad shows ±12 st both ways; values beyond sit at the edge
    const float x = r.getCentreX() + juce::jlimit (-1.0f, 1.0f, throatV / 12.0f) * (r.getWidth() * 0.5f - 10.0f);
    const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, pitchV / 12.0f) * (r.getHeight() * 0.5f - 10.0f);
    for (auto [rad, a] : { std::pair { 22.0f, 0.22f }, std::pair { 12.0f, 0.45f } })
    {
        g.setColour (T.aqua.withAlpha (a));
        g.drawEllipse (x - rad, y - rad, 2.0f * rad, 2.0f * rad, 1.0f);
    }
    g.setColour (T.pearl);
    g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
    g.setColour (T.wellText);
    g.setFont (fonts.ui (600, 9.5f));
    g.drawText ("PITCH", r.reduced (6.0f, 4.0f), juce::Justification::topLeft);
    g.drawText ("THROAT", r.reduced (6.0f, 4.0f), juce::Justification::bottomRight);
}

void KuralEditor::ThroatPad::setFrom (juce::Point<float> p)
{
    const auto r = getLocalBounds().toFloat();
    const float tx = juce::jlimit (-1.0f, 1.0f, (p.x - r.getCentreX()) / (r.getWidth() * 0.5f - 10.0f)) * 12.0f;
    const float ty = juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - p.y) / (r.getHeight() * 0.5f - 10.0f)) * 12.0f;
    throatAtt->setValueAsPartOfGesture (std::round (tx * 10.0f) / 10.0f);
    pitchAtt->setValueAsPartOfGesture (std::round (ty * 4.0f) / 4.0f);     // quarter-tone steps while dragging
}

void KuralEditor::ThroatPad::mouseDown (const juce::MouseEvent& e)
{
    pitchAtt->beginGesture();
    throatAtt->beginGesture();
    setFrom (e.position);
}
void KuralEditor::ThroatPad::mouseDrag (const juce::MouseEvent& e) { setFrom (e.position); }
void KuralEditor::ThroatPad::mouseUp (const juce::MouseEvent&) { pitchAtt->endGesture(); throatAtt->endGesture(); }
void KuralEditor::ThroatPad::mouseDoubleClick (const juce::MouseEvent&)
{
    pitchAtt->setValueAsCompleteGesture (0.0f);
    throatAtt->setValueAsCompleteGesture (0.0f);
}

// ========================================================================================== look
KuralEditor::Look::Look (const ui::Fonts& f) : fonts (f)
{
    const auto& T = ui::theme();
    setColour (juce::PopupMenu::backgroundColourId, T.panel);
    setColour (juce::PopupMenu::textColourId, T.ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, T.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, T.pearl);
    setColour (juce::ComboBox::textColourId, T.ink);
    setColour (juce::ComboBox::arrowColourId, T.muted);
    setColour (juce::TooltipWindow::textColourId, T.ink);
    setColour (juce::AlertWindow::backgroundColourId, T.panel);
    setColour (juce::AlertWindow::textColourId, T.ink);
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    setColour (juce::TextEditor::textColourId, T.ink);
    setColour (juce::TextEditor::outlineColourId, T.panelLine);
    setColour (juce::TextButton::buttonColourId, T.panel);
    setColour (juce::TextButton::textColourOffId, T.ink);
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
    const juce::String size = s.getProperties()["size"].toString();
    const bool big = size == "big", mini = size == "mini";
    const float textH = big ? 0.0f : mini ? 22.0f : 30.0f;
    float d = (float) juce::jmin (big ? h : w, h - (int) textH);
    const auto area = juce::Rectangle<float> (big ? (float) x : (float) x + 0.5f * ((float) w - d), (float) y, d, d).reduced (big ? 4.0f : 2.0f);
    const float r = area.getWidth() * 0.5f;
    const auto c = area.getCentre();
    const float stroke = big ? 5.0f : mini ? 3.0f : 4.0f;

    // track and value arc, growing from the neutral point (0 dB, centre, 0 st) when it is inside the range
    const auto& range = s.getNormalisableRange();
    const double neutral = range.start < 0.0 && range.end > 0.0 ? 0.0 : range.start;
    const float zeroPos = (float) range.convertTo0to1 (juce::jlimit (range.start, range.end, neutral));
    const float aZero = a0 + zeroPos * (a1 - a0), aVal = a0 + pos * (a1 - a0);
    juce::Path track, arc;
    const float ar = r - stroke * 0.5f;
    track.addCentredArc (c.x, c.y, ar, ar, 0.0f, a0, a1, true);
    arc.addCentredArc (c.x, c.y, ar, ar, 0.0f, juce::jmin (aZero, aVal), juce::jmax (aZero, aVal), true);
    g.setColour (T.knobTrack);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (T.accent);
    g.strokePath (arc, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // the face: sea glass — a pale disc, a lighter top, a glint
    const float br = r - stroke - (big ? 6.0f : 3.0f);
    g.setColour (T.knobFace.darker (0.06f));
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (T.knobFace.brighter (0.25f));
    g.fillEllipse (c.x - br * 0.86f, c.y - br * 0.92f, br * 1.72f, br * 1.72f);
    if (big)
    {
        juce::Path glint;
        glint.addCentredArc (c.x, c.y, br * 0.78f, br * 0.78f, 0.0f, -2.2f, -1.1f, true);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.strokePath (glint, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (T.ink);
    g.drawLine ({ c.getPointOnCircumference (br * 0.25f, aVal), c.getPointOnCircumference (br * 0.9f, aVal) }, big ? 4.0f : mini ? 2.2f : 3.0f);

    if (! big)
    {
        const float ty = area.getBottom() + (mini ? 1.0f : 3.0f);
        g.setColour (T.muted);
        g.setFont (fonts.ui (600, mini ? 9.5f : 11.0f));
        g.drawText (s.getProperties()["label"].toString(), juce::Rectangle<float> ((float) x - 8.0f, ty, (float) w + 16.0f, mini ? 10.0f : 13.0f),
                    juce::Justification::centred);
        g.setColour (T.ink);
        g.setFont (fonts.ui (600, mini ? 10.5f : 12.5f));
        g.drawText (s.getTextFromValue (s.getValue()), juce::Rectangle<float> ((float) x - 8.0f, ty + (mini ? 10.0f : 13.0f), (float) w + 16.0f, mini ? 11.0f : 15.0f),
                    juce::Justification::centred);
    }
}

void KuralEditor::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto& T = ui::theme();
    const auto style = b.getProperties()["style"].toString();
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    if (style == "led")
    {
        const float d = juce::jmin (r.getWidth(), r.getHeight()) - 2.0f;
        const auto c = r.getCentre();
        if (on)
        {
            g.setColour (T.led.withAlpha (0.3f));
            g.fillEllipse (c.x - d * 0.5f - 2.0f, c.y - d * 0.5f - 2.0f, d + 4.0f, d + 4.0f);
            g.setColour (T.led);
            g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
        }
        else
        {
            g.setColour (T.muted.withAlpha (over ? 0.9f : 0.6f));
            g.drawEllipse (c.x - d * 0.5f + 0.75f, c.y - d * 0.5f + 0.75f, d - 1.5f, d - 1.5f, 1.5f);
        }
        return;
    }
    if (style == "well")
    {
        // the preset browser sits in a small deep-water well
        const bool name = b.getButtonText().length() > 2;
        g.setColour (T.well.brighter (over ? 0.12f : 0.0f));
        if (name) g.fillRect (r);
        else
        {
            juce::Path p;
            const bool left = b.getButtonText() == juce::String::fromUTF8 ("\xe2\x97\x80");
            p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 6.0f, 6.0f, left, ! left, left, ! left);
            g.fillPath (p);
        }
        return;
    }
    const bool dark = style == "tab" || style == "segment" || style == "chip";
    juce::ignoreUnused (dark);
    g.setColour (on ? T.accent : juce::Colour (0xfff6f8f8).brighter (over ? 0.0f : 0.0f).overlaidWith (T.accent.withAlpha (over ? 0.06f : 0.0f)));
    g.fillRoundedRectangle (r, 5.0f);
    if (! on)
    {
        g.setColour (T.cardLine.darker (down ? 0.2f : 0.0f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }
}

void KuralEditor::Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto& T = ui::theme();
    const auto style = b.getProperties()["style"].toString();
    if (style == "led") return;
    if (style == "well")
    {
        const auto t = b.getButtonText();
        if (t.length() <= 2)
        {
            g.setColour (T.aqua);
            g.setFont (fonts.ui (500, 12.0f));
            g.drawText (t, b.getLocalBounds(), juce::Justification::centred);
            return;
        }
        auto r = b.getLocalBounds().toFloat().reduced (8.0f, 0.0f);
        g.setColour (T.pearl);
        g.setFont (fonts.ui (600, 15.0f));
        const float tw = juce::jmin (r.getWidth() - 60.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 4.0f);
        g.drawText (t, r.removeFromLeft (tw), juce::Justification::centredLeft, true);
        g.setColour (T.wellText);
        g.setFont (fonts.ui (600, 11.0f));
        g.drawText (b.getProperties()["category"].toString().toUpperCase(), r.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);
        return;
    }
    g.setColour (b.getToggleState() ? T.pearl : T.ink);
    g.setFont (fonts.ui (style == "tab" ? 700 : 600, style == "tab" ? 12.0f : 13.0f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void KuralEditor::Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& T = ui::theme();
    const bool dark = (bool) box.getProperties()["dark"];
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (dark ? T.well.brighter (box.isMouseOver (true) ? 0.12f : 0.0f) : juce::Colour (0xfff6f8f8));
    g.fillRoundedRectangle (r, 5.0f);
    if (! dark)
    {
        g.setColour (T.cardLine.darker (box.isMouseOver (true) ? 0.15f : 0.0f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }
    juce::Path arrow;
    const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 1.5f, ax + 3.5f, ay - 1.5f, ax, ay + 2.5f);
    g.setColour (dark ? T.aqua : T.muted);
    g.fillPath (arrow);
    box.setColour (juce::ComboBox::textColourId, dark ? T.pearl : T.ink);
}

void KuralEditor::Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 20, box.getHeight() - 2);
    label.setFont (fonts.ui (500, box.getHeight() < 24 ? 12.5f : 14.0f));
    label.setColour (juce::Label::textColourId, (bool) box.getProperties()["dark"] ? ui::theme().pearl : ui::theme().ink);
}

void KuralEditor::Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto& T = ui::theme();
    g.fillAll (T.panel);
    g.setColour (T.panelLine);
    g.drawRect (0, 0, w, h, 1);
}

void KuralEditor::Look::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto& T = ui::theme();
    g.fillAll (T.panel);
    g.setColour (T.panelLine);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (T.ink);
    g.setFont (fonts.ui (500, 14.0f));
    g.drawFittedText (text, juce::Rectangle<int> (8, 4, w - 16, h - 8), juce::Justification::centredLeft, 4);
}

void KuralEditor::Look::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    g.setColour (ui::theme().muted.withAlpha (over || dragging ? 0.8f : 0.4f));
    for (int i = 1; i <= 2; ++i)
    {
        const float d = (float) i * 4.0f;
        g.drawLine ((float) w - d, (float) h - 1.5f, (float) w - 1.5f, (float) h - d, 1.0f);
    }
}

juce::AudioProcessorEditor* KuralProcessor::createEditor()
{
    return new KuralEditor (*this);
}
