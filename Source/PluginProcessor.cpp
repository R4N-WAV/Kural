#include "PluginProcessor.h"
#include "PluginEditor.h"

KuralProcessor::KuralProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KURAL", kural::createParameterLayout())
{
}

bool KuralProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    // mono → mono, mono → stereo (harmonies will be spread across the stereo field), stereo → stereo
    if (out == juce::AudioChannelSet::stereo()) return inOk;
    if (out == juce::AudioChannelSet::mono())   return in == juce::AudioChannelSet::mono();
    return false;
}

void KuralProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (1, samplesPerBlock);
    mono.assign ((size_t) maxBlock, 0.0f);
    pitch.prepare (fs);

    outGain.reset (fs, 0.03);
    wetMix.reset (fs, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (kural::ids::output)->load()));
    wetMix.setCurrentAndTargetValue (apvts.getRawParameterValue (kural::ids::bypass)->load() > 0.5f ? 0.0f : 1.0f);

    fifo.reset();
    clockSec.store (0.0);
    inPeak = outPeak = 0.0f;
    peakCount = 0;
    setLatencySamples (0);           // dry path is not delayed; analysis runs alongside it
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

    // ---- analysis input: mono sum of the input channels
    const int useIn = juce::jmin (inCh, chans);
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
    pitch.process (m, n, [this] (const kural::dsp::PitchFrame& f) { pushFrame (f); });

    // ---- mono input on a stereo output: the voice goes to both sides
    if (useIn == 1 && outCh > 1 && chans > 1)
        buffer.copyFrom (1, start, buffer, 0, start, n);

    // ---- output gain with click-free bypass. Latency is zero, so dry and processed are aligned.
    const bool bypass = bypassedByHost || apvts.getRawParameterValue (kural::ids::bypass)->load() > 0.5f;
    outGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (kural::ids::output)->load()));
    wetMix.setTargetValue (bypass ? 0.0f : 1.0f);
    const int useOut = juce::jmin (outCh, chans);
    if (! outGain.isSmoothing() && ! wetMix.isSmoothing())
    {
        const float g = 1.0f + (outGain.getTargetValue() - 1.0f) * wetMix.getTargetValue();
        if (std::abs (g - 1.0f) > 1.0e-9f)
            for (int c = 0; c < useOut; ++c) buffer.applyGain (c, start, n, g);
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = 1.0f + (outGain.getNextValue() - 1.0f) * wetMix.getNextValue();
            for (int c = 0; c < useOut; ++c) buffer.getWritePointer (c, start)[i] *= g;
        }
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
