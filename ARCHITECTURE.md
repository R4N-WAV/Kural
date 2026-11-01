# Kural – Architecture

Kural by R4N is a Windows x64 VST3 vocal harmonizer: one vocal in, the dry vocal plus up to 16
harmony voices out. This document covers how the code is organised, how data and threads flow, and
how Kural fits into the existing R4N projects.

## 1. R4N ecosystem findings (inspected 1 Nov 2026)

| Repository | What it contains | Relevance to Kural |
|---|---|---|
| `R4N-WAV/anbu` | Complete, released plugin (optical leveler + colour stage). JUCE 8.0.8 via CMake FetchContent, C++17, static MSVC runtime, VST3 (Windows) + VST3/AU (Mac). ~5,800 lines. | The only R4N codebase with real code. Primary reference for build, packaging, CI and visual language. |
| `R4N-WAV/Isai` | README only ("Synth VST3"). | No code or design to study yet. |
| `R4N-WAV/isai-reference` | README only. | Nothing to study. |
| ATELIER | Not found in any R4N repository this account can reach. | Nothing to reuse. If it exists elsewhere, point us to it. |

**What Anbu does well and Kural adopts (as patterns, not copied code):**
- Build: JUCE 8.0.8 fetched by CMake, `KURAL_JUCE_DIR` for a local copy, static runtime (no VC++ redistributable), JUCE splash/telemetry off, `R4Nx` manufacturer code shared by the family.
- Testing: a console stress program run in CI over sample rates, block sizes (including blocks larger than announced), layouts, automation, state and bypass; a headless editor snapshot tool; pluginval in CI.
- Packaging: Inno Setup 6.7 dark wizard installing into `C:\Program Files\Common Files\VST3`, uninstaller kept in `Program Files\R4N\<name>`, locked-file check, CI that installs, verifies and uninstalls on a clean runner, and publishes a `latest` release from `main`.
- Visual grammar: the product's one-word Tamil name as the logo in **Catamaran 800**, "by R4N" beneath in Catamaran 600, all labels in **Barlow Semi Condensed**, thin accent hairlines (Anbu: gold "zari" thread), a dark analysis screen, design-unit layout scaled as a whole.

**What Kural does differently, on purpose:**
- Anbu is a skeuomorphic hardware panel with pre-rendered 3D assets (Python + C++ height-field renderer). Kural's centrepiece is a live pitch/harmony display that changes every frame, so its surface is drawn live (vector), with no pre-rendered hardware.
- Palette: Anbu's maroon plate and gold thread become deep water (abyss → shelf → lit surface) and a **pearl** thread (the Gulf of Mannar pearl beds off the Tamil coast), with a muted cyan voice line and sea-glass accents. See `Source/UI/Theme.cpp`.
- DSP lives in a framework-free static library (`KuralDsp`) so it can be unit-tested without JUCE and reused by offline tools. Anbu keeps DSP in headers inside the plugin.
- Windows only (the brief targets Windows x64 VST3); no AU/Mac job.

**Reused verbatim:** the five OFL font files (Catamaran 600/800, Barlow Semi Condensed 500/600/700) and their licence texts. Nothing in Anbu or Isai was modified.

## 2. Repository layout

```
CMakeLists.txt            build: KuralDsp (no JUCE), Kural VST3, test tools
Source/
  DSP/                    framework-free C++17 (unit-tested in Tests/DspTests.cpp)
    Decimator.h           integer-factor FIR decimator for the analysis path
    PitchDetector.h/.cpp  YIN-based pitch detector with confidence, voicing and octave guards
  Params.h/.cpp           parameter IDs and layout (IDs are permanent once released)
  PluginProcessor.h/.cpp  audio engine host glue, analysis FIFO, state
  PluginEditor.h/.cpp     editor: layout, look-and-feel, controls
  UI/Theme.h/.cpp         palette, fonts, Tamil name, pearl hairline
  UI/PitchView.h/.cpp     live pitch display (real analysis frames only)
Tests/
  DspTests.cpp            pitch accuracy/robustness on known-truth signals (no JUCE)
  KuralStress.cpp         processor robustness + no-allocation-on-audio-thread check
  KuralSnapshot.cpp       headless render of the real editor fed by the real processor
Resources/fonts/          OFL fonts compiled into the binary
packaging/windows/        Inno Setup script, artwork generator and artwork
.github/workflows/        CI: tests, pluginval, installer test, release
```

Planned modules (created when their phase starts, each in `Source/DSP/` unless noted):
`Harmony/` (scale and chord logic, interval → target pitch), `Shifter/` (PSOLA and phase-vocoder
voices), `Formant/` (spectral envelope), `Voice/` (per-voice processing chain and naturalisation),
`Mixer/` (voice summing, pan/width, dry/wet), `Presets/` (in `Source/`), `AI/` (optional, separate
library, never on the audio thread).

