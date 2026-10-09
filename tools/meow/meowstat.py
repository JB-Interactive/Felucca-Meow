# SPDX-License-Identifier: GPL-3.0-only
"""Acoustic profile of meows: per call duration, F0 / intensity contours (normalised time), harmonic
amplitude profile, HNR, jitter, shimmer. Usage: meowstat.py OUT.json FILE.wav... (resampled to 8 kHz)"""
import sys, json
import numpy as np
import parselmouth
from parselmouth.praat import call

NP = 20  # contour points


def analyse(path, fs_target=8000):
    snd = parselmouth.Sound(path)
    if snd.n_channels > 1:
        snd = snd.convert_to_mono()
    if snd.sampling_frequency != fs_target:
        snd = snd.resample(fs_target)
    pitch = snd.to_pitch_ac(time_step=0.005, pitch_floor=200, pitch_ceiling=1600, voicing_threshold=0.5)
    f0 = pitch.selected_array['frequency']
    t = pitch.xs()
    inten = snd.to_intensity(minimum_pitch=200, time_step=0.005)
    it = np.array([inten.get_value(x) if inten.get_value(x) == inten.get_value(x) else -100 for x in t])
    imax = np.nanmax(it)
    voiced = (f0 > 0) & (it > imax - 25)
    if voiced.sum() < 10:
        return None
    idx = np.nonzero(voiced)[0]
    # the longest run of (nearly) continuous voicing: the call
    runs, s = [], idx[0]
    for a, b in zip(idx[:-1], idx[1:]):
        if b - a > 6:  # gap > 30 ms
            runs.append((s, a)); s = b
    runs.append((s, idx[-1]))
    s, e = max(runs, key=lambda r: r[1] - r[0])
    t0, t1 = t[s], t[e]
    dur = t1 - t0
    if dur < 0.08:
        return None
    tt = np.linspace(t0, t1, NP)
    f0c = np.interp(tt, t[s:e + 1][f0[s:e + 1] > 0], f0[s:e + 1][f0[s:e + 1] > 0])
    peak = f0c.max()
    st = 12 * np.log2(f0c / peak)
    ic = np.interp(tt, t, it) - it[s:e + 1].max()
    # harmonic profile at 5 points (H1..H6 in dB re the strongest), centroid
    x = snd.values[0]
    fs = snd.sampling_frequency
    hp = []
    cen = []
    for tc in np.linspace(t0, t1, 7)[1:-1]:
        i0 = int(tc * fs) - 512
        seg = x[max(0, i0):i0 + 1024]
        if len(seg) < 1024:
            seg = np.pad(seg, (0, 1024 - len(seg)))
        sp = np.abs(np.fft.rfft(seg * np.hanning(1024), 8192))
        fr = np.fft.rfftfreq(8192, 1 / fs)
        fq = pitch.get_value_at_time(tc)
        if not fq or fq != fq:
            fq = float(np.interp(tc, tt, f0c))
        h = []
        for k in range(1, 7):
            if k * fq > fs / 2 - 100:
                h.append(np.nan); continue
            j = np.argmin(abs(fr - k * fq)); w = max(2, int(0.25 * fq / (fr[1])))
            h.append(20 * np.log10(sp[max(0, j - w):j + w].max() + 1e-12))
        h = np.array(h); hp.append(h - np.nanmax(h))
        cen.append(float((sp * fr).sum() / sp.sum()))
    part = snd.extract_part(t0, t1)
    pp = call(part, "To PointProcess (periodic, cc)", 200, 1600)
    jit = call(pp, "Get jitter (local)", 0, 0, 0.0001, 0.02, 1.3)
    shim = call([part, pp], "Get shimmer (local)", 0, 0, 0.0001, 0.02, 1.3, 1.6)
    hnr = call(part.to_harmonicity_cc(0.01, 200, 0.1, 1.0), "Get mean", 0, 0)
    # F1 by Burg (max formant 4000 at 8 kHz: only F1, F2 are inside)
    fm = part.to_formant_burg(time_step=0.01, max_number_of_formants=3, maximum_formant=4000)
    f1 = [fm.get_value_at_time(1, part.xmin + q * (part.xmax - part.xmin)) for q in np.linspace(0.05, 0.95, NP)]
    f2 = [fm.get_value_at_time(2, part.xmin + q * (part.xmax - part.xmin)) for q in np.linspace(0.05, 0.95, NP)]
    return dict(file=path.split('/')[-1], dur=float(dur), f0_peak=float(peak), f0_mean=float(f0c.mean()),
                peak_pos=float(np.argmax(f0c) / (NP - 1)), f0_st=st.tolist(), int_db=ic.tolist(),
                harm=[h.tolist() for h in hp], centroid=cen, jitter=float(jit), shimmer=float(shim),
                hnr=float(hnr), f1=[float(v) if v == v else None for v in f1],
                f2=[float(v) if v == v else None for v in f2])


if __name__ == '__main__':
    out = []
    for p in sys.argv[2:]:
        try:
            r = analyse(p)
        except Exception as ex:  # a broken file: skipped, named
            print('skip', p, ex, file=sys.stderr); r = None
        if r:
            out.append(r)
    json.dump(out, open(sys.argv[1], 'w'))
    print(len(out), 'calls')
