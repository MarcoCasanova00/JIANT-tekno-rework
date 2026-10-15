/* SPDX-License-Identifier: GPL-3.0-only */
/* SYRINX engine test (src/eng_syrinx.c) on the Mac, through hostsim.c as regress.c.
 *   build/host/syrinx_test [DEMODIR]          (run_tests.sh: build/syrinx_demo)
 * 1. in tune: the corners of PRESS x TENSE (whistle .. pulses), notes 36..96, soft and hard, and every preset's voice
 *    (no syllables, sweep, duet, breath): the pitch of the track's output (autocorrelation over many periods) within
 *    SY_CENTS of the key. The second half on its own (the sum less the first half's render): DUET 40's detune, the
 *    intervals of DUET 70, 100, 127.
 * 2. PRESS walks whistle -> pulses: at TENSE 40, the beak open, the harmonic centroid of the output rises with PRESS
 *    (<= 1.35 at 0, >= 2.4 at 127) and the waveform's crest with it; velocity is pressure (a soft note is the purer).
 * 3. no DC (every preset, four notes), no clipping at the loud corners (four voices at velocity 127): the track's sum
 *    below 1.2 x VOICE_FS a voice.
 * 4. determinism: a note from silence renders the same samples again; the voices' state stays bounded under
 *    random knob jumps (30 s, four voices).
 * 5. cost: host instructions per sample and voice (four notes held, the idle mix taken off): every preset and the
 *    heaviest corner; fails above SY_COST_MAX.
 * 6. demos into DEMODIR: every preset playing a phrase, a PRESS sweep (whistle -> growl), a TENSE sweep, a dawn
 *    chorus (four presets on four tracks). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define SY_CENTS 3.0               /* tuning tolerance */
#define SY_COST_MAX 260.0          /* host instructions per sample and voice */

static int bad;
static void check(const char *what, int ok)
{
    printf("syrinx: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* track 0 SYRINX with e[0..7], a flat envelope, no sends, POLY */
static void setup(const int16_t *e)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], ENGI_SYRINX, 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = e[i];
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 10;
    trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
}
static const int16_t *pe(uint32_t pi)
{
    static int16_t e[8];
    uint32_t i;
    for (i = 0; i < 8u; i++)
        e[i] = ENG_SYRINX.presets[pi].e[i];
    return e;
}
/* the track's sum (track_render) of nn notes from silence (two blocks of it: the part's trachea plays the last block's
 * sum in the next one, a silent block drops it), n samples */
static void render(const int16_t *e, const uint32_t *notes, uint32_t nn, uint32_t vel, double *y, uint32_t n)
{
    static int32_t b[CTL];
    uint32_t i, k;
    setup(e);
    track_render(&trk[0], b, CTL);
    track_render(&trk[0], b, CTL);
    for (k = 0; k < nn; k++)
        trk_note_on(&trk[0], notes[k], vel);
    for (i = 0; i < n; i += CTL) {
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k];
    }
}

/* the frequency of y[a..b) near f: the normalised autocorrelation's peak at the lag of m periods (m periods >= 600
 * samples), parabolic between lags; 0 if no peak above 0.5 */
