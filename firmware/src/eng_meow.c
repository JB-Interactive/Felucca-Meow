/* SPDX-License-Identifier: GPL-3.0-only */
/* MEOW: a cat's meow from every key, in the key's pitch. 4 voices.
 * Built on the VOICE engine (eng_formant.c): its glottal source, its Klatt resonators (fres_coef) and its
 * ratio table (fmt_ratio); three formants in cascade, F1 -> F2 -> F3 (a cat's F4 lies above the band
 * that matters, and dropping it keeps the cost below VOICE's).
 *
 * A meow is a voiced call with diphthong-like formant transitions whose F0 follows an arc that peaks
 * where the mouth is opened widest (Nicastro, after Schoetz & van de Weijer, Speech Prosody 2014). Each
 * note plays one, over LEN, as three curves of its progress u (0 .. 1):
 *
 *   u       0 .. 0.12   0.12 .. 0.30   0.30 .. 0.60   0.60 .. 1
 *   mouth   [m] closed  [i]            [a] wide open  [u] closing
 *   pitch   below       rising         the key (peak) falling below the start
 *   level   ~35 %       opening        full           fading out
 *
 * The mouth is a cat's, not a person's: a vocal tract of about 8 cm (human adults ~17 cm), modelled as a
 * tube by Ekstroem, Cros Vila, Schoetz & Edlund, "A single formant explicates the ubiquity of 'meow'"
 * (VIHAR 2024, doi:10.31234/osf.io/edmuv): neutral F1 1103, F2 3309, F3 5516 Hz; raising the jaw lowers
 * them by about 703, 1247 and 1432 Hz. In so short a tract F2 and F3 lie too high to colour the vowel
 * much: the "iau" a listener hears is mostly F1 sweeping up and down with the jaw. So the mouth here is
 * one gesture, JAW (closed .. open) with lip ROUNDing at the end and a NASAL [m] at the start, not a
 * row of human vowels (human vowel formants, even scaled up, sound like a child singing "mi-a-u").
 * The [m] is a design choice, not a measurement: it makes the call read as "meow" to human ears.
 * Measured meows: mean F0 393 .. 661 Hz, mean duration 545 .. 932 ms (Schoetz, van de Weijer & Eklund,
 * PeerJ Preprints 2019): LEN's default is 0.7 s.
 *
 *   MODE    the call (MEOW; MEW MRRP YOWL PURR HISS play MEOW for now)
 *   SIZE    the tract: x1.33 .. x0.75 of the 8 cm cat's formants (kitten ~6 cm .. big tom ~11 cm)
 *   LEN     0.15 .. 1.5 s; at 127 the call holds at the open [a] while the key is held, then closes
 *   BEND    the arc, 0 .. 12 semitones: the start lies BEND / 2 below the key, the end BEND below
 *   WOW     vibrato (6.5 Hz) in the second half, up to +-1 semitone
 *   BRTH    breath noise into the resonators
 *   ROUGH   every second glottal pulse weaker: a subharmonic an octave down (growl)
 *   RAND    per note: LEN, BEND and SIZE wander (0 = every meow alike)
 *
 * The call is the voice's amplitude (meow_amp, engine_t.amp): a note plays its meow to the end, however
 * short the key or the step; the track's ADSR is not used. Velocity scales it as usual.
 * Voice state: ph[0] glottal phase, ph[1] vibrato phase, ph[2] bit 0 the pulse parity (ROUGH);
 * s[0..5] F1..F3 y1 / y2, s[6] tilt, s[7] progress (Q20, bits 0..20) and the note's random bits (21..31). */
#define MEOW_U1 (1 << 20)                               /* u = 1: the call is over */
#define MEOW_UMASK 0x001FFFFF
#define MEOW_HOLD_U (MEOW_U1 * 45 / 100)                /* LEN 127: held here (the arc's peak) */
#define MEOW_FOLOW 16                                   /* formant key tracking / 127: the mouth hardly moves with F0 */

enum { MEOW_M, MEOW_MEW, MEOW_MRRP, MEOW_YOWL, MEOW_PURR, MEOW_HISS, MEOW_NMODE };
static const char *const N_MEOW_MODE[MEOW_NMODE] = {"MEOW", "MEW", "MRRP", "YOWL", "PURR", "HISS"};

/* the 8 cm tract (Ekstroem et al. 2024): formants (Hz) with the jaw closed and how far opening it raises
 * them (to about the neutral tube's); lip rounding lowers them by up to these parts (Q8: protruded lips
 * lengthen the tract, F2 most); the nasal [m] pulls F1 to MEOW_NF1 and widens the bands above */
