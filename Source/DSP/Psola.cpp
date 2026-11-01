#include "Psola.h"
#include <algorithm>
#include <cmath>

namespace kural::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
std::int64_t pow2AtLeast (std::int64_t n) { std::int64_t p = 1; while (p < n) p <<= 1; return p; }
}

// ================================================================================== PitchMarker
void PitchMarker::prepare (double sampleRate, float minHz)
{
    fs = sampleRate > 1000.0 ? sampleRate : 48000.0;
    pMax = (float) (fs / std::max (30.0f, minHz));
    pMin = (float) (fs / 1600.0);
    unvoicedPeriod = (float) (fs * 0.010);
    latencySamples = (int) std::ceil (2.0f * pMax) + 16;
    searchStride = std::max (1, (int) std::floor (fs / 24000.0));
    const std::int64_t hist = pow2AtLeast ((std::int64_t) (latencySamples + 4 * pMax + 64));
    ring.assign ((size_t) hist, 0.0f);
    ringMask = hist - 1;
    const std::int64_t mk = pow2AtLeast ((std::int64_t) (4.0 * hist / std::max (pMin, 8.0f)) + 64);
    marks.assign ((size_t) mk, AnalysisMark {});
    marksMask = mk - 1;
    reset();
}

void PitchMarker::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    tNow = -1;
    markCount = 0;
    detPeriod = 0.0f;
    detVoicing = Voicing::silence;
    detTime = -1;
    recentPeriod = 0.0f;
    recentTime = 0;
}

