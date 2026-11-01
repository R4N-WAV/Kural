# Kural – TODO

## Completed
- [x] Inspect R4N repositories (Anbu, Isai, isai-reference; ATELIER not found) – findings in ARCHITECTURE.md
- [x] Project documents (this file, DEVELOPMENT_PLAN, ARCHITECTURE, DSP_DESIGN, VALIDATION)
- [x] CMake/JUCE 8.0.8 project, framework-free `KuralDsp` library
- [x] VST3 foundation: buses (mono/stereo → stereo, mono → mono), MIDI input declared, output gain, click-free bypass, zero latency, versioned state
- [x] Pitch detector (YIN + decimation + confidence + voicing + octave guards + time stamps)
- [x] 30 DSP accuracy/robustness tests (all pass); four detector weaknesses found by them and fixed (one only visible on Windows CI)
- [x] Stress test: 6 sample rates × 4 block patterns × 3 layouts, hostile signals, automation, bypass, state, re-prepare, editor lifecycle, no allocation on the audio thread (90 checks, all pass on Linux)
- [x] Editor: Tamil logo குரல் (shaping checked in a headless render), live pitch display from real frames, input meter, output knob, bypass, scalable
- [x] Installer (Inno Setup, dark wizard, artwork with correctly shaped Tamil), CI install/validate/uninstall test
- [x] GitHub Actions: Windows build, tests, snapshots, pluginval strictness 10, installer install/validate/uninstall, release publishing – all green on Windows (run 3)

## Current
- [ ] Manual: load in FL Studio, Cubase, Reaper (checklist in VALIDATION.md)

## Remaining (next: Phase 3)
- [ ] `KuralRender` offline tool (WAV → WAV through the real processor)
- [ ] Real-vocal test set + baseline detector results on it
- [ ] Test that exercises the sub-multiple octave guard directly (it is covered only indirectly)
- [ ] Transition labelling in the detector (frames straddling a note change)
- [ ] PSOLA and phase-vocoder prototypes, comparison, selection
- [ ] Harmony planner (scales, diatonic intervals, expression kept)
- [ ] First voices, formant preservation, dry/wet, latency reporting + dry alignment
- [ ] Preset system (format with version field, factory protection, import/export)
- [ ] Phases 4–10 per DEVELOPMENT_PLAN.md

## Known issues / limitations
- Real recorded vocals not tested yet; all accuracy figures come from synthetic signals.
- Very breathy singing (aspiration within ~10 dB of the voice): ~3–4 % of voiced frames still have octave errors on synthetic tests.
- Pitch reported ~18 ms after the fact (half the analysis window); fine for display, handled by look-ahead in the harmony engine.
- Linux test builds need `CXXFLAGS="-DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XRENDER=0 -DJUCE_USE_XSHM=0"` at configure time when the X11 extension headers are missing (JUCE's helper tool is built without the project's definitions).
- Installer is not code-signed (SmartScreen warning on first run).
- MIDI input is accepted and ignored until Phase 7.
