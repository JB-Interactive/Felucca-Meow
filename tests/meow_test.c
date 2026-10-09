/* SPDX-License-Identifier: GPL-3.0-only */
/* MEOW engine test (src/eng_meow.c, MEOW-1), through hostsim.c as noise_test.c; built WITHOUT FELUCCA_TRK_DEF
 * (MEOW-1's power-on parts).
 *   build/host/meow_test
 * 1. the engine: number 14 (ENGI_MEOW, append-only), its name in the ENG list, offered (eng_ok), shown after VOICE.
 * 2. power-on (TRK_DEF): PURR BASS, MEOW LEAD, HISS HAT, DRUM KIT; the factory pattern 14 MIAU, MEOW LEAD suggests it.
 * 3. every preset: a note sounds, never at full scale, and its voice ends by itself (a held key too, except LEN 127).
 * 4. the calls' lengths at LEN 84, RAND 0: MEOW 0.45 .. 0.9 s (the measured meows: 0.55 .. 0.93 s), MEW shorter,
 *    YOWL longer than 1.2 s, HISS (a spit) shorter than 0.25 s.
 * 5. pitch: MEOW's arc peaks at the key (+-50 cents); PURR's pulses run at the key (A0: 27.5 Hz +-3 %);
 *    MRRP's trill pulses at 25 Hz (+-2 Hz).
 * 6. LEN 127 holds while the key is held, and the call closes within 1 s of the release. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("meow: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define MAXS (FS * 6)
static int16_t buf[MAXS];

/* part 1 plays MEOW preset pi (or its MODE overridden, mode >= 0), one note from silence: held for gate s, rendered
 * for secs s into buf (left); returns the samples until the voice ended (0: still active at the end) */
static uint32_t play(uint32_t pi, int mode, int len, uint32_t note, double gate, double secs)
{
    track_t *t = &trk[0];
    uint32_t f, i, n = (uint32_t)(secs * FS), ended = 0;
    host_tracks_init();
    host_preset(t, ENGI_MEOW, pi);
    t->p[P_VOICE] = V_POLY;
    t->p[P_REV] = t->p[P_DLY] = t->p[P_CHOR] = 0;
    if (mode >= 0)
        t->p[P_E0] = (int16_t)mode;
    if (len >= 0)
        t->p[P_E2] = (int16_t)len;
    t->p[P_E7] = 0;                                      /* RAND 0: the call as designed */
    for (i = 0; i < 4u; i++)
        if (i)
            trk[i].p[P_MUTE] = 1;
    trk_note_on(t, note, 100);
    for (f = 0; f < n; f += CTL) {
        int32_t o[2 * CTL];
        if (gate >= 0 && f >= (uint32_t)(gate * FS) && f < (uint32_t)(gate * FS) + CTL)
            trk_note_off(t, note);
        mix_block(o, CTL);
        for (i = 0; i < CTL && f + i < MAXS; i++)
            buf[f + i] = (int16_t)(o[2 * i] > 32767 ? 32767 : o[2 * i] < -32768 ? -32768 : o[2 * i]);
        if (!ended && f > CTL * 4u) {
            uint32_t k, a = 0;
            for (k = 0; k < NVOICE; k++)
                a += t->v[k].active;
            if (!a)
                ended = f;
        }
    }
    return ended;
}

static int32_t peak(uint32_t a, uint32_t b)
{
    int32_t m = 0;
    for (; a < b; a++)
        m = abs(buf[a]) > m ? abs(buf[a]) : m;
    return m;
}