static const uint16_t MEOW_F0[3] = {400, 2060, 4080}, MEOW_FJ[3] = {1000, 1250, 1430};
static const uint8_t MEOW_RND[3] = {50, 77, 38};
#define MEOW_NF1 300

/* a curve: points (u Q8, value), smoothstep between them; the first point at u 0, the last at 256 */
typedef struct { uint8_t u; int16_t v; } mpt_t;
static int32_t mcurve(const mpt_t *c, uint32_t n, int32_t u8)   /* u8: u * 256 in Q8 (0 .. 65536) */
{
    uint32_t i;
    int32_t t, d;
    for (i = 1; i + 1 < n && (int32_t)c[i].u << 8 <= u8; i++)
        ;
    d = ((int32_t)c[i].u - c[i - 1].u) << 8;
    t = d > 0 ? clamp(((u8 - ((int32_t)c[i - 1].u << 8)) << 8) / d, 0, 256) : 256;
    t = (t * t * (768 - 2 * t)) >> 16;                  /* smoothstep, Q8 */
    return c[i - 1].v + (((c[i].v - c[i - 1].v) * t) >> 8);
}

/* MEOW: level (Q8 of full), jaw (Q8: closed .. open), rounding and nasality (Q8), pitch (Q8 of BEND, 0 = the key).
 * One gesture: the jaw opens to its widest at the pitch peak (u ~0.45) and closes again, the lips round
 * towards the end ([u]), the [m] lets go as the jaw starts to open */
static const mpt_t MEOW_AMP[] = {{0, 0}, {8, 80}, {28, 90}, {80, 256}, {145, 256}, {215, 120}, {255, 0}};
static const mpt_t MEOW_JAW[] = {{0, 0}, {26, 0}, {52, 70}, {115, 256}, {160, 225}, {228, 40}, {255, 0}};
static const mpt_t MEOW_ROUND[] = {{0, 0}, {140, 0}, {215, 256}, {255, 256}};
static const mpt_t MEOW_NASAL[] = {{0, 256}, {22, 256}, {42, 0}, {255, 0}};
static const mpt_t MEOW_PITCH[] = {{0, -140}, {31, -120}, {115, 0}, {165, -40}, {255, -256}};

/* the note's random value k (-128 .. 127, three of them from 11 bits), scaled by RAND */
static int32_t meow_rnd(const voice_t *v, const int16_t *p, uint32_t k)
{
    static const uint8_t K[3] = {1, 97, 181};
    uint32_t r = ((uint32_t)v->s[7] >> 21) * K[k % 3u];
    return ((int32_t)((r ^ (r >> 8)) & 255u) - 128) * p[P_E7] / 127;
}

static void meow_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[7] = (int32_t)((noise32(&formant_nz) >> 21) << 21);   /* the call starts over; its random bits */
    if (!v->env && !v->env_out) {                       /* a fresh voice (not a retrigger): from rest */
        uint32_t i;
        v->ph[0] = v->ph[1] = v->ph[2] = 0;
        for (i = 0; i < 7u; i++)
            v->s[i] = 0;
    }
}

/* once per control tick: the call moves on, its level is the voice's; at its end the voice ends */
static int32_t meow_amp(track_t *t, voice_t *v, int32_t adsr)
{
    const int16_t *p = t->p;
    uint32_t u = (uint32_t)v->s[7] & MEOW_UMASK, inc;
    int32_t len = p[P_E2];
    (void)adsr;
    if (!v->active)                                     /* (taken for another part: env_tick ended it) */
        return 0;
    if (u >= (uint32_t)MEOW_U1) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;                                   /* (the ADSR never ends the voice) */
    if (len >= 127) {                                   /* HOLD: 0.7 s, waits at the peak while the key is held */
        len = 84;
        if (v->gate && u >= (uint32_t)MEOW_HOLD_U)
            goto level;
    }
    /* 150 ms * 10^(len / 126) (x 2^(st16 / 192): st16 = len * 5.06), RAND +-30 % */
    inc = (5073u << 16) / fmt_ratio(len * 5 + len / 16 + ((meow_rnd(v, p, 0) * 73) >> 7));   /* Q20 per tick */
    u += inc;
    if (u > (uint32_t)MEOW_U1)
        u = MEOW_U1;
    v->s[7] = (int32_t)(((uint32_t)v->s[7] & ~(uint32_t)MEOW_UMASK) | u);
level:
    return (mcurve(MEOW_AMP, NELEM(MEOW_AMP), (int32_t)(u >> 4)) * 32767) >> 8;
}

