#include "PitchDetector.h"
#include <algorithm>

namespace kural::dsp
{
void PitchDetector::prepare (double hostSampleRate, const Config& c)
{
    cfg = c;
    fs = hostSampleRate > 1000.0 ? hostSampleRate : 48000.0;

    // integer decimation to ~21–24 kHz: plenty of bandwidth for the YIN difference function and
    // 2–8× less work at high sample rates (44.1/48 → 2, 88.2/96 → 4, 176.4 → 8, 192 → 9)
    const int factor = std::max (1, (int) std::floor (fs / 21000.0));
    dec.prepare (factor);
    fa = fs / factor;

    tauMin = std::max (2, (int) std::floor (fa / cfg.maxHz));
    tauMax = (int) std::ceil (fa / cfg.minHz) + 2;          // +2: room for the parabolic fit at the edge
    win = tauMax;                                            // integration window = longest period searched
    frameLen = win + tauMax + 1;
    hopLen = std::max (1, (int) std::lround (cfg.hopMs * 0.001 * fa));
    ringLen = 1;
    while (ringLen < frameLen + 1) ringLen <<= 1;

    ring.assign ((size_t) ringLen * 2, 0.0f);
    frame.assign ((size_t) frameLen, 0.0f);
    diff.assign ((size_t) tauMax + 1, 0.0f);
    cmnd.assign ((size_t) tauMax + 1, 1.0f);
    centreBackHost = (frameLen - 0.5 * (win + 0.5 * tauMax)) * factor + dec.groupDelayInput();
    reset();
}

void PitchDetector::reset()
{
    dec.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    wpos = 0; filled = 0; sinceHop = 0; hostCount = 0;
    prevTau = 0.0f;
    framesSinceVoiced = 1 << 20;
}

PitchFrame PitchDetector::analyse() noexcept
{
    PitchFrame out;
    const int start = (wpos - frameLen + ringLen) % ringLen;
    const float* x = ring.data() + start;                    // contiguous thanks to the doubled ring

    double energy = 0.0;
    for (int j = 0; j < frameLen; ++j) energy += (double) x[j] * x[j];
    const double rms = std::sqrt (energy / frameLen);
    out.rmsDb = rms > 1e-6 ? (float) (20.0 * std::log10 (rms)) : -120.0f;

    auto stamp = [&] (double tauEst)
    {
        const double backAnalysis = frameLen - 0.5 * (win + tauEst);
        out.centreSample = hostCount - 1 - (std::int64_t) std::llround (backAnalysis * dec.getFactor() + dec.groupDelayInput());
    };

    ++framesSinceVoiced;
    if (out.rmsDb < cfg.silenceDb)
    {
        stamp (0.5 * tauMax);
        out.voicing = Voicing::silence;
        if (framesSinceVoiced * cfg.hopMs > 200.0f) prevTau = 0.0f;
        return out;
    }

    // ---- difference function d(τ) = e(0) + e(τ) − 2 r(τ), all over a window of `win` samples
    double e0 = 0.0;
    for (int j = 0; j < win; ++j) e0 += (double) x[j] * x[j];
    double eTau = e0;
    diff[0] = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        eTau += (double) x[tau + win - 1] * x[tau + win - 1] - (double) x[tau - 1] * x[tau - 1];
        const float* y = x + tau;
        float r = 0.0f;
        for (int j = 0; j < win; ++j) r += x[j] * y[j];
        diff[(size_t) tau] = (float) std::max (0.0, e0 + eTau - 2.0 * r);
    }

    // ---- cumulative-mean-normalised difference d'(τ)
    cmnd[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += diff[(size_t) tau];
        cmnd[(size_t) tau] = running > 1e-12 ? (float) (diff[(size_t) tau] * tau / running) : 1.0f;
    }

    auto localMinNear = [&] (int centre, int radius)
    {
        int best = -1;
        for (int t = std::max (tauMin, centre - radius); t <= std::min (tauMax - 1, centre + radius); ++t)
            if (best < 0 || cmnd[(size_t) t] < cmnd[(size_t) best]) best = t;
        return best;
    };

