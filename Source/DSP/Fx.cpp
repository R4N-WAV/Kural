#include "Fx.h"
#include <algorithm>

namespace kural::dsp::fx
{
namespace
{
constexpr double pi = 3.14159265358979323846;
int pow2AtLeast (int n) { int p = 1; while (p < n) p <<= 1; return p; }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
}

// ======================================================================================== Biquad
void Biquad::highPass (double fs, double f, double q) noexcept
{
    const double w = 2.0 * pi * std::clamp (f, 5.0, 0.45 * fs) / fs, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    const double n = 1.0 / (1.0 + a);
    b0 = (float) ((1.0 + c) * 0.5 * n); b1 = (float) (-(1.0 + c) * n); b2 = b0;
    a1 = (float) (-2.0 * c * n); a2 = (float) ((1.0 - a) * n);
}
void Biquad::lowPass (double fs, double f, double q) noexcept
{
    const double w = 2.0 * pi * std::clamp (f, 5.0, 0.45 * fs) / fs, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    const double n = 1.0 / (1.0 + a);
    b0 = (float) ((1.0 - c) * 0.5 * n); b1 = (float) ((1.0 - c) * n); b2 = b0;
    a1 = (float) (-2.0 * c * n); a2 = (float) ((1.0 - a) * n);
}
void Biquad::bandPass (double fs, double f, double q) noexcept
{
    const double w = 2.0 * pi * std::clamp (f, 5.0, 0.45 * fs) / fs, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    const double n = 1.0 / (1.0 + a);
    b0 = (float) (a * n); b1 = 0.0f; b2 = (float) (-a * n);
    a1 = (float) (-2.0 * c * n); a2 = (float) ((1.0 - a) * n);
}
void Biquad::peak (double fs, double f, double q, double dB) noexcept
{
    const double A = std::pow (10.0, dB / 40.0), w = 2.0 * pi * std::clamp (f, 5.0, 0.45 * fs) / fs;
    const double c = std::cos (w), a = std::sin (w) / (2.0 * q), n = 1.0 / (1.0 + a / A);
    b0 = (float) ((1.0 + a * A) * n); b1 = (float) (-2.0 * c * n); b2 = (float) ((1.0 - a * A) * n);
    a1 = b1; a2 = (float) ((1.0 - a / A) * n);
}
void Biquad::highShelf (double fs, double f, double dB) noexcept
{
    const double A = std::pow (10.0, dB / 40.0), w = 2.0 * pi * std::clamp (f, 5.0, 0.45 * fs) / fs;
    const double c = std::cos (w), a = std::sin (w) / std::sqrt (2.0), sA = 2.0 * std::sqrt (A) * a;
    const double n = 1.0 / ((A + 1.0) - (A - 1.0) * c + sA);
    b0 = (float) (A * ((A + 1.0) + (A - 1.0) * c + sA) * n);
    b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * c) * n);
    b2 = (float) (A * ((A + 1.0) + (A - 1.0) * c - sA) * n);
    a1 = (float) (2.0 * ((A - 1.0) - (A + 1.0) * c) * n);
    a2 = (float) (((A + 1.0) - (A - 1.0) * c - sA) * n);
}
void Biquad::lowShelf (double fs, double f, double dB) noexcept
{
    const double A = std::pow (10.0, dB / 40.0), w = 2.0 * pi * std::clamp (f, 5.0, 0.45 * fs) / fs;
    const double c = std::cos (w), a = std::sin (w) / std::sqrt (2.0), sA = 2.0 * std::sqrt (A) * a;
    const double n = 1.0 / ((A + 1.0) + (A - 1.0) * c + sA);
    b0 = (float) (A * ((A + 1.0) - (A - 1.0) * c + sA) * n);
    b1 = (float) (2.0 * A * ((A - 1.0) - (A + 1.0) * c) * n);
    b2 = (float) (A * ((A + 1.0) - (A - 1.0) * c - sA) * n);
    a1 = (float) (-2.0 * ((A - 1.0) + (A + 1.0) * c) * n);
    a2 = (float) (((A + 1.0) + (A - 1.0) * c - sA) * n);
}

