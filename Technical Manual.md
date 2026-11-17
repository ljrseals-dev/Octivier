# Octivier Harmonic Sculptor — Technical Manual

**Document revision:** 1.0  
**Project version:** 1.0.0  
**Status:** Engineering prototype; release gate blocked until the JUCE integration is built and validated.

## 1. Purpose and scope

Octivier is a JUCE audio plug-in prototype for pitch-aware harmonic emphasis, spectral shaping, and stereo decorrelation. It is a harmonic-timbre sculptor, **not a pitch shifter**: it does not guarantee a true octave-up copy or a specific musical interval.

The supplied text file contains newer source blocks than the GitHub ZIP in several places. This package uses the GitHub ZIP as the project base and applies the text-file versions of `CMakeLists.txt`, processor/editor sources, smoke test, preset JSON, workflow, README, architecture, changelog, build status, and validation report where supplied. The text-file source adds YIN/CMND pitch tracking, RMS compensation, and oversampled saturation compared with the older ZIP source. This manual describes the reconciled source in this package, not intended features that are absent from that source. Read `BUILD_STATUS.md`, `docs/VALIDATION_REPORT.md`, and `docs/ARCHITECTURE.md` for the current verification boundary.

## 2. Project contents

- `CMakeLists.txt` — JUCE plug-in targets and a JUCE-independent protocol-test mode.
- `Source/PluginProcessor.h/.cpp` — audio processor, parameter state, pitch tracking, coefficient generation and processing.
- `Source/PluginEditor.h/.cpp` — plug-in editor and APVTS parameter attachments.
- `Tests/SlotOwnershipTests.cpp` — independent stress test for the slot/mailbox ownership protocol.
- `Tests/SmokeTests.cpp` — JUCE-backed smoke-test source; not verified until built with JUCE.
- `Presets/` — factory preset data and its format notes.
- `docs/` — architecture notes and validation report.
- `tools/make_test_tone.py` — helper for creating test tones.

## 3. Requirements

- CMake 3.22 or newer.
- A C++17 compiler supported by the chosen platform (GCC/Clang on Linux, Visual Studio C++ on Windows, Xcode/Clang on macOS).
- Git for the bootstrap script.
- JUCE source checkout. The CMake project uses JUCE APIs (`juce_add_plugin`, `juce::juce_audio_utils`, `juce::juce_dsp`) and therefore **requires JUCE**.

The Steinberg VST 3 SDK is not a substitute for JUCE. JUCE supplies the framework and plug-in wrappers used by this code. The VST 3 SDK is relevant if building a plug-in directly against Steinberg's VST3 interfaces or for separate validation workflows. Official sources:

- JUCE: https://github.com/juce-framework/JUCE
- Steinberg VST3 SDK: https://github.com/steinbergmedia/vst3sdk

JUCE is not bundled in this source archive. `scripts/build_all.sh` (Linux/macOS) and `scripts/build_all.ps1` (Windows) clone a shallow JUCE checkout into `./JUCE` if one is not already present. Review JUCE and platform dependencies/licensing before redistributing a combined bundle.

## 4. Build and test

### 4.1 Protocol-only test (no JUCE required)

From the project root:

```sh
cmake -S . -B build-protocol \
  -DOCTIVIER_PROTOCOL_TESTS_ONLY=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-protocol --parallel 2
ctest --test-dir build-protocol -C Release --output-on-failure
```

This builds only `OctivierSlotOwnershipTests`. It does not compile the plug-in processor/editor.

### 4.2 Full JUCE plug-in build

Linux/macOS:

```sh
./scripts/build_all.sh
```

Windows PowerShell (Visual Studio 2022 toolchain recommended):

```powershell
.\scripts\build_all.ps1
```

Or configure manually after obtaining JUCE:

