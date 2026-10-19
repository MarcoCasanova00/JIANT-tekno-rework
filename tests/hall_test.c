/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
 * The reference reverb below (sloop_fdn) is SLOOP's (https://github.com/isod89/sloop-fm1, firmware/src/fx.c, 2.7.1,
 * GPL-3.0-only; Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hugelton Instruments, and SLOOP's authors), carried
 * here unchanged in its arithmetic as a yardstick only. */
/* HALL (src/fx.c rev_hall, the 0.9.14 proposal) against ROOM and against SLOOP's FDN, on the same sends.
 *   build/host/hall_test [WAVDIR]
 * Impulse responses: RT60 (Schroeder, -5 .. -25 dB, x3), the echo density (the time the response's normalised echo
 * density, Abel & Huang, first reaches 0.9: how soon the tail is a smooth wash), the tail's left / right correlation
 * (0: as wide as two rooms; 1: mono), its spectral centroid; level against ROOM. Checks: HALL's RT60 rises with SIZE,
 * it is bounded under loud noise and dies after it, wider and denser than ROOM. Demos: a pluck pattern through each. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("hall: %-98s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---------------------------------------------------------------- SLOOP 2.7.1's reverb (reference) --- */
#define SREV_MOD 12
static const uint16_t SREV_LINE[4] = {1559, 1931, 2389, 2791};
static const uint16_t SREV_AP[2] = {556, 441};
static int16_t srev_line[1559 + 1931 + 2389 + 2791 + SREV_MOD + 2], srev_ap[556 + 441];
static struct { uint32_t rev_ph; uint16_t line_i[4], ap_i[2]; int32_t line_lp[4]; } sfx;
static void sloop_fdn(const int32_t *rev_in, int32_t *ol, int32_t *or_, uint32_t n, int32_t rsize, int32_t rdamp)
{
    uint32_t i, k;
    int32_t g = 17000 + rsize * 104, lpk = 32767 - rdamp * 200, ma, mb;
    const uint32_t L0 = SREV_LINE[0] + SREV_MOD + 2u, B1 = L0, B2 = B1 + SREV_LINE[1], B3 = B2 + SREV_LINE[2];
    {
        int32_t m0 = osc_sine(sfx.rev_ph), m1;
        sfx.rev_ph += LFO_INC[30];
        m1 = osc_sine(sfx.rev_ph);
        ma = (SREV_MOD << 8) + ((m0 * SREV_MOD) >> 7), mb = (SREV_MOD << 8) + ((m1 * SREV_MOD) >> 7);
    }
    for (i = 0; i < n; i++) {
        int32_t a = mulq15(rev_in[i], 13000), o0, o1, o2, o3;
        int16_t *c = srev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t b = c[sfx.ap_i[k]], v = a + (b >> 1);
            c[sfx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = b - (v >> 1);
            if (++sfx.ap_i[k] >= SREV_AP[k]) sfx.ap_i[k] = 0;
            c += SREV_AP[k];
        }
        c = srev_line;
        {
            int32_t s0, s1, d0, d1, r = ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2);
            uint32_t ri = sfx.line_i[0] + ((uint32_t)r >> 8), rj;
            if (ri >= L0) ri -= L0;
            rj = ri + 1u >= L0 ? 0u : ri + 1u;
            o0 = c[ri] + (((c[rj] - c[ri]) * (r & 255)) >> 8);
            o1 = c[B1 + sfx.line_i[1]];
            o2 = c[B2 + sfx.line_i[2]];
            o3 = c[B3 + sfx.line_i[3]];
            s0 = o0 + o1, d0 = o0 - o1, s1 = o2 + o3, d1 = o2 - o3;
            sfx.line_lp[0] += mulq15(((s0 + s1) >> 1) - sfx.line_lp[0], lpk);
            sfx.line_lp[1] += mulq15(((d0 + d1) >> 1) - sfx.line_lp[1], lpk);
            sfx.line_lp[2] += mulq15(((s0 - s1) >> 1) - sfx.line_lp[2], lpk);
            sfx.line_lp[3] += mulq15(((d0 - d1) >> 1) - sfx.line_lp[3], lpk);
            c[sfx.line_i[0]] = (int16_t)clamp(mulq15(sfx.line_lp[0], g) + a, -32768, 32767);
            c[B1 + sfx.line_i[1]] = (int16_t)clamp(mulq15(sfx.line_lp[1], g) - a, -32768, 32767);
            c[B2 + sfx.line_i[2]] = (int16_t)clamp(mulq15(sfx.line_lp[2], g) + a, -32768, 32767);
            c[B3 + sfx.line_i[3]] = (int16_t)clamp(mulq15(sfx.line_lp[3], g) - a, -32768, 32767);
            if (++sfx.line_i[0] >= L0) sfx.line_i[0] = 0;
            if (++sfx.line_i[1] >= SREV_LINE[1]) sfx.line_i[1] = 0;
            if (++sfx.line_i[2] >= SREV_LINE[2]) sfx.line_i[2] = 0;
            if (++sfx.line_i[3] >= SREV_LINE[3]) sfx.line_i[3] = 0;
        }
        ol[i] = o0 + o2;
        or_[i] = o1 - o3;
    }
}

