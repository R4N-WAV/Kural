# Kural – Validation

Only results that were actually run are recorded here. "Pending" means not yet done.

## Test environments

| Environment | Used for | Status |
|---|---|---|
| Linux x86-64 (Ubuntu 24.04, GCC 13.3, Xeon server core), development container | DSP tests, stress test, snapshots | Run 1 Nov 2026, see below |
| GitHub Actions `windows-2022`, MSVC 2022, x64 | Release build, all tests, pluginval, installer install/uninstall on a clean machine | **All green since run 3 (commit 31466d0)** |
| GitHub Actions `ubuntu-24.04` | DSP tests on every push | Green |
| FL Studio / Cubase / Reaper on Windows 10/11 | Real host loading and use | **Pending (manual; no DAW in the build environment)** |

## Automated results (Linux, 1 Nov 2026, version 0.1.0)

- `KuralDspTests`: **30/30 pass** – figures in DSP_DESIGN.md §1.
- `KuralStress` (full): **90/90 pass**:
  - 44.1, 48, 88.2, 96, 176.4, 192 kHz × blocks of 1, 64, 512, 4096 (random sizes up to 2× the announced size) × layouts 1→1, 1→2, 2→2: finite output, **0 allocations/frees on the audio thread**, pitch frames produced.
  - Hostile input (silence, DC, impulses, Nyquist, +18 dBFS, denormal-range): finite, no allocation.
  - Output gain and bypass automated every block: no allocation, largest sample step 0.248 on a vocal-like signal (smoothing works).
  - Bypass parameter and 0 dB: bit-exact pass-through, latency 0. Host bypass: crossfade with no jump (largest step 0.011), bit-exact after the fade.
  - −6 dB measured as ×0.5012. Mono in on stereo out reaches both channels.
  - State round trip; state version saved; older state with a missing parameter falls back to the default; garbage state ignored.
  - Re-prepared at changing sample rates/block sizes: finite, no allocation.
  - Editor created, resized and destroyed 5× (headless).
- `KuralSnapshot`: editor rendered at 1× and 2×; Tamil logo குரல் shaped correctly (கு ligature, pulli on ல்); pitch line follows the synthetic phrase including glides, vibrato and gaps for the consonant and the breath.
- Pitch detector CPU: 1.3–1.6 % of one core at 44.1–192 kHz.

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

## Audio quality validation (pending – starts with Phase 3)

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
