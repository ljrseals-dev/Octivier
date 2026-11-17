# Build and Validation Status

**Updated:** 2026-10-12  
**Release gate:** BLOCKED  
**Plugin build:** NOT RUN — compatible JUCE source checkout is not present in this package/environment.  
**Standalone protocol test:** PASS — compiled with GCC 14.2 / CMake 3.31.6 and passed CTest.

## Evidence

Command run:

```text
cmake -S . -B build-protocol -DOCTIVIER_PROTOCOL_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-protocol --parallel 2
ctest --test-dir build-protocol --output-on-failure
```

Observed output:

```text
[ 50%] Building CXX object CMakeFiles/OctivierSlotOwnershipTests.dir/Tests/SlotOwnershipTests.cpp.o
[100%] Linking CXX executable OctivierSlotOwnershipTests
[100%] Built target OctivierSlotOwnershipTests
1/1 Test #1: OctivierSlotOwnershipTests .......   Passed    0.29 sec
100% tests passed, 0 tests failed out of 1
```

The standalone test stress-tests a separately compiled model of the mailbox and three-slot ownership protocol over 250,000 accepted requests. It is evidence for the protocol pattern, **not** proof that the JUCE plugin compiles or that the integrated plugin is race-free under ThreadSanitizer. The plugin source integration has not yet been compiled against JUCE.

## Current claim status

| Claim | Status | Basis / next requirement |
|---|---|---|
| Request payload publication is ordered | IMPLEMENTED BY DESIGN; plugin UNVERIFIED | Mailbox states are Empty/Writing/Ready/Reading; producer claims Empty before payload write and publishes Ready with release; worker claims Ready with acquire. Compile and run TSAN stress against integrated code. |
| Coefficient slots have exclusive ownership states | IMPLEMENTED BY DESIGN; plugin UNVERIFIED | Three fixed slots; worker claims Idle via CAS, publishes Ready, audio claims Ready and retires old Reading slot only after a replacement is acquired. Integrated plugin build and TSAN remain required. |
| Coefficients are plain values, not reference-counted pointers | IMPLEMENTED BY INSPECTION | Audio path uses fixed arrays of five-float biquad coefficients and per-channel state. Real-time host audit still required. |
| Formant/Air use shelf biquads | IMPLEMENTED BY INSPECTION; RESPONSE UNVERIFIED | RBJ-style high shelves are generated off-thread and published with the harmonic bank. Golden frequency-response tests are not yet included. |
| JUCE plugin build | BLOCKED / NOT RUN | Supply a compatible JUCE 7/8 checkout and configure with `-DJUCE_DIR=...`. |
| JUCE smoke tests | NOT RUN | Require JUCE build; target includes processor and editor source. |
| ThreadSanitizer / ASan | NOT RUN | Run against the integrated plugin and exchange stress harness on a supported toolchain. |
| pluginval / DAW validation | NOT RUN | Requires compiled plugin binaries and target hosts. |
| Release certification | BLOCKED | Do not represent as production-certified until all release gates are completed. |

## Important lifecycle note

`prepareToPlay` and `releaseResources` stop the coefficient worker before resetting/releasing shared DSP data. This is a non-real-time lifecycle operation. The host lifecycle behavior still needs testing, especially repeated prepare/release, sample-rate changes, and rapid unload/reload.