/* ---------------------------------------------------------------- rendering --- */
#define NMAX (14u * FS / CTL * CTL)
static int32_t xl[NMAX], xr[NMAX];
enum { M_ROOM, M_HALL, M_SLOOP, M_N };
static const char *const MNAME[M_N] = {"JIANT ROOM", "JIANT HALL (proposal)", "SLOOP 2.7.1 FDN"};
static void model_reset(uint32_t m, int sz, int dmp, int mod, int wide)
{
    host_tracks_init();
    rev_clear();
    memset(fx.comb_i, 0, sizeof fx.comb_i); memset(fx.ap_i, 0, sizeof fx.ap_i); memset(fx.rm_m, 0, sizeof fx.rm_m);
    fx.rm_ph = 0;
    memset(&sfx, 0, sizeof sfx); memset(srev_line, 0, sizeof srev_line); memset(srev_ap, 0, sizeof srev_ap);
    song.g[G_RTYPE] = m == M_HALL ? 5 : 0;
    fx.rtype = (uint8_t)song.g[G_RTYPE];
    song.g[G_RSIZE] = (int16_t)sz; song.g[G_RDAMP] = (int16_t)dmp; song.g[G_RMOD] = (int16_t)mod;
    song.g[G_RWIDE] = (int16_t)wide; song.g[G_RPRE] = 0; song.g[G_RFILT] = 0; song.g[G_RRATE] = 30;
    fxg_g = 32767; fxg_quiet = 0;
}
/* the model's response to x(t) (n samples): left, right */
static void model_run(uint32_t m, int32_t (*x)(uint32_t), uint32_t n)
{
    static int32_t in[CTL], w[CTL], l[CTL], r[CTL];
    uint32_t t, i;
    for (t = 0; t + CTL <= n; t += CTL) {
        for (i = 0; i < CTL; i++) {
            in[i] = x(t + i);
            w[i] = 0;
            fx_sl[i] = fx_sr[i] = 0;
        }
        if (m == M_SLOOP) {
            sloop_fdn(in, l, r, CTL, song.g[G_RSIZE], song.g[G_RDAMP]);
            for (i = 0; i < CTL; i++) { xl[t + i] = l[i]; xr[t + i] = r[i]; }
        } else {
            rev_run(fx.rtype, in, w, CTL);
            for (i = 0; i < CTL; i++) { xl[t + i] = w[i] + fx_sl[i]; xr[t + i] = w[i] + fx_sr[i]; }
        }
    }
}
static int32_t x_imp(uint32_t t) { return t == 0 ? 60000 : 0; }
static uint32_t xs = 0x2468ACEu;
static int32_t noise(int32_t amp)
{
    xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5;
    return (int32_t)(((int64_t)(int32_t)xs * amp) >> 31);
}
static int32_t x_burst(uint32_t t) { return t < FS / 50u ? noise(40000) : 0; }
static int32_t x_loud(uint32_t t) { return t < 2u * FS ? noise(200000) : 0; }
/* the demo: plucked saw notes, an arpeggio of a minor 9th chord, 8th notes at 100 BPM, 6 s, then the tail */
static double pl_ph[4], pl_env[4];
static int32_t x_pluck(uint32_t t)
{
    static const double F[8] = {110.0, 164.81, 220.0, 261.63, 329.63, 246.94, 196.0, 130.81};
    uint32_t step = (uint32_t)(FS * 60.0 / 100.0 / 2.0), k = (t / step) % 8u, v = (t / step) % 4u;
    double s = 0;
    uint32_t j;
    if (t % step == 0 && t < 6u * FS) { pl_env[v] = 1.0; pl_ph[v] = 0; }
    for (j = 0; j < 4u; j++) {
        double f = F[(k + 8u - ((v + 4u - j) % 4u)) % 8u];
        pl_ph[j] += f / FS; if (pl_ph[j] >= 1.0) pl_ph[j] -= 1.0;
        s += (2.0 * pl_ph[j] - 1.0) * pl_env[j];
        pl_env[j] *= 0.99985;
    }
    return (int32_t)(s * 9000.0);
}
/* RT60 from the envelope's slope (10 ms windows, DC out, left + right): a line fitted from where it first falls 5 dB under
 * its peak to where it first falls 25 dB under (x3): the integer reverbs' rounding floor never enters it */