static void meow_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    fres_t r1, r2, r3;
    int32_t f[3], b[3], j, buzz, oq, tilt, gv, gn, og, ogk, jaw, rnd, nas, bend, pst, lvl, rough;
    uint32_t inc, i, te, tp, rp, rn, ratio, f0, ph = v->ph[0], par = v->ph[2] & 1u;
    int32_t u8 = (int32_t)(((uint32_t)v->s[7] & MEOW_UMASK) >> 4);   /* u, Q16 of 256 */
    int32_t y1 = v->s[0], y2 = v->s[1], y3 = v->s[2], y4 = v->s[3], y5 = v->s[4], y6 = v->s[5];
    int32_t lp = v->s[6], nst = formant_nz;

    /* pitch: the arc (BEND, RAND +-40 %) and WOW in the second half */
    bend = p[P_E3] * 16;
    bend += (bend * meow_rnd(v, p, 1) * 102) >> 15;
    pst = (mcurve(MEOW_PITCH, NELEM(MEOW_PITCH), u8) * bend) >> 8;
    if (p[P_E4] && u8 > 100 << 8) {                     /* vibrato fades in from u 0.39 to 0.6 */
        int32_t fade = clamp((u8 - (100 << 8)) / 54, 0, 256);
        v->ph[1] += 20280000u;                          /* 6.5 Hz per control tick: 6.5 * CTL / FS * 2^32 */
        pst += (((sine_i(v->ph[1]) * p[P_E4]) >> 15) * fade * 16 / 127) >> 8;   /* +-1 semitone */
    }
    {
        uint32_t r = fmt_ratio(pst);
        inc = (m->inc >> 16) * r + (((m->inc & 0xFFFFu) * r) >> 16);
    }

    /* mouth: jaw, rounding, nasality -> F1..F3, times SIZE (RAND +-2 semitones) and key tracking */
    jaw = mcurve(MEOW_JAW, NELEM(MEOW_JAW), u8);
    rnd = mcurve(MEOW_ROUND, NELEM(MEOW_ROUND), u8);
    nas = mcurve(MEOW_NASAL, NELEM(MEOW_NASAL), u8);
    ratio = fmt_ratio((64 - p[P_E1]) * 5 / 4 + ((meow_rnd(v, p, 2) * 32) >> 7) +   /* +-5 st: x1.33 .. x0.75 */
                      ((MEOW_FOLOW * (v->pitch_cur - 60 * 16) * 516) >> 16) + (m->cutoff >> 8));
    f0 = (uint32_t)(((uint64_t)inc * 705600u) >> 32);   /* F0 in Hz * 16 */
    for (j = 0; j < 3; j++) {
        int32_t ff = (MEOW_F0[j] << 4) + ((MEOW_FJ[j] * jaw) >> 4), bb;   /* Hz * 16 */
        ff -= (((ff * MEOW_RND[j]) >> 8) * rnd) >> 8;
        if (j == 0)
            ff += (((MEOW_NF1 << 4) - ff) * nas) >> 8;
        bb = 80 * 16 + ff / 14;                         /* ~7 % of F plus 80 Hz: rounder than VOICE's narrow bands */
        bb += (bb * nas * (j ? 3 : 1)) >> 8;            /* [m]: the nose damps the bands, F2 / F3 most */
        ff = (int32_t)(((int64_t)ff * ratio) >> 16);
        bb = (int32_t)(((int64_t)bb * ratio) >> 16);
        if (j == 0 && (uint32_t)ff < f0 + (f0 >> 4) && ff > 0) {   /* F1 follows a high F0 (as VOICE) */
            bb = (int32_t)(((int64_t)bb * (int32_t)(f0 + (f0 >> 4))) / ff);
            ff = (int32_t)(f0 + (f0 >> 4));
        }
        f[j] = clamp(ff, 60 * 16, 9000 * 16);
        b[j] = clamp(bb, 20 * 16, 1500 * 16);
    }
    fres_coef(&r1, (uint32_t)f[0], (uint32_t)b[0]);
    fres_coef(&r2, (uint32_t)f[1], (uint32_t)b[1]);
    fres_coef(&r3, (uint32_t)f[2], (uint32_t)b[2]);

    /* source: brighter as the call gets louder ([m] dark, [a] open), + SHP, + velocity */
    lvl = mcurve(MEOW_AMP, NELEM(MEOW_AMP), u8);        /* Q8 */
    buzz = clamp(((30 + ((lvl * 45) >> 8)) << 8) + m->shape - (64 << 8) + (v->vel - 96) * 40, 0, 127 << 8);
    oq = 58982 - ((buzz * 284) >> 8);                   /* open quotient Q16: 0.90 .. 0.35 */
    tilt = 2500 + ((buzz * 109) >> 8);
    te = (uint32_t)oq << 16;
    tp = (te >> 8) * 166u;
    rp = (1u << 30) / ((tp >> 16) | 1u);
    rn = (1u << 30) / (((te - tp) >> 16) | 1u);
    gv = 32767 - p[P_E5] * p[P_E5];                     /* BRTH: voicing fades to half at the top */
    gn = p[P_E5] * 200;
    rough = 32767 - p[P_E6] * 180;                      /* ROUGH: the weak pulse, 1.0 .. 0.3 */
    og = 11000;
    og = (int32_t)(((int64_t)og * fmt_ratio((60 * 16 - v->pitch_cur) / 4)) >> 16);   /* 1.5 dB / oct (as VOICE) */
    ogk = (og >> 3) * (1 + (buzz >> 11));               /* presence k = 0.12 .. 0.8: soft, the formants are high already */

    for (i = 0; i < n; i++) {
        int32_t e, x, a, s;
        if (ph < tp)
            e = (sine_i(((ph >> 16) * rp) << 1) * 17644) >> 15;
        else if (ph < te)
            e = -sine_i(((ph - tp) >> 16) * rn);
        else
            e = 0;
        e += blep(ph - te, inc) >> 1;
        if (par)
            e = (e * rough) >> 15;
        lp += ((e - lp) * tilt) >> 14;
        x = (lp * gv) >> 15;
        if (gn) {
            int32_t nz = (((int32_t)(noise32(&nst) >> 16) - 32768) * gn) >> 15;
            x += ph < te ? nz : nz >> 1;
        }
        a = (int32_t)(((int64_t)r1.a * (x << 6) + (int64_t)r1.b * y1 + (int64_t)r1.c * y2 + (1 << 29)) >> 30);
        y2 = y1;
        y1 = a;
        a = (int32_t)(((int64_t)r2.a * a + (int64_t)r2.b * y3 + (int64_t)r2.c * y4 + (1 << 29)) >> 30);
        y4 = y3;
        y3 = a;
        a = (int32_t)(((int64_t)r3.a * a + (int64_t)r3.b * y5 + (int64_t)r3.c * y6 + (1 << 29)) >> 30);
        y6 = y5;
        y5 = clamp(a, -(1 << 28), 1 << 28);
        s = soft_knee(clamp((int32_t)(((int64_t)y5 * og + (int64_t)(y5 - y6) * ogk) >> 21), -200000, 200000), 24000);
        if (ph + inc < ph)                              /* a new glottal period: the other pulse */
            par ^= 1u;
        ph += inc;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph;
    v->ph[2] = (v->ph[2] & ~1u) | par;
    formant_nz = nst;
    v->s[0] = y1;
    v->s[1] = y2;
    v->s[2] = y3;
    v->s[3] = y4;
    v->s[4] = y5;
    v->s[5] = y6;
    v->s[6] = lp;
}