## 3. Signal and data flow

Current build:

```
input (mono/stereo) ──┬──────────────────────────────► output gain ─► bypass crossfade ─► output
                      │                                 (dry path, zero latency)
                      └─► mono sum ─► Decimator ─► YIN analysis (every 5 ms) ─► PitchFrame
                                                                                   │
                                                          lock-free FIFO (2048) ◄──┘
                                                                   │
                                                 editor timer (60 Hz) ─► PitchView
```

Target (Phases 3–7): the same PitchFrames feed a `HarmonyPlanner` that turns the detected pitch,
key/scale/chord (or MIDI notes) and each voice's interval into a target pitch per voice. Each active
voice then runs a pitch shifter + formant stage + timing/naturalisation + per-voice processing; the
`Mixer` sums voices with pan/width into stereo and blends them with the dry vocal (delayed by the
reported latency so dry and wet stay aligned). Analysis is computed **once** and shared by all voices.

## 4. Threading model

| Thread | May do | Must never do |
|---|---|---|
| Audio (processBlock) | DSP on preallocated buffers, atomics, push to lock-free FIFO | allocate/free, lock, file or network I/O, UI calls, model loading, unbounded loops |
| Message (editor) | pop FIFO, read atomics, paint, parameter changes via APVTS attachments | touch DSP state directly |
| Background (future: AI, preset scanning) | heavy work, file I/O | block or wait on the audio thread |

- All allocation happens in `prepareToPlay` (and constructors). This is **tested**: `KuralStress`
  hooks the global allocator and fails if `processBlock` allocates or frees on its own thread, at
  every supported sample rate, block size and layout.
- Hosts may send blocks larger than announced; `processBlock` splits them into announced-size pieces.
- Parameters are read from APVTS atomics; gain changes are smoothed (30 ms), bypass crossfades (20 ms).
- The editor can be closed at any time; the audio engine does not depend on it (the FIFO just drops
  frames when nobody reads).

## 5. Parameters and state

- Registered now: `output` (dB, −24…+12), `bypass` (linked to the host's bypass).
- Rule: a parameter is registered only when the engine behind it exists. IDs are never renamed or
  reused once released; new parameters get a higher `ParameterID` version hint.
- State is the APVTS tree as XML plus `stateVersion` and `pluginVersion` attributes. A missing
  parameter (session from an older version) falls back to its default; unknown ones are ignored;
  garbage is ignored. All three cases are tested.

Planned IDs (reserved, not registered yet): global `key`, `scale`, `mode`, `voiceCount`,
`harmonyMix`, `dryLevel`, `harmonyLevel`, `humanize`, `naturalness`, `formantPreserve`, `width`,
`quality`, `globalPitch`, `globalTiming`; per voice `vNN_on`, `vNN_interval`, `vNN_semis`,
`vNN_cents`, `vNN_octave`, `vNN_formant`, `vNN_timing`, `vNN_humanize`, `vNN_vibAmt`, `vNN_vibRate`,
`vNN_gain`, `vNN_pan`, `vNN_width`, `vNN_mute`, `vNN_solo` (NN = 01…16). That is ~15 × 16 + 14 ≈ 250
automatable values; the per-voice set will be reviewed before release to keep the host list usable
(e.g. voice character and per-voice processing as non-automatable state).

## 6. Bus layouts and MIDI

- Input mono or stereo; output stereo (mono input is copied to both sides), or mono → mono.
- The plugin declares a MIDI input from the first build so the bus configuration never changes later
  (hosts cache it). MIDI is currently cleared and ignored; it drives harmony notes in Phase 7.

## 7. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Pitch-shift quality on real vocals (artifacts, phasiness, "chipmunk") | Core value of the product | PSOLA for voiced material, unvoiced passed through, phase vocoder with spectral envelope as HQ path; listening tests on real vocals before any phase is called done |
| Latency vs. quality: PSOLA needs ~2 pitch periods of look-ahead (≈33 ms at 60 Hz) | Tracking/monitoring feel | Quality modes with honestly reported latency; Low Latency mode with a higher pitch floor |
| CPU with 16 voices × per-voice processing | Usability in real sessions | Shared analysis (done), inactive voices skipped, per-voice processing optional and grouped, benchmarks at 1/4/8/12/16 voices |
| Host differences in MIDI routing to effects (FL Studio, Cubase, Reaper) | MIDI mode usability | Test in each host; document routing per host |
| ~250 automatable parameters | Host parameter lists become unusable | Review exposure before release; group per-voice settings |
| No DAW available in the build environment | DAW loading can only be verified by a person on Windows | pluginval (strictness 10) + install test in CI; manual DAW checklist in VALIDATION.md |
| Unsigned installer | SmartScreen warning | Documented; code signing is a later decision (cost) |
