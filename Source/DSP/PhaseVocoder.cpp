#include "PhaseVocoder.h"
#include <algorithm>
#include <cmath>

namespace kural::dsp
{
namespace
{
constexpr float pi = 3.14159265358979f, twoPi = 6.28318530717959f;
float princarg (float a) { return a - twoPi * std::floor ((a + pi) / twoPi); }
int fftSizeFor (double fs)
{
    int n = 256;
    while (n < 0.040 * fs) n <<= 1;          // 2048 at 44.1/48 kHz (~43 ms): resolves harmonics of low voices
    return n;
}
}

// =================================================================================== analysis
void PvAnalysis::prepare (double sampleRate)
{
    fs = sampleRate > 1000.0 ? sampleRate : 48000.0;
    N = fftSizeFor (fs);
    H = N / 4;
    fft.prepare (N);
    ring.assign ((size_t) N, 0.0f);
    win.resize ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) N);
    const size_t B = (size_t) N / 2 + 1;
    prevPhase.assign (B, 0.0f);
    spec.assign (B, {});
    mag.assign (B, 0.0f); logEnv.assign (B, 0.0f); instBin.assign (B, 0.0f);
    peaks.reserve (B); peaks.clear();
    region.assign (B, 0);
    work.assign ((size_t) N, {});
    cep.assign ((size_t) N, {});
    filled = sinceHop = pos = 0;
}

void PvAnalysis::onPitchFrame (const PitchFrame& f) noexcept
{
    detVoicing = f.voicing;
    if (f.f0Hz > 0.0f) detF0 = f.f0Hz;
}

bool PvAnalysis::push (float x) noexcept
{
    ring[(size_t) pos] = x;
    pos = pos + 1 == N ? 0 : pos + 1;
    if (filled < N) ++filled;
    if (++sinceHop >= H && filled >= N)
    {
        sinceHop = 0;
        analyse();
        return true;
    }
    return false;
}

void PvAnalysis::analyse() noexcept
{
    for (int i = 0; i < N; ++i) work[(size_t) i] = { ring[(size_t) ((pos + i) % N)] * win[(size_t) i], 0.0f };
    fft.forward (work.data());
    const int B = N / 2 + 1;
    float maxMag = 0.0f;
    for (int k = 0; k < B; ++k)
    {
        spec[(size_t) k] = work[(size_t) k];
        mag[(size_t) k] = std::abs (work[(size_t) k]);
        maxMag = std::max (maxMag, mag[(size_t) k]);
        const float ph = std::arg (work[(size_t) k]);
        const float expected = twoPi * (float) k * (float) H / (float) N;
        const float d = princarg (ph - prevPhase[(size_t) k] - expected);
        instBin[(size_t) k] = (float) k + d * (float) N / (twoPi * (float) H);
        prevPhase[(size_t) k] = ph;
    }

    // peaks (local maxima above −80 dB re the frame maximum) and the region each bin belongs to:
    // bins between two peaks go to the nearer one, split at the lowest bin between them
    peaks.clear();
    const float floorMag = maxMag * 1.0e-4f;
    for (int k = 2; k < B - 2; ++k)
        if (mag[(size_t) k] > floorMag && mag[(size_t) k] > mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1]
            && mag[(size_t) k] > mag[(size_t) k - 2] && mag[(size_t) k] >= mag[(size_t) k + 2])
            peaks.push_back (k);
    if (peaks.empty()) peaks.push_back (1);
    size_t p = 0;
    for (int k = 0; k < B; ++k)
    {
        while (p + 1 < peaks.size() && k > peaks[p])
        {
            // lowest bin between this peak and the next decides the boundary
            int lo = peaks[p];
            for (int j = peaks[p]; j <= peaks[p + 1]; ++j) if (mag[(size_t) j] < mag[(size_t) lo]) lo = j;
            if (k > lo) ++p; else break;
        }
        region[(size_t) k] = (int) p;
    }

    voiced = detVoicing == Voicing::voiced || detVoicing == Voicing::uncertain;
    envelope();
}

void PvAnalysis::envelope() noexcept
{
    // cepstral smoothing of the log magnitude; the lifter stays below the pitch period so the
    // harmonics do not leak into the envelope
    const int B = N / 2 + 1;
    for (int k = 0; k < B; ++k) cep[(size_t) k] = { std::log (mag[(size_t) k] + 1.0e-9f), 0.0f };
    for (int k = B; k < N; ++k) cep[(size_t) k] = cep[(size_t) (N - k)];
    fft.inverse (cep.data());
    const float period = detF0 > 0.0f ? (float) (fs / detF0) : (float) (fs / 200.0);
    const int qc = std::clamp ((int) (0.6f * period), 12, (int) (0.006 * fs));
    for (int q = 0; q < N; ++q)
    {
        const int d = std::min (q, N - q);
        const float w = d < qc ? 0.5f + 0.5f * std::cos (pi * (float) d / (float) qc) : 0.0f;   // tapered lifter
        cep[(size_t) q] *= w / (float) N;
    }
    fft.forward (cep.data());
    for (int k = 0; k < B; ++k) logEnv[(size_t) k] = cep[(size_t) k].real();
}

