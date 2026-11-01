# Kural – Development plan

Priorities: real DSP before visual polish, sound quality before feature count, reliability before
complexity. A phase is done only when its deliverable is validated, including listening on real
vocals where audio is involved.

| Phase | Deliverable | Status |
|---|---|---|
| 1. Research and foundation | Stable, recognisable Kural VST3 foundation | **Done in code; Windows CI run pending; DAW check pending (manual)** |
| 2. Core pitch engine | Reliable vocal analysis | **Implemented and tested on synthetic signals; real-vocal validation pending** |
| 3. First harmony engine | Harmonizer with a few high-quality voices | Next |
| 4. Expand to 16 voices | Working 16-voice engine, CPU benchmarked | Planned |
| 5. Naturalness and voice differentiation | Convincing layered stacks | Planned |
| 6. Creative processing | Modes, per-voice processing, stereo | Planned |
| 7. Musical and MIDI integration | Chord-aware harmony, MIDI notes/chords, automation | Planned |
| 8. Optional AI | Local, optional, genuinely useful AI features | Research later; never blocks the core |
| 9. Interface refinement | Polished commercial UI, logo, Tamil verified in hosts | Planned (foundation exists) |
| 10. Validation and release | Installable, validated release | Pipeline exists; release criteria in VALIDATION.md |

## Phase 1 – what exists

- Repository structure, CMake build (JUCE 8.0.8), framework-free `KuralDsp` library.
- VST3 plugin: mono/stereo in, stereo out, MIDI input declared, output gain, click-free bypass,
  state save/restore with versioning, zero latency.
- Editor in the R4N family language with Kural's ocean identity: Tamil logo குரல் (Catamaran),
  live pitch display, input meter, output knob, bypass. Scales as a whole when resized.
- Tests: DSP accuracy (28 checks), processor robustness incl. no audio-thread allocation (90 checks),
  headless editor snapshot.
- Windows CI: build, tests, snapshots, pluginval (strictness 10), Inno Setup installer built,
  installed, validated and uninstalled on a clean runner, release published from `main`.
- Remaining for Phase 1: confirm the first Windows CI run is green; load the plugin in FL Studio,
  Cubase and Reaper (manual, checklist in VALIDATION.md).

## Phase 2 – what exists / remains

- Done: YIN detector with decimation, confidence, four voicing states, octave guards, accurate time
  stamps, block-size independence; ~1.5 % of one core.
- Remains: validate on real recorded vocals (low/high male, female, falsetto, spoken, melisma,
  breathy, sibilant; different recording qualities); transition labelling; pitch-target smoothing
  for the harmony engine (designed in Phase 3 with the planner).

## Phase 3 – next steps, in order

1. Build an offline render tool (`KuralRender`: WAV in → WAV out through the real processor) so every
   DSP change can be listened to and measured on the same files.
2. Assemble the real-vocal test set (user-provided recordings, or a dataset with a licence that
   allows this use) and record baseline detector results on it.
3. Prototype TD-PSOLA (pitch marks from the shared analysis) and a phase-vocoder shifter offline;
   compare on the test set (intervals −12…+12, sustained/melismatic/breathy); pick per DSP_DESIGN.md.
4. Harmony planner: key/scale, diatonic intervals in scale degrees, expression preserved.
5. Two to four voices, formant preservation, dry/wet; latency measured and reported; dry delayed to
   match.
6. Register the parameters for exactly what exists (key, scale, voice on/interval/level/pan, mix).
7. Listening review before moving to 16 voices.

## Dependencies

- JUCE 8.0.8 (GPLv3 / commercial licence: **a JUCE licence decision is needed before selling Kural**;
  the current JUCE terms allow free use under a revenue limit — check the current JUCE licence).
- Inno Setup 6.7+ (CI downloads it), pluginval (CI downloads it).
- Fonts: Catamaran and Barlow Semi Condensed, SIL OFL 1.1 (licence files in `Resources/`).
- Later, possibly: ONNX Runtime (MIT) and models with commercial-compatible licences.

## Decisions needed from the owner

- Real vocal recordings for testing (or approval of a public dataset).
- JUCE licensing for commercial sale; code signing for the installer (removes SmartScreen warning).
- Whether ATELIER exists somewhere else (not found in the reachable repositories).
