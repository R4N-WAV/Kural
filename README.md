# Kural by R4N · குரல்

A vocal harmonizer for Windows (VST3, 64-bit): one recorded vocal in, the original plus up to 16
harmony voices out. *Kural* (குரல்) is Tamil for "voice".

## Status: early development (v0.1.0)

| Works now | Not built yet |
|---|---|
| Live pitch tracking of the incoming vocal (YIN-based, tested to < 0.4 cent median on synthetic signals) | Harmony voices, pitch shifting, formants |
| Pitch display: the voice drawn as a flowing line on a note grid, from real analysis only | Key/scale/chord logic, MIDI control |
| Output level, click-free bypass, state saved with the project | Presets, quality modes |
| Windows installer + portable zip, built and tested by CI | Optional AI features |

Details: [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) · [ARCHITECTURE.md](ARCHITECTURE.md) ·
[DSP_DESIGN.md](DSP_DESIGN.md) · [VALIDATION.md](VALIDATION.md) · [TODO.md](TODO.md) ·
install guide: [INSTALL.md](INSTALL.md)

## Download

Every push to `main` that passes all checks is published as the
[latest development build](https://github.com/R4N-WAV/Kural/releases/tag/latest):
`Kural-Setup-Windows.exe` (installer) and `Kural-VST3-Windows.zip` (portable).

## Build from source

Requirements: CMake ≥ 3.22, a C++17 compiler (Visual Studio 2022 on Windows). JUCE 8.0.8 is fetched
automatically (or pass `-DKURAL_JUCE_DIR=<path>`).

```bash
# Windows (what CI does)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Kural_VST3 KuralDspTests KuralStress KuralSnapshot
build\Release\KuralDspTests.exe
build\KuralStress_artefacts\Release\KuralStress.exe
# plugin: build\Kural_artefacts\Release\VST3\Kural.vst3

# DSP core and its tests only (any OS, no JUCE download)
cmake -S . -B build-dsp -DKURAL_DSP_ONLY=ON && cmake --build build-dsp && build-dsp/KuralDspTests
```

Linux is used only for development test builds. If the X11 extension headers are missing, configure
with `CXXFLAGS="-DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XRENDER=0 -DJUCE_USE_XSHM=0"`.

Installer artwork: `python3 packaging/windows/make_images.py` (Pillow with libraqm, for Tamil shaping).

## Credits

Fonts: Catamaran (The Catamaran Authors) and Barlow Semi Condensed (The Barlow Project Authors), SIL Open Font
License 1.1 – see `Resources/FONT_LICENSE_*.txt`. Built with JUCE.