// ============================================================================================ EQ
void Eq::prepare (double sampleRate) { fs = sampleRate; reset(); }
void Eq::reset()
{
    for (auto* set : { &hp1, &hp2, &pres, &air }) for (auto& b : *set) b.reset();
    current = target;
    design();
    counter = 0;
}
void Eq::design() noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        // 4th-order Butterworth low cut (Q 0.541, 1.307); 20 Hz = off in practice
        hp1[(size_t) c].highPass (fs, current.lowCutHz, 0.5412);
        hp2[(size_t) c].highPass (fs, current.lowCutHz, 1.3066);
        pres[(size_t) c].peak (fs, 3200.0, 0.9, current.presenceDb);
        air[(size_t) c].highShelf (fs, 10000.0, current.airDb);
    }
}
void Eq::process (float* l, float* r, int n) noexcept
{
    // neutral settings (low cut at its 20 Hz minimum, no boost or cut) pass the signal untouched
    auto neutral = [] (const EqParams& p) { return p.lowCutHz <= 20.5f && std::abs (p.presenceDb) < 0.05f && std::abs (p.airDb) < 0.05f; };
    if (neutral (target) && neutral (current))
    {
        for (auto* set : { &hp1, &hp2, &pres, &air }) for (auto& b : *set) b.reset();
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        // parameters glide every 32 samples (no zipper, no sudden coefficient jumps)
        if (++counter >= 32)
        {
            counter = 0;
            bool changed = false;
            auto glide = [&] (float& c, float t, float step) { if (std::abs (t - c) > 1e-3f) { c += std::clamp (t - c, -step, step); changed = true; } };
            const float hzStep = std::max (1.0f, current.lowCutHz * 0.03f);
            glide (current.lowCutHz, target.lowCutHz, hzStep);
            glide (current.presenceDb, target.presenceDb, 0.25f);
            glide (current.airDb, target.airDb, 0.25f);
            if (changed) design();
        }
        float* ch[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            if (ch[c] == nullptr) continue;
            float x = ch[c][i];
            x = hp2[(size_t) c].process (hp1[(size_t) c].process (x));
            x = pres[(size_t) c].process (x);
            x = air[(size_t) c].process (x);
            ch[c][i] = x;
        }
    }
}

// ==================================================================================== Compressor
void Compressor::prepare (double sampleRate)
{
    fs = sampleRate;
    atk = 1.0f - (float) std::exp (-1.0 / (0.005 * fs));
    rel = 1.0f - (float) std::exp (-1.0 / (0.120 * fs));
    envCoef = 1.0f - (float) std::exp (-1.0 / (0.010 * fs));
    reset();
}
void Compressor::process (float* l, float* r, int n) noexcept
{
    const float a = std::clamp (params.amount, 0.0f, 1.0f);
    if (a <= 1e-4f && gainDb > -0.01f) return;
    const float thr = -12.0f - 18.0f * a;            // dBFS (RMS)
    const float ratio = 1.0f + 5.0f * a;
    const float knee = 8.0f;
    const float makeup = 0.5f * -thr * (1.0f - 1.0f / ratio) * a;
    for (int i = 0; i < n; ++i)
    {
        const float x = r != nullptr ? 0.5f * (l[i] * l[i] + r[i] * r[i]) : l[i] * l[i];
        env += (x - env) * envCoef;
        const float lv = 10.0f * std::log10 (env + 1e-12f);
        const float over = lv - thr;
        float red = 0.0f;
        if (over > knee * 0.5f) red = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f) { const float t = over + knee * 0.5f; red = (1.0f - 1.0f / ratio) * t * t / (2.0f * knee); }
        const float want = -red;
        gainDb += (want - gainDb) * (want < gainDb ? atk : rel);
        const float g = dbToGain (gainDb + makeup * (a > 0.0f ? 1.0f : 0.0f));
        l[i] *= g;
        if (r != nullptr) r[i] *= g;
    }
}

