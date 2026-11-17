# Octivier Validation Report

**Report date:** 2026-10-12  
**Build status:** JUCE plugin NOT COMPILED; standalone ownership-protocol test PASSED  
**Classification:** Engineering prototype; not release-certified

## Implementation status

| Area | Status | Evidence / limitation |
|---|---|---|
| JUCE processor/editor source | IMPLEMENTED; COMPILE UNVERIFIED | Source files and CMake targets are present; no JUCE checkout was available for compilation. |
| Host parameter exposure | IMPLEMENTED; COMPILE UNVERIFIED | APVTS parameters and UI attachments are present. |
| State save/restore | IMPLEMENTED; TEST NOT RUN | JUCE ValueTree XML binary serialization and smoke-test source are included. |
| VST3/AU/Standalone project definitions | IMPLEMENTED; BUILD NOT RUN | JUCE CMake formats; AU is macOS-only. |
| Pitch tracker | IMPLEMENTED, UNVERIFIED | Bounded normalized autocorrelation; octave errors/dropouts remain possible. Not canonical YIN. |
| Harmonic filter bank | IMPLEMENTED, RESPONSE UNVERIFIED | 16 peak-filter coefficient slots; no golden frequency-response curves. |
| Coefficient handoff | IMPLEMENTED BY INSPECTION; INTEGRATION UNVERIFIED | Fixed three-slot ring with Idle/Writing/Ready/Reading states; worker claims Idle with CAS; audio adopts Ready at block boundaries and retires the previous slot after acquiring a replacement. |
| Request mailbox | IMPLEMENTED BY INSPECTION; INTEGRATION UNVERIFIED | Producer claims Empty before writing payload and publishes Ready with release; worker claims Ready with acquire only after securing a slot. |
| Audio-thread coefficient ownership | IMPLEMENTED BY INSPECTION | Plain-value coefficients and per-channel filter states; no JUCE reference-counted coefficient pointers in the audio path. Requires integrated real-time audit. |
| Formant / Air shelves | IMPLEMENTED BY INSPECTION; RESPONSE UNVERIFIED | RBJ-style high shelves at nominal 3.2 kHz and 10 kHz (Air corner constrained below Nyquist), generated off-thread. No golden response test yet. |
| Automatic RMS compensation | NOT IMPLEMENTED | Parameter exists; processing intentionally leaves gain neutral. |
| Oversampled saturation | NOT IMPLEMENTED | Tube/Tape waveshaping runs at host rate; aliasing is possible. |
| Standalone ownership protocol stress | PASS | GCC 14.2 / CMake 3.31.6; 250,000 request handoffs; 1/1 CTest passed. This tests the protocol model, not the integrated JUCE processor. |
| Plugin compilation / JUCE smoke test | NOT RUN | Compatible JUCE source tree unavailable. |
| ThreadSanitizer / ASan | NOT RUN | Must run on integrated code/toolchain. |
| CPU and latency measurement | NOT VERIFIED | No benchmark run; decorrelation introduces delay. |
| Pluginval / DAW host runs | NOT RUN | Requires compiled binaries and host environment. |
| Audio perceptual/ABX tests | NOT RUN | No listening data supplied. |

## Test plan before release

1. Build Release and Debug on each target OS with a compatible JUCE version; compile all plugin and smoke-test targets.
2. Run the JUCE smoke test and deterministic tests for pitch error over 55–1200 Hz, silence/noise gating, state round-trip, parameter bounds, and finite output at 44.1/48/88.2/96/192 kHz.
3. Run ThreadSanitizer against repeated request updates, prepare/release cycles, and worker/audio stress. Run ASan/UBSan where supported.
4. Run pluginval and test load/unload, automation, state recall, mono/stereo, sample-rate changes, offline rendering, suspend/resume, and arbitrary block sizes in at least two DAWs.
5. Measure the RBJ shelf and harmonic-filter responses against golden curves; test stability near Nyquist and during rapid pitch glides.
6. Add oversampling or disable saturation modes pending aliasing measurements.
7. Implement real RMS compensation, or remove/rename the control before public beta.
8. Conduct loudness-matched listening and ABX comparisons against a clean duplicate and conventional pitch-shifted octave double. Do not claim guaranteed octave-up effect.

## Release gate

**BLOCKED.** The standalone ownership-protocol test passed, but the JUCE plugin has not been compiled. Release certification requires an integrated JUCE build, automated tests, sanitizer runs, pluginval/DAW host tests, DSP response measurements, real-time audit, and listening evaluation. No release certification is claimed by this report.
