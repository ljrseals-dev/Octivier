# Octivier Harmonic Sculptor — JUCE Plugin Project

**Status: engineering prototype / validation candidate. Not release-certified.** This project converts the supplied Octivier concept into a JUCE C++ VST3, AU (macOS), and Standalone project. It uses a block-rate autocorrelation pitch estimate and a bank of dynamically tuned peak filters to attenuate odd partials and emphasize even partials. It does **not** resynthesize or translate pitch by an octave. The “octave-up” result is a perceptual/timbre hypothesis and must be judged with listening tests.

## Features implemented in this project

- JUCE AudioProcessor with host-automatable parameters and XML state serialization.
- VST3, AU, and Standalone build targets (AU is macOS-only).
- Pitch estimate with gate, confidence hold, sensitivity, and manual/freeze fundamental.
- Classic, Sub, Hollow, and Square harmonic modes; 16 partial filter slots.
- Strength/focus smoothing; bounded filter frequency/Q; parallel dry blend.
- Fixed three-slot coefficient-generation ring and state-published request mailbox; coefficients are plain values and are adopted at block boundaries.
- Formant and Air controls use RBJ-style high-shelf biquads built off the audio thread.
- Stereo delay-based decorrelation controls, simple Tube/Tape waveshaping, output trim.
- Graphite/cyan/amber UI, parameter attachments, factory preset selector, live pitch/odd/even readouts.
- Deterministic synthetic test-tone generator and JUCE smoke-test target.

## Important limitations — read before use

1. **No guaranteed octave translation.** Harmonic EQ changes spectral balance; it does not create missing harmonics or guarantee that listeners hear a stable octave above.
2. **Tracker is deliberately simple.** The lag search evaluates the full candidate range and selects a strong short-lag peak, but autocorrelation can still produce octave errors and fail on breathy, polyphonic, noisy, or transient material. Intended source is monophonic.
3. **Formant and Air are broad high-shelf tone controls, not a vocal-tract/formant-preservation model.** Their frequency responses have not yet been verified against golden curves.
4. **RMS compensation is a smoothed approximation, not perceptual loudness matching.** When enabled it tracks pre-trim input/output power with a 300 ms envelope, follows the RMS ratio with additional smoothing, and clamps compensation to +/-12 dB. It is not LUFS/BS.1770 matching and needs listening and level-step validation.
5. **Decorrelator is a lightweight delay implementation.** It is not a phase-safe stereoizer and may comb-filter when blended with dry signal.
6. **Harmonic meter is a diagnostic FFT readout**, using a 2,048-point Hann window and single-bin integration at integer multiples of the tracked fundamental, updated at approximately 20 Hz. It is not calibrated: it does not sum adjacent leakage bins and may under-read detuned or jittering partials.
7. **Release validation remains incomplete.** The JUCE-independent slot/mailbox protocol stress test passes, but the integrated JUCE plugin has not been compiled, and ThreadSanitizer, CPU profiling, pluginval, DAW tests, and perceptual ABX have not been run. Do not claim zero latency: the decorrelation path is intentionally delayed and reported tail is 250 ms.
8. The plugin's source can be built only after a compatible JUCE source tree is supplied. This package does not bundle JUCE. See `BUILD_STATUS.md` for exact test evidence and the remaining release gates.

## Build prerequisites

- CMake 3.22+
- C++17 compiler supported by your JUCE version
- JUCE 8 recommended (JUCE 7 may work; not verified here)
- Windows: Visual Studio 2022 with Desktop development with C++; macOS: Xcode; Linux: a supported Clang/GCC toolchain and JUCE dependencies

Place JUCE in `./JUCE` or point `JUCE_DIR` to an existing JUCE checkout. Do not download third-party dependencies during the build unless your organization permits it.

### Windows (PowerShell)

```powershell
cmake -S . -B build -DJUCE_DIR="C:/SDK/JUCE" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Octivier_VST3 Octivier_Standalone
```

Open `build/Octivier.sln` in Visual Studio if you prefer the IDE. AU is not a Windows format.

### macOS

```bash
cmake -S . -B build -DJUCE_DIR="$HOME/SDK/JUCE" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Octivier_VST3 Octivier_AU Octivier_Standalone
```

### Linux

```bash
cmake -S . -B build -DJUCE_DIR="$HOME/SDK/JUCE" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Octivier_VST3 Octivier_Standalone
```

Target names can vary slightly with JUCE/CMake generator; inspect `cmake --build build --target help` if needed.

## Run the demo

The test-tone tool requires Python 3, NumPy, and SoundFile:

```bash
python -m pip install numpy soundfile
python tools/make_test_tone.py --out octivier_test_tone.wav --seconds 8 --sr 48000 --f0 220
```

Import the WAV into a DAW, insert Octivier, and compare bypass vs processing at matched loudness. Try low strength first. Use a monophonic vocal, bass, or lead; avoid interpreting the UI's tracker readout as a guaranteed F0 truth.

## Smoke tests

```bash
cmake -S . -B build -DJUCE_DIR="C:/SDK/JUCE" -DOCTIVIER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target OctivierSmokeTests
ctest --test-dir build -C Release --output-on-failure
```

The smoke test checks finite output and state serialization. It does **not** certify frequency response, real-time safety, host compatibility, or perceptual quality.

A JUCE-independent protocol stress test can be built without JUCE:

```bash
cmake -S . -B build-protocol -DOCTIVIER_PROTOCOL_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-protocol --parallel 2
ctest --test-dir build-protocol --output-on-failure
```

This test validates the mailbox/slot-state protocol model only; it does not compile or execute the plugin processor.

## Presets and state

The editor has a factory preset selector. Each factory preset is a complete 19-parameter snapshot defined in `Source/Presets.h`; selecting a preset writes every parameter through JUCE host notifications. `Manual` leaves the current state unchanged. The host's normal plugin-state mechanism is the authoritative recall path. `Save State` writes a processor XML state snapshot to `Documents/Octivier-State.xml` for diagnostics; it is not a multi-preset browser.

## Parameter overview

| Control | Function | Notes |
|---|---|---|
| Octave Strength | Scales odd cuts/even boosts | 0 = no harmonic emphasis |
| Harmonic Focus | Weighting of even/odd emphasis | Negative = lower partial emphasis |
| Odd Cut / Even Boost | Maximum per-partial change | Narrow dynamic peak filters |
| Tracking / Gate | Pitch tracker behavior | Monophonic source recommended |
| Freeze / Manual F0 | Hold a chosen fundamental | Useful for sustained notes, not polyphonic tracking |
| Mode | Classic, Sub, Hollow, Square | Timbre modes, not octave shifters |
| Delay / Width / Mod Depth | Stereo decorrelation | May produce comb filtering |
| Character | Clean, Tube, Tape | Simple waveshaping; no oversampling yet |
| Formant / Air | RBJ-style high shelves | Shelf curves still need golden-response verification; not a vocal-tract model |
| Dry Blend / Output | Parallel mix and output trim | Dry Blend 0 = fully processed; 1 = dry |

## License and attribution

This repository is a clean implementation of the supplied concept description. It does not include JUCE itself; JUCE is licensed separately by its authors. Confirm JUCE licensing obligations for your intended distribution. Do not imply endorsement by any named engineer or third party without permission.
