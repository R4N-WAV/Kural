#include "PluginProcessor.h"
#include "PluginEditor.h"

KuralProcessor::KuralProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KURAL", kural::createParameterLayout())
{
    using namespace kural;
    auto raw = [this] (const juce::String& id) { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; };
    pOutput = raw (ids::output);           pBypass = raw (ids::bypass);
    pKey = raw (ids::key);                 pScale = raw (ids::scale);
    pHarmOn = raw (ids::harmonyOn);        pHarmLevel = raw (ids::harmonyLevel);
    pDryLevel = raw (ids::dryLevel);       pConsonants = raw (ids::consonants);
    pGlide = raw (ids::glide);
    for (int v = 0; v < registeredVoices; ++v)
        pVoice[(size_t) v] = { raw (ids::voiceId (v, "on")), raw (ids::voiceId (v, "mode")), raw (ids::voiceId (v, "interval")),
                               raw (ids::voiceId (v, "semis")), raw (ids::voiceId (v, "octave")), raw (ids::voiceId (v, "cents")),
                               raw (ids::voiceId (v, "formant")), raw (ids::voiceId (v, "level")), raw (ids::voiceId (v, "pan")) };
}

bool KuralProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    // mono → mono, mono → stereo (harmonies spread across the stereo field), stereo → stereo
    if (out == juce::AudioChannelSet::stereo()) return inOk;
    if (out == juce::AudioChannelSet::mono())   return in == juce::AudioChannelSet::mono();
    return false;
}

void KuralProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (1, samplesPerBlock);
    mono.assign ((size_t) maxBlock, 0.0f);
    harmL.assign ((size_t) maxBlock, 0.0f);
    harmR.assign ((size_t) maxBlock, 0.0f);

    engine.prepare (fs);
    latency = engine.latency();
    int ringSize = 1;
    while (ringSize < latency + 1) ringSize <<= 1;
    for (auto& r : dryRing) r.assign ((size_t) ringSize, 0.0f);
    dryMask = ringSize - 1;
    dryPos = 0;

    outGain.reset (fs, 0.03);
    dryGain.reset (fs, 0.03);
    wetMix.reset (fs, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    dryGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pDryLevel->load(), -59.9f));
    wetMix.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 0.0f : 1.0f);
    readParameters();

    fifo.reset();
    clockSec.store (0.0);
    inPeak = outPeak = 0.0f;
    peakCount = 0;
    setLatencySamples (latency);
}

void KuralProcessor::readParameters() noexcept
{
    using namespace kural::dsp;
    HarmonyEngineParams ep;
    ep.scale = Scale::make ((int) pKey->load(), (ScaleType) juce::jlimit (0, (int) ScaleType::chromatic, (int) pScale->load()));
    ep.unvoicedGain = pConsonants->load() * 0.01f;
    ep.glideMs = pGlide->load();
    ep.harmonyGain = pHarmOn->load() > 0.5f ? juce::Decibels::decibelsToGain (pHarmLevel->load(), -59.9f) : 0.0f;
    engine.setParams (ep);
    for (int v = 0; v < kural::registeredVoices; ++v)
    {
        const auto& p = pVoice[(size_t) v];
        HarmonyVoiceParams vp;
        vp.on = p.on->load() > 0.5f && pHarmOn->load() > 0.5f;
        vp.interval.mode = p.mode->load() > 0.5f ? IntervalMode::semitones : IntervalMode::scaleSteps;
        vp.interval.steps = vp.interval.mode == IntervalMode::semitones ? (int) std::lround (p.semis->load())
                                                                        : (int) std::lround (p.interval->load()) - 6;
        vp.interval.octave = (int) std::lround (p.octave->load());
        vp.interval.cents = p.cents->load();
        vp.formantSemis = p.formant->load();
        vp.gain = juce::Decibels::decibelsToGain (p.level->load(), -59.9f);
        vp.pan = p.pan->load();
        engine.setVoice (v, vp);
    }
}

void KuralProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midi.clear();                    // MIDI input is accepted but not used yet (Phase 7)
    process (buffer, false);
}

void KuralProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midi.clear();
    process (buffer, true);
}

void KuralProcessor::process (juce::AudioBuffer<float>& buffer, bool bypassedByHost)
{
    juce::ScopedNoDenormals noDenormals;
    readParameters();
    const int n = buffer.getNumSamples();
    // a host may send more samples than announced in prepareToPlay: work in announced-size pieces
    for (int start = 0; start < n; start += maxBlock)
        processChunk (buffer, start, juce::jmin (maxBlock, n - start), bypassedByHost);
}

void KuralProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int n, bool bypassedByHost)
{
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    const int chans = buffer.getNumChannels();
    if (n <= 0 || chans == 0) return;
    const int useIn = juce::jmin (inCh, chans), useOut = juce::jmin (outCh, chans);

    // ---- mono sum for the analysis and the voices
    float* m = mono.data();
    if (useIn <= 0)
        std::fill (m, m + n, 0.0f);
    else
    {
        std::copy (buffer.getReadPointer (0, start), buffer.getReadPointer (0, start) + n, m);
        for (int c = 1; c < useIn; ++c)
        {
            const float* s = buffer.getReadPointer (c, start);
            for (int i = 0; i < n; ++i) m[i] += s[i];
        }
        if (useIn > 1)
            for (int i = 0; i < n; ++i) m[i] *= 1.0f / (float) useIn;
    }
    for (int i = 0; i < n; ++i) inPeak = juce::jmax (inPeak, std::abs (m[i]));

    // ---- harmonies (stereo, delayed by the engine latency)
    std::fill (harmL.begin(), harmL.begin() + n, 0.0f);
    std::fill (harmR.begin(), harmR.begin() + n, 0.0f);
    engine.process (m, harmL.data(), useOut > 1 ? harmR.data() : nullptr, n,
                    [this] (const kural::dsp::PitchFrame& f, const kural::dsp::HarmonyEngine&) { pushFrame (f); });

    // ---- dry, delayed by the same latency; mono input feeds both sides
    const bool bypass = bypassedByHost || pBypass->load() > 0.5f;
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    dryGain.setTargetValue (juce::Decibels::decibelsToGain (pDryLevel->load(), -59.9f));
    wetMix.setTargetValue (bypass ? 0.0f : 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const int w = dryPos;
        const int r = (dryPos - latency) & dryMask;
        float in0 = useIn > 0 ? buffer.getSample (0, start + i) : 0.0f;
        float in1 = useIn > 1 ? buffer.getSample (1, start + i) : in0;
        dryRing[0][(size_t) w] = in0;
        dryRing[1][(size_t) w] = in1;
        dryPos = (dryPos + 1) & dryMask;
        const float d0 = dryRing[0][(size_t) r], d1 = dryRing[1][(size_t) r];
        const float og = outGain.getNextValue(), dg = dryGain.getNextValue(), wm = wetMix.getNextValue();
        const float p0 = (d0 * dg + harmL[(size_t) i]) * og;
        const float p1 = (d1 * dg + harmR[(size_t) i]) * og;
        // bypassed: exactly the delayed input; switching crossfades so it never clicks
        const float o0 = wm >= 1.0f ? p0 : wm <= 0.0f ? d0 : d0 + (p0 - d0) * wm;
        const float o1 = wm >= 1.0f ? p1 : wm <= 0.0f ? d1 : d1 + (p1 - d1) * wm;
        if (useOut > 0) buffer.setSample (0, start + i, o0);
        if (useOut > 1) buffer.setSample (1, start + i, o1);
    }
    for (int c = useOut; c < chans; ++c) buffer.clear (c, start, n);

    for (int c = 0; c < useOut; ++c)
        outPeak = juce::jmax (outPeak, buffer.getMagnitude (c, start, n));

    // meters: publish roughly every 20 ms
    peakCount += n;
    if (peakCount >= (int) (fs * 0.02))
    {
        auto toDb = [] (float v) { return v > 1.0e-6f ? 20.0f * std::log10 (v) : -120.0f; };
        inputPeakDb.store (toDb (inPeak), std::memory_order_relaxed);
        outputPeakDb.store (toDb (outPeak), std::memory_order_relaxed);
        inPeak = outPeak = 0.0f;
        peakCount = 0;
    }
}

void KuralProcessor::pushFrame (const kural::dsp::PitchFrame& f) noexcept
{
    AnalysisFrame a;
    a.timeSec = (double) f.centreSample / fs;
    a.f0Hz = f.f0Hz;
    a.confidence = f.confidence;
    a.rmsDb = f.rmsDb;
    a.voicing = f.voicing;
    for (int v = 0; v < displayVoices; ++v)
    {
        a.shift[(size_t) v] = engine.voiceShift (v);
        if (engine.voiceActive (v)) a.activeVoices = (std::uint16_t) (a.activeVoices | (1u << v));
    }
    clockSec.store (a.timeSec, std::memory_order_relaxed);

    // full FIFO (editor closed or stalled): the frame is dropped, never blocks
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) fifoData[(size_t) scope.startIndex1] = a;
    else if (scope.blockSize2 > 0) fifoData[(size_t) scope.startIndex2] = a;
}

int KuralProcessor::popAnalysis (AnalysisFrame* dest, int maxFrames)
{
    const auto scope = fifo.read (juce::jmin (maxFrames, fifo.getNumReady()));
    int k = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[k++] = fifoData[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[k++] = fifoData[(size_t) (scope.startIndex2 + i)];
    return k;
}

void KuralProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", kural::stateVersion, nullptr);
    state.setProperty ("pluginVersion", KURAL_VERSION_STRING, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KuralProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            // parameters missing from an older session keep their defaults; unknown ones are ignored
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KuralProcessor();
}