static double pitch_of(const double *y, uint32_t a, uint32_t b, double f)
{
    double P = FS / f, best = -2, r[3];
    uint32_t m = (uint32_t)ceil(600 / P), L, L0 = 0, lo, hi, k;
    if (m < 1u)
        m = 1;
    lo = (uint32_t)floor(m * P * 0.97);
    hi = (uint32_t)ceil(m * P * 1.03);
    for (L = lo; L <= hi; L++) {
        double s = 0, e0 = 0, e1 = 0, c;
        for (k = a; k + L < b; k++) {
            s += y[k] * y[k + L];
            e0 += y[k] * y[k];
            e1 += y[k + L] * y[k + L];
        }
        c = s / sqrt(e0 * e1 + 1e-30);
        if (c > best) {
            best = c;
            L0 = L;
        }
    }
    if (best < 0.5 || L0 <= lo || L0 >= hi)
        return 0;
    for (k = 0; k < 3u; k++) {
        double s = 0, e0 = 0, e1 = 0;
        uint32_t LL = L0 - 1u + k, j;
        for (j = a; j + LL < b; j++) {
            s += y[j] * y[j + LL];
            e0 += y[j] * y[j];
            e1 += y[j + LL] * y[j + LL];
        }
        r[k] = s / sqrt(e0 * e1 + 1e-30);
    }
    {
        double d = r[0] - 2 * r[1] + r[2], off = d < 0 ? 0.5 * (r[0] - r[2]) / d : 0;
        return FS * m / (L0 + off);
    }
}
static double key_hz(uint32_t note) { return 440.0 * pow(2, ((double)note - 69) / 12); }

/* harmonics 1..30 of f over y[a..b) (Hann, Goertzel): their centroid (in harmonics); the crest factor */
static double centroid(const double *y, uint32_t a, uint32_t b, double f, double *crest)
{
    double e = 0, ek = 0, pk = 0, rms = 0;
    uint32_t k, j, n = b - a;
    for (j = a; j < b; j++) {
        rms += y[j] * y[j];
        if (fabs(y[j]) > pk)
            pk = fabs(y[j]);
    }
    *crest = pk / sqrt(rms / n + 1e-30);
    for (k = 1; k <= 30u && f * k < 18000; k++) {
        double w = 2 * M_PI * f * k / FS, c = 2 * cos(w), q0, q1 = 0, q2 = 0, h;
        for (j = 0; j < n; j++) {
            q0 = c * q1 - q2 + y[a + j] * (0.5 - 0.5 * cos(2 * M_PI * j / (n - 1)));
            q2 = q1;
            q1 = q0;
        }
        h = q1 * q1 + q2 * q2 - c * q1 * q2;
        e += h;
        ek += k * h;
    }
    return e > 0 ? ek / e : 0;
}

