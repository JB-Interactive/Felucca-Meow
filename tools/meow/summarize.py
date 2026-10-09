# SPDX-License-Identifier: GPL-3.0-only
"""Summary of meowstat.py JSON files: medians / IQR and the mean contours. Usage: summarize.py A.json [B.json ...]"""
import sys, json
import numpy as np


def q(v):
    v = np.array([x for x in v if x is not None and x == x], float)
    return f"{np.median(v):8.3f} [{np.percentile(v, 25):7.3f} .. {np.percentile(v, 75):7.3f}]"


for path in sys.argv[1:]:
    d = json.load(open(path))
    print(f"== {path.split('/')[-1]}: {len(d)} calls")
    for k in ('dur', 'f0_peak', 'f0_mean', 'peak_pos', 'jitter', 'shimmer', 'hnr'):
        print(f"  {k:9s}", q([r[k] for r in d]))
    st = np.array([r['f0_st'] for r in d]); it = np.array([r['int_db'] for r in d])
    print("  F0 st re peak (20 pts, median):", " ".join(f"{x:+.1f}" for x in np.median(st, 0)))
    print("  level dB re max (median):      ", " ".join(f"{x:+.0f}" for x in np.median(it, 0)))
    f1 = np.array([[np.nan if x is None else x for x in r['f1']] for r in d])
    print("  F1 Hz (Burg, median):           ", " ".join(f"{x:.0f}" for x in np.nanmedian(f1, 0)))
    f2 = np.array([[np.nan if x is None else x for x in r.get('f2', [None] * 20)] for r in d])
    print("  F2 Hz (Burg, median):           ", " ".join(f"{x:.0f}" for x in np.nanmedian(f2, 0)))
    h = np.array([np.nanmean(np.array(r['harm']), 0) for r in d])
    print("  H1..H6 dB re strongest (median):", " ".join(f"{x:+.0f}" for x in np.nanmedian(h, 0)))
    hm = np.array([np.array(r['harm']) for r in d])   # calls x 5 points x 6
    for i, name in enumerate(('20%', '35%', '50%', '65%', '80%')):
        print(f"     at {name}:", " ".join(f"{x:+.0f}" for x in np.nanmedian(hm[:, i, :], 0)))
    print("  centroid Hz (median at 5 pts):  ", " ".join(f"{x:.0f}" for x in np.median(np.array([r['centroid'] for r in d]), 0)))
