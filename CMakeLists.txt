cmake_minimum_required(VERSION 3.22)

# JUCE's CMake support checks for both compiler languages during configuration.
project(Octivier VERSION 1.0.0 LANGUAGES C CXX)

include(CTest)

option(OCTIVIER_PROTOCOL_TESTS_ONLY
       "Build JUCE-independent tests only; do not configure the plugin" OFF)
option(OCTIVIER_BUILD_TESTS
       "Build JUCE-backed plugin smoke tests" OFF)
option(OCTIVIER_ENABLE_TSAN
       "Instrument the standalone slot-ownership test with ThreadSanitizer" OFF)
option(OCTIVIER_ENABLE_RTSAN
       "Instrument the plugin with Clang RealtimeSanitizer" OFF)

# Keep warning policy consistent across the JUCE-independent test programs.
function(octivier_configure_test target)
    target_compile_features(${target} PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        )
    endif()
endfunction()

function(octivier_add_source_test target)
    add_executable(${target} ${ARGN})
    octivier_configure_test(${target})
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/Source")
endfunction()

# These tests deliberately have no JUCE dependency. This mode is suitable for
# protocol/pitch CI and returns before JUCE is located or configured.
if(OCTIVIER_PROTOCOL_TESTS_ONLY)
    enable_testing()

    find_package(Threads REQUIRED)

    octivier_add_source_test(OctivierSlotOwnershipTests
        Tests/SlotOwnershipTests.cpp
    )
    target_link_libraries(OctivierSlotOwnershipTests PRIVATE Threads::Threads)
    add_test(NAME OctivierSlotOwnershipTests COMMAND OctivierSlotOwnershipTests)

    octivier_add_source_test(AcfBehaviorPreservationTests
        Tests/AcfBehaviorPreservationTests.cpp
        Source/AcfPitchTracker.cpp
    )
    add_test(NAME AcfBehaviorPreservationTests COMMAND AcfBehaviorPreservationTests)

    octivier_add_source_test(PitchTrackerPreparationTests
        Tests/PitchTrackerPreparationTests.cpp
        Source/AcfPitchTracker.cpp
        Source/YinPitchTracker.cpp
    )
    add_test(NAME PitchTrackerPreparationTests COMMAND PitchTrackerPreparationTests)

    octivier_add_source_test(AcfPitchTrackerBehaviorTests
        Tests/AcfPitchTrackerBehaviorTests.cpp
        Source/AcfPitchTracker.cpp
    )
    add_test(NAME AcfPitchTrackerBehaviorTests COMMAND AcfPitchTrackerBehaviorTests)

    octivier_add_source_test(PitchValidation
        Tests/PitchValidationTests.cpp
        Source/AcfPitchTracker.cpp
        Source/YinPitchTracker.cpp
    )
    add_test(NAME PitchValidationQuick COMMAND PitchValidation --quick)

    octivier_add_source_test(RmsCompensationTests
        Tests/RmsCompensationTests.cpp
    )
    add_test(NAME RmsCompensationTests COMMAND RmsCompensationTests)

    if(OCTIVIER_ENABLE_TSAN)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            message(FATAL_ERROR
                "OCTIVIER_ENABLE_TSAN requires a Clang or GCC compiler")
        endif()

        target_compile_options(OctivierSlotOwnershipTests PRIVATE
            -fsanitize=thread
            -g
            -O1
        )
        target_link_options(OctivierSlotOwnershipTests PRIVATE -fsanitize=thread)
    endif()

    return()
endif()

# JUCE is supplied by the caller or by a local ./JUCE checkout. No network
# fetches or dependency bootstrapping are performed by this project.
set(JUCE_DIR "${PROJECT_SOURCE_DIR}/JUCE" CACHE PATH
    "Path to the JUCE source checkout")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE was not found at '${JUCE_DIR}'. "
        "Set -DJUCE_DIR=/path/to/JUCE (JUCE 8 recommended). "
        "For JUCE-independent tests, set -DOCTIVIER_PROTOCOL_TESTS_ONLY=ON.")
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

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

# PluginProcessor.h and PluginEditor.h include JuceHeader.h. JUCE only
# generates that umbrella header when this function is explicitly requested.
juce_generate_juce_header(Octivier)

target_sources(Octivier PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AcfPitchTracker.cpp
    Source/YinPitchTracker.cpp
)

target_compile_features(Octivier PRIVATE cxx_std_17)

target_compile_definitions(Octivier
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Octivier
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(OCTIVIER_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(OctivierSmokeTests
        PRODUCT_NAME "Octivier Smoke Tests"
    )
    juce_generate_juce_header(OctivierSmokeTests)

    target_sources(OctivierSmokeTests PRIVATE
        Tests/SmokeTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AcfPitchTracker.cpp
        Source/YinPitchTracker.cpp
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
        juce::juce_recommended_warning_flags
    )

    add_test(NAME OctivierSmokeTests COMMAND OctivierSmokeTests)
endif()

if(OCTIVIER_ENABLE_RTSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "OCTIVIER_ENABLE_RTSAN requires Clang; MSVC and GCC are not supported")
    endif()

    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-fsanitize=realtime" OCTIVIER_HAS_RTSAN)

    if(NOT OCTIVIER_HAS_RTSAN)
        message(FATAL_ERROR
            "The selected Clang compiler does not support -fsanitize=realtime")
    endif()

    target_compile_options(Octivier PRIVATE -fsanitize=realtime -g)
    target_link_options(Octivier PRIVATE -fsanitize=realtime)
endif()
