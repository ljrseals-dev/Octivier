# Architecture Notes

## Ownership model

`PluginProcessor` owns APVTS parameters, pitch history, delay memory,
telemetry atomics, a three-slot coefficient-generation ring, a single-slot
request mailbox, the oversampler, and per-channel biquad state.

### Coefficient ring

Slot states are `Idle`, `Writing`, `Ready`, `Reading`.

- The worker is the only writer of `Writing` and the only transitioner of
  `Idle → Writing → Ready`.
- The audio thread is the only writer of `Reading` and the only transitioner
  of `Ready → Reading` and `Reading → Idle`.
- At most one slot is in `Reading` and at most one slot is in `Writing` at
  any instant. No slot is ever touched by both threads in overlapping
  ownership states.
- The audio thread adopts a new `Ready` slot at a block boundary, holds it for
  the entire block, and retires its previous slot at the top of the next
  block, before attempting to adopt another.

### Request mailbox

Mailbox states are `Empty`, `Writing`, `Ready`, `Reading`.

- The audio thread claims `Empty` with a CAS before writing the payload and
  publishes `Ready` with release semantics.
- The worker claims `Ready` with acquire only after it has secured an `Idle`
  slot. If no slot is available, it does not consume the request; the audio
  thread's `pending` flag remains set and the request is retried on the next
  worker cycle.

## Filtering

- Coefficients are five-float biquad records. No JUCE reference-counted
  coefficient pointer is replaced or destroyed in `processBlock`.
- The per-channel transposed-direct-form-II state is audio-thread-owned.
- Formant and Air controls are RBJ high shelves in the same coefficient
  generation as the harmonic filters. Response curves have not yet been
  compared to golden data.
- Saturation uses `juce::dsp::Oversampling<float>` in 2× IIR polyphase
  half-band mode; latency is reported to the host.

## Lifecycle

`prepareToPlay` and `releaseResources` stop the worker before resetting or
releasing shared DSP state. This lifecycle protocol still requires host stress
and sanitizer validation, especially for repeated prepare / release and
sample-rate changes.

## Pitch estimation

Pitch estimation uses the first input channel, a recent 2048-sample window,
mean removal, and canonical YIN CMND over a bounded lag range, with a
parabolic refinement and an octave-error guard. Octave errors and dropouts
remain possible and have not been validated against a reference dataset.

## Signal path

The wet path is filtered, shelf-shaped, optionally oversampled-saturated,
then delayed and decorrelated on the right channel. Dry blend is a linear
parallel mix at the output. `PluginEditor` uses APVTS attachments; UI
telemetry reads atomics; parameter writes use JUCE host notifications.

## Verification boundary

`Tests/SlotOwnershipTests.cpp` is a JUCE-independent stress test of the
mailbox / slot-state protocol model. It compiled and passed in the recorded
environment. It does **not** compile or execute `PluginProcessor.cpp`, and it
is not a substitute for ThreadSanitizer on the integrated plugin. The JUCE
plugin remains uncompiled until a compatible JUCE source checkout is supplied.