static double rt60_of(const int32_t *l, const int32_t *r, uint32_t n)
{
    static double env[NMAX / 441u + 1u];
    uint32_t w = FS / 100u, nw = n / w, i, j, pk = 0, a = 0, b = 0;
    double hl = 0, hr = 0, sx = 0, sy = 0, sxx = 0, sxy = 0, c;
    for (i = 0; i < nw; i++) {
        double e = 0;
        for (j = 0; j < w; j++) {
            double u = l[i * w + j], v = r[i * w + j];
            hl += (u - hl) / 2048.0; hr += (v - hr) / 2048.0;
            e += (u - hl) * (u - hl) + (v - hr) * (v - hr);
        }
        env[i] = 10.0 * log10(e / w + 1e-9);
        if (env[i] > env[pk]) pk = i;
    }
    for (i = pk; i < nw && !b; i++) {
        if (!a && env[i] <= env[pk] - 5.0) a = i;
        if (a && env[i] <= env[pk] - 25.0) b = i;
    }
    if (!a || !b || b <= a + 2u) return -1;
    for (i = a; i <= b; i++) { double x = (double)i * w / FS; sx += x; sy += env[i]; sxx += x * x; sxy += x * env[i]; }
    c = (double)(b - a + 1u);
    return -60.0 / ((c * sxy - sx * sy) / (c * sxx - sx * sx));
}
/* the normalised echo density (Abel & Huang 2006): 20 ms windows, the share of samples beyond one standard deviation
 * over erfc(1 / sqrt 2); the first time it reaches 0.9 */
