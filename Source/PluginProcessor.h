#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "DSP/Fx.h"
#include "DSP/HarmonyEngine.h"
#include "Params.h"
#include "Presets.h"

// Kural audio processor.
//
//   input ──► mono sum ──► HarmonyEngine (pitch analysis, harmony planning, PSOLA voices) ─┐
//         └─► dry, delayed by the engine latency ──────────────────────────── × dry level ─┴─► output level ─► out
//
// The latency (2 periods of 65 Hz: 31 ms at 48 kHz) is constant and reported to the host, also with
// harmonies off or bypassed, so tracks stay aligned and hosts never see the latency change.
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
    kural::PresetManager presets { apvts };   // message thread only

    // ---- analysis feed for the editor (audio thread writes, message thread reads)
    static constexpr int displayVoices = kural::dsp::HarmonyEngine::maxVoices;
    struct AnalysisFrame
    {
        double timeSec = 0.0;     // position of the frame centre, seconds since playback was prepared
        float f0Hz = 0.0f, confidence = 0.0f, rmsDb = -120.0f;
        kural::dsp::Voicing voicing = kural::dsp::Voicing::silence;
        std::uint16_t activeVoices = 0;                 // bit v = voice v sounding
        std::array<float, displayVoices> shift {};      // semitones each voice is shifted by (what is applied)
        float leadShift = 0.0f;                         // semitones the lead is moved by (tune + transpose)
        bool leadActive = false;                        // the processed lead replaces the dry vocal
    };
    int popAnalysis (AnalysisFrame* dest, int maxFrames);     // message thread
    double currentSampleRate() const { return fs; }
    double analysisClockSec() const { return clockSec.load (std::memory_order_relaxed); }
    std::atomic<float> inputPeakDb { -120.0f }, outputPeakDb { -120.0f };
    std::atomic<float> cpuLoad { 0.0f };              // share of the block's real time spent processing (smoothed)

private:
    void process (juce::AudioBuffer<float>&, bool bypassedByHost);
    void processChunk (juce::AudioBuffer<float>&, int start, int n, bool bypassedByHost);
    void pushFrame (const kural::dsp::PitchFrame&) noexcept;
    void readParameters() noexcept;

    double fs = 48000.0;
    int maxBlock = 512, latency = 0;
    kural::dsp::HarmonyEngine engine;
    std::vector<float> mono, harmL, harmR, leadBuf, fxL, fxR;
    kural::dsp::fx::Eq eq;
    kural::dsp::fx::Compressor comp;
    kural::dsp::fx::Tube tube;
    kural::dsp::fx::Mutate mutate;
    kural::dsp::fx::Delay delay;
    kural::dsp::fx::Reverb reverb;
    float widthS = 1.0f;
    double bpm = 120.0;
    std::array<std::vector<float>, 2> dryRing;
    int dryMask = 0, dryPos = 0;
    juce::SmoothedValue<float> outGain, wetMix, dryGain, leadMix;   // wetMix: 1 = processed, 0 = bypassed (crossfade)

    // cached parameter pointers (no lookups on the audio thread)
    std::atomic<float> *pOutput = nullptr, *pBypass = nullptr, *pKey = nullptr, *pScale = nullptr, *pHarmOn = nullptr,
                       *pHarmLevel = nullptr, *pDryLevel = nullptr, *pConsonants = nullptr, *pGlide = nullptr,
                       *pTuneOn = nullptr, *pRetune = nullptr, *pTuneVibrato = nullptr, *pHumanize = nullptr,
                       *pFlex = nullptr, *pLeadPitch = nullptr, *pLeadFormant = nullptr,
                       *pNatural = nullptr, *pDuetOn = nullptr, *pDuetLevel = nullptr, *pDuetSpread = nullptr,
                       *pLowCut = nullptr, *pPresence = nullptr, *pAir = nullptr, *pComp = nullptr, *pTube = nullptr,
                       *pMutOn = nullptr, *pMutRatio = nullptr, *pMutMix = nullptr, *pDlyMix = nullptr, *pDlyTime = nullptr,
                       *pDlySync = nullptr, *pDlyFb = nullptr, *pDlyPing = nullptr, *pRvbMix = nullptr, *pRvbSize = nullptr,
                       *pRvbDecay = nullptr, *pRvbDamp = nullptr, *pWidth = nullptr;
    struct VoicePtrs { std::atomic<float> *on, *mode, *interval, *semis, *octave, *cents, *formant, *level, *pan; };
    std::array<VoicePtrs, kural::registeredVoices> pVoice {};

    static constexpr int fifoSize = 2048;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<AnalysisFrame> fifoData = std::vector<AnalysisFrame> ((size_t) fifoSize);
    std::atomic<double> clockSec { 0.0 };
    float inPeak = 0.0f, outPeak = 0.0f;
    int peakCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuralProcessor)
};
