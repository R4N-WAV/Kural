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
- *Breath continuity:* with heavy breath noise (coloured by the formants) the double/triple period can
  genuinely correlate better than the true one within a frame (measured dn 0.07–0.15 at 2P vs
  0.15–0.26 at P), so no single-frame rule can decide. If the previous frame was voiced at P and the
  new one lands on 2P/3P while P is still nearly as good (dn(P) ≤ 2·dn(2P) + 0.05), P is kept. A real
  octave drop is not held back (at the old lag a note an octave lower leaves its odd harmonics
  uncancelled); tested with drops under breath noise. Found when the Windows CI run produced
  different breath noise than Linux (see VALIDATION.md) and one frame failed.

**Interpolation on raw `d(τ)`, not `d'(τ)`.** The normalisation multiplies by τ, tilting the minimum
toward shorter lags. Measured: interpolating on `d'` read 1318 Hz 2.5–3.2 cents sharp; on `d` the
bias is ≤ 0.21 cents.

**Time stamps.** Each frame carries the host-sample index of the analysis window centre
(decimation group delay included), so later stages can align harmony targets with the audio.

**Measured results** (synthetic signals with exactly known pitch; `KuralDspTests`, all 30 pass):

| Test | Result |
|---|---|
| Sine 65 Hz – 1.3 kHz, all six sample rates | worst median 0.24 c, worst p95 0.31 c, 100 % voiced, 0 gross errors |
| Sung vowels /a/ /i/ (glottal source + 3 formants), 82 Hz – 1 kHz, 44.1/48/96 kHz | worst median 0.16 c, worst p95 0.32 c, 0 gross errors |
| Vibrato ±50 c at 5.5 Hz on 131/262/523 Hz | median ≤ 1.73 c, p95 ≤ 3.38 c (tracks the vibrato) |
| Glide of a fifth in 100 ms + octave jumps | 0 octave errors |
| Strong 2nd harmonic / missing fundamental, 98–392 Hz | 0 gross errors |
| Vowel + white noise at 20 dB and 12 dB SNR | 0 % gross errors, 100 % voiced |
| Breathy vowel, aspiration −20 dB, 36 noise realisations | 0 % gross errors, 100 % voiced |
| Breathy vowel, aspiration −14 dB, 36 noise realisations | 0.14 % gross errors, 95.1 % voiced |
| Octave drop under −14 dB breath noise, 24 realisations | 0 frames left on the old note |
| White noise / silence / −69 dBFS tone | 0 voiced frames / all silence / treated as silence |
| Block sizes 1, 37, 4096 | identical frames |
| Pitch step at 500 ms | reported at 509.7 ms |

Breath noise, wider sweep (120 realisations each, 110/196/392 Hz; tuning seeds 1–40, held-out
seeds 41–80 shown in brackets): −20 dB 0 % gross; −14 dB 0.12 % (0.09 %); −10 dB 3.2 % (3.7 %),
down from 0.47 % and 15.7 % before the breath-continuity guard. Very breathy singing (aspiration
within 10 dB of the voice) therefore still produces occasional octave errors — a known limitation,
to be re-measured on real recordings.

"Gross error" = more than 50 cents off.

**Measured on real singing** (`tools/eval/eval_pitch.py` on the `test-vocals` branch: 244 tracks;
62 scored against a reference, 209,623 reference-voiced frames; references: vocadito annotations,
Dagstuhl ChoirSet manual or CREPE larynx-microphone f0; 2 choir tracks excluded with the reason in
`tools/eval/excluded_tracks.txt`):

| Material | Voicing recall | Within 50 c (RPA) | Octave errors | Median error |
|---|---|---|---|---|
| Solo singers (vocadito, 40) | 94.8 % | 96.4 % | 0.99 % | 2.0 c |
| Choir, close mics with bleed (DCS, 22) | 94.4 % | 91.0 % | 4.15 % | 2.7 c |
| — soprano / alto / tenor / bass | 96.9 / 92.7 / 95.3 / 93.4 % | 96.3 / 85.4 / 90.6 / 91.5 % | 0.2 / 5.8 / 5.0 / 5.1 % | 1.6–3.9 c |

