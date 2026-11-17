# Build and Validation Status

**Updated:** 2026-11-17  
**Release gate:** BLOCKED — JUCE plugin and host validation remain outstanding.  
**Current review branch:** `repair/yin-pitch-validation-v2`  
**Default branch:** unchanged during this work.

## Completed validation

- **JUCE-independent Release build:** PASS — [Pitch and protocol validation run](https://github.com/ljrseals-dev/Octivier/actions/runs/44191350998).
- **CTest:** PASS — all four tests passed:
  - `OctivierSlotOwnershipTests`
  - `AcfBehaviorPreservationTests`
  - `PitchTrackerPreparationTests`
  - `AcfPitchTrackerBehaviorTests`
- **ACF extraction equivalence:** PASS — 1,000 frame rows bit-identical to the frozen pre-extraction implementation under the same compiler/build configuration. This is not a cross-compiler or cross-architecture guarantee.
- **Corrected ACF pitch sweep:** PASS — E1–C7 at 48 and 96 kHz; sine tolerance 35 cents and saw tolerance 60 cents. The full benchmark continues to use a stricter 50-cent correctness definition.
- **YIN preparation and safety:** PASS — sample-rate-dependent frame sizing, invalid configuration rejection, Nyquist guard, non-finite input rejection, and unrepresentable difference-function rejection.
- **Full pitch matrix:** PASS — 154 result rows plus header across 11 configurations, including matched-frame ACF/YIN and separate 40 Hz experiments. The CI artifact contains the full CSV, quick CSV, and ACF-preservation CSV.
- **Test-tone tool syntax:** PASS — `python -m py_compile tools/make_test_tone.py`.
- **ThreadSanitizer:** PASS — standalone mailbox/SPSC stress model, [Protocol Tests run](https://github.com/ljrseals-dev/Octivier/actions/runs/44191350059). This does not instrument the integrated JUCE plugin.
- **Linux and macOS protocol tests:** PASS.
- **Windows protocol target:** build passed; its test was still running at the time of this status update.

## Pitch benchmark findings

At 20 dB SNR on matched frames (correct means within 50 cents):

| Configuration | ACF sine | YIN sine | ACF saw | YIN saw | Mean estimate time ACF → YIN |
|---|---:|---:|---:|---:|---:|
| 48 kHz / 2,048 | 88.4% | 92.8% | 89.5% | 92.8% | 0.665 → 0.728 ms |
| 48 kHz / 4,096 | 90.2% | 92.8% | 92.0% | 92.8% | 1.52 → 1.66 ms |
| 96 kHz / 4,096 | 79.7% | 92.8% | 90.2% | 92.4% | 2.64 → 2.93 ms |
| 96 kHz / 8,192 | 86.2% | 92.8% | 92.4% | 92.4% | 6.05 → 6.66 ms |

YIN is approximately 10–11% slower in these matched-frame measurements. At 0 and -5 dB it returns unvoiced on the sine/saw fixtures; ACF continues returning estimates but with substantial errors. YIN therefore has a modest clean-signal accuracy advantage, not a universal win.

The separate 40 Hz YIN tests detect 41.2 Hz correctly on the 20 dB sine/saw fixtures. Their 4,096-sample/48 kHz and 8,192-sample/96 kHz windows have approximately 85.3 ms frame duration; measured compute times are approximately 2.17 ms and 8.69 ms. Frame duration and first-valid estimate time are not host end-to-end latency.

## Implemented corrections

- ACF no longer selects the first sample above the correlation threshold. It selects a genuine local peak and uses parabolic lag refinement.
- ACF preparation rejects invalid rates/F0 ranges, Nyquist violations, and undersized frames that cannot reach the requested low F0.
- The plugin scales its ACF window with sample rate: 2,048 samples at 48 kHz, 4,096 at 96 kHz, and 8,192 at 192 kHz. Its chronological frame scratch is allocated in `prepareToPlay`, not in `processBlock`.
- YIN rejects invalid configurations, non-finite frames, and difference-function sums that cannot be represented by its float work buffers.
- Pitch-update scheduling retains excess samples across audio blocks.
- The test-tone generator validates duration, sample rate, and modulated F0 before allocating output.
- Mailbox stress coverage was expanded; production mailbox code remains unchanged.

## Remaining release gates

- **JUCE 8 plugin build:** NOT RUN — a compatible JUCE source checkout is not available in this environment.
- **JUCE-backed smoke tests:** NOT RUN.
- **Integrated-plugin TSan/RTSan:** NOT RUN. The current TSan result applies only to the standalone SPSC protocol model.
- **Host-driven real-time validation:** NOT RUN. An instrumented build alone would not establish host real-time safety.
- **pluginval / DAW validation:** NOT RUN.
- **Perceptual listening / ABX:** NOT RUN.
- **RMS Compensation:** remains a documented neutral placeholder; it is not claimed as implemented.

Do not describe this project as production-ready or release-certified until the remaining JUCE, host, and listening gates are completed.
