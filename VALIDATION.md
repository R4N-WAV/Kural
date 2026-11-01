# Kural – Validation

Only results that were actually run are recorded here. "Pending" means not yet done.

## Test environments

| Environment | Used for | Status |
|---|---|---|
| Linux x86-64 (Ubuntu 24.04, GCC 13.3, Xeon server core), development container | DSP tests, stress test, snapshots | Run 1 Nov 2026, see below |
| GitHub Actions `windows-2022`, MSVC 2022, x64 | Release build, all tests, pluginval, installer install/uninstall on a clean machine | **All green since run 3 (commit 31466d0)** |
| GitHub Actions `ubuntu-24.04` | DSP tests on every push | Green |
| FL Studio / Cubase / Reaper on Windows 10/11 | Real host loading and use | **Pending (manual; no DAW in the build environment)** |

## Automated results (Linux, 1 Nov 2026, version 0.2.0)

- `KuralDspTests`: **66/66 pass** – pitch detection (30), harmony planning (16), pitch shifting (8), pitch correction (12).
  Figures in DSP_DESIGN.md §1–§3.
- `KuralStress` (full): **92/92 pass**, with the harmony engine running (default: two voices):
  - 44.1, 48, 88.2, 96, 176.4, 192 kHz × blocks of 1, 64, 512, 4096 (random sizes up to 2× the announced size) × layouts 1→1, 1→2, 2→2: finite output, **0 allocations/frees on the audio thread**, pitch frames produced.
  - Hostile input (silence, DC, impulses, Nyquist, +18 dBFS, denormal-range): finite, no allocation.
  - Output gain and bypass automated every block: no allocation, largest sample step 0.248.
  - Bypass parameter, and harmonies off at 0 dB: exactly the input delayed by the reported latency (1493 samples at 48 kHz), bit for bit. Host bypass: crossfade without a jump, then the delayed input bit-exact.
  - **Harmony note:** a sung C4, voice 1 a third above, dry off → 329.7 Hz (E4, expected 329.6) in C major and 311.2 Hz (E♭4, expected 311.1) in C minor.
  - −6 dB measured as ×0.5012; mono in on stereo out reaches both channels; state round trip, version, older-state defaults, garbage state; re-prepare at changing rates/blocks; editor created/resized/destroyed 5×.
- `KuralSnapshot`: editor rendered with the default voices on a synthetic phrase: the voice lines sit a
  scale-correct third above and below the lead (G→B/E, A→C/F, B→D/G, D→F/B); Tamil logo shaped correctly.
- **CPU** (`KuralLab bench`, 20 s sung line at 48 kHz, one Xeon server core, Release build, analysis included):

  | Voices | 1 | 4 | 8 | 12 | 16 |
  |---|---|---|---|---|---|
  | TD-PSOLA (in the plugin) | 1.8 % | 2.6 % | 3.5 % | 4.3 % | 5.5 % |
  | Phase vocoder (compared) | 2.6 % | 3.5 % | – | – | 7.0 % |

  Desktop CPUs are typically faster per core than this server core; to be re-measured in a DAW on Windows.
- **Latency:** 1493 samples at 48 kHz (31.1 ms), 1373 at 44.1 kHz, 2970 at 96 kHz; reported to the host.

## Windows CI history

- Run 1 (commit f6c4208): MSVC build and configure succeeded; the Ubuntu DSP job passed. The Windows
  DSP test **failed one check**: the breathy-vowel test produced one octave error. Cause: the test
  used `std::normal_distribution`, which MSVC and libstdc++ implement differently, so Windows saw
  different breath noise than Linux — and on that noise the detector made an error the Linux seed
  had hidden. Fixes: portable Gaussian noise in the tests; breathy tests now run over 36 noise
  realisations with an error-rate limit; a breath-continuity guard in the detector (gross errors at
  −14 dB breath 0.47 % → 0.12 %, at −10 dB 15.7 % → 3.2 %, confirmed on held-out seeds); a new test
  that real octave drops are still followed under breath noise.
- Run 2 (commit e319481): build, DSP tests (30/30, identical figures to Linux), robustness checks
  (90/90), editor snapshots and **pluginval strictness 10 passed** on Windows; the installer was
  built, installed, replaced a stale copy and matched the built binary. The extra pluginval pass on
  the *installed* copy failed only because the CI script did not quote the path ("Program Files"
  was split at the space). Script fixed.
