# Changelog

## 2026-10-12 — concurrency and packaging revision

- Replaced live JUCE IIR coefficient-pointer mutation with fixed-value biquad
  coefficients and audio-thread-owned filter state.
- Added a three-slot `Idle / Writing / Ready / Reading` coefficient-generation
  ring with CAS ownership transitions.
- Replaced the boolean request flag with an `Empty / Writing / Ready /
  Reading` mailbox; producer claims before writing payload, then
  release-publishes it.
- Worker reserves a coefficient slot before consuming a request; it does not
  consume a request if no slot is available.
- Added RBJ high-shelf coefficients for Formant and Air and publishes them
  with the harmonic bank.
- Stopped the worker around `prepareToPlay` / `releaseResources`
  reconfiguration to avoid resetting shared slot / sample-rate data while the
  worker is active.
- Added a JUCE-independent 250,000-request protocol stress test, CMake-only
  test mode, and cross-platform GitHub Actions workflow.
- Added `BUILD_STATUS.md` and updated validation docs.

## Outstanding (not yet implemented or verified)

- Oversampled saturation: implemented in code, aliasing not yet measured.
- Automatic RMS compensation: implemented in code, not yet validated against
  transients and sustained signals.
- Golden frequency-response tests for the harmonic filter bank and both
  shelves: not yet added.
- ThreadSanitizer / ASan runs against the integrated plugin: not yet
  performed.
- pluginval and DAW host runs: not yet performed.
- Perceptual ABX: not yet performed.

Release remains BLOCKED because the JUCE plugin has not been compiled and
host / sanitizer validation has not been performed.
