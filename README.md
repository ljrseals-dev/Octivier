# Octivier Harmonic Sculptor — JUCE Plugin Project

**Status: engineering prototype / validation candidate. Not release-certified.**

Octivier is a monophonic harmonic-timbre processor. It analyses the incoming
pitch, re-weights the existing harmonic series (narrow attenuation on odd
partials, tight emphasis on even partials), and optionally adds a decorrelated
stereo layer. It is **not a pitch shifter**: the waveform is never translated in
time or frequency. The “octave-up” perception that motivated the original
concept is a timbre effect and must be judged on listening tests, not on the
code alone.

The current state of the build, tests, and release gate is recorded in
`BUILD_STATUS.md`. Read that file before treating any capability here as
verified.

## Features implemented in this project

- JUCE AudioProcessor with host-automatable parameters and XML state
  serialization.
- VST3, AU, and Standalone build targets (AU is macOS-only).
- YIN-based pitch estimate (cumulative mean normalized difference function,
  parabolic refinement, octave-error guard) with gate, confidence hold,
  sensitivity, and manual / freeze fundamental.
- Classic, Sub, Hollow, and Square harmonic modes over 16 partial filter
  slots.
- Per-sample smoothing on every user parameter, tied to sample rate.
- Fixed three-slot coefficient-generation ring with explicit ownership states
  (`Idle` / `Writing` / `Ready` / `Reading`) and block-boundary adoption.
- Single-slot request mailbox with explicit (`Empty` / `Writing` / `Ready` /
  `Reading`) states for payload publication.
- Coefficients are plain five-float biquad records. No reference-counted
  coefficient pointer is constructed, copied, or destroyed on the audio
  thread.
- Formant and Air are RBJ high shelves at 3.2 kHz and 10 kHz (Q = 0.707),
  published in the same generation as the harmonic bank.
- 2× oversampled Tube and Tape waveshaping (IIR polyphase half-band).
- Bounded RMS compensation with asymmetric attack / release on the input
  reference.
- FFT-based harmonic meter (1024-pt Hann) with banded integration around each
  tracked harmonic.
- Graphite / cyan / amber UI with parameter attachments, factory preset
  selector, and live pitch / odd / even readouts.
- JUCE-independent standalone protocol stress test and JUCE-backed smoke
  test target.

## Important limitations — read before use

1. **No guaranteed octave translation.** Harmonic re-weighting changes
   spectral balance; it does not create missing partials or guarantee that
   listeners hear a stable octave above.

2. **Monophonic source only.** The tracker is YIN-based with an octave-error
   guard. It can mis-lock on breathy onsets, wide vibrato, polyphonic bleed,
   and highly inharmonic material. The confidence readout is a first-class
   signal, not decoration.

3. **Formant and Air are tone controls.** They are implemented as RBJ high
   shelves and their frequency responses have not yet been compared to golden
   data. They are not a formant-preservation model.

4. **RMS compensation has a defined reference.** Input RMS is captured before
   processing, smoothed asymmetrically (attack ≈ 50 ms, release ≈ 150 ms), and
   compared to the output RMS per block. The gain is clamped to ±12 dB and
   smoothed. It is neutral when the toggle is off.

5. **Decorrelator is a stereo delay.** Independent L/R taps with a continuous
   mod phase. It will comb-filter against a fully dry mix — this is inherent
   to any short-delay stereoizer.

6. **Harmonic meter is diagnostic.** It is a real 1024-point Hann-windowed
   FFT with banded integration around each tracked harmonic, but it is not
   normalized for window gain or FFT size, and its band width scales with the
   fundamental. Its dB readings are for orientation, not metrology.

7. **The audio thread is allocation-free and deallocation-free by
   construction.** Harmonic and shelf coefficients are pre-built by a
   dedicated `juce::Thread` into a fixed three-slot generation ring.
   Ownership states are explicit, and the audio thread only adopts a slot at a
   block boundary. This is an ownership-model claim, verified by inspection,
   not a certification: no ThreadSanitizer or host stress run has been
   recorded yet.

8. **Saturation is 2× oversampled.** Reported latency includes the
   oversampler.

9. **Latency and tail.** Reported latency equals the oversampler latency.
   Reported tail is 100 ms. The decorrelator does not contribute to host
   latency because it acts only on the wet path.

10. **Release status: BLOCKED.** See `BUILD_STATUS.md` for the current build
    state, the tests that have and have not been run, and the release gates
    that remain. The JUCE plugin has not been compiled in the recorded
    environment.

## Build prerequisites

- CMake 3.22+
- C++17 compiler supported by your JUCE version
- JUCE 8 recommended (8.0.4 or compatible)
- Windows: Visual Studio 2022 with Desktop development with C++; macOS: Xcode;
  Linux: a supported Clang/GCC toolchain and JUCE dependencies

Place JUCE in `./JUCE` or point `JUCE_DIR` at an existing JUCE checkout. The bootstrap scripts can fetch JUCE 8.0.4 from its official GitHub repository; in restricted or offline environments, provision JUCE manually and set `JUCE_DIR`.

### Windows (PowerShell)



## Technical manual and build bootstrap

Read [`Technical Manual.md`](Technical%20Manual.md) for the full architecture, setup, DSP caveats, and release checklist. This CMake project uses JUCE APIs directly; the Steinberg VST3 SDK is not a drop-in replacement. The scripts in `scripts/` fetch JUCE 8.0.4 if missing, run the protocol test, then attempt the full plug-in and JUCE smoke-test build. The full plug-in build remains unverified until it succeeds on a supported toolchain.
