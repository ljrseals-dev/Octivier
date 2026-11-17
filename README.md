# Octivier Harmonic Sculptor — JUCE Plugin Project

**Status: engineering prototype / validation candidate. Not release-certified.** This project converts the supplied Octivier concept into a JUCE C++ VST3, AU (macOS), and Standalone project. It uses a block-rate autocorrelation pitch estimate and a bank of dynamically tuned peak filters to attenuate odd partials and emphasize even partials. It does **not** resynthesize or translate pitch by an octave. The “octave-up” result is a perceptual/timbre hypothesis and must be judged with listening tests.

## Features implemented in this project

- JUCE AudioProcessor with host-automatable parameters and XML state serialization.
- VST3, AU, and Standalone build targets (AU is macOS-only).
- Pitch estimate with gate, confidence hold, sensitivity, and manual/freeze fundamental.
- Classic, Sub, Hollow, and Square harmonic modes; 16 partial filter slots.
- Strength/focus smoothing; bounded filter frequency/Q; parallel dry blend.
- Stereo delay-based decorrelation controls, simple Tube/Tape waveshaping, output trim.
- Graphite/cyan/amber UI, parameter attachments, factory preset selector, live pitch/odd/even readouts.
- Deterministic synthetic test-tone generator and JUCE smoke-test target.

## Important limitations — read before use

1. **No guaranteed octave translation.** Harmonic EQ changes spectral balance; it does not create missing harmonics or guarantee that listeners hear a stable octave above.
2. **Tracker is deliberately simple.** Autocorrelation can produce octave errors and fail on breathy, polyphonic, noisy, or transient material. Intended source is monophonic.
3. **“Formant” and “Air” are not full shelf filters yet.** They currently act as broad gain trims. Do not market them as formant preservation.
4. **RMS compensation is exposed but neutral.** It is not yet a real dry/wet loudness-matching implementation.
5. **Decorrelator is a lightweight delay implementation.** It is not a phase-safe stereoizer and may comb-filter when blended with dry signal.
6. **Harmonic meter is a diagnostic approximation**, not calibrated odd/even spectral integration.
7. **Realtime allocation audit, CPU profiling, sanitizer runs, pluginval, DAW tests, and perceptual ABX are not performed here.** Do not claim zero latency: the decorrelation path is intentionally delayed and reported tail is 250 ms.
8. The plugin's source can be built only after a compatible JUCE source tree is supplied. This package does not bundle JUCE.

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

## Presets and state

The editor has a factory preset selector. Preset selection writes parameter values through JUCE host notifications. The host's normal plugin-state mechanism is the authoritative recall path. `Save State` writes a processor XML state snapshot to `Documents/Octivier-State.xml` for diagnostics; it is not a multi-preset browser.

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
| Formant / Air | Broad gain trims in this version | Labels are provisional; not true shelf/formant processing |
| Dry Blend / Output | Parallel mix and output trim | Mix 0 = full processed signal |

## License and attribution

This repository is a clean implementation of the supplied concept description. It does not include JUCE itself; JUCE is licensed separately by its authors. Confirm JUCE licensing obligations for your intended distribution. Do not imply endorsement by any named engineer or third party without permission.