/* ------------------------------------------------------------ 1. tuning --- */
static void test_tuning(void)
{
    static double y[FS];
    static const int16_t PR[] = {0, 40, 80, 127}, TN[] = {0, 40, 80, 127};
    double worst = 0;
    uint32_t a, b, n, v, total = 0, off = 0;
    char msg[160];
    for (a = 0; a < 4u; a++)
        for (b = 0; b < 4u; b++)
            for (n = 36; n <= 96u; n += 4u)
                for (v = 0; v < 2u; v++) {
                    int16_t e[8] = {PR[a], TN[b], 0, 0, 64, 50, 0, 0};
                    uint32_t note = n;
                    double f, ct;
                    render(e, &note, 1, v ? 127 : 30, y, FS);
                    f = pitch_of(y, FS / 4, FS, key_hz(n));
                    ct = f > 0 ? 1200 * log2(f / key_hz(n)) : 1e9;
                    total++;
                    if (!(fabs(ct) <= SY_CENTS)) {
                        if (off++ < 6u)
                            printf("syrinx:   off: PRESS %d TENSE %d note %u vel %u: %.2f cents\n", PR[a], TN[b], n, v ? 127 : 30, ct);
                    }
                    if (fabs(ct) > worst)
                        worst = fabs(ct);
                }
    snprintf(msg, sizeof msg, "in tune: PRESS x TENSE corners, notes 36..96, soft / hard (%u / %u within %.0f cents, worst %.2f)",
             total - off, total, SY_CENTS, worst);
    check(msg, !off);
    worst = 0;
    off = total = 0;
    for (a = 0; a < ENG_SYRINX.npresets; a++)
        for (n = 48; n <= 96u; n += 6u) {
            int16_t e[8];
            uint32_t note = n;
            double f, ct;
            memcpy(e, pe(a), sizeof e);
            e[2] = e[3] = e[6] = e[7] = 0;
            render(e, &note, 1, 100, y, FS);
            f = pitch_of(y, FS / 4, FS, key_hz(n));
            ct = f > 0 ? 1200 * log2(f / key_hz(n)) : 1e9;
            total++;
            if (!(fabs(ct) <= SY_CENTS) && off++ < 6u)
                printf("syrinx:   off: preset %s note %u: %.2f cents\n", ENG_SYRINX.presets[a].name, n, ct);
            if (fabs(ct) > worst)
                worst = fabs(ct);
        }
    snprintf(msg, sizeof msg, "in tune: every preset's voice, notes 48..96 (%u / %u, worst %.2f cents)", total - off, total, worst);
    check(msg, !off);
    {   /* after a chirp (SWEEP -64 from above, +63 from below; ~60 ms) the key, exactly */
        double w = 0;
        int16_t sws[2] = {-64, 63};
        for (a = 0; a < 2u; a++) {
            int16_t e[8] = {40, 80, 0, 0, 64, 50, 0, 0};
            uint32_t note = 67;
            double f, ct;
            e[2] = sws[a];
            render(e, &note, 1, 100, y, FS);
            f = pitch_of(y, FS / 2, FS, key_hz(note));
            ct = f > 0 ? 1200 * log2(f / key_hz(note)) : 1e9;
            if (getenv("VERBOSE"))
                printf("syrinx:   SWEEP %d: %.2f cents, sw %d\n", sws[a], ct, sy_vx[0][0].sw);
            if (fabs(ct) > w)
                w = fabs(ct);
        }
        snprintf(msg, sizeof msg, "SWEEP: after a chirp down or up the voice lands on the key (worst %.2f cents)", w);
        check(msg, w <= 1.0);
    }
    {   /* half B on its own: A does not hear B and the filters are linear (no knee at PRESS 40), so B's 0.6 x = the
         * sum less A's alone x 0.6: DUET 40 (32.5 cents up), 70 (2 semitones), 100 (a fifth), 127 (an octave) */
        static double a1[FS];
        static const int16_t DU[4] = {40, 70, 100, 127};
        static const double UP[4] = {77 / 2.3674, 200, 700, 1200};   /* (DUET 40: 1 + 40 x 40 x 189 / 3969 = 77 / 4096) */
        int16_t e[8] = {40, 80, 0, 0, 64, 50, 0, 0};
        uint32_t note = 64, d, i;
        double w = 0;
        render(e, &note, 1, 100, a1, FS);
        for (d = 0; d < 4u; d++) {
            double f, ct, want = key_hz(64) * pow(2, UP[d] / 1200);
            e[6] = DU[d];
            render(e, &note, 1, 100, y, FS);
            for (i = 0; i < FS; i++)
                y[i] = y[i] - 0.6 * a1[i];
            f = pitch_of(y, FS / 4, FS, want);
            ct = f > 0 ? 1200 * log2(f / want) : 1e9;
            if (fabs(ct) > w)
                w = fabs(ct);
        }
        snprintf(msg, sizeof msg, "DUET: the second half alone, 32.5 cents / 2 / 7 / 12 semitones up (worst %.2f cents)", w);
        check(msg, w <= SY_CENTS);
    }
}

