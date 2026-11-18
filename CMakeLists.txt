cmake_minimum_required(VERSION 3.22)
project(Octivier VERSION 1.0.0 LANGUAGES CXX)

option(OCTIVIER_PROTOCOL_TESTS_ONLY "Build only the JUCE-independent tests" OFF)
option(OCTIVIER_ENABLE_TSAN "Build standalone protocol tests with ThreadSanitizer" OFF)
option(OCTIVIER_ENABLE_RTSAN "Build Octivier with RealtimeSanitizer (Clang only)" OFF)
if (OCTIVIER_PROTOCOL_TESTS_ONLY)
    enable_testing()
    add_executable(OctivierSlotOwnershipTests Tests/SlotOwnershipTests.cpp)
    target_compile_features(OctivierSlotOwnershipTests PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(OctivierSlotOwnershipTests PRIVATE /W4 /WX)
    else()
        target_compile_options(OctivierSlotOwnershipTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        find_package(Threads REQUIRED)
        target_link_libraries(OctivierSlotOwnershipTests PRIVATE Threads::Threads)
    endif()
    add_test(NAME OctivierSlotOwnershipTests COMMAND OctivierSlotOwnershipTests)

    add_executable(AcfBehaviorPreservationTests
        Tests/AcfBehaviorPreservationTests.cpp
        Source/AcfPitchTracker.cpp
    )
    target_include_directories(AcfBehaviorPreservationTests PRIVATE Source)
    target_compile_features(AcfBehaviorPreservationTests PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(AcfBehaviorPreservationTests PRIVATE /W4 /WX)
    else()
        target_compile_options(AcfBehaviorPreservationTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME AcfBehaviorPreservationTests COMMAND AcfBehaviorPreservationTests)

    add_executable(PitchTrackerPreparationTests
        Tests/PitchTrackerPreparationTests.cpp
        Source/AcfPitchTracker.cpp
        Source/YinPitchTracker.cpp
    )
    target_include_directories(PitchTrackerPreparationTests PRIVATE Source)
    target_compile_features(PitchTrackerPreparationTests PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(PitchTrackerPreparationTests PRIVATE /W4 /WX)
    else()
        target_compile_options(PitchTrackerPreparationTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME PitchTrackerPreparationTests COMMAND PitchTrackerPreparationTests)

    add_executable(AcfPitchTrackerBehaviorTests
        Tests/AcfPitchTrackerBehaviorTests.cpp
        Source/AcfPitchTracker.cpp
    )
    target_include_directories(AcfPitchTrackerBehaviorTests PRIVATE Source)
    target_compile_features(AcfPitchTrackerBehaviorTests PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(AcfPitchTrackerBehaviorTests PRIVATE /W4 /WX)
    else()
        target_compile_options(AcfPitchTrackerBehaviorTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME AcfPitchTrackerBehaviorTests COMMAND AcfPitchTrackerBehaviorTests)

    add_executable(PitchValidation
        Tests/PitchValidationTests.cpp
        Source/AcfPitchTracker.cpp
        Source/YinPitchTracker.cpp
    )
    target_include_directories(PitchValidation PRIVATE Source)
    target_compile_features(PitchValidation PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(PitchValidation PRIVATE /W4 /WX)
    else()
        target_compile_options(PitchValidation PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    if (OCTIVIER_ENABLE_TSAN)
        if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            message(FATAL_ERROR "ThreadSanitizer requires Clang or GCC")
        endif()
        target_compile_options(OctivierSlotOwnershipTests PRIVATE -fsanitize=thread -g -O1)
        target_link_options(OctivierSlotOwnershipTests PRIVATE -fsanitize=thread)
    endif()
    return()
endif()

# Supply JUCE through -DJUCE_DIR=/path/to/JUCE or place it in ./JUCE.
if (NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE source tree")
endif()
if (NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found. Set -DJUCE_DIR=/path/to/JUCE (JUCE 8 recommended). See README.md.")
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
    Source/AcfPitchTracker.cpp
    Source/YinPitchTracker.cpp
)
target_compile_features(Octivier PRIVATE cxx_std_17)
target_compile_definitions(Octivier PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(Octivier PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags
)

option(OCTIVIER_BUILD_TESTS "Build JUCE-backed smoke tests" OFF)
if (OCTIVIER_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(OctivierSmokeTests PRODUCT_NAME "Octivier Smoke Tests")
    target_sources(OctivierSmokeTests PRIVATE Tests/SmokeTests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/AcfPitchTracker.cpp Source/YinPitchTracker.cpp)
    target_compile_features(OctivierSmokeTests PRIVATE cxx_std_17)
    target_compile_definitions(OctivierSmokeTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Octivier")
    target_link_libraries(OctivierSmokeTests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME OctivierSmokeTests COMMAND OctivierSmokeTests)
endif()

if (OCTIVIER_ENABLE_RTSAN)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "RealtimeSanitizer requires Clang")
    endif()
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-fsanitize=realtime" HAVE_RTSAN)
    if (NOT HAVE_RTSAN)
        message(FATAL_ERROR "Compiler does not support -fsanitize=realtime")
    endif()
    target_compile_options(Octivier PRIVATE -fsanitize=realtime -g)
    target_link_options(Octivier PRIVATE -fsanitize=realtime)
endif()
