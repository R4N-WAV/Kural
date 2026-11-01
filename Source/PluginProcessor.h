#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "DSP/PitchDetector.h"
#include "Params.h"

// Kural audio processor.
//
// Current build (Phases 1–2): the dry vocal passes through with output gain and a click-free
// bypass, while the shared pitch analyser tracks the incoming voice. The analysis results feed the
// editor's pitch display through a lock-free FIFO; later phases feed them to the harmony engine.
//
// Threading: processBlock never allocates, locks, or touches files/UI. prepareToPlay does all
// allocation. The editor only reads atomics and pops from the FIFO (single producer/single consumer).
class KuralProcessor : public juce::AudioProcessor
{
public:
    KuralProcessor();
    ~KuralProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (kural::ids::bypass); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kural"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- analysis feed for the editor (audio thread writes, message thread reads)
    struct AnalysisFrame
    {
        double timeSec = 0.0;     // position of the frame centre, seconds since playback was prepared
        float f0Hz = 0.0f, confidence = 0.0f, rmsDb = -120.0f;
        kural::dsp::Voicing voicing = kural::dsp::Voicing::silence;
    };
    int popAnalysis (AnalysisFrame* dest, int maxFrames);     // message thread
    double currentSampleRate() const { return fs; }
    double analysisClockSec() const { return clockSec.load (std::memory_order_relaxed); }
    std::atomic<float> inputPeakDb { -120.0f }, outputPeakDb { -120.0f };

private:
    void process (juce::AudioBuffer<float>&, bool bypassedByHost);
    void processChunk (juce::AudioBuffer<float>&, int start, int n, bool bypassedByHost);
    void pushFrame (const kural::dsp::PitchFrame&) noexcept;

    double fs = 48000.0;
    int maxBlock = 512;
    kural::dsp::PitchDetector pitch;
    std::vector<float> mono;
    juce::SmoothedValue<float> outGain, wetMix;   // wetMix: 1 = processed, 0 = bypassed (crossfade)

    static constexpr int fifoSize = 2048;
    juce::AbstractFifo fifo { fifoSize };
    std::array<AnalysisFrame, fifoSize> fifoData {};
    std::atomic<double> clockSec { 0.0 };
    float inPeak = 0.0f, outPeak = 0.0f;
    int peakCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuralProcessor)
};
