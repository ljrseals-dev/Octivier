# Build and Validation Status

**Updated:** 2026-10-12  
**Release gate:** BLOCKED  
**JUCE plugin build:** NOT RUN — a JUCE source checkout was not available in this environment.  
**Standalone protocol test:** PASS — GCC 14.2.0 / CMake 3.31.6; 1/1 CTest passed.

## Source reconciliation

The supplied `Octivier .txt` contains newer source blocks than the GitHub ZIP for the processor/editor and several documentation files. Those supplied blocks have been applied over the GitHub ZIP base. The reconciled source includes YIN/CMND pitch tracking, a 2× IIR polyphase half-band oversampling stage for Tube/Tape saturation, and smoothed RMS compensation. These implementations are still unverified in an integrated JUCE build.

## Verified command and result

```sh
cmake -S . -B build-protocol -DOCTIVIER_PROTOCOL_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-protocol --parallel 2
ctest --test-dir build-protocol -C Release --output-on-failure
```

Observed in this environment:

```text
-- The CXX compiler identification is GNU 14.2.0
-- Configuring done
-- Generating done
[ 50%] Building CXX object CMakeFiles/OctivierSlotOwnershipTests.dir/Tests/SlotOwnershipTests.cpp.o
[100%] Linking CXX executable OctivierSlotOwnershipTests
[100%] Built target OctivierSlotOwnershipTests
1/1 Test #1: OctivierSlotOwnershipTests .......   Passed    0.07 sec
100% tests passed, 0 tests failed out of 1
```

The test exercises a JUCE-independent model of the mailbox and three-slot ownership protocol over 250,000 accepted handoffs. It does **not** compile or execute `PluginProcessor.cpp` and is not evidence that the integrated plug-in is race-free.

## Not yet verified

- Full JUCE configure and compilation of VST3/Standalone targets.
- JUCE-backed `OctivierSmokeTests` and state round-trip.
- AU target on macOS.
- ThreadSanitizer / ASan / UBSan on integrated code.
- Pitch accuracy against golden tones and noisy/polyphonic inputs.
- Harmonic/shelf frequency responses against reference curves.
- Oversampler latency and aliasing performance.
- RMS compensation behavior around transients and sustained signals.
- CPU usage, pluginval, load/unload, automation, preset recall and DAW host runs.
- Loudness-matched listening and ABX evaluation.

## Release gate

**BLOCKED.** Do not describe the plug-in as built, production-ready, release-certified, or validated in a DAW until the full build, smoke tests, sanitizer runs, DSP measurements, host validation and listening tests have actually been completed and the raw logs recorded.