float PitchMarker::interp (double pos) const noexcept
{
    const double f = std::floor (pos);
    const auto i = (std::int64_t) f;
    const float t = (float) (pos - f);
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    // Catmull-Rom
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

bool PitchMarker::markAtOrBefore (double t, AnalysisMark& out) const noexcept
{
    if (markCount == 0) return false;
    // marks are in time order; search backwards from the newest (only a few steps in practice)
    const std::int64_t oldest = std::max<std::int64_t> (0, markCount - (marksMask + 1));
    for (std::int64_t k = markCount - 1; k >= oldest; --k)
    {
        const auto& m = marks[(size_t) (k & marksMask)];
        if ((double) m.pos <= t) { out = m; return true; }
    }
    return false;
}

bool PitchMarker::markAfter (std::int64_t pos, AnalysisMark& out) const noexcept
{
    const std::int64_t oldest = std::max<std::int64_t> (0, markCount - (marksMask + 1));
    for (std::int64_t k = markCount - 1; k > oldest; --k)
    {
        const auto& prev = marks[(size_t) ((k - 1) & marksMask)];
        if (prev.pos == pos) { out = marks[(size_t) (k & marksMask)]; return true; }
        if (prev.pos < pos) break;
    }
    return false;
}

void PitchMarker::onPitchFrame (const PitchFrame& f) noexcept
{
    detVoicing = f.voicing;
    detTime = f.centreSample;
    if ((f.voicing == Voicing::voiced || f.voicing == Voicing::uncertain) && f.f0Hz > 0.0f)
        detPeriod = (float) (fs / f.f0Hz);
    if (f.voicing == Voicing::voiced && f.f0Hz > 0.0f)
    {
        recentPeriod = (float) (fs / f.f0Hz);
        recentTime = f.centreSample;
    }
}

void PitchMarker::push (float x) noexcept
{
    ++tNow;
    ring[(size_t) (tNow & ringMask)] = x;
    advance();
}

float PitchMarker::correlate (std::int64_t a, std::int64_t b, int len, int stride) const noexcept
{
    // normalised correlation of the `len` samples before a and before b
    double xy = 0.0, xx = 0.0, yy = 0.0;
    for (int i = 1; i <= len; i += stride)
    {
        const double x = at (a - i), y = at (b - i);
        xy += x * y; xx += x * x; yy += y * y;
    }
    const double d = std::sqrt (xx * yy);
    return d > 1e-12 ? (float) (xy / d) : 0.0f;
}

void PitchMarker::addMark (std::int64_t pos, float period, float sim, bool voiced) noexcept
{
    marks[(size_t) (markCount & marksMask)] = { pos, period, sim, voiced };
    ++markCount;
}

void PitchMarker::advance() noexcept
{
    // place marks as far as the input allows; each needs the period before it and a little search
    // room after it. In practice this places at most one mark per call.
    for (int guard = 0; guard < 4; ++guard)
    {
        const bool detVoiced = detPeriod > 0.0f && (detVoicing == Voicing::voiced || detVoicing == Voicing::uncertain);
        const float P = detVoiced ? std::clamp (detPeriod, pMin, pMax) : unvoicedPeriod;

        if (markCount == 0)
        {
            // first mark: wait for one period of history, then start on the largest peak in it
            if (tNow < (std::int64_t) (2 * P)) return;
            std::int64_t best = tNow - (std::int64_t) P;
            float bestV = -1.0f;
            for (std::int64_t i = tNow - (std::int64_t) P; i <= tNow; ++i)
                if (std::abs (at (i)) > bestV) { bestV = std::abs (at (i)); best = i; }
            addMark (best, P, 0.0f, false);
            continue;
        }

        const auto& last = marks[(size_t) ((markCount - 1) & marksMask)];

        // The detector reports ~20 ms late, so at the start of a note it still says "unvoiced". If a
        // period was heard recently, probe for it here (wider search) instead of waiting: otherwise the
        // first periods of every note are treated as consonant and left unshifted.
        const bool probe = ! detVoiced && recentPeriod > 0.0f && tNow - recentTime < (std::int64_t) (0.5 * fs);
        if (! detVoiced && ! probe)
        {
            const double cand = (double) last.pos + unvoicedPeriod;
            if (cand > (double) tNow) return;
            const auto pos = (std::int64_t) std::llround (cand);
            addMark (pos, (float) (pos - last.pos), 0.0f, false);
            continue;
        }

        const float Pc = detVoiced ? P : std::clamp (recentPeriod, pMin, pMax);
        const int search = std::max (2, (int) ((probe ? 0.25f : 0.15f) * Pc));
        const double cand = (double) last.pos + Pc;
        if (cand + search > (double) tNow) return;            // not enough input yet

        // align with the previous period: coarse search on a stride, then fine around the best
        const int len = (int) std::lround (Pc);
        const auto c0 = (std::int64_t) std::llround (cand);
        std::int64_t best = c0;
        float bestC = -2.0f;
        for (int d = -search; d <= search; d += searchStride)
        {
            const float c = correlate (last.pos, c0 + d, len, searchStride);
            if (c > bestC) { bestC = c; best = c0 + d; }
        }
        if (searchStride > 1)
        {
            const std::int64_t centre = best;
            for (std::int64_t p = centre - searchStride + 1; p <= centre + searchStride - 1; ++p)
            {
                if (p == centre || p > tNow || p <= last.pos + (std::int64_t) pMin) continue;
                const float c = correlate (last.pos, p, len, 1);
                if (c > bestC) { bestC = c; best = p; }
            }
            bestC = correlate (last.pos, best, len, 1);
        }
        // two consecutive periods that are not alike are not voiced, whatever the detector said;
        // a probe (detector not voiced yet) must be clearly periodic
        const bool voiced = bestC > (probe ? 0.7f : 0.55f);
        if (probe && ! voiced)
        {
            const auto pos = (std::int64_t) std::llround (cand);
            addMark (pos, (float) (pos - last.pos), bestC, false);
            continue;
        }
        addMark (best, (float) (best - last.pos), bestC, voiced);
    }
}

// =================================================================================== PsolaVoice
void PsolaVoice::prepare (const PitchMarker& m)
{
    fs = m.sampleRate();
    // a grain spans up to ±maxPeriod × 2 (lowest formant ratio) around its synthesis mark
    const std::int64_t n = pow2AtLeast ((std::int64_t) (8.0f * m.maxPeriod()) + 64);
    acc.assign ((size_t) n, 0.0f);
    accU.assign ((size_t) n, 0.0f);
    accMask = n - 1;
    powCoef = 1.0f - (float) std::exp (-1.0 / (0.040 * fs));
    gainCoef = 1.0f - (float) std::exp (-1.0 / (0.060 * fs));
    reset (m);
}

void PsolaVoice::reset (const PitchMarker&)
{
    std::fill (acc.begin(), acc.end(), 0.0f);
    std::fill (accU.begin(), accU.end(), 0.0f);
    inPow = outPow = 0.0f;
    levelGain = 1.0f;
    started = false;
    semis = settings.semitones;
    voicedMix = 0.0f;
    lastMarkPos = -1;
}

void PsolaVoice::addGrain (std::vector<float>& dest, const PitchMarker& m, const AnalysisMark& a, double tsAt, float formant, float gain) noexcept
{
    // grain: input around a.pos, ±P, Hann window; laid down around tsAt, time-scaled by 1/formant
    const float P = std::clamp (a.period, 8.0f, m.maxPeriod());
    const double halfOut = P / formant;
    const std::int64_t tOut = m.now() - m.latency();
    // never write into output that has already been sent
    const auto first = std::max (tOut, (std::int64_t) std::ceil (tsAt - halfOut));
    const auto last = (std::int64_t) std::floor (tsAt + halfOut);
    const double invHalf = 1.0 / halfOut;
    for (std::int64_t t = first; t <= last; ++t)
    {
        const double u = ((double) t - tsAt) * invHalf;       // −1 … 1
        const float w = (float) (0.5 + 0.5 * std::cos (pi * u));
        const double src = (double) a.pos + u * P;
        dest[(size_t) (t & accMask)] += gain * w * m.interp (src);
    }
}

float PsolaVoice::next (const PitchMarker& m) noexcept
{
    const std::int64_t tOut = m.now() - m.latency();
    if (tOut < 0) return 0.0f;

    // glide the pitch shift
    const float glide = settings.glideMs > 0.0f ? 1.0f - std::exp (-1.0f / (0.001f * settings.glideMs * (float) fs)) : 1.0f;
    semis += (settings.semitones - semis) * glide;
    const float formant = std::exp2 (std::clamp (settings.formantSemis, -12.0f, 12.0f) / 12.0f);

    if (! started) { ts = (double) tOut; started = true; }

    // lay down every grain whose left edge has reached the output position
    for (int guard = 0; guard < 64; ++guard)
    {
        AnalysisMark a;
        // the grain reads up to one period after its mark: only marks with that much input after them
        const double limit = std::min (ts, (double) m.now() - m.maxPeriod());
        if (! m.markAtOrBefore (limit, a)) { ts = std::max (ts, (double) tOut); break; }
        const float P = std::clamp (a.period, 8.0f, m.maxPeriod());
        if (ts - P / formant > (double) tOut) break;

        // voiced grains are shifted; unvoiced ones are copied at their own spacing and level.
        // A mark counts as voiced if the period before it OR the period after it is periodic: at a note's
        // start the first mark is compared with the breath or consonant before it, and judging it by
        // that alone left the first periods of every note unshifted (measured on real singing: 97 % of
        // the off-pitch frames were within 40 ms of a voicing edge). The mix glides over ~1–2 grains.
        AnalysisMark nx;
        const bool hasNext = m.markAfter (a.pos, nx);
        const bool voiced = a.voiced || (hasNext && nx.voiced);
        voicedMix += ((voiced ? 1.0f : 0.0f) - voicedMix) * 0.75f;
        const float ratio = voiced ? std::exp2 (semis / 12.0f) : 1.0f;
        if (voicedMix > 0.001f) addGrain (acc, m, a, ts, voiced ? formant : 1.0f, voicedMix);
        if (voicedMix < 0.999f) addGrain (accU, m, a, ts, 1.0f, (1.0f - voicedMix) * settings.unvoicedGain);
        // advance by the period that follows this mark (the distance to the next mark) when it is known:
        // using the period before it lets the synthesis drift against the marks under vibrato, which now
        // and then repeats or skips a period (measured: unshifted output 5.7 dB envelope error vs 2.8)
        const float Pnext = hasNext ? std::clamp ((float) (nx.pos - a.pos), 8.0f, m.maxPeriod()) : P;
        ts += Pnext / ratio;
    }

    const size_t idx = (size_t) (tOut & accMask);
    const float v = acc[idx], u = accU[idx];
    acc[idx] = accU[idx] = 0.0f;

    // level match of the voiced part: power of the shifted voice vs. power of the input at the same
    // time, measured only while there is voiced output; the gain glides and holds through gaps
    if (std::abs (v) > 0.0f || outPow > 1e-12f)
    {
        const float x = m.at (tOut);
        inPow += (x * x * voicedMix - inPow) * powCoef;
        outPow += (v * v - outPow) * powCoef;
        if (outPow > 1e-10f && voicedMix > 0.5f)
        {
            const float target = std::clamp (std::sqrt (inPow / outPow), 0.25f, 4.0f);
            levelGain += (target - levelGain) * gainCoef;
        }
    }
    return levelGain * v + u;
}
} // namespace kural::dsp