    // ---- absolute threshold: first dip below the threshold, followed down to its minimum
    int tau = -1;
    bool belowThreshold = false;
    for (int t = tauMin; t < tauMax; ++t)
        if (cmnd[(size_t) t] < cfg.threshold)
        {
            while (t + 1 < tauMax && cmnd[(size_t) t + 1] < cmnd[(size_t) t]) ++t;
            tau = t; belowThreshold = true;
            break;
        }
    if (tau < 0)
    {
        tau = tauMin;
        for (int t = tauMin; t < tauMax; ++t)
            if (cmnd[(size_t) t] < cmnd[(size_t) tau]) tau = t;
        // no clear periodicity: if the previous pitch still has a reasonable dip, stay with it
        if (prevTau > 0.0f && framesSinceVoiced * cfg.hopMs <= 60.0f)
        {
            const int alt = localMinNear ((int) std::lround (prevTau), std::max (2, (int) (prevTau * 0.06f)));
            if (alt > 0 && cmnd[(size_t) alt] < cfg.uncertainLimit) tau = alt;
        }
    }

    // ---- octave guard: a strong 2nd harmonic can push the half-period dip under the threshold.
    // If the difference at twice the lag is much smaller (< ¼), the longer lag is the real period.
    // A ½ ratio was not enough: breath noise sometimes correlates better over the longer lag by
    // chance (measured 0.047 vs 0.13), while a real dominant-2nd-harmonic case is near zero at P.
    // Judged on the energy-normalised raw difference d(τ) / 2e(0) (≈ 1 − normalised autocorrelation),
    // not on d': breath noise raises d at every lag about equally, but the cumulative normalisation
    // makes longer lags look better and flipped breathy voices an octave down (measured).
    // Both a relative and an absolute margin are required: on a clean periodic signal both values
    // are close to zero, and the ratio of two tiny numbers says nothing.
    if (belowThreshold && 2 * tau < tauMax - 1 && e0 > 1e-12)
    {
        const int alt = localMinNear (2 * tau, 3);
        const double norm = 0.5 / e0;
        const double dTau = diff[(size_t) tau] * norm, dAlt = alt > 0 ? diff[(size_t) alt] * norm : 1.0;
        if (alt > 0 && dAlt < 0.25 * dTau && dTau - dAlt > 0.04)
            tau = alt;
    }

    // ---- sub-multiple check (the opposite error): with breath noise d'(P) can sit just above the
    // threshold while d'(2P) dips below it, so the first-dip rule picks 2P. A lag of τ/2 or τ/3 whose
    // raw difference is as small as at τ is the real period (any multiple of a period is periodic too).
    if (e0 > 1e-12)
    {
        const double norm = 0.5 / e0;
        for (int k = 3; k >= 2; --k)
        {
            const int c = (int) std::lround ((double) tau / k);
            if (c < tauMin) continue;
            const int cand = localMinNear (c, 2);
            if (cand > 0 && diff[(size_t) cand] * norm <= diff[(size_t) tau] * norm + 0.02 && cmnd[(size_t) cand] < cfg.uncertainLimit)
            {
                tau = cand;
                break;
            }
        }
    }

    // ---- parabolic interpolation around the chosen lag, on the raw difference function: the
    // normalisation in d' multiplies by τ, which tilts the minimum towards shorter lags and made
    // high notes read up to 3 cents sharp (measured; see DSP_DESIGN.md)
    double tauRef = tau;
    if (tau > 1 && tau < tauMax)
    {
        const double a = diff[(size_t) tau - 1], b = diff[(size_t) tau], c = diff[(size_t) tau + 1];
        const double den = a - 2.0 * b + c;
        if (den > 1e-12) tauRef = tau + std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
    }

    const float ap = cmnd[(size_t) tau];
    out.confidence = std::clamp (1.0f - ap, 0.0f, 1.0f);
    out.f0Hz = (float) (fa / tauRef);
    stamp (tauRef);

    if (ap < cfg.threshold)            out.voicing = Voicing::voiced;
    else if (ap < cfg.uncertainLimit)  out.voicing = Voicing::uncertain;
    else                               { out.voicing = Voicing::unvoiced; out.f0Hz = 0.0f; }

    if (out.voicing == Voicing::voiced)
    {
        prevTau = (float) tauRef;
        framesSinceVoiced = 0;
    }
    return out;
}
} // namespace kural::dsp