/* ------------------------------------------------------------ 2. the regimes --- */
static void test_regimes(void)
{
    static double y[FS];
    static const int16_t PR[] = {0, 32, 64, 96, 127};
    double c[5], cr[5];
    uint32_t i, note = 60, mono = 1;
    char msg[200];
    for (i = 0; i < 5u; i++) {
        int16_t e[8] = {PR[i], 40, 0, 0, 127, 0, 0, 0};
        render(e, &note, 1, 127, y, FS);
        c[i] = centroid(y, FS / 4, FS, key_hz(note), &cr[i]);
        if (i && c[i] < c[i - 1] - 0.05)
            mono = 0;
    }
    snprintf(msg, sizeof msg, "PRESS whistle -> pulses: centroid %.2f %.2f %.2f %.2f %.2f, crest %.1f .. %.1f",
             c[0], c[1], c[2], c[3], c[4], cr[0], cr[4]);
    check(msg, c[0] <= 1.35 && c[4] >= 2.4 && mono && cr[4] > cr[0] + 0.5);
    {
        int16_t e[8] = {110, 40, 0, 0, 127, 0, 0, 0};
        double s, h, k;
        render(e, &note, 1, 20, y, FS);
        s = centroid(y, FS / 4, FS, key_hz(note), &k);
        render(e, &note, 1, 127, y, FS);
        h = centroid(y, FS / 4, FS, key_hz(note), &k);
        snprintf(msg, sizeof msg, "velocity is pressure: centroid %.2f soft, %.2f hard", s, h);
        check(msg, h > s + 0.3);
    }
    {   /* TENSE 127 stays a whistle at any PRESS */
        int16_t e[8] = {127, 127, 0, 0, 127, 0, 0, 0};
        double k, cc;
        render(e, &note, 1, 127, y, FS);
        cc = centroid(y, FS / 4, FS, key_hz(note), &k);
        snprintf(msg, sizeof msg, "TENSE 127: a whistle even at PRESS 127 (centroid %.2f)", cc);
        check(msg, cc < 1.6);
    }
}

/* ------------------------------------------------------------ 3. DC, clipping --- */
static void test_dc_clip(void)
{
    static double y[FS];
    double worst = 0, peak = 0;
    uint32_t pi, k, i;
    char msg[160];
    for (pi = 0; pi < ENG_SYRINX.npresets; pi++) {
        static const uint32_t N[4] = {48, 60, 72, 84};
        for (k = 0; k < 4u; k++) {
            double m = 0;
            render(pe(pi), &N[k], 1, 100, y, FS);
            for (i = FS / 4; i < FS; i++)
                m += y[i];
            m = fabs(m / (FS - FS / 4)) / VOICE_FS;
            if (getenv("VERBOSE"))
                printf("syrinx:   DC %-10s note %u: %.3f %%\n", ENG_SYRINX.presets[pi].name, N[k], m * 100);
            if (m > worst)
                worst = m;
        }
    }
    snprintf(msg, sizeof msg, "no DC: every preset, 4 notes (worst |mean| %.3f %% of a voice)", worst * 100);
    check(msg, worst < 0.01);
    {
        static const int16_t TN[] = {0, 64}, TR[] = {0, 64, 127}, DU[] = {0, 100};
        uint32_t a, b, c, d;
        static const uint32_t CH[4] = {60, 64, 67, 72};
        for (a = 0; a < 2u; a++)
            for (b = 0; b < 3u; b++)
                for (c = 0; c < 2u; c++)
                    for (d = 0; d < 2u; d++) {
                        int16_t e[8] = {127, TN[a], 0, 0, 127, TR[b], DU[c], d ? 127 : 0};
                        render(e, CH, 4, 127, y, FS);
                        for (i = 0; i < FS; i++)
                            if (fabs(y[i]) > peak)
                                peak = fabs(y[i]);
                    }
    }
    snprintf(msg, sizeof msg, "no clipping: loud corners, 4 voices at velocity 127 (peak %.2f x VOICE_FS a voice)", peak / VOICE_FS / 4);
    check(msg, peak < 1.2 * VOICE_FS * 4);
}

