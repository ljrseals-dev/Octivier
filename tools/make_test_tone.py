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
t = np.arange(int(a.seconds*a.sr), dtype=np.float64)/a.sr
f0 = a.f0 * (1.0 + 0.015*np.sin(2*np.pi*4.7*t))
phase = 2*np.pi*np.cumsum(f0)/a.sr
x = np.zeros_like(t)
for h in range(1, 25): x += (1.0/h)*np.sin(h*phase + 0.08*np.sin(2*np.pi*0.7*t))
x *= 0.18 / max(np.max(np.abs(x)), 1e-12)
sf.write(a.out, x.astype(np.float32), a.sr, subtype='PCM_24')
print(f'Wrote {a.out} ({a.sr} Hz, {a.seconds:.2f} s)')