/* the frequency of the strongest autocorrelation lag in lo..hi Hz of buf[a .. a + n) (normalised, parabolic) */
static double f0_at(uint32_t a, uint32_t n, double lo, double hi)
{
    uint32_t l, l0 = (uint32_t)(FS / hi), l1 = (uint32_t)(FS / lo), best = l0;
    double bv = -1e30, r[3] = {0, 0, 0};
    for (l = l0; l <= l1; l++) {
        double s = 0;
        uint32_t i;
        for (i = 0; i + l < n; i++)
            s += (double)buf[a + i] * buf[a + i + l];
        s /= (double)(n - l);
        if (s > bv) {
            bv = s;
            best = l;
        }
    }
    for (l = 0; l < 3u; l++) {
        double s = 0;
        uint32_t i, k = best + l - 1u;
        for (i = 0; i + k < n; i++)
            s += (double)buf[a + i] * buf[a + i + k];
        r[l] = s / (double)(n - k);
    }
    {
        double d = r[0] - 2 * r[1] + r[2], o = d != 0 ? 0.5 * (r[0] - r[2]) / d : 0;
        return FS / ((double)best + o);
    }
}

/* the length of the call: the first to the last 5 ms window above -30 dB of the loudest */
static double call_len(uint32_t n)
{
    uint32_t w = FS / 200, i, first = 0, last = 0, k;
    double e[2000], m = 0;
    for (k = 0; k * w + w <= n && k < 2000u; k++) {
        double s = 0;
        for (i = 0; i < w; i++)
            s += (double)buf[k * w + i] * buf[k * w + i];
        e[k] = s;
        m = s > m ? s : m;
    }
    for (i = 0; i < k; i++)
        if (e[i] > m * 1e-3) {
            if (!first)
                first = i;
            last = i;
        }
    return (double)(last - first + 1u) * w / FS;
}

static uint32_t preset_of(const char *name)
{
    uint32_t k;
    for (k = 0; k < ENG_MEOW.npresets; k++)
        if (!strcmp(ENG_MEOW.presets[k].name, name))
            return k;
    return 255u;
}