VocalSet (180 tracks, 20 singers, no reference): isolated octave outliers (a voiced frame more than
6 semitones from its ±50 ms neighbours) 0.55 % overall; breathy 0.03 %, straight 0.19 %, belt 0.50 %,
spoken 0.53 %, vibrato 0.70 %; **vocal fry 3.1 % (female 5.9 %)** — the hardest material, as expected
for irregular glottal pulses.

- Octave errors are almost all an octave *down* (two periods chosen). On the choir tracks the close
  mics also pick up the other singers: two voices a fifth apart repeat together every two periods
  of the lower one, and that common period is found (d′ ≈ 0.02 at 2P against 0.16–0.20 at P). On
  solo voices the few errors are creaky stretches where alternate cycles differ.
- A sweep of the threshold (0.15/0.20/0.25) and the double-period guard (0.25/0.10/off) moved RPA
  only within 94.3–94.9 % overall; the defaults are best on solo voices and were kept.
- False alarms (Kural voiced where the reference is not): 9.3 % on solo singers (mostly breathy
  note ends); 35 % on the choir tracks, where the other singers are audible during this singer's rests.
- Consequence for harmonies: an octave-low reading moves the lead's scale note and the harmony note
  together, so the shift in semitones is unchanged; only the pitch screen shows the lead an octave
  low for those frames.

**Cost** (measured, Xeon server core, `-O2`, no SIMD flags): 1.3–1.6 % of one core at any sample
rate (decimation keeps the work constant), shared by all voices. An FFT-based difference function
would cut this further if needed.

**Known limitations:** analysis window 33–37 ms (needed for 60 Hz) delays the reported pitch by
about half a window (~18 ms); pitches above 1.5 kHz (whistle register) are not searched; polyphonic
input (doubled vocals, bleed) is not handled; frames straddling a note change are not yet labelled
as transitions.

## 2. Pitch shifting — Implemented (TD-PSOLA in the plugin; phase vocoder as the compared alternative)

Three shifters were built and compared (code in `Source/DSP/Psola.*`, `Source/DSP/PhaseVocoder.*`;
the ATELIER baseline in `tools/eval/KuralLab.cpp`):

| | TD-PSOLA (Kural) | Phase vocoder (Kural) | Rotating-tap (ATELIER baseline) |
|---|---|---|---|
| Principle | pitch-synchronous two-period grains re-spaced by period/ratio | Laroche–Dolson peak shifting with identity phase locking, cepstral envelope | two cross-faded delay taps, delay ramps at (1 − ratio) over a 50 ms grain |
| Formants | kept (grains keep the envelope); shifted by resampling grains | kept by envelope rescaling; shifted by reading the envelope at k/ratio | move with the pitch |
| Consonants | passed unshifted at the voice's consonant level | frames marked unvoiced pass unshifted at that level | shifted like everything else |
| Shared analysis | detector + pitch marks | detector + one forward FFT, envelope, peaks | none |
| Per-voice work | overlap-add of grains | peak moves + one inverse FFT per hop | two interpolated taps |
| Latency | 2 periods of 65 Hz + 16 samples = 31.1 ms at 48 kHz | FFT size: 2048 at 44.1/48 kHz = 42.7 ms | ~W/2 = 25 ms (not reported by ATELIER) |
| CPU, 1 / 16 voices (incl. analysis) | 1.8 % / 5.5 % of one core | 2.6 % / 7.0 % | – |

**TD-PSOLA in detail.**
- *Pitch marks (shared).* Marks are placed one period apart, the period coming from the latest
  detector frame. Each new mark is aligned with the previous one by maximising the normalised
  correlation of the period before each mark (search ±15 % of a period, coarse on a stride of
  fs/24 kHz, then fine). The best correlation doubles as an instant voicing measure: two consecutive
  periods that correlate below 0.55 are unvoiced, whatever the detector said ~20 ms ago. In unvoiced
  material marks are 10 ms apart.
- *Voices.* Each voice keeps a synthesis time `ts` on the input time axis. Whenever a grain's left
  edge reaches the output position, the latest mark at or before `ts` is taken, a Hann grain of ±1
  period around it is overlap-added around `ts` (cubic interpolation, so `ts` can be fractional), and
  `ts` advances by period / ratio. Only marks with a full period of input after them are used, so
  no grain ever reads input that has not arrived (constant latency 2 × longest period).