```sh
cmake -S . -B build -DJUCE_DIR="$PWD/JUCE" \
  -DCMAKE_BUILD_TYPE=Release -DOCTIVIER_BUILD_TESTS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

The exact targets and formats available depend on the operating system and JUCE/toolchain configuration. AU is a macOS format. Do not report a successful full build unless the configure, compile and test logs show it.

### 4.3 Build artifacts

JUCE's CMake integration normally places generated plug-in targets beneath the configured build tree. Find artifacts by inspecting the build output and target directories; do not assume a fixed installation path. `COPY_PLUGIN_AFTER_BUILD` is disabled, so the build does not automatically install/copy the plug-in into a host scan directory.

## 5. DSP overview

### Parameters

The parameter layout includes Octave Strength, Harmonic Focus, Odd Cut, Even Boost, Tracking Sensitivity, Gate, Decorrelate Delay, Width, Delay Mod Depth, Formant Shelf, Air Shelf, Mode, Character, Character Amount, Dry Blend, Output, RMS Compensation, Freeze Tracking, and Manual Fundamental.

The text-file source includes an input-RMS reference, smoothed gain compensation bounded to approximately ±12 dB, and a 2× IIR polyphase half-band oversampler around Tube/Tape waveshaping. These features are implemented by source inspection but remain unvalidated: gain behavior around transients, oversampler latency, and residual aliasing must be measured. A parameter existing in the UI/state is not proof of correct behavior.

### Pitch tracking

The current `Source/PluginProcessor.cpp` implementation uses a recent 2048-sample window, the YIN difference function, cumulative mean normalized difference (CMND), parabolic lag refinement, and an octave-error guard. It is still unvalidated against a reference-tone dataset; octave errors, dropouts and missed fundamentals remain possible.

### Harmonic shaping

The processor generates a bank of 16 peak-filter coefficients and two high-shelf filters (Formant and Air). Coefficient generation is performed away from the audio thread and the generated bank is handed over using fixed slots. Frequency-response curves have not been validated against golden reference data.

### Stereo decorrelation

The wet path is filtered and shaped, optionally saturated through a 2× oversampling stage, then uses delay/modulation for decorrelation. The dry blend is mixed in parallel. Latency, stereo compatibility, auto-gain behavior, and perceptual effect still require measurement and listening tests.

## 6. Concurrency and real-time intent

The coefficient generation ring has three slots with `Idle`, `Writing`, `Ready`, and `Reading` states. A separate request mailbox transfers parameter/pitch requests from the audio thread to the worker. The intended rule is that each slot is owned by one side at a time, and the audio thread adopts coefficient data only at block boundaries.

These are design intentions supported by source inspection and an independent protocol-model stress test. They are **not yet an integrated real-time safety certification**. The plug-in must still be built and tested under sanitizers, repeated `prepareToPlay`/`releaseResources` cycles, rapid parameter changes, and real hosts.

## 7. Validation checklist before release

1. Build the plug-in and JUCE smoke-test target on each supported operating system.
2. Save raw CMake configure/build output and CTest logs in `BUILD_STATUS.md` or attach them to the release.
3. Test finite output, state save/restore, parameter bounds, mono/stereo, arbitrary block sizes, and sample rates from 44.1 kHz through 192 kHz.
4. Validate pitch estimates against known tones across the intended range, silence, noise and polyphonic inputs.
5. Compare filter responses to golden curves and check near-Nyquist stability.
6. Run ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer where supported against integrated code.
7. Run pluginval and load/unload, automation, preset/state recall and offline-render tests in multiple DAWs.
8. Measure CPU, latency, aliasing and stereo compatibility; conduct loudness-matched listening and ABX tests.
9. Either implement and validate RMS compensation / oversampled saturation or remove/rename controls that imply those features are finished.

## 8. Release policy

The release gate remains **BLOCKED** until the JUCE plug-in compiles and the integration, DSP, real-time, host, and listening checks above are completed. The protocol-only test must never be presented as proof of the whole plug-in's correctness.