/* ------------------------------------------------------------ 4. determinism, bounded --- */
static void test_determinism(void)
{
    static double y1[FS / 2], y2[FS / 2];
    int16_t e[8] = {90, 50, -30, 40, 70, 60, 20, 60};
    uint32_t note = 72, i, same = 1;
    render(e, &note, 1, 100, y1, FS / 2);
    render(e, &note, 1, 100, y2, FS / 2);
    for (i = 0; i < FS / 2; i++)
        same &= y1[i] == y2[i];
    check("determinism: a note from silence renders the same samples again", same);
    {
        static int32_t b[CTL];
        uint32_t r = 4242, blk, k, ok = 1, stuck = 0;
        double peak = 0;
        setup(e);
        for (blk = 0; blk < 30u * FS / CTL && ok; blk++) {
            if (blk % 64u == 0u) {
                for (k = 0; k < 8u; k++) {
                    r = r * 1664525u + 1013904223u;
                    if ((r >> 28) < 6u)
                        trk[0].p[P_E0 + k] = (int16_t)(k == 2u ? (int32_t)((r >> 8) % 128u) - 64 : (int32_t)((r >> 8) % 128u));
                }
                r = r * 1664525u + 1013904223u;
                trk_note_off(&trk[0], 36u + (r >> 8) % 72u);
                r = r * 1664525u + 1013904223u;
                trk_note_on(&trk[0], 24u + (r >> 8) % 90u, 1u + (r >> 16) % 127u);
            }
            track_render(&trk[0], b, CTL);
            for (k = 0; k < CTL; k++)
                if (abs(b[k]) > peak)
                    peak = abs(b[k]);
            for (k = 0; k < SY_POLY; k++) {
                const voice_t *v = &trk[0].v[k];
                if (v->active && (v->s[0] >= 805306368 || v->s[0] <= -805306368))   /* (at the clamp, |x| 1.5) */
                    stuck++;
            }
        }
        {
            char msg[160];
            snprintf(msg, sizeof msg, "bounded: 30 s of random knob jumps and notes (peak %.2f x VOICE_FS, %u blocks at the clamp)",
                     peak / VOICE_FS, stuck);
            check(msg, ok && peak < 6 * VOICE_FS && stuck < 10u);
        }
    }
}

/* ------------------------------------------------------------ 5. cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) == 0)
        return ri.ri_instructions;
#endif
    return 0;
}
/* instructions per sample of e with notes held (after 0.2 s), less the idle part (no note) */
static double cost(const int16_t *e, const uint32_t *notes, uint32_t nn)
{
    static int32_t b[CTL];
    uint32_t i, k;
    uint64_t t0, t1;
    double idle;
    setup(e);
    t0 = instr_now();
    for (i = 0; i < FS; i += CTL)
        track_render(&trk[0], b, CTL);
    t1 = instr_now();
    idle = (double)(t1 - t0) / FS;
    setup(e);
    for (k = 0; k < nn; k++)
        trk_note_on(&trk[0], notes[k], 100);
    for (i = 0; i < FS / 5; i += CTL)
        track_render(&trk[0], b, CTL);
    t0 = instr_now();
    for (i = 0; i < FS; i += CTL)
        track_render(&trk[0], b, CTL);
    t1 = instr_now();
    return ((double)(t1 - t0) / FS - idle) / nn;
}
static void test_cost(void)
{
    static const uint32_t CH[4] = {60, 64, 67, 72}, HI[4] = {79, 84, 88, 91};
    double worst = 0, c;
    uint32_t pi, wp = 0;
    char msg[200];
    if (!instr_now()) {
        printf("syrinx: cost: no instruction counter on this host (proc_pid_rusage); not checked\n");
        return;
    }
    for (pi = 0; pi < ENG_SYRINX.npresets; pi++) {
        c = cost(pe(pi), CH, 4);
        if (c > worst) {
            worst = c;
            wp = pi;
        }
    }
    {
        int16_t e[8] = {127, 64, 0, 64, 127, 64, 100, 127};
        double hc = cost(e, HI, 4), cc = cost(e, CH, 4);
        snprintf(msg, sizeof msg, "cost a voice: presets <= %.0f (%s), heaviest corner %.0f (C4 chord) / %.0f (G5 chord), limit %.0f",
                 worst, ENG_SYRINX.presets[wp].name, cc, hc, SY_COST_MAX);
        check(msg, worst <= SY_COST_MAX && cc <= SY_COST_MAX && hc <= 1.6 * SY_COST_MAX);
    }
}

