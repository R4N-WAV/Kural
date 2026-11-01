// Robustness checks for the Kural processor (run in CI on Windows).
// usage: KuralStress [quick]
// Every check prints one line; the program exits non-zero if any check fails.
//
// Includes a real-time safety check: the global allocator is hooked and processBlock must not
// allocate or free memory, at any sample rate, block size or channel layout.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <algorithm>

// ---------------------------------------------------------------- allocation tracking
// counted only on the thread that runs processBlock (JUCE's own timer/message threads may allocate
// at the same time; that is normal and not what this checks)
namespace
{
thread_local bool trackAllocs = false;
std::atomic<long> allocsInAudio { 0 };
}
void* operator new (std::size_t n)
{
    if (trackAllocs) allocsInAudio.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    if (trackAllocs) allocsInAudio.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (n == 0 ? 1 : n);
}
void* operator new[] (std::size_t n, const std::nothrow_t& t) noexcept { return operator new (n, t); }
void operator delete (void* p) noexcept
{
    if (p != nullptr && trackAllocs) allocsInAudio.fetch_add (1, std::memory_order_relaxed);
    std::free (p);
}
void operator delete[] (void* p) noexcept { operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { operator delete (p); }

namespace
{
int failures = 0;
void report (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

void setParam (KuralProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

bool setLayout (KuralProcessor& p, int in, int out)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (in == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    l.outputBuses.add (out == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    return p.setBusesLayout (l);
}

struct Signal
{
    enum Kind { vocal, silence, dc, impulse, nyquist, loud, tiny } kind;
    double fs;
    juce::Random rng { 11 };
    double ph = 0;
    long long t = 0;
    float next()
    {
        const long long i = t++;
        switch (kind)
        {
            case vocal:
            {
                // a sung line: harmonic tone with vibrato, gliding between notes, with breath gaps
                const double sec = (double) i / fs;
                const double f = 196.0 * std::pow (2.0, (std::floor (std::fmod (sec, 2.0) * 2.0) * 2.0) / 12.0)
                                 * std::pow (2.0, 0.4 / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * sec));
                ph += 2.0 * juce::MathConstants<double>::pi * f / fs;
                const bool breath = std::fmod (sec, 1.0) > 0.85;
                const double tone = std::sin (ph) + 0.5 * std::sin (2 * ph) + 0.25 * std::sin (3 * ph);
                return (float) (breath ? 0.02 * (rng.nextFloat() * 2.0 - 1.0) : 0.25 * tone);
            }
            case silence: return 0.0f;
            case dc:      return 0.5f;
            case impulse: return i % 24000 == 0 ? 1.0f : 0.0f;
            case nyquist: return (i & 1) ? 1.0f : -1.0f;
            case loud:    return (float) (8.0 * std::sin ((double) i * 0.05));     // +18 dBFS
            case tiny:    return (float) (1.0e-30 * std::sin ((double) i * 0.05)); // denormal range
        }
        return 0.0f;
    }
};

struct Stats { bool finite = true; float peak = 0.0f; long long n = 0; long allocs = 0; };

Stats run (KuralProcessor& p, Signal sig, double seconds, int maxBlock, bool randomBlocks, int inCh, int outCh,
           std::function<void (int)> perBlock = {}, std::vector<float>* captureL = nullptr)
{
    Stats st;
    juce::Random rb (5);
    const int chans = juce::jmax (inCh, outCh);
    juce::AudioBuffer<float> buf (chans, maxBlock * 2 + 1);
    juce::MidiBuffer midi;
    midi.ensureSize (256);
    const long long total = (long long) (seconds * sig.fs);
    int block = 0;
    for (long long done = 0; done < total; ++block)
    {
        int n = randomBlocks ? 1 + rb.nextInt (maxBlock * 2) : maxBlock;       // also larger than announced
        n = (int) juce::jmin ((long long) n, total - done);
        if (perBlock) perBlock (block);
        buf.setSize (chans, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const float v = sig.next();
            for (int c = 0; c < chans; ++c) buf.setSample (c, i, c < inCh ? v : 0.0f);
        }
        midi.clear();
        if (block % 7 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        allocsInAudio.store (0);
        trackAllocs = true;
        p.processBlock (buf, midi);
        trackAllocs = false;
        st.allocs += allocsInAudio.load();
        for (int c = 0; c < outCh; ++c)
        {
            const float* d = buf.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite (d[i])) st.finite = false;
                st.peak = juce::jmax (st.peak, std::abs (d[i]));
            }
        }
        if (captureL != nullptr)
            for (int i = 0; i < n; ++i) captureL->push_back (buf.getSample (0, i));
        done += n;
        st.n += n;
    }
    return st;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const bool quick = argc > 1 && juce::String (argv[1]) == "quick";
    const double secs = quick ? 1.5 : 6.0;

    // 1. every sample rate × block-size pattern × layout: finite output, no allocation, frames produced
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int blk : { 1, 64, 512, 4096 })
            for (auto [inCh, outCh] : { std::pair { 1, 1 }, std::pair { 1, 2 }, std::pair { 2, 2 } })
            {
                if (blk == 1 && fs > 96000.0 && quick) continue;
                KuralProcessor p;
                const bool ok = setLayout (p, inCh, outCh);
                p.prepareToPlay (fs, blk);
                auto st = run (p, { Signal::vocal, fs }, blk == 1 ? 0.5 : secs, blk, blk > 1, inCh, outCh);
                std::vector<KuralProcessor::AnalysisFrame> fr (2048);
                int total = 0, voiced = 0, k;
                while ((k = p.popAnalysis (fr.data(), (int) fr.size())) > 0)
                    for (int i = 0; i < k; ++i) { ++total; voiced += fr[(size_t) i].voicing == kural::dsp::Voicing::voiced; }
                report (ok && st.finite && st.allocs == 0 && st.peak < 2.0f && total > 0 && voiced > total / 3,
                        juce::String::formatted ("%6.1f kHz, block %4d %s, %d->%d ch: finite, %ld allocs on audio thread, %d frames (%d voiced)",
                                                 fs / 1000.0, blk, blk > 1 ? "(random)" : "        ", inCh, outCh, st.allocs, total, voiced));
            }

    // 2. hostile signals stay finite; denormal-range input does not explode
    {
        const char* names[] = { "silence", "dc", "impulses", "nyquist", "+18 dBFS", "denormal-range" };
        Signal::Kind kinds[] = { Signal::silence, Signal::dc, Signal::impulse, Signal::nyquist, Signal::loud, Signal::tiny };
        for (int i = 0; i < 6; ++i)
        {
            KuralProcessor p;
            p.prepareToPlay (48000.0, 256);
            auto st = run (p, { kinds[i], 48000.0 }, secs, 256, true, 2, 2);
            report (st.finite && st.allocs == 0, juce::String ("hostile input: ") + names[i]
                                                     + juce::String::formatted (": finite %s, %ld allocs, peak %.2f", st.finite ? "yes" : "NO", st.allocs, st.peak));
        }
    }

    // 3. automation every block (output gain and bypass) never produces a step > the smoothing allows
    {
        KuralProcessor p;
        p.prepareToPlay (48000.0, 128);
        juce::Random r (3);
        std::vector<float> out;
        Signal s { Signal::vocal, 48000.0 };
        auto st = run (p, s, secs, 128, false, 2, 2, [&] (int b)
        {
            setParam (p, kural::ids::output, -24.0f + 36.0f * r.nextFloat());
            if (b % 37 == 0) setParam (p, kural::ids::bypass, (b / 37) % 2 == 0 ? 1.0f : 0.0f);
        }, &out);
        // largest sample-to-sample jump vs. the input's own: smoothing keeps it bounded
        float maxJump = 0.0f;
        for (size_t i = 1; i < out.size(); ++i) maxJump = juce::jmax (maxJump, std::abs (out[i] - out[i - 1]));
        report (st.finite && st.allocs == 0 && maxJump < 0.5f,
                juce::String::formatted ("automation every block: finite, no allocation, largest step %.3f", maxJump));
    }

    // 4. bypass, and harmonies off at 0 dB, give exactly the input delayed by the reported latency
    for (int mode = 0; mode < 2; ++mode)
    {
        KuralProcessor p;
        if (mode == 0) setParam (p, kural::ids::bypass, 1.0f); else setParam (p, kural::ids::harmonyOn, 0.0f);
        p.prepareToPlay (48000.0, 512);          // start settled (no crossfade in progress)
        const int lat = p.getLatencySamples();
        Signal a { Signal::vocal, 48000.0 }, b { Signal::vocal, 48000.0 };
        std::vector<float> out;
        run (p, a, 1.0, 512, true, 2, 2, {}, &out);
        std::vector<float> in;
        for (size_t i = 0; i < out.size(); ++i) in.push_back (b.next());
        bool exact = lat > 0;
        for (size_t i = 0; i < out.size() && exact; ++i) exact = out[i] == (i >= (size_t) lat ? in[i - (size_t) lat] : 0.0f);
        report (exact, juce::String::formatted (mode == 0 ? "parameter bypass = input delayed by the reported latency (%d samples), bit-exact"
                                                          : "harmonies off, dry 0 dB = input delayed by the latency (%d samples), bit-exact", lat));
    }
    {
        // host bypass crossfades (20 ms) from the processed signal to the delayed dry one, so it never
        // clicks; once the fade has finished the output must be the delayed input, bit for bit
        KuralProcessor p;
        setParam (p, kural::ids::output, -12.0f);
        p.prepareToPlay (48000.0, 512);
        const int lat = p.getLatencySamples();
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        Signal a { Signal::vocal, 48000.0 };
        std::vector<float> in;
        bool exact = true;
        float firstBlockMaxJump = 0.0f, prev = 0.0f;
        for (int blk = 0; blk < 12; ++blk)
        {
            for (int i = 0; i < 512; ++i) { const float v = a.next(); in.push_back (v); buf.setSample (0, i, v); buf.setSample (1, i, v); }
            p.processBlockBypassed (buf, midi);
            for (int i = 0; i < 512; ++i)
            {
                if (blk == 0) firstBlockMaxJump = juce::jmax (firstBlockMaxJump, std::abs (buf.getSample (0, i) - prev));
                prev = buf.getSample (0, i);
                const size_t t = (size_t) (blk * 512 + i);
                if (blk >= 3) exact = exact && buf.getSample (0, i) == in[t - (size_t) lat];   // 3 blocks = 32 ms > 20 ms fade
            }
        }
        report (exact && firstBlockMaxJump < 0.25f,
                juce::String::formatted ("host bypass: crossfades without a jump (largest step %.3f), then the delayed input bit-exact", firstBlockMaxJump));
    }

    // 5. output gain does what it says (-6 dB → ×0.501), harmonies off
    {
        KuralProcessor p;
        setParam (p, kural::ids::output, -6.0f);
        setParam (p, kural::ids::harmonyOn, 0.0f);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 4096);
        juce::MidiBuffer midi;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 4096; ++i) buf.setSample (c, i, 0.5f);
        p.processBlock (buf, midi);
        const float g = buf.getSample (0, 4095) / 0.5f;
        report (std::abs (g - juce::Decibels::decibelsToGain (-6.0f)) < 1e-4f, juce::String::formatted ("output -6 dB measured as x%.4f", g));
    }

    // 6. mono input on a stereo output reaches both sides
    {
        KuralProcessor p;
        setLayout (p, 1, 2);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 4096);
        juce::MidiBuffer midi;
        for (int i = 0; i < 4096; ++i) { buf.setSample (0, i, 0.3f); buf.setSample (1, i, 0.0f); }
        p.processBlock (buf, midi);
        report (std::abs (buf.getSample (1, 4095) - buf.getSample (0, 4095)) < 1e-6f && buf.getSample (1, 4095) != 0.0f,
                "mono in, stereo out: both channels carry the voice");
    }

    // 6b. the harmony is the right note: a sung C4 with only voice 1 (third above) → E4 in C major,
    //     Eb4 in C minor; dry off, so the output holds the harmony alone
    for (int scale = 0; scale < 2; ++scale)
    {
        KuralProcessor p;
        setParam (p, kural::ids::dryLevel, -60.0f);
        setParam (p, kural::ids::scale, (float) scale);            // 0 Major, 1 Natural minor
        setParam (p, kural::ids::voiceId (1, "on").toRawUTF8(), 0.0f);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        std::vector<float> out;
        double ph = 0.0;
        for (int blk = 0; blk < 48000 * 2 / 256; ++blk)
        {
            for (int i = 0; i < 256; ++i)
            {
                ph += 2.0 * juce::MathConstants<double>::pi * 261.63 / 48000.0;
                const float v = (float) (0.2 * (std::sin (ph) + 0.5 * std::sin (2 * ph) + 0.3 * std::sin (3 * ph)));
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < 256; ++i) out.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
        }
        kural::dsp::PitchDetector det;
        det.prepare (48000.0);
        std::vector<float> f0s;
        det.process (out.data(), (int) out.size(), [&] (const kural::dsp::PitchFrame& f)
                     { if (f.voicing == kural::dsp::Voicing::voiced && f.centreSample > 48000) f0s.push_back (f.f0Hz); });
        std::sort (f0s.begin(), f0s.end());
        const float med = f0s.empty() ? 0.0f : f0s[f0s.size() / 2];
        const float expect = scale == 0 ? 329.63f : 311.13f;
        report (! f0s.empty() && std::abs (1200.0f * std::log2 (med / expect)) < 10.0f,
                juce::String::formatted ("sung C4, voice 1 third above in C %s: harmony at %.1f Hz (expected %.1f)", scale == 0 ? "major" : "minor", med, expect));
    }

    // 6c. pitch correction: a tone sung 40 cents sharp of C4, tune on (C major, retune 0), harmonies
    //     off → the output is C4; and with the lead pitch automated every block (random sizes), no
    //     allocation and finite output
    {
        KuralProcessor p;
        setParam (p, kural::ids::harmonyOn, 0.0f);
        setParam (p, kural::ids::tuneOn, 1.0f);
        setParam (p, kural::ids::retune, 0.0f);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        std::vector<float> out;
        double ph = 0.0;
        const double f0 = 261.63 * std::pow (2.0, 40.0 / 1200.0);
        for (int blk = 0; blk < 48000 * 2 / 256; ++blk)
        {
            for (int i = 0; i < 256; ++i)
            {
                ph += 2.0 * juce::MathConstants<double>::pi * f0 / 48000.0;
                const float v = (float) (0.2 * (std::sin (ph) + 0.5 * std::sin (2 * ph) + 0.3 * std::sin (3 * ph)));
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < 256; ++i) out.push_back (buf.getSample (0, i));
        }
        kural::dsp::PitchDetector det;
        det.prepare (48000.0);
        std::vector<float> f0s;
        det.process (out.data(), (int) out.size(), [&] (const kural::dsp::PitchFrame& f)
                     { if (f.voicing == kural::dsp::Voicing::voiced && f.centreSample > 48000) f0s.push_back (f.f0Hz); });
        std::sort (f0s.begin(), f0s.end());
        const float med = f0s.empty() ? 0.0f : f0s[f0s.size() / 2];
        report (! f0s.empty() && std::abs (1200.0f * std::log2 (med / 261.63f)) < 5.0f,
                juce::String::formatted ("tune on: C4 sung 40 c sharp comes out at %.2f Hz (expected 261.63)", med));

        KuralProcessor q;
        setParam (q, kural::ids::tuneOn, 1.0f);
        q.prepareToPlay (48000.0, 512);
        juce::Random r (11);
        auto st = run (q, { Signal::vocal, 48000.0 }, secs, 512, true, 2, 2, [&] (int b)
        {
            setParam (q, kural::ids::leadPitch, -12.0f + 24.0f * r.nextFloat());
            if (b % 23 == 0) setParam (q, kural::ids::tuneOn, (b / 23) % 2 == 0 ? 0.0f : 1.0f);
        });
        report (st.finite && st.allocs == 0 && st.peak < 2.0f,
                juce::String::formatted ("tune toggled and lead pitch automated every block: finite, %ld allocs, peak %.2f", st.allocs, st.peak));
    }

    // 6d. everything on: tune, duet, naturalness, every effect, with delay time, reverb and drive
    //     automated every block at random sizes: finite, bounded, no allocation on the audio thread
    for (double fsx : { 44100.0, 96000.0 })
    {
        KuralProcessor p;
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
                 { kural::ids::tuneOn, 1.0f }, { kural::ids::duetOn, 1.0f }, { kural::ids::naturalness, 100.0f },
                 { kural::ids::eqLowCut, 150.0f }, { kural::ids::eqPresence, 6.0f }, { kural::ids::eqAir, 6.0f },
                 { kural::ids::compAmount, 80.0f }, { kural::ids::tubeDrive, 70.0f }, { kural::ids::mutateOn, 1.0f },
                 { kural::ids::delayMix, 40.0f }, { kural::ids::delayFeedback, 90.0f }, { kural::ids::delaySync, 0.0f },
                 { kural::ids::reverbMix, 50.0f }, { kural::ids::reverbDecay, 100.0f }, { kural::ids::width, 180.0f },
                 { kural::ids::voiceId (2, "on").toRawUTF8(), 1.0f }, { kural::ids::voiceId (3, "on").toRawUTF8(), 1.0f } })
            setParam (p, id, v);
        p.prepareToPlay (fsx, 512);
        juce::Random r (17);
        auto st = run (p, { Signal::vocal, fsx }, secs, 512, true, 2, 2, [&] (int)
        {
            setParam (p, kural::ids::delayTime, 10.0f + 1990.0f * r.nextFloat());
            setParam (p, kural::ids::reverbSize, 100.0f * r.nextFloat());
            setParam (p, kural::ids::tubeDrive, 100.0f * r.nextFloat());
            setParam (p, kural::ids::eqLowCut, 20.0f + 380.0f * r.nextFloat());
        });
        report (st.finite && st.allocs == 0 && st.peak < 4.0f,
                juce::String::formatted ("%.1f kHz, every effect on and automated: finite, %ld allocs, peak %.2f", fsx / 1000.0, st.allocs, st.peak));
    }

    // 6e. presets: every factory preset loads, sets what it says, and plays (finite, no allocation);
    //     a user preset saves and loads back; A/B switching restores each side exactly
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kural-preset-test");
        tmp.deleteRecursively();
        kural::PresetManager::setUserFolderForTests (tmp);
        KuralProcessor p;
        p.prepareToPlay (48000.0, 256);
        auto& pm = p.presets;
        pm.refresh();
        int played = 0, bad = 0;
        long allocs = 0;
        for (int i = 0; i < pm.list().size(); ++i)
        {
            if (! pm.list()[i].factory) continue;
            pm.load (i);
            for (const auto& f : kural::PresetManager::factory())
                if (pm.list()[i].name == f.name)
                    for (auto [id, val] : f.values)
                        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
                            if (std::abs (r->convertFrom0to1 (r->getValue()) - val) > 0.51f) ++bad;
            auto st = run (p, { Signal::vocal, 48000.0 }, 0.4, 256, true, 2, 2);
            if (st.finite && st.peak < 4.0f) ++played;
            allocs += st.allocs;
        }
        const int nFactory = (int) kural::PresetManager::factory().size();
        // user preset round trip
        p.apvts.getParameter (kural::ids::retune)->setValueNotifyingHost (p.apvts.getParameter (kural::ids::retune)->convertTo0to1 (123.0f));
        const bool saved = pm.saveUser ("Test Tide");
        pm.load (0);
        int userIdx = -1;
        for (int i = 0; i < pm.list().size(); ++i) if (pm.list()[i].name == "Test Tide") userIdx = i;
        pm.load (userIdx);
        const float back = p.apvts.getParameter (kural::ids::retune)->convertFrom0to1 (p.apvts.getParameter (kural::ids::retune)->getValue());
        // A/B
        pm.selectSlot (0);
        auto* rp = p.apvts.getParameter (kural::ids::reverbMix);
        rp->setValueNotifyingHost (rp->convertTo0to1 (10.0f));
        pm.selectSlot (1);
        rp->setValueNotifyingHost (rp->convertTo0to1 (70.0f));
        pm.selectSlot (0);
        const float a = rp->convertFrom0to1 (rp->getValue());
        pm.selectSlot (1);
        const float b = rp->convertFrom0to1 (rp->getValue());
        report (played == nFactory && bad == 0 && allocs == 0 && saved && userIdx >= 0 && std::abs (back - 123.0f) < 1.0f && std::abs (a - 10.0f) < 0.6f && std::abs (b - 70.0f) < 0.6f,
                juce::String::formatted ("presets: %d/%d factory presets load as defined and play (%d mismatches, %ld allocs); user preset round trip %.0f ms (123); A/B %.0f / %.0f %% (10 / 70)",
                                         played, nFactory, bad, allocs, back, a, b));
        tmp.deleteRecursively();
    }

    // 7. state: save → restore round trip, and an older/partial state falls back to defaults
    {
        KuralProcessor a, b;
        setParam (a, kural::ids::output, -7.5f);
        setParam (a, kural::ids::bypass, 1.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        const float out = b.apvts.getRawParameterValue (kural::ids::output)->load();
        const float byp = b.apvts.getRawParameterValue (kural::ids::bypass)->load();
        report (std::abs (out + 7.5f) < 0.051f && byp > 0.5f, juce::String::formatted ("state round trip: output %.1f dB, bypass %s", out, byp > 0.5f ? "on" : "off"));

        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        bool hasVersion = xml != nullptr && xml->getIntAttribute ("stateVersion", -1) == kural::stateVersion;
        report (hasVersion, "saved state carries its state version");

        // remove the output parameter to simulate a session saved before it existed
        if (xml != nullptr)
            for (auto* c : xml->getChildIterator())
                if (c->getStringAttribute ("id") == kural::ids::output) { xml->removeChildElement (c, true); break; }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        KuralProcessor c;
        setParam (c, kural::ids::output, 5.0f);   // a stale value that must not survive
        c.setStateInformation (old.getData(), (int) old.getSize());
        const float cOut = c.apvts.getRawParameterValue (kural::ids::output)->load();
        report (std::abs (cOut) < 0.051f, juce::String::formatted ("partial (older) state: missing parameter falls back to its default (%.1f dB)", cOut));

        KuralProcessor d;
        const char junk[] = "not a state";
        d.setStateInformation (junk, (int) sizeof junk);
        report (std::abs (d.apvts.getRawParameterValue (kural::ids::output)->load()) < 0.051f, "garbage state is ignored safely");
    }

    // 8. changing sample rate and block size on a running instance
    {
        KuralProcessor p;
        bool fine = true;
        long allocs = 0;
        for (double fs : { 44100.0, 192000.0, 48000.0, 96000.0 })
            for (int blk : { 32, 2048, 480 })
            {
                p.prepareToPlay (fs, blk);
                auto st = run (p, { Signal::vocal, fs }, 0.3, blk, true, 2, 2);
                fine = fine && st.finite;
                allocs += st.allocs;
            }
        report (fine && allocs == 0, "re-prepared at changing sample rates and block sizes: finite, no allocation");
    }

    // 9. the editor opens and closes repeatedly without leaks or crashes (headless)
    {
        KuralProcessor p;
        p.prepareToPlay (48000.0, 512);
        run (p, { Signal::vocal, 48000.0 }, 1.0, 512, false, 2, 2);
        bool ok = true;
        for (int i = 0; i < 5; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            ok = ok && ed != nullptr && ed->getWidth() > 0;
            ed->setSize (ed->getWidth() * 3 / 2, ed->getHeight() * 3 / 2);
        }
        report (ok, "editor created, resized and destroyed 5 times");
    }

    std::printf ("\n%s\n", failures == 0 ? "all checks passed" : "SOME CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