- Run 3 (commit 31466d0): **everything green** – DSP 30/30, robustness 90/90, snapshots, pluginval
  strictness 10 on the built plugin, installer built, installed on the clean runner (stale copy
  replaced, binary hash matched, uninstaller outside the VST3 folder), pluginval 5 on the installed
  copy, uninstalled cleanly. Published as the `latest` pre-release (Kural-Setup-Windows.exe 4.2 MB,
  Kural-VST3-Windows.zip 2.7 MB).

## Manual DAW checklist (pending)

For each of FL Studio, Cubase, Reaper:
- [ ] Installer runs; plugin appears after rescan, listed under R4N
- [ ] Loads on a mono and on a stereo vocal track; audio passes; no clicks on insert/remove
- [ ] Pitch display follows a sung vocal; gaps on consonants/breaths; no octave jumps on sustained notes
- [ ] Output automation recorded and played back smoothly
- [ ] Bypass (plugin button and host bypass) is click-free
- [ ] Project saved, closed, reopened: settings restored
- [ ] Sample-rate change (44.1 → 48 → 96 kHz) and buffer-size change while loaded
- [ ] Window resize; Tamil logo rendered correctly on the user's Windows display scaling (100/150/200 %)
- [ ] 1-hour session with transport start/stop: stable
- MIDI routing: not applicable until Phase 7

## Pitch correction on real singers (1 Nov 2026, chromatic scale, `KuralLab tune`)

Pitch measured by the detector on the output. "Note centre" = the 180 ms running mean of the pitch,
distance to the nearest semitone (median over voiced frames).

| Singer | Input: within 10 c / centre off | Natural (25 ms, vibrato kept): centre off / motion kept | Hard (0 ms, flattened): within 10 c / centre off |
|---|---|---|---|
| vocadito 36 (low male) | 20 % / 23.4 c | 16.7 c / 16.1 c (in 15.0) | 75 % / 1.8 c |
| vocadito 37 | 15 % / 27.3 c | 9.6 c / 9.4 c (in 8.8) | 94 % / 0.5 c |
| vocadito 2 | 34 % / 14.3 c | 11.0 c / 8.4 c (in 7.8) | 92 % / 0.6 c |
| VocalSet m4 vibrato | 19 % / 27.9 c | 33.7 c / 45.0 c (in 47.4) | 77 % / 2.0 c |
| VocalSet f3 straight (scales) | 11 % / 36.5 c | 11.0 c / 8.5 c (in 7.3) | 93 % / 0.4 c |

Hard tune does what it should. Natural tune keeps the singer's motion (vibrato and scoops within
±2 c of the input) and moves note centres towards the pitch, but the 180 ms measure counts scoops and
the first frames of quickly reached notes, so it understates it; on wide, slow vibrato (m4) the note
centre is not yet found reliably. To be judged by listening.

## Real-vocal measurements (1 Nov 2026)

Test set: branch `test-vocals`, 244 tracks (vocadito 40 solo tracks; Dagstuhl ChoirSet 24 tracks
with manual or CREPE larynx-mic f0; VocalSet 180 tracks: 20 singers × belt, breathy, fast forte, lip
trill, messa di voce, spoken, straight, vibrato, vocal fry; all CC BY 4.0). Pitch detector and shifter results in DSP_DESIGN.md §1 and §2.
Listening page (A/B, blind option): Kural Listening Test artifact — three singers × three shifters
at +4 and −5 st, and full harmonies. **Owner's listening verdict: pending.**

## Audio quality validation (listening; remaining)

Real-vocal set needed: low male, high male, female, falsetto, spoken, sustained, fast melodic,
melisma, vibrato, breathy, strong sibilance, quiet, loud, different recording qualities.
For each: detector accuracy against a hand-checked reference; once harmony exists: note selection,
formant naturalness, consonant/breath handling, transitions, stereo/mono compatibility, artifacts
(listening, A/B), latency (measured), CPU at 1/4/8/12/16 voices, memory, long-run stability.

## Release criteria (Phase 10)

All automated checks green on Windows CI; manual checklist passed in all three hosts; listening
review signed off on the real-vocal set; measured latency and CPU documented; installer tested on a
clean Windows 10 and 11 machine.

## Known limitations

See TODO.md "Known issues".