/* ------------------------------------------------------------ 6. demos --- */
static FILE *wav_open(const char *dir, const char *name, uint32_t frames)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    f = fopen(path, "wb");
    if (f)
        wav_hdr(f, frames);
    return f;
}
/* a phrase of ten notes (a seeded walk in the preset's register), the mix as the device plays it */
static void demo_preset(const char *dir, uint32_t pi)
{
    static int32_t o[2 * CTL];
    uint32_t frames = (uint32_t)(FS * 6), i, k, r = 777 + pi * 31u, t_next = FS / 10, held = 0, note = 0;
    char name[64], *c;
    FILE *f;
    int32_t lo = 60, hi = 84;
    snprintf(name, sizeof name, "%02u_%s", pi, ENG_SYRINX.presets[pi].name);
    for (c = name; *c; c++)
        if (*c == ' ')
            *c = '_';
    f = wav_open(dir, name, frames);
    if (!f)
        return;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], ENGI_SYRINX, pi);
    if (strstr(ENG_SYRINX.presets[pi].name, "CROW") || strstr(ENG_SYRINX.presets[pi].name, "CRANE") ||
        strstr(ENG_SYRINX.presets[pi].name, "DOVE") || strstr(ENG_SYRINX.presets[pi].name, "OWL"))
        lo = 45, hi = 62;
    for (i = 0; i < frames; i += CTL) {
        if (i >= t_next && i < frames - FS) {
            if (held)
                trk_note_off(&trk[0], note);
            r = r * 1664525u + 1013904223u;
            note = (uint32_t)(lo + (int32_t)((r >> 8) % (uint32_t)(hi - lo + 1)));
            trk_note_on(&trk[0], note, 60u + (r >> 20) % 68u);
            held = 1;
            t_next = i + FS / 6 + (r >> 12) % (FS / 3);
        } else if (held && i >= t_next - FS / 12) {
            trk_note_off(&trk[0], note);
            held = 0;
        }
        mix_block(o, CTL);
        for (k = 0; k < CTL; k++)
            wav_put(f, o[2 * k], o[2 * k + 1]);
    }
    fclose(f);
}
/* one note, a knob swept 0 -> 127 over 8 s */
static void demo_sweep(const char *dir, const char *name, const int16_t *e, uint32_t knob, uint32_t note)
{
    static int32_t o[2 * CTL];
    uint32_t frames = (uint32_t)(FS * 9), i, k;
    FILE *f = wav_open(dir, name, frames);
    if (!f)
        return;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], ENGI_SYRINX, 0);
    for (k = 0; k < 8u; k++)
        trk[0].p[P_E0 + k] = e[k];
    trk[0].p[P_ATK] = 10;
    trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], note, 110);
    for (i = 0; i < frames; i += CTL) {
        int32_t v = (int32_t)((double)i / (FS * 8) * 127);
        trk[0].p[P_E0 + knob] = (int16_t)(knob == 1u ? 127 - (v > 127 ? 127 : v) : (v > 127 ? 127 : v));
        if (i == FS * 8)
            trk_note_off(&trk[0], note);
        mix_block(o, CTL);
        for (k = 0; k < CTL; k++)
            wav_put(f, o[2 * k], o[2 * k + 1]);
    }
    fclose(f);
}
static void demos(const char *dir)
{
    uint32_t pi;
    for (pi = 0; pi < ENG_SYRINX.npresets; pi++)
        demo_preset(dir, pi);
    {
        int16_t e[8] = {0, 40, 0, 0, 80, 60, 0, 6};
        demo_sweep(dir, "sweep_press", e, 0, 76);
        e[0] = 100;
        demo_sweep(dir, "sweep_tense", e, 1, 76);
    }
    printf("syrinx: demos in %s\n", dir);
}

int main(int argc, char **argv)
{
    test_tuning();
    test_regimes();
    test_dc_clip();
    test_determinism();
    test_cost();
    if (argc > 1)
        demos(argv[1]);
    printf("syrinx: %s\n", bad ? "FAILED" : "all ok");
    return bad ? 1 : 0;
}