// ===================================================================================== voice
void PvVoice::prepare (const PvAnalysis& a)
{
    N = a.fftSize();
    H = a.hop();
    fft.prepare (N);
    out.assign ((size_t) N, {});
    ola.assign ((size_t) N * 2, 0.0f);
    win.resize ((size_t) N);
    float sumSq = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        win[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) N);
        sumSq += win[(size_t) i] * win[(size_t) i];
    }
    norm = (float) H / sumSq;               // Hann analysis × Hann synthesis at hop N/4 → 1/1.5
    const size_t B = (size_t) N / 2 + 1;
    prevPeakBin.assign (B, 0.0f); prevTheta.assign (B, 0.0f); theta.assign (B, 0.0f);
    prevPeakCount = 0;
    readPos = 0;
    gainMix = 1.0f;
}

void PvVoice::synthesise (const PvAnalysis& a) noexcept
{
    const int B = N / 2 + 1;
    const float r = a.voiced ? std::exp2 (settings.semitones / 12.0f) : 1.0f;
    const float fr = a.voiced ? std::exp2 (std::clamp (settings.formantSemis, -12.0f, 12.0f) / 12.0f) : 1.0f;
    gainMix += ((a.voiced ? 1.0f : settings.unvoicedGain) - gainMix) * 0.5f;
    std::fill (out.begin(), out.end(), Fft::cpx {});

    const int nPeaks = (int) a.peaks.size();
    int j = 0;
    for (int i = 0; i < nPeaks; ++i)
    {
        const int kp = a.peaks[(size_t) i];
        const float f = a.instBin[(size_t) kp];
        // continue the phase rotation of the previous frame's peak nearest in frequency
        while (j + 1 < prevPeakCount && std::abs (prevPeakBin[(size_t) j + 1] - f) <= std::abs (prevPeakBin[(size_t) j] - f)) ++j;
        const float prev = prevPeakCount > 0 && std::abs (prevPeakBin[(size_t) j] - f) < 3.0f ? prevTheta[(size_t) j] : 0.0f;
        const float th = princarg (prev + twoPi * (r - 1.0f) * f * (float) H / (float) N);
        theta[(size_t) i] = th;
        const int shift = (int) std::lround (r * f - (float) kp);
        const Fft::cpx rot = std::polar (1.0f, th);

        // the peak's region moves as a block
        int k0 = kp, k1 = kp;
        while (k0 > 0 && a.region[(size_t) k0 - 1] == i) --k0;
        while (k1 < B - 1 && a.region[(size_t) k1 + 1] == i) ++k1;
        if (r < 1.0f)
        {
            // shifted down, the peaks end up r times closer: narrow each region by r so neighbouring
            // regions do not overlap and interfere at their destination
            k0 = std::max (k0, kp - (int) std::ceil ((float) (kp - k0) * r));
            k1 = std::min (k1, kp + (int) std::ceil ((float) (k1 - kp) * r));
        }
        for (int k = k0; k <= k1; ++k)
        {
            const int kd = k + shift;
            if (kd <= 0 || kd >= B - 1) continue;
            // formants: output magnitude follows the envelope at the destination (scaled by fr)
            const float src = std::clamp ((float) kd / fr, 0.0f, (float) (B - 1));
            const int s0 = (int) src;
            const float sf = src - (float) s0;
            const float envDst = a.logEnv[(size_t) s0] + sf * (a.logEnv[(size_t) std::min (s0 + 1, B - 1)] - a.logEnv[(size_t) s0]);
            const float corr = std::exp (std::clamp (envDst - a.logEnv[(size_t) k], -6.0f, 6.0f));
            out[(size_t) kd] += a.spec[(size_t) k] * rot * corr;
        }
    }
    prevPeakCount = nPeaks;
    for (int i = 0; i < nPeaks; ++i)
    {
        prevPeakBin[(size_t) i] = a.instBin[(size_t) a.peaks[(size_t) i]];
        prevTheta[(size_t) i] = theta[(size_t) i];
    }

    for (int k = 1; k < N / 2; ++k) out[(size_t) (N - k)] = std::conj (out[(size_t) k]);
    out[0] = { out[0].real(), 0.0f };
    out[(size_t) N / 2] = { out[(size_t) N / 2].real(), 0.0f };
    fft.inverse (out.data());

    // overlap-add: this frame covers the N input samples ending at the next output call
    const int start = readPos + 1 - N;
    const float g = norm / (float) N * gainMix;
    for (int i = 0; i < N; ++i)
        ola[(size_t) ((start + i + 2 * N) % (2 * N))] += out[(size_t) i].real() * win[(size_t) i] * g;
}

float PvVoice::next() noexcept
{
    const int idx = readPos - N;
    float y = 0.0f;
    if (idx >= 0)
    {
        const size_t p = (size_t) (idx % (2 * N));
        y = ola[p];
        ola[p] = 0.0f;
    }
    readPos = readPos + 1;
    if (readPos >= 1 << 30) readPos -= (1 << 30) - ((1 << 30) % (2 * N));   // keep the index bounded, same phase
    return y;
}
} // namespace kural::dsp
