# Kural – DSP design

Status per section: **Implemented** (in the code and tested), **Designed** (decided, not coded),
**Open** (needs investigation/benchmarks before deciding).

## 1. Pitch detection — Implemented

`Source/DSP/PitchDetector.{h,cpp}`, plain C++17, tested by `Tests/DspTests.cpp`.

**Method.** The input is summed to mono and decimated by an integer factor to ~21–24 kHz
(44.1/48 kHz → 2, 88.2/96 → 4, 176.4 → 8, 192 → 9) with a Blackman-Harris windowed-sinc FIR computed
only at output samples. Every 5 ms a YIN estimator (de Cheveigné & Kawahara, 2002) runs on the last
`W + τmax` samples, `τmax = fa / 60 Hz`, `W = τmax`:

1. difference function `d(τ) = e(0) + e(τ) − 2r(τ)` (sliding energy, direct cross-correlation),
2. cumulative-mean-normalised difference `d'(τ)`,
3. first dip of `d'` under the threshold (0.15), followed to its minimum; global minimum if none,
4. octave guards (below), 5. parabolic interpolation, 6. confidence and voicing.

**Search range** 60–1500 Hz: low male voices (B1) to above soprano high C; falsetto included.

**Voicing states:** silence (window RMS < −55 dBFS), voiced (`d' < 0.15`), uncertain
(`0.15 ≤ d' < 0.35`), unvoiced (`d' ≥ 0.35`, e.g. fricatives, breath). Confidence = `1 − d'`.

**Octave handling** (each found necessary by a failing test, then fixed):
- *Double-period guard:* a dominant 2nd harmonic can push the half-period dip under the threshold.
  The longer lag wins only if its energy-normalised raw difference `d(τ)/2e(0)` is < ¼ of the
  shorter lag's **and** at least 0.04 lower. A first version compared `d'` by ratio: on clean tones
  both values are ~0, the ratio was meaningless and the detector read clean tones an octave low.
  A ½ ratio still flipped breathy voices (breath noise sometimes correlates better over the longer
  lag by chance: measured 0.047 vs 0.13); ¼ separates the cases.
- *Sub-multiple guard:* when breath noise lifts `d'(P)` just over the threshold the first-dip rule
  can choose 2P; a lag of τ/2 or τ/3 whose raw difference is as small as at τ is preferred.
- *Continuity:* with no dip under the threshold, a reasonable dip at the previous voiced lag
  (within 60 ms) is kept, avoiding flicker on uncertain frames.

**Interpolation on raw `d(τ)`, not `d'(τ)`.** The normalisation multiplies by τ, tilting the minimum
toward shorter lags. Measured: interpolating on `d'` read 1318 Hz 2.5–3.2 cents sharp; on `d` the
bias is ≤ 0.21 cents.

**Time stamps.** Each frame carries the host-sample index of the analysis window centre
(decimation group delay included), so later stages can align harmony targets with the audio.

**Measured results** (synthetic signals with exactly known pitch; `KuralDspTests`, all 28 pass):

| Test | Result |
|---|---|
| Sine 65 Hz – 1.3 kHz, all six sample rates | worst median 0.24 c, worst p95 0.31 c, 100 % voiced, 0 gross errors |
| Sung vowels /a/ /i/ (glottal source + 3 formants), 82 Hz – 1 kHz, 44.1/48/96 kHz | worst median 0.16 c, worst p95 0.32 c, 0 gross errors |
| Vibrato ±50 c at 5.5 Hz on 131/262/523 Hz | median ≤ 1.73 c, p95 ≤ 3.38 c (tracks the vibrato) |
| Glide of a fifth in 100 ms + octave jumps | 0 octave errors |
| Strong 2nd harmonic / missing fundamental, 98–392 Hz | 0 gross errors |
| Vowel + white noise at 20 dB and 12 dB SNR | 0 % gross errors, 100 % voiced |
| Breathy vowel (aspiration noise at −14 dB) | 0 gross errors, 99.7 % voiced |
| White noise / silence / −69 dBFS tone | 0 voiced frames / all silence / treated as silence |
| Block sizes 1, 37, 4096 | identical frames |
| Pitch step at 500 ms | reported at 509.7 ms |

"Gross error" = more than 50 cents off. These signals prove the algorithm; **real recorded vocals
have not been tested yet** (see VALIDATION.md).

**Cost** (measured, Xeon server core, `-O2`, no SIMD flags): 1.3–1.6 % of one core at any sample
rate (decimation keeps the work constant), shared by all voices. An FFT-based difference function
would cut this further if needed.

**Known limitations:** analysis window 33–37 ms (needed for 60 Hz) delays the reported pitch by
about half a window (~18 ms); pitches above 1.5 kHz (whistle register) are not searched; polyphonic
input (doubled vocals, bleed) is not handled; frames straddling a note change are not yet labelled
as transitions.

## 2. Pitch shifting — Designed (Phase 3), to be benchmarked

**Recommendation: a hybrid, chosen per material and quality mode.**

