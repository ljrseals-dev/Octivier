# Architecture Notes

- `PluginProcessor` owns APVTS parameters, pitch history, delay memory, telemetry atomics, a three-slot coefficient-generation ring, a single-producer/single-consumer request mailbox, and per-channel biquad state.
- Coefficient slot states are `Idle`, `Writing`, `Ready`, and `Reading`. The worker claims an `Idle` slot with compare/exchange, then claims a `Ready` request mailbox. It writes plain-value coefficients and publishes the slot as `Ready` with release semantics. The audio thread acquires a ready slot at a block boundary and only retires its previous slot after a replacement has been acquired.
- The request mailbox has `Empty`, `Writing`, `Ready`, and `Reading` states. The audio thread claims `Empty` before writing request payload and publishes `Ready` with release semantics. The worker acquires `Ready` before reading the payload. If no coefficient slot is available, it does not consume the request.
- Coefficients are five-float biquad records; no JUCE reference-counted coefficient pointer is replaced or destroyed in `processBlock`. The per-channel transposed-direct-form-II state is audio-thread-owned.
- `prepareToPlay` and `releaseResources` stop the worker before resetting shared DSP state. This lifecycle protocol still requires host stress and sanitizer validation.
- Pitch estimation uses the first input channel, a recent 2048-sample window, mean removal, and normalized autocorrelation over a bounded lag range. Octave errors and dropouts remain possible.
- Formant and Air controls are implemented as RBJ-style high shelves in the same coefficient generation as the harmonic filters. Response curves have not yet been compared to golden data.
- The wet path is filtered and optionally saturated, then delay/decorrelated on the right channel. Dry blend is a linear parallel mix. Tube/Tape waveshaping is not oversampled; aliasing is possible.
- `PluginEditor` uses APVTS attachments. UI telemetry reads atomics; parameter writes use JUCE host notifications.

## Verification boundary

`Tests/SlotOwnershipTests.cpp` is a JUCE-independent stress test of the mailbox/slot-state protocol model. It compiled and passed in the recorded environment. It does **not** compile or execute `PluginProcessor.cpp`, and it is not a substitute for ThreadSanitizer on the integrated plugin. The JUCE plugin remains uncompiled until a compatible JUCE source checkout is supplied.
