cmake_minimum_required(VERSION 3.22)
project(Octivier VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
#  Mode 1: standalone protocol test (no JUCE required).
#  Used by the CI matrix and by anyone without a JUCE tree.
# ---------------------------------------------------------------------------
option(OCTIVIER_PROTOCOL_TESTS_ONLY "Build only the JUCE-independent protocol test" OFF)

if (OCTIVIER_PROTOCOL_TESTS_ONLY)
    enable_testing()
    add_executable(OctivierSlotOwnershipTests Tests/SlotOwnershipTests.cpp)
    target_compile_features(OctivierSlotOwnershipTests PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(OctivierSlotOwnershipTests PRIVATE /W4 /WX)
    else()
        target_compile_options(OctivierSlotOwnershipTests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        find_package(Threads REQUIRED)
        target_link_libraries(OctivierSlotOwnershipTests PRIVATE Threads::Threads)
    endif()
    add_test(NAME OctivierSlotOwnershipTests COMMAND OctivierSlotOwnershipTests)
    return()
endif()

# ---------------------------------------------------------------------------
#  Mode 2: full plugin build.  Requires JUCE.
#  Supply with -DJUCE_DIR=/path/to/JUCE or place JUCE in ./JUCE.
# ---------------------------------------------------------------------------
if (NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE source tree")
endif()
if (NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found.\n"
        "  -DJUCE_DIR=/path/to/JUCE   (JUCE 8.0.4 or compatible)\n"
        "See BUILD_STATUS.md for the current build state.")
endif()
add_subdirectory("${JUCE_DIR}" JUCE)

juce_add_plugin(Octivier
    COMPANY_NAME "Octivier Audio"
    BUNDLE_ID "audio.octivier.harmonicsculptor"
    PLUGIN_MANUFACTURER_CODE Octv
    PLUGIN_CODE Ocv1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Octivier Harmonic Sculptor"
    DESCRIPTION "Pitch-aware harmonic emphasis and decorrelation"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(Octivier PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)
target_compile_features(Octivier PRIVATE cxx_std_17)
target_compile_definitions(Octivier PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(Octivier PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
#  JUCE-backed smoke test (finite output, state serialization).
# ---------------------------------------------------------------------------
option(OCTIVIER_BUILD_TESTS "Build JUCE-backed smoke tests" OFF)
if (OCTIVIER_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(OctivierSmokeTests PRODUCT_NAME "Octivier Smoke Tests")
    target_sources(OctivierSmokeTests PRIVATE
        Tests/SmokeTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    )
    target_compile_features(OctivierSmokeTests PRIVATE cxx_std_17)
    target_compile_definitions(OctivierSmokeTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="Octivier"
    )
    target_link_libraries(OctivierSmokeTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
    )
    add_test(NAME OctivierSmokeTests COMMAND OctivierSmokeTests)
endif()