- *Formant shift* resamples the grain (output length 2P / formant ratio), independent of the pitch.
- *Unvoiced grains* go to a separate accumulator at the consonant level and are never shifted, so
  16 voices do not stack 16 shifted copies of every "s".
- *Level.* Overlapping grains interfere differently for every waveform and interval (measured:
  −0.4 to −7 dB with a fixed 1/√ratio law), so the voiced part is level-matched to the input:
  power of shifted output vs. input at the same time, 40 ms averages, gain glides over 60 ms, only
  while voiced. Result: within ±0.4 dB from −12 to +12 st.
- *Ratio.* From the harmony planner (§3): it changes only when the harmony note changes and glides
  (default 30 ms); between changes the voice follows the lead's own vibrato at a constant interval.

**Measured on synthetic sung vowels (`KuralDspTests`, all pass):** unshifted reconstruction 69 dB
SNR (98/220/523 Hz); −12…+12 st on 110/220/440 Hz: median error ≤ 0.45 cents, 0 gross errors,
level within 0.42 dB; +4 st on a vibrato line with a glide: median 4.8 c; first formant kept within
4 % for ±7 st, moved ×1.31/×0.72 for a ±5 st formant shift (expected ×1.335/×0.749); consonant
noise at exactly the set level; identical output for block sizes 1 and 4096. Phase vocoder: 88 dB
unshifted, ≤ 0.36 c median error.

**Real-vocal comparison** (`tools/eval/eval_shift.py`, 12 singers: 8 vocadito solo voices and DCS
soprano/alto/tenor/bass; −12…+12 st; consonant level 0.3; formants preserved):

| | On pitch (all shifts) | Pitch error | Formant drift | Periodicity | Level | CPU, 1 voice | Latency |
|---|---|---|---|---|---|---|---|
| **TD-PSOLA (Kural)** | 91.4 % | 3.1 c | **2.1 dB** | 0.98 | +0.1 dB | 1.6 % | 31 ms |
| Phase vocoder | 84.9 % | 3.3 c | 3.0 dB | 0.94 | −1.7 dB | 2.1 % | 46 ms |
| ATELIER rotating tap | 91.7 % | 4.4 c | 7.8 dB | 0.96 | −1.3 dB | 0.2 % | 25 ms |

Unshifted floors: formant drift 0.4 / 1.1 / 0.1 dB. "On pitch" counts input-voiced frames whose
output is within 50 c of the target; PSOLA leaves consonants unshifted by design, which costs it a
little here. −12 st is under-counted for every shifter: the bass voices land below the detector's
60 Hz floor.

**Two PSOLA flaws found on real singing and fixed:**
- *Drift under vibrato:* synthesis advanced by the period before the chosen mark; it now advances by
  the distance to the next mark. Unshifted output became the input delayed by exactly the reported
  latency (51–57 dB local SNR), and the vibrato test error fell from 4.8 to 0.6 cents.
- *Unshifted note onsets:* 97 % of off-pitch frames were within 40 ms of a voicing edge, i.e. the
  first periods of every note passed unshifted (an audible dry flam). A mark now counts as voiced if
  the period after it is periodic too, and the marker probes the recently heard period itself while
  the detector (≈20 ms late) still says unvoiced. On pitch: 89.5 % → 91.4 % over the 12 singers.

**Third fix, after the owner's listening test ("PSOLA sounds best, but some parts sound robotic"):**
snapping each synthesis grain to one mark repeats (up) or drops (down) whole periods at a rate of
|f_out − f_in|, which leaves a *ghost of the original pitch* in the harmony. New measure in
`eval_shift.py` (energy at the input's harmonics relative to the output's, 8 real singers):
PSOLA −31.9 dB before (phase vocoder −36.3, ATELIER −34.0). Grains are now blended from the two
periods around each synthesis point: −36.4 dB overall, +12 st −34.0 → −48.3 dB, +4 st −33.8 → −36.7;
−5 st unchanged (−28.4 dB) — downward shifts still need work.