// ========================================================================================== Tube
void Tube::prepare (double sampleRate)
{
    fs = sampleRate;
    smooth = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
    for (int c = 0; c < 2; ++c)
    {
        // anti-imaging / anti-aliasing at 2× rate: 8th-order Butterworth at 0.40 of the base rate
        const double q[4] = { 0.5098, 0.6013, 0.9000, 2.5629 };
        for (int j = 0; j < 4; ++j)
        {
            up[(size_t) c][(size_t) j].lowPass (2.0 * fs, 0.40 * fs, q[j]);
            down[(size_t) c][(size_t) j].lowPass (2.0 * fs, 0.40 * fs, q[j]);
        }
        dcBlock[(size_t) c].highPass (fs, 12.0, 0.707);
    }
    reset();
}
void Tube::reset()
{
    for (auto& ch : up) for (auto& b : ch) b.reset();
    for (auto& ch : down) for (auto& b : ch) b.reset();
    for (auto& b : dcBlock) b.reset();
    driveS = params.drive; mixS = params.mix;
}
float Tube::shape (float x, float k) const noexcept
{
    // asymmetric: a small bias gives the even harmonics of a triode; normalised so quiet signals pass at unity
    constexpr float bias = 0.12f;
    const float t0 = std::tanh (k * bias);
    const float slope = k * (1.0f - t0 * t0);
    return (std::tanh (k * (x + bias)) - t0) / slope;
}
void Tube::process (float* l, float* r, int n) noexcept
{
    if (params.drive <= 1e-4f && driveS <= 1e-4f) return;
    float* ch[2] = { l, r };
    for (int i = 0; i < n; ++i)
    {
        driveS += (params.drive - driveS) * smooth;
        mixS += (params.mix - mixS) * smooth;
        const float k = 1.0f + 14.0f * driveS * driveS;
        for (int c = 0; c < 2; ++c)
        {
            if (ch[c] == nullptr) continue;
            const float x = ch[c][i];
            // 2× oversampling: zero-stuff, filter, shape, filter, keep every other sample
            auto& U = up[(size_t) c];
            auto& D = down[(size_t) c];
            auto lp = [] (std::array<Biquad, 4>& f, float v) { for (auto& b : f) v = b.process (v); return v; };
            const float u0 = lp (U, 2.0f * x);
            const float u1 = lp (U, 0.0f);
            const float s0 = lp (D, shape (u0, k));
            lp (D, shape (u1, k));
            const float y = dcBlock[(size_t) c].process (s0);
            ch[c][i] = x + (y - x) * mixS;
        }
    }
}

// ======================================================================================== Mutate
void Mutate::prepare (double sampleRate) { fs = sampleRate; reset(); }
void Mutate::process (float* l, float* r, int n) noexcept
{
    const float target = params.on ? std::clamp (params.mix, 0.0f, 1.0f) : 0.0f;
    if (target <= 1e-4f && mixS <= 1e-4f) return;
    const float want = params.fixedHz > 0.0f ? params.fixedHz : pitchHz * params.ratio;
    const float sm = 1.0f - std::exp (-1.0f / (0.01f * (float) fs));
    for (int i = 0; i < n; ++i)
    {
        freqS += (std::clamp (want, 20.0f, 5000.0f) - freqS) * sm;
        mixS += (target - mixS) * sm;
        phase += freqS / fs;
        if (phase >= 1.0) phase -= 1.0;
        const float m = (float) std::sin (2.0 * pi * phase);
        l[i] += (l[i] * m - l[i]) * mixS;
        if (r != nullptr) r[i] += (r[i] * m - r[i]) * mixS;
    }
}