int main(void)
{
    uint32_t k, n;
    char what[160];
    /* 1. the engine */
    check("MEOW is engine 14 (ENGI_MEOW), named in the ENG list, offered", ENGINES[ENGI_MEOW] == &ENG_MEOW &&
          NENGINES == 15u && !strcmp(N_ENGNAME[ENGI_MEOW], "MEOW") && eng_ok(ENGI_MEOW));
    check("MEOW is shown right after VOICE (ENGINE_ORDER)", eng_vis(eng_rank(5u) + 1u) == ENGI_MEOW);
    /* 2. power-on, the pattern */
    check("power-on parts: PURR BASS, MEOW LEAD, HISS HAT, DRUM KIT (TRK_DEF)",
          TRK_DEF[0][0] == ENGI_MEOW && TRK_DEF[0][1] == preset_of("PURR BASS") &&
          TRK_DEF[1][0] == ENGI_MEOW && TRK_DEF[1][1] == preset_of("MEOW LEAD") &&
          TRK_DEF[2][0] == ENGI_MEOW && TRK_DEF[2][1] == preset_of("HISS HAT") && TRK_DEF[3][0] == ENGI_DRUM);
    check("factory pattern 14 is MIAU; MEOW LEAD suggests it", NPATTERNS == 14u && !strcmp(PATTERNS[13].name, "MIAU") &&
          ENG_MEOW.presets[preset_of("MEOW LEAD")].pat == 14u);
    for (k = 0; k < ENG_MEOW.npresets; k++)
        if (ENG_MEOW.presets[k].pat > NPATTERNS)
            break;
    check("every MEOW preset suggests a factory pattern or none", k == ENG_MEOW.npresets);
    /* 3. every preset: sounds, no full scale, ends by itself */
    for (k = 0; k < ENG_MEOW.npresets; k++) {
        const preset_t *p = &ENG_MEOW.presets[k];
        int hold = p->e[2] >= 127, purr = p->e[0] == MEOW_PURR;
        uint32_t note = purr ? 36u : 72u, end = play(k, -1, -1, note, hold ? 1.0 : 3.0, 5.0);
        int32_t pk = peak(0, 5u * FS);
        snprintf(what, sizeof what, "%-9s: sounds (peak %5d), below full scale, the voice ends by itself (%.2f s)",
                 p->name, (int)pk, end / (double)FS);
        check(what, pk > 1500 && pk < 32000 && end && end < (uint32_t)(4.5 * FS));
    }
    /* 4. lengths at LEN 84 */
    {
        static const char *const NAME[MEOW_NMODE] = {"MEOW", "MEW", "MRRP", "YOWL", "PURR", "HISS"};
        double l[MEOW_NMODE];
        for (k = 0; k < MEOW_NMODE; k++) {
            play(0, (int)k, 84, k == MEOW_PURR ? 36u : 72u, 4.0, 4.0);
            l[k] = call_len(4u * FS);
        }
        snprintf(what, sizeof what, "LEN 84: MEOW %.2f s (0.45 .. 0.9), MEW %.2f (shorter), YOWL %.2f (> 1.2), HISS %.2f (< 0.25)",
                 l[MEOW_M], l[MEOW_MEW], l[MEOW_YOWL], l[MEOW_HISS]);
        check(what, l[MEOW_M] > 0.45 && l[MEOW_M] < 0.9 && l[MEOW_MEW] < l[MEOW_M] && l[MEOW_YOWL] > 1.2 && l[MEOW_HISS] < 0.25);
        (void)NAME;
    }
    /* 5. pitch: MEOW's peak at the key, PURR at the key, MRRP's trill */
    {
        double best = 0, f, cents;
        play(preset_of("MEOW LEAD"), -1, 84, 72u, 3.0, 1.2);
        for (n = FS / 20u; n + 2048u < FS; n += 512u)
            if (peak(n, n + 2048u) > 4000 && (f = f0_at(n, 2048u, 300, 900)) > best)
                best = f;
        cents = 1200.0 * log2(best / 523.25);
        snprintf(what, sizeof what, "MEOW's arc peaks at the key: C5 523.3 Hz, measured %.1f Hz (%+.0f cents, +-50)", best, cents);
        check(what, fabs(cents) < 50);
        play(preset_of("PURR SUB"), -1, 126, 21u, 3.0, 1.2);
        f = f0_at(FS / 5u, FS / 2u, 15, 60);
        snprintf(what, sizeof what, "PURR's pulses run at the key: A0 27.5 Hz, measured %.2f Hz (+-3 %%)", f);
        check(what, fabs(f / 27.5 - 1.0) < 0.03);
        play(preset_of("MRRP"), -1, 126, 72u, 3.0, 1.0);   /* the trill: the period of the 1 ms envelope */
        {
            static double env[600];
            double mean = 0, bv = -1e30, fr;
            uint32_t i, j, l, bl = 20;
            for (i = 0; i < 600u; i++) {                   /* 0.05 .. 0.65 s */
                double s = 0;
                for (j = 0; j < FS / 1000u; j++)
                    s += abs(buf[FS / 20u + i * (FS / 1000u) + j]);
                env[i] = s;
                mean += s / 600.0;
            }
            for (l = 20; l <= 100u; l++) {                 /* 10 .. 50 Hz */
                double s = 0;
                for (i = 0; i + l < 600u; i++)
                    s += (env[i] - mean) * (env[i + l] - mean);
                s /= 600.0 - l;
                if (s > bv) {
                    bv = s;
                    bl = l;
                }
            }
            fr = 1000.0 / bl;
            snprintf(what, sizeof what, "MRRP's trill pulses at 25 Hz: measured %.1f Hz (+-2)", fr);
            check(what, fabs(fr - 25.0) < 2.0);
        }
    }
    /* 6. LEN 127: held while the key is, closes after the release */
    {
        uint32_t end = play(preset_of("MEOW LEAD"), -1, 127, 72u, 2.0, 4.0);
        int32_t held = peak((uint32_t)(1.7 * FS), (uint32_t)(1.9 * FS));
        snprintf(what, sizeof what, "LEN 127: still sounding at 1.8 s with the key held (peak %d), ends %.2f s after the release",
                 (int)held, end / (double)FS - 2.0);
        check(what, held > 2000 && end > 2u * FS && end < 3u * FS);
    }
    printf(bad ? "MEOW TEST FAILED\n" : "meow: all ok\n");
    return bad != 0;
}