| | TD-PSOLA (pitch-synchronous overlap-add) | Phase vocoder + spectral envelope | WSOLA + resampling |
|---|---|---|---|
| Formants | preserved naturally (grains keep the envelope); formant shift by resampling grains | need explicit envelope (cepstral / true envelope) | shifted with pitch (chipmunk) unless corrected |
| Transients/consonants | good if unvoiced segments are not shifted | smeared (phasiness) without phase locking/transient handling | good |
| Latency | ~2 pitch periods look-ahead (≈33 ms at 60 Hz, ≈10 ms at 200 Hz) | FFT size: 1024–2048 at 48 kHz (21–43 ms) | window size |
| CPU per voice | very low (overlap-add) | high (FFT per voice; envelope shared) | low |
| Weakness | needs good pitch marks; fails on noisy/polyphonic input | phasiness at large shifts | formants |

Plan:
1. **Primary engine: TD-PSOLA** on voiced frames, using the shared analysis to place pitch marks
   (epochs, refined to waveform peaks in each period). Each voice resynthesises at its own target
   period from the same analysis grains: per-voice cost is only the overlap-add.
2. **Unvoiced material (consonants, breaths) is not pitch-shifted** for harmony voices: it is either
   passed at reduced level or omitted per voice (user choice), which avoids duplicated, exaggerated
   consonants across 16 voices.
3. **High Quality mode:** phase vocoder with identity phase locking and the spectral envelope
   estimated once (shared), re-applied per voice. Benchmarked against PSOLA on real vocals; kept
   only if it sounds better for large intervals.
4. **Low Latency mode:** PSOLA with a raised pitch floor (shorter look-ahead) for tracking.

Before choosing, both engines are prototyped offline and compared on the real-vocal set (intervals
−12…+12, sustained vs. melismatic, breathy), listening plus objective checks (pitch accuracy of the
output via the detector, spectral-envelope distance for formant preservation, CPU per voice).
Plain resampling is not used as a shifting method.

## 3. Harmony generation — Designed (Phase 3/7)

- Pitch classes 0–11 with key root 0–11; scales as interval sets: major, natural/harmonic/melodic
  minor, major/minor pentatonic, blues, chromatic, custom (12-bit mask).
- **Diatonic intervals** are computed in scale degrees, not semitones: the detected pitch is snapped
  to the nearest scale degree (with hysteresis so vibrato does not flip degrees), the voice's
  interval (e.g. a third = +2 degrees) gives the target degree, converted back to a pitch class and
  octave. The singer's deviation from the snapped note (vibrato, slides, expression) is added back so
  harmonies follow the performance instead of being quantised.
- A note *between* scale degrees (chromatic passing tone) keeps the interval of the nearest degree.
- **Chord-aware mode:** each voice targets the nearest chord tone above/below; chords from a
  user-defined progression (bar/beat via host transport) or from MIDI.
- **MIDI mode:** held notes become voice targets directly (lowest note → voice 1 …); chords allocate
  voices to notes. Note changes glide over a short, settable time.
- Target changes are smoothed on note boundaries only (portamento), never within a held note.

## 4. Formant processing — Designed (Phase 3/5)

- Preservation comes with PSOLA. Per-voice formant shift (±12 semitones) by resampling each grain by
  a factor independent of the pitch ratio.
- In the phase-vocoder path: spectral envelope by cepstral smoothing (true-envelope iteration if
  quality needs it), computed once per frame, shared.
- Defaults per register: voices more than ~5 semitones above the lead get a small upward formant
  correction (−20…−40 % of the pitch shift) so they do not sound like a sped-up lead; lower voices
  a matching downward one. All adjustable.

## 5. Naturalisation — Designed (Phase 5)

Per voice, deterministic (seeded) slow random walks — band-limited noise, not periodic LFOs — for:
pitch drift (±3–8 c), onset timing (0–40 ms, applied at note onsets via a per-voice delay line, not
continuous wow), vibrato depth/rate offsets, formant variation (±3 %), gain (±1 dB) and a small
spectral tilt difference for timbre. A global **Naturalness** amount scales all of them; per-voice
amounts override. Seeds are fixed per voice so renders are repeatable and presets sound the same
every time. Limitation, stated plainly: transforming one performance cannot recreate 16 independent
performances; these cues create the impression of several singers, not new performances.

## 6. Creative modes — Designed (Phase 6)

Each mode is a set of real engine settings (not a label): Natural (preserve formants, light
naturalisation), Tight (no timing offset, minimal drift), Wide (spread pan/width, moderate
variation), Choir (more voices, larger variation, wider register distribution), Synthetic (formant
shift and modulation pushed, PSOLA artefacts allowed), Octave (±12/±24 with formant handling).

## 7. Optional local AI — Open (Phase 8)

Candidates to evaluate later, all local/offline through ONNX Runtime on a background thread:
neural pitch estimators (e.g. CREPE, FCPE, RMVPE – licences and model sizes to be checked) as an
offline "high accuracy analysis" for rendering, and timbre/voice-characterisation models for voice
variation. Admission criteria: measurable improvement over the DSP path on the real-vocal set,
licence compatible with a commercial product, CPU/latency benchmarked, DSP fallback always present.
Nothing AI-related is implemented or claimed now.

## 8. Quality modes and latency — Designed

Low Latency / Balanced / High Quality will differ in shifting engine, look-ahead and analysis
settings; each mode's latency is measured and reported to the host exactly, and the dry path is
delayed by the same amount so dry and wet stay aligned. Current build: latency 0 (nothing delays
the audio; analysis runs alongside).
