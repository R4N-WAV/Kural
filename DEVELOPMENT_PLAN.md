# Kural – Development plan

Priorities: real DSP before visual polish, sound quality before feature count, reliability before
complexity. A phase is done only when its deliverable is validated, including listening on real
vocals where audio is involved.

**Scope (owner, 1 Nov 2026):** Kural is a universal vocal effects plugin in the spirit of Auto-Tune EFX —
pitch correction, pitch shifting, harmonies, formant ("throat") control and transforming effects — with
a large preset library. Reference for how shifted vocals should sound: Logic Pro's Flex Pitch (notes
put in tune as a whole while the singer's vibrato, drift and transitions are kept; formants separate).
The interface is redesigned around the ocean theme (three directions on the "Kural Interface
Directions" canvas; the owner picks one). Order chosen by the owner: sound quality first, then pitch
correction, effects, presets, redesign.

| Phase | Deliverable | Status |
|---|---|---|
| 1. Research and foundation | Stable, recognisable Kural VST3 foundation | **Done; Windows CI green (build, tests, pluginval 10, installer); DAW check pending (manual)** |
| 2. Core pitch engine | Reliable vocal analysis | **Done: 96.4 % within 50 c on 40 solo singers (DSP_DESIGN §1)** |
| 3. First harmony engine | Harmonizer with a few high-quality voices | **In progress (v0.2): 4 voices working in the plugin; listening review by the owner pending** |
| 3b. Pitch correction and lead processing | Tune (retune, vibrato kept, humanize, flex-tune), lead pitch and formant | **Done in DSP and parameters (v3); interface controls with the redesign; listening pending** |
| 4. Expand to 16 voices | Working 16-voice engine, CPU benchmarked | Planned |
| 5. Naturalness and voice differentiation | Convincing layered stacks | Planned |
| 6. Creative processing | Transform modules (Duet, Vocoder, Mutate/ring mod, Tube), Pitch & Throat pad, per-voice processing, stereo | Planned |
| 6b. Preset library | Factory presets in the spirit of Auto-Tune EFX, browser, versioned format | Planned |
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
- Tests: DSP accuracy (30 checks), processor robustness incl. no audio-thread allocation (90 checks),
  headless editor snapshot.
- Windows CI: build, tests, snapshots, pluginval (strictness 10), Inno Setup installer built,
  installed, validated and uninstalled on a clean runner, release published from `main`.
- Remaining for Phase 1: load the plugin in FL Studio, Cubase and Reaper (manual, checklist in
  VALIDATION.md).

## Phase 2 – what exists / remains

- Done: YIN detector with decimation, confidence, four voicing states, octave guards, accurate time
  stamps, block-size independence; ~1.5 % of one core.
- Remains: validate on real recorded vocals (low/high male, female, falsetto, spoken, melisma,
  breathy, sibilant; different recording qualities); transition labelling; pitch-target smoothing
  for the harmony engine (designed in Phase 3 with the planner).

## Phase 3 – status (v0.2.0)

Done:
1. `KuralLab` offline tool (framework-free): the real detector, shifters and full harmony engine
   over WAV files (`analyse`, `shift`, `bench`, `harmonize`).
2. Real-vocal test set: the `Fetch test vocals` workflow builds it from vocadito, Dagstuhl ChoirSet
   and VocalSet (all CC BY 4.0) into the `test-vocals` branch with a manifest and attribution
   (244 tracks).
3. Three shifters built (TD-PSOLA, phase vocoder, ATELIER's rotating-tap baseline) and compared on
   synthetic and stand-in material with `tools/eval/eval_shift.py`; TD-PSOLA is used in the plugin.
   On 12 real singers PSOLA keeps formants best (2.1 dB drift vs 3.0 and 7.8) at exact level;
   two flaws it showed on real singing were fixed (DSP_DESIGN.md §2).
4. Harmony planner: 8 scale types + custom, scale-degree intervals, expression kept, vibrato-proof
   note decisions.
5. HarmonyEngine with up to 16 voices; 4 exposed (interval, octave, fine, formant, level, pan);
   formants preserved; consonants unshifted at their own level; dry delayed by the reported latency.
6. Parameters for exactly what exists (v2 IDs; voice IDs reserved to v16), harmony panel, voice lines
   on the pitch screen.

Remaining before Phase 4:
7. **Listening review by the owner** (listening page with real vocals through each shifter and the
   full harmony engine) and in a DAW (VALIDATION.md checklist).
8. Fix what the listening review finds.

## Dependencies

- JUCE 8.0.8 (GPLv3 / commercial licence: **a JUCE licence decision is needed before selling Kural**;
  the current JUCE terms allow free use under a revenue limit — check the current JUCE licence).
- Inno Setup 6.7+ (CI downloads it), pluginval (CI downloads it).
- Fonts: Catamaran and Barlow Semi Condensed, SIL OFL 1.1 (licence files in `Resources/`).
- Later, possibly: ONNX Runtime (MIT) and models with commercial-compatible licences.

## Decisions needed from the owner

- Listening review of the shifter comparison and the harmonies (page linked from VALIDATION.md).
- JUCE licensing for commercial sale; code signing for the installer (removes SmartScreen warning).