// ========================================================================================= Delay
void Delay::prepare (double sampleRate, float maxMs)
{
    fs = sampleRate;
    const int len = pow2AtLeast ((int) (maxMs * 0.001 * fs) + 4);
    bufL.assign ((size_t) len, 0.0f);
    bufR.assign ((size_t) len, 0.0f);
    mask = len - 1;
    reset();
}
void Delay::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    w = 0;
    timeS = params.timeMs;
    mixS = params.mix;
    for (auto* b : { &toneL, &toneR, &hpL, &hpR }) b->reset();
}
void Delay::process (float* l, float* r, int n) noexcept
{
    if (params.mix <= 1e-4f && mixS <= 1e-4f) { // keep the line running quietly so switching on is clean
        for (int i = 0; i < n; ++i) { bufL[(size_t) w] = bufR[(size_t) w] = 0.0f; w = (w + 1) & mask; }
        return;
    }
    toneL.lowPass (fs, params.toneHz, 0.707); toneR.lowPass (fs, params.toneHz, 0.707);
    hpL.highPass (fs, 150.0, 0.707); hpR.highPass (fs, 150.0, 0.707);
    const float sm = 1.0f - std::exp (-1.0f / (0.05f * (float) fs));
    const float fb = std::clamp (params.feedback, 0.0f, 0.95f);
    const float maxT = (float) (mask - 4) / (float) fs * 1000.0f;
    for (int i = 0; i < n; ++i)
    {
        timeS += (std::clamp (params.timeMs, 1.0f, maxT) - timeS) * sm;   // a glide, like tape, never a jump
        mixS += (params.mix - mixS) * sm;
        const float d = timeS * 0.001f * (float) fs;
        auto rd = [&] (const std::vector<float>& b)
        {
            const float pos = (float) w - d;
            const int i0 = (int) std::floor (pos);
            const float t = pos - (float) i0;
            return b[(size_t) (i0 & mask)] * (1.0f - t) + b[(size_t) ((i0 + 1) & mask)] * t;
        };
        const float dl = toneL.process (hpL.process (rd (bufL)));
        const float dr = toneR.process (hpR.process (rd (bufR)));
        const float inL = l[i], inR = r != nullptr ? r[i] : l[i];
        if (params.pingPong)
        {
            bufL[(size_t) w] = 0.5f * (inL + inR) + fb * dr;
            bufR[(size_t) w] = fb * dl;
        }
        else
        {
            bufL[(size_t) w] = inL + fb * dl;
            bufR[(size_t) w] = inR + fb * dr;
        }
        w = (w + 1) & mask;
        l[i] = inL + dl * mixS;
        if (r != nullptr) r[i] = inR + dr * mixS;
        else l[i] += dr * mixS;
    }
}