static const preset_t MEOW_PRESETS[] = {
    /* MODE SIZE LEN BEND | WOW BRTH ROUGH RAND */
    {"MEOW LEAD", {MEOW_M, 64, 84, 5, 30, 12, 10, 30}, {0, 64, 127, 40}, 0, 0, FX(0, 20, 30, 40), PAT(4)},
};

static const engine_t ENG_MEOW = {
    .name = "MEOW",
    .page_title = {"CALL", "TONE"},
    .edit = {
        {"MODE", F_ENUM, 0, MEOW_NMODE - 1, 0, N_MEOW_MODE, 0},
        {"SIZE", F_INT, 0, 127, 64, 0, 0},
        {"LEN", F_INT, 0, 127, 84, 0, 0},
        {"BEND", F_SEMI, 0, 12, 5, 0, 0},
        {"WOW", F_PCT, 0, 127, 30, 0, 0},
        {"BRTH", F_PCT, 0, 127, 12, 0, 0},
        {"ROUGH", F_PCT, 0, 127, 10, 0, 0},
        {"RAND", F_PCT, 0, 127, 30, 0, 0},
    },
    .presets = MEOW_PRESETS,
    .npresets = NELEM(MEOW_PRESETS),
    .note_on = meow_note_on,
    .render = meow_render,
    .amp = meow_amp,
    .knob = {P_E1, P_E2, P_E3, P_E6},
    .poly = 4,
};
