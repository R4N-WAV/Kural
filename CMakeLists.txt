cmake_minimum_required(VERSION 3.22)

project(Kural VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Static MSVC runtime: no Visual C++ Redistributable needed on the user's machine.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------- framework-free DSP core
# Pitch analysis (and later harmony, pitch shifting, formants) lives here with no JUCE dependency,
# so it can be unit-tested on its own and used by offline tools.
add_library(KuralDsp STATIC
    Source/DSP/PitchDetector.cpp
    Source/DSP/Harmony.cpp
    Source/DSP/Psola.cpp
    Source/DSP/PhaseVocoder.cpp)
target_include_directories(KuralDsp PUBLIC Source)

option(KURAL_BUILD_TESTS "Build the DSP unit tests" ON)
if(KURAL_BUILD_TESTS)
    add_executable(KuralDspTests Tests/DspTests.cpp Tests/PitchTests.cpp Tests/HarmonyTests.cpp Tests/ShifterTests.cpp)
    target_include_directories(KuralDspTests PRIVATE Tests)
    target_link_libraries(KuralDspTests PRIVATE KuralDsp)

    # offline measurement tool: the real detector and shifters over WAV files (tools/eval)
    add_executable(KuralLab tools/eval/KuralLab.cpp)
    target_include_directories(KuralLab PRIVATE tools/eval)
    target_link_libraries(KuralLab PRIVATE KuralDsp)
endif()

option(KURAL_DSP_ONLY "Only build the DSP core and its tests (no JUCE)" OFF)
if(KURAL_DSP_ONLY)
    return()
endif()

# ---------------------------------------------------------------- JUCE: local folder or fetched
if(KURAL_JUCE_DIR)
    add_subdirectory(${KURAL_JUCE_DIR} juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.8
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(KURAL_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    KURAL_VERSION_STRING="${PROJECT_VERSION}")

if(UNIX AND NOT APPLE)   # Linux is only used for test builds (CI and development), not shipped
    list(APPEND KURAL_COMMON_DEFS
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0
        JUCE_USE_XRENDER=0 JUCE_USE_XSHM=0 JUCE_ALSA=0 JUCE_JACK=0)
endif()

# Fonts compiled into the plugin: Catamaran (Tamil + Latin, logo) and Barlow Semi Condensed (UI), both OFL
file(GLOB KURAL_FONT_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/*.ttf)
juce_add_binary_data(KuralAssets HEADER_NAME KuralBinaryData.h NAMESPACE KuralBinaryData SOURCES ${KURAL_FONT_FILES})

set(KURAL_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/UI/Theme.cpp
    Source/UI/PitchView.cpp)

juce_add_plugin(Kural
    PRODUCT_NAME                "Kural"
    COMPANY_NAME                "R4N"
    BUNDLE_ID                   "com.r4n.kural"
    PLUGIN_MANUFACTURER_CODE    R4Nx          # shared by every R4N plugin
    PLUGIN_CODE                 Krl1
    FORMATS                     VST3
    VST3_CATEGORIES             Fx "Pitch Shift"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE          # harmony notes/chords from a keyboard (Phase 7); declared from the start
    NEEDS_MIDI_OUTPUT           FALSE         # so hosts never have to re-scan a changed bus layout
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VERSION                     ${PROJECT_VERSION})

target_sources(Kural PRIVATE ${KURAL_PLUGIN_SOURCES})
target_include_directories(Kural PRIVATE Source)
target_compile_definitions(Kural PUBLIC ${KURAL_COMMON_DEFS})
target_link_libraries(Kural
    PRIVATE KuralDsp KuralAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- test tools (run in CI)
option(KURAL_BUILD_TOOLS "Build the stress test and snapshot tools" ON)
if(KURAL_BUILD_TOOLS)
    # robustness: sample rates, block sizes, layouts, automation, state, bypass, no allocation on the audio thread
    juce_add_console_app(KuralStress PRODUCT_NAME "KuralStress")
    target_sources(KuralStress PRIVATE Tests/KuralStress.cpp ${KURAL_PLUGIN_SOURCES})
    target_include_directories(KuralStress PRIVATE Source)
    target_compile_definitions(KuralStress PRIVATE ${KURAL_COMMON_DEFS} JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(KuralStress PRIVATE KuralDsp KuralAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

    # headless render of the real editor fed by the real processor (checks layout and Tamil shaping)
    juce_add_console_app(KuralSnapshot PRODUCT_NAME "KuralSnapshot")
    target_sources(KuralSnapshot PRIVATE Tests/KuralSnapshot.cpp ${KURAL_PLUGIN_SOURCES})
    target_include_directories(KuralSnapshot PRIVATE Source)
    target_compile_definitions(KuralSnapshot PRIVATE ${KURAL_COMMON_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(KuralSnapshot PRIVATE KuralDsp KuralAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
