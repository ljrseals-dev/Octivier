#!/usr/bin/env python3
"""Create a deterministic harmonic-rich mono WAV for plugin audition."""
import argparse
import numpy as np
import soundfile as sf

p = argparse.ArgumentParser()
p.add_argument('--out', default='octivier_test_tone.wav')
p.add_argument('--seconds', type=float, default=8.0)
p.add_argument('--sr', type=int, default=48000)
p.add_argument('--f0', type=float, default=220.0)
a = p.parse_args()
if not np.isfinite(a.seconds) or a.seconds <= 0.0 or a.seconds > 3600.0:
    p.error("--seconds must be finite and in the range (0, 3600]")
if a.sr < 8000 or a.sr > 768000:
    p.error("--sr must be between 8000 and 768000 Hz")
if not np.isfinite(a.f0) or a.f0 <= 0.0 or a.f0 * 1.015 >= a.sr * 0.5:
    p.error("--f0 must be finite, positive, and remain below Nyquist after modulation")
sample_count = int(round(a.seconds * a.sr))
if sample_count <= 0:
    p.error("--seconds and --sr produce an empty output")
t = np.arange(sample_count, dtype=np.float64)/a.sr
f0 = a.f0 * (1.0 + 0.015*np.sin(2*np.pi*4.7*t))
phase = 2*np.pi*np.cumsum(f0)/a.sr
x = np.zeros_like(t)
for h in range(1, 25): x += (1.0/h)*np.sin(h*phase + 0.08*np.sin(2*np.pi*0.7*t))
x *= 0.18 / max(np.max(np.abs(x)), 1e-12)
sf.write(a.out, x.astype(np.float32), a.sr, subtype='PCM_24')
print(f'Wrote {a.out} ({a.sr} Hz, {a.seconds:.2f} s)')