// ======================================================================================== Reverb
void Reverb::prepare (double sampleRate)
{
    fs = sampleRate;
    // mutually prime line lengths (ms at size 1), spread so the echo density builds evenly
    const float ms[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 73.3f };
    for (int i = 0; i < N; ++i)
    {
        baseLen[(size_t) i] = (int) (ms[i] * 0.001f * (float) fs);
        const int len = pow2AtLeast ((int) (baseLen[(size_t) i] * 1.6f) + 64);
        lines[(size_t) i].assign ((size_t) len, 0.0f);
        masks[(size_t) i] = len - 1;
    }
    const float dms[4] = { 4.77f, 3.59f, 2.53f, 1.73f };
    for (int i = 0; i < 4; ++i)
    {
        diffLen[(size_t) i] = std::max (1, (int) (dms[i] * 0.001f * (float) fs));
        diff[(size_t) i].assign ((size_t) diffLen[(size_t) i], 0.0f);
    }
    const int pl = pow2AtLeast ((int) (0.2 * fs) + 4);
    pre.assign ((size_t) pl, 0.0f);
    preMask = pl - 1;
    reset();
}
void Reverb::reset()
{
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    for (auto& d : diff) std::fill (d.begin(), d.end(), 0.0f);
    std::fill (pre.begin(), pre.end(), 0.0f);
    writes.fill (0); diffW.fill (0); lpState.fill (0.0f);
    for (int i = 0; i < N; ++i) modPhase[(size_t) i] = (float) i / N;
    preW = 0;
    mixS = params.mix;
}
void Reverb::process (float* l, float* r, int n) noexcept
{
    if (params.mix <= 1e-4f && mixS <= 1e-4f) return;
    const float size = 0.5f + std::clamp (params.size, 0.0f, 1.0f);                // 0.5 … 1.5
    const float t60 = 0.3f + 6.0f * std::pow (std::clamp (params.decay, 0.0f, 1.0f), 1.6f);
    const float damp = 0.05f + 0.9f * std::clamp (params.damping, 0.0f, 1.0f);    // one-pole low-pass amount
    const int preD = std::clamp ((int) (params.preDelayMs * 0.001f * (float) fs), 0, preMask - 1);
    const float sm = 1.0f - std::exp (-1.0f / (0.05f * (float) fs));
    float g[N];
    int len[N];
    for (int i = 0; i < N; ++i)
    {
        len[i] = std::min ((int) (baseLen[(size_t) i] * size), masks[(size_t) i] - 40);
        g[i] = std::pow (10.0f, -3.0f * (float) len[i] / (t60 * (float) fs));
    }
    const float modDepth = 0.0006f * (float) fs;                 // ±0.6 ms, slow: no metallic ring
    const float modInc = 0.7f / (float) fs;
    const float w = std::clamp (params.width, 0.0f, 1.0f);
    for (int s = 0; s < n; ++s)
    {
        mixS += (params.mix - mixS) * sm;
        const float in = r != nullptr ? 0.5f * (l[s] + r[s]) : l[s];
        pre[(size_t) preW] = in;
        float x = pre[(size_t) ((preW - preD) & preMask)];
        preW = (preW + 1) & preMask;
        for (int d = 0; d < 4; ++d)           // series allpass diffusion
        {
            auto& buf = diff[(size_t) d];
            int& wi = diffW[(size_t) d];
            const float del = buf[(size_t) wi];
            const float v = x + 0.6f * del;
            buf[(size_t) wi] = v;
            x = del - 0.6f * v;
            wi = (wi + 1) % diffLen[(size_t) d];
        }
        float o[N], sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            auto& line = lines[(size_t) i];
            float d = (float) len[i];
            if (i < 4)                         // modulate half the lines
            {
                modPhase[(size_t) i] += modInc * (1.0f + 0.13f * (float) i);
                if (modPhase[(size_t) i] >= 1.0f) modPhase[(size_t) i] -= 1.0f;
                d += modDepth * std::sin (2.0f * 3.14159265f * modPhase[(size_t) i]);
            }
            const float pos = (float) writes[(size_t) i] - d;
            const int i0 = (int) std::floor (pos);
            const float t = pos - (float) i0;
            float v = line[(size_t) (i0 & masks[(size_t) i])] * (1.0f - t) + line[(size_t) ((i0 + 1) & masks[(size_t) i])] * t;
            lpState[(size_t) i] += (v - lpState[(size_t) i]) * (1.0f - damp);
            o[i] = lpState[(size_t) i] * g[i];
            sum += o[i];
        }
        const float h = 2.0f / N * sum;        // Householder feedback: o − (2/N) Σo
        for (int i = 0; i < N; ++i)
        {
            auto& line = lines[(size_t) i];
            line[(size_t) writes[(size_t) i]] = o[i] - h + x * (i % 2 == 0 ? 0.35f : -0.35f);
            writes[(size_t) i] = (writes[(size_t) i] + 1) & masks[(size_t) i];
        }
        const float wl = o[0] + o[2] + o[4] + o[6], wr = o[1] + o[3] + o[5] + o[7];
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * w;
        const float outL = (mid + side) * 0.5f, outR = (mid - side) * 0.5f;
        l[s] += outL * mixS;
        if (r != nullptr) r[s] += outR * mixS;
        else l[s] += outR * mixS;
    }
}
} // namespace kural::dsp::fx