static double ned_time(const int32_t *l, uint32_t n)
{
    uint32_t w = FS / 50u, i, j;
    for (i = 0; i + w < n; i += FS / 1000u) {
        double s = 0, c = 0;
        for (j = 0; j < w; j++) s += (double)l[i + j] * l[i + j];
        s = sqrt(s / w);
        if (s < 1.0) continue;
        for (j = 0; j < w; j++) c += fabs((double)l[i + j]) > s;
        if (c / w / 0.3173 >= 0.9) return (double)(i + w / 2u) / FS;
    }
    return -1;
}
static double corr(const int32_t *a, const int32_t *b, uint32_t i0, uint32_t i1)
{
    double sab = 0, saa = 0, sbb = 0;
    uint32_t i;
    for (i = i0; i < i1; i++) { sab += (double)a[i] * b[i]; saa += (double)a[i] * a[i]; sbb += (double)b[i] * b[i]; }
    return sab / sqrt(saa * sbb + 1e-30);
}
static double centroid(const int32_t *a, uint32_t i0, uint32_t i1)   /* by zero-crossing rate's proxy: a DFT */
{
    /* the energy of the first difference over the energy: 2 - 2 cos(w) weighted; returned as Hz */
    double e = 0, d = 0;
    uint32_t i;
    for (i = i0 + 1u; i < i1; i++) { double q = a[i] - a[i - 1]; d += q * q; e += (double)a[i] * a[i]; }
    return FS / (2.0 * M_PI) * acos(1.0 - d / (2.0 * e + 1e-30));
}
static double energy(const int32_t *a, uint32_t i0, uint32_t i1)
{
    double e = 0;
    uint32_t i;
    for (i = i0; i < i1; i++) e += (double)a[i] * a[i];
    return e;
}
static void wav(const char *path, int32_t (*x)(uint32_t), uint32_t n, double dry, double scale)
{
    FILE *f = fopen(path, "wb");
    uint32_t i, v;
    if (!f) return;
    fwrite("RIFF", 1, 4, f); v = 36 + n * 4; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    { uint16_t h[2] = {1, 2}; fwrite(h, 2, 2, f); v = FS; fwrite(&v, 4, 1, f); v = FS * 4; fwrite(&v, 4, 1, f); h[0] = 4; h[1] = 16; fwrite(h, 2, 2, f); }
    fwrite("data", 1, 4, f); v = n * 4; fwrite(&v, 4, 1, f);
    memset(pl_ph, 0, sizeof pl_ph); memset(pl_env, 0, sizeof pl_env);
    for (i = 0; i < n; i++) {
        double d = x(i) * dry, a = (xl[i] * scale + d) / 2.0, b = (xr[i] * scale + d) / 2.0;
        int16_t s[2] = {(int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a), (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b)};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : 0;
    uint32_t m, n = 12u * FS / CTL * CTL;
    double rt[M_N], nd[M_N], cr[M_N], ce[M_N], lv[M_N];
    printf("hall: impulse responses at SIZE 90 DAMP 60 MOD 0 WIDE 0 (both firmwares' defaults)\n");
    for (m = 0; m < M_N; m++) {
        model_reset(m, 90, 60, 0, 0);
        model_run(m, x_imp, n);
        nd[m] = ned_time(xl, n);
        model_reset(m, 90, 60, 0, 0);
        model_run(m, x_burst, n);
        rt[m] = rt60_of(xl, xr, n);
        cr[m] = corr(xl, xr, FS / 10u, FS);
        ce[m] = centroid(xl, FS / 2u, FS);
        model_reset(m, 90, 60, 0, 0);
        model_run(m, x_burst, n);
        lv[m] = energy(xl, 0, 2u * FS) + energy(xr, 0, 2u * FS);
        printf("hall:   %-22s RT60 %5.2f s, dense (NED 0.9) at %4.0f ms, tail L/R corr %5.2f, tail centroid %5.0f Hz, level %+5.1f dB\n",
               MNAME[m], rt[m], nd[m] * 1000, cr[m], ce[m], 10 * log10(lv[m] / lv[0]));
    }
    {
        double r0, r90, r127;
        model_reset(M_HALL, 0, 60, 0, 0); model_run(M_HALL, x_burst, n); r0 = rt60_of(xl, xr, n);
        model_reset(M_HALL, 90, 30, 0, 0); model_run(M_HALL, x_burst, n); r90 = rt60_of(xl, xr, n);
        model_reset(M_HALL, 127, 30, 0, 0); model_run(M_HALL, x_burst, NMAX); r127 = rt60_of(xl, xr, NMAX);
        printf("hall:   HALL RT60 (DAMP 30): SIZE 0 %.2f s, 90 %.2f s, 127 %.2f s\n", r0, r90, r127);
        check("HALL: RT60 rises with SIZE: 0 under 0.6 s, 90 within 1.5 .. 5 s, 127 over 5 s", r0 > 0 && r0 < 0.6 && r90 > 1.5 && r90 < 5 && r127 > 5);
    }
    check("HALL: wider than ROOM (tail L/R correlation under 0.3 at WIDE 0)", cr[M_HALL] < 0.3 && cr[M_HALL] < cr[M_ROOM]);
    check("HALL: dense sooner than ROOM (never, -1) and SLOOP (NED 0.9)", nd[M_HALL] > 0 && (nd[M_ROOM] < 0 || nd[M_HALL] <= nd[M_ROOM]) && nd[M_HALL] <= nd[M_SLOOP]);
    check("HALL: level within 4 dB of ROOM (a noise burst, 2 s)", fabs(10 * log10(lv[M_HALL] / lv[M_ROOM])) < 4.0);
    {   /* bounded under 2 s of loud noise at SIZE 127, MOD 127, then dies */
        int32_t pk = 0;
        uint32_t i;
        double e0, e1;
        model_reset(M_HALL, 127, 0, 127, 127);
        model_run(M_HALL, x_loud, NMAX);
        for (i = 0; i < NMAX; i++) { int32_t a = abs(xl[i]); if (a > pk) pk = a; }
        e0 = energy(xl, 2u * FS, 3u * FS);
        e1 = energy(xl, 13u * FS, 14u * FS);
        printf("hall:   HALL SIZE 127 DAMP 0 MOD 127 WIDE 127 under a 200000 noise: peak %d, 12 s on %.1f dB\n", pk, 10 * log10((e1 + 1) / e0));
        check("HALL: bounded under 2 s of loud noise (SIZE 127, MOD 127), its tail dying", pk < (1 << 20) && e1 < e0 * 0.1);
    }
    {   /* cost on this host (a ratio only: the FM-1's is measured by tests/cpu_vg.py and target_budget) */
        static const int MM[3] = {M_ROOM, M_HALL, M_SLOOP};
        double ns[3];
        uint32_t q, rep;
        for (q = 0; q < 3u; q++) {
            struct timespec t0, t1;
            model_reset((uint32_t)MM[q], 90, 60, 20, 0);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (rep = 0; rep < 5u; rep++) model_run((uint32_t)MM[q], x_burst, NMAX);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ns[q] = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (5.0 * NMAX);
        }
        printf("hall:   cost on this host, ns a sample (with the harness): ROOM (MOD 20) %.1f, HALL %.1f, SLOOP FDN %.1f\n", ns[0], ns[1], ns[2]);
    }
    if (dir) {
        char p[512];
        for (m = 0; m < M_N; m++) {
            static const char *const F[M_N] = {"room", "hall", "sloopfdn"};
            model_reset(m, 90, 60, m == M_HALL ? 20 : 0, 0);
            memset(pl_ph, 0, sizeof pl_ph); memset(pl_env, 0, sizeof pl_env);
            model_run(m, x_pluck, n);
            snprintf(p, sizeof p, "%s/reverb_%s.wav", dir, F[m]);
            wav(p, x_pluck, n, 1.0, 1.0);
            printf("hall: demo %s\n", p);
        }
    }
    printf(bad ? "hall test FAILED\n" : "hall test passed\n");
    return bad != 0;
}
