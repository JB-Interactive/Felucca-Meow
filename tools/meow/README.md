# MEOW analysis

How the MEOW engine (`firmware/src/eng_meow.c`) was fitted: the same measurements on recorded
meows and on hostsim renders, compared side by side. No recording is part of the firmware.

Reference recordings (download them yourself, check their licences):

- CatMeows, Ludovico et al. 2020, https://zenodo.org/records/4008297 (CC BY-NC 4.0, 8 kHz)
- Cat Sound Classification Dataset V2, Pandeya & Lee 2018, https://zenodo.org/records/4724180
  (CC BY 4.0, 44.1 kHz; the Happy and MotherCall classes are meows)

Needs `pip install praat-parselmouth numpy`.

```
python3 tools/meow/meowstat.py real.json catmeows/dataset/*.wav     # F0, level, F1/F2, H1..H6, jitter, shimmer, HNR
for k in 7 8 9 10 11 12 13 14; do SENDS=0,0,0 NOTE=$k OCT=1 SECS=1 build/host/hostsim 14 0 0 syn_$k.wav; done
python3 tools/meow/meowstat.py syn.json syn_*.wav
python3 tools/meow/summarize.py real.json syn.json
python3 tools/meow/ltas.py "catmeows/dataset/*.wav" "syn_*.wav"   # long-term spectrum, 250 Hz bands (SR=16000: 500 Hz)
```
