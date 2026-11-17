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
