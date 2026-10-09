# SPDX-License-Identifier: GPL-3.0-only
"""Long-term average spectrum of the voiced call (8 kHz, 250 Hz bands, dB re the strongest band). ltas.py GLOB..."""
import sys, glob
import numpy as np
import parselmouth, os
SR = int(os.environ.get("SR", "8000")); BW = SR // 32
for g in sys.argv[1:]:
    acc = None; n = 0
    for f in sorted(glob.glob(g)):
        s = parselmouth.Sound(f).convert_to_mono()
        if s.sampling_frequency != SR:
            s = s.resample(SR)
        x = s.values[0]
        it = s.to_intensity(200, 0.005); v = np.array([it.get_value(t) for t in it.xs()]); v = np.nan_to_num(v, nan=-100)
        on = it.xs()[v > v.max() - 15]
        x = x[int(on[0] * SR):int(on[-1] * SR)]
        spec = np.zeros(513)
        for i in range(0, len(x) - 1024, 256):
            spec += np.abs(np.fft.rfft(x[i:i + 1024] * np.hanning(1024))) ** 2
        if not spec.sum() > 0:
            continue
        spec /= spec.sum(); acc = spec if acc is None else acc + spec; n += 1
    fr = np.fft.rfftfreq(1024, 1 / SR)
    bands = [10 * np.log10(acc[(fr >= b) & (fr < b + 250)].sum()) for b in range(0, SR // 2, BW)]
    m = max(bands)
    print(f"{g.split('/')[-1][:18]:18s} n={n:3d} ", " ".join(f"{b - m:+4.0f}" for b in bands))
print(" " * 25, " ".join(f"{b // 100:4d}" for b in range(0, SR // 2, BW)), "(x100 Hz)")