**Downward shifts, investigated further (8 singers, −5/−7 st):**
- The ghost measure was biased against downward shifts: a fixed 46 ms window has the same width in
  Hz for every voice, so with the narrower harmonic spacing of a lowered voice its leakage reached the
  measured bins. It now uses 10 periods of the voice being measured, and a new column, *ghost excess*,
  subtracts the input's own energy at the same places between its harmonics (breath, jitter and
  vibrato put some there in any voice). With the fair measure, PSOLA adds +2.9 dB at −5 st and +3.1 dB
  at −7 st; a plain resampler (ATELIER tap) adds +1.8/+2.0, the phase vocoder +6.6/+6.6. Downward PSOLA
  is close to the floor of the method, not broken.
- Tried and rejected (measured worse, kept out of the code):
  - narrower grains for downward shifts (0.5–0.8 periods): ghost no better, formant error 2.4 → 3.6 dB;
  - pulse-anchored marks (snap or pull each mark onto the energy peak of its period): on pitch
    97.5 → 95.3 %, ghost excess +2.9 → +4.8 dB (the peak picker adds jitter); snap-only: no change;
  - periodic/aperiodic split (grains from the average of two periods, the rest passed unshifted):
    jitter leaks the original harmonics into the unshifted part, ghost excess −0.5 → +4.5 dB overall.
- Finding to act on: on **upward** shifts the excess is negative (−4.0 dB at +4 st): PSOLA leaves *less*
  breath and turbulence between the harmonics than the singer had, i.e. the shifted voice is
  over-clean. That is a plausible part of "robotic" and a target for naturalisation (§5): add back
  aperiodic energy shaped like the input's, without pitch.

**Choice:** TD-PSOLA for the plugin — best formant preservation, exact level, lowest latency of the
formant-preserving candidates, lowest CPU per voice of the two. The phase vocoder is kept in the code
for further comparison but not used. The listening test is the final judge (VALIDATION.md).

**Known limitations:** below 65 Hz grains are clamped (artifacts on very low bass notes); a second
voice in the input (bleed, doubles) breaks pitch marking; unison voices (ratio 1) sit up to one
period after the dry signal and comb against it unless detuned or delayed (Phase 5 naturalisation).

## 3. Harmony generation — Implemented (scales, diatonic intervals); chords and MIDI in Phase 7

`Source/DSP/Harmony.*`, tested in `Tests/HarmonyTests.cpp` (expected notes written out by hand).
- Scales: major, natural/harmonic/melodic minor, major/minor pentatonic, blues, chromatic, custom
  (12-bit mask, root always included). Degrees count every scale note from the root.
- Intervals in scale steps (a third = 2 steps: major or minor as the key requires) or fixed
  semitones, plus octaves and cents.
- The singer's expression is kept: target = harmony note + (lead − lead's scale note), so the shift
  is constant while the lead holds a note, through vibrato and scoops.
- **Note decisions** (which scale note the lead is on), found necessary by a failing test (wide
  vibrato flipped the harmony 19× in 3 s with simple hysteresis):
  - *arrival*: the lead within 25 cents of a scale note for 25 ms → follow (steps/leaps: 25 ms);
  - *centre*: a 100 ms smoothed lead crossing the midpoint by more than 30 cents → follow (catches
    slow slides; vibrato up to ±65 c centred between two notes never triggers it);
  - the first note of a phrase (after 300 ms of silence) is taken at once.
- A chromatic passing note keeps the interval of the scale note it is nearest to (parallel motion).

## 4. Formant processing — Implemented in PSOLA (§2); per-register defaults in Phase 5

Formants are preserved by construction (grains); the per-voice formant control (±12 st) resamples
grains. Planned for Phase 5: defaults that lean high voices slightly brighter/smaller and low voices
darker, as a starting point the user can change.

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

## 8. Quality modes and latency — Designed (current build: one mode, 31 ms at 48 kHz)

Low Latency / Balanced / High Quality will differ in shifting engine, look-ahead and analysis
settings; each mode's latency is measured and reported to the host exactly, and the dry path is
delayed by the same amount so dry and wet stay aligned. Current build: one mode, PSOLA with a 65 Hz
floor: 1493 samples at 48 kHz (31.1 ms), reported to the host and applied to the dry path; it does
not change when harmonies are switched off or bypassed. A Low Latency mode with a 100 Hz floor
would be ~21 ms (low male voices would then use clamped grains).
