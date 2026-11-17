# Changelog

## 2026-10-12 — concurrency and packaging revision

- Replaced live JUCE IIR coefficient-pointer mutation with fixed-value biquad coefficients and audio-thread-owned filter state.
- Added a three-slot `Idle/Writing/Ready/Reading` coefficient-generation ring with CAS ownership transitions.
- Replaced the boolean request flag with an `Empty/Writing/Ready/Reading` mailbox; producer claims before writing payload, then release-publishes it.
- Worker reserves a coefficient slot before consuming a request; it does not consume a request if no slot is available.
- Added RBJ-style high-shelf coefficients for Formant and Air and publishes them with the harmonic bank.
- Stopped the worker around `prepareToPlay`/`releaseResources` reconfiguration to avoid resetting shared slot/sample-rate data while the worker is active.
- Added a JUCE-independent 250,000-request protocol stress test, CMake-only test mode, and cross-platform GitHub Actions workflow.
- Added `BUILD_STATUS.md` and updated validation docs. Release remains BLOCKED because the JUCE plugin has not been compiled and host/sanitizer validation has not been performed.

## Unreleased — DSP review fixes

- Drive the delay modulation LFO from a monotonic sample counter instead of the wrapping delay-ring index.
- Replace break-on-first-threshold pitch selection with a full-range correlation scan and shortest strong-lag selection; preallocate lag scores in `prepareToPlay`.
- Clear the retained fundamental when the pitch gate closes so filters stop following stale pitch and the next note reacquires from zero.
- Use an explicit identity biquad for out-of-range harmonic slots.
- Replace the alternating-sample odd/even meter with a 2,048-point Hann FFT and single-bin integration at tracked harmonic frequencies.
- Make factory presets complete 19-parameter snapshots so preset switching cannot inherit stale values.
- These source changes have not been compiled or run; plugin, FFT-response, preset, LFO, sanitizer, pluginval, and DAW validation remain outstanding.
