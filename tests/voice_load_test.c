/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* (0.9.19) the shared voice budget under real songs (voice.c voice_cap / voice_alloc / voice_victim, the load governor
 * gov_tick, the overload guard shed_voice), on the host build of the UI tests: each scene played for seconds with both
 * cores (CORES 2, TRACKS 6), counting what every track asked for and what it got:
 *   req     the voices asked for (a POLY note, each voice of a MONO / UNISON note)
 *   cut     voices ended before their time that could be heard: a held note taken, or a release / a hit still above
 *           -40 dB of full scale (VS_STEAL, VS_OWN, VS_POLY, VS_SHED); quiet: the same under -40 dB (not heard)
 *   trim    tails under -50 dB the load budget ended; deny: extra UNISON voices left out
 *   busy    the voices sounding (not fading), mean and the most at once; cap: the budget (voices; 0.9.19: units)
 *   gov     the halves the governor's cap was under the cores' (and why it went down: over the budget 3 halves, past
 *           GOV_HOT, the guard), the load modelled
 * The load: no clock on the host, so each half's is modelled from what sounds in it, with gen.c's fitted costs (GC_ENG a
 * part with voices, GC_VOX a voice, the base and GC_REV; thousandths of the CPU TEST song, 63 % of a half on two cores)
 * times the device's factor (gen.c GC_DEV_X100, x1.6) and a +-4 % jitter (the sequencer's, the UI's interrupts), fed to
 * the governor and the guard as audio.c's shed_check does (65 % tight / over, 75 % hot, 80 % two halves: a shed).
 *   build/host/voice_load_test [TABLE]   prints the table (and checks: see vl_checks); TABLE=1 the table only */
#define VSTAT(ev, t, v) vs_ev((ev), (const void *)(t), (const void *)(v))
static void vs_ev(unsigned ev, const void *t, const void *v);
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#ifndef GOV_OVER_PCT                                    /* (0.9.18's lines: the load budget's 65 %, 75 %) */
#define GOV_OVER_PCT 65u
#define GOV_OVER_PCT_1C 55u
#define GOV_HOT_PCT 75u
#define GOV_HOT_PCT_1C 70u
#endif
#define VL_SECS 24u
#define VL_AUD 328                                      /* -40 dB of full scale, Q15: a cut under it is not heard */
#define VL_DEV 160u                                     /* gen.c GC_DEV_X100 */
typedef struct {
    uint32_t req[NTRK], held[NTRK], cut[NTRK], quiet[NTRK], trim[NTRK], deny[NTRK], own[NTRK], pcut[NTRK];
    uint64_t busy_sum[NTRK];
    uint32_t busy_max[NTRK];
    uint64_t tot_sum, cap_sum;
    uint32_t late, tot_max, blocks, halves, gov_low, gov_over, gov_hot, gov_shed, tight, cap_min, sheds;
    uint64_t load_sum;
    uint32_t load_max;
} vl_t;
static vl_t vl;
static int vl_on;

static uint32_t vl_trk_of(const void *v)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if ((const char *)v >= (const char *)trk[k].v && (const char *)v < (const char *)(trk[k].v + NVOICE))
            return k;
    return 0;
}
static void vs_ev(unsigned ev, const void *tp, const void *vp)
{
    const voice_t *v = vp;
    uint32_t k = tp ? (uint32_t)((const track_t *)tp - trk) % NTRK : vl_trk_of(vp);
    int loud;
    if (!vl_on)
        return;
    if (ev == VS_REQ) { vl.req[k]++; return; }
    if (ev == VS_DENY) { vl.deny[k]++; return; }
    if (!v || !v->active || v->stage == 4u)
        return;
    k = vl_trk_of(vp);
    if (ev == VS_TRIM) { vl.trim[k]++; return; }
    loud = v->gate || voice_level(&trk[k], v) >= VL_AUD;
    if (ev == VS_OWN) vl.own[k]++;
    if (ev == VS_POLY) { vl.pcut[k] += loud; return; }  /* (the engine's own voices all busy: not the budget) */
    if (v->gate) vl.held[k]++;                          /* (a note cut while it is held) */
    else if (loud) vl.cut[k]++;                         /* (a release or a hit still heard) */
    else vl.quiet[k]++;
}

/* --------------------------------------------------------------------------------- the load, modelled --- */
static uint32_t vl_rng = 1u;
static uint32_t vl_units;                               /* this half's so far (thousandths of CPU TEST) */
static void vl_block_units(void)
{
    uint32_t k, i;
    for (k = 0; k < NTRK && k < trk_live; k++) {
        const track_t *t = &trk[k];
        uint32_t e = t->engine % NENGINES, n = 0;
        for (i = 0; i < NVOICE; i++)
            n += t->v[i].active;
        if (n)
            vl_units += GC_ENG[e] + GC_VOX[e] * n;
    }
    vl_units += (mc_two() ? 295u : 380u) + GC_REV[(uint32_t)song.g[G_RTYPE] % 5u];
}
static uint8_t vl_tcalm, vl_sover, vl_shed;
static void vl_half(void)
{
    uint32_t nb = HALF_FRAMES / CTL, u = vl_units / nb, pct, c0 = gov_cap;
    int32_t j;
    vl_units = 0;
    vl_rng = vl_rng * 1664525u + 1013904223u;
    j = (int32_t)(vl_rng >> 24) % 9 - 4;                /* -4 .. +4 % */
    pct = u * 63u * VL_DEV / 100000u;
    pct = (uint32_t)clamp((int32_t)pct * (100 + j) / 100, 0, 200);
    if (vl_shed) { vl_shed = 0; shed_voice(); vl.sheds++; }   /* (the ISR: at the next half's start) */
    if (pct > (mc_two() ? 65u : 55u)) { load_tight = 1; vl_tcalm = 0; }   /* (audio.c TIGHT_ON, TIGHT_OFF) */
    else if (load_tight && pct < (mc_two() ? 60u : 50u) && ++vl_tcalm >= 16u) load_tight = 0;
    vl_sover = (uint8_t)(vl_sover << 1 | (pct > 80u));
    if ((vl_sover & 3u) == 3u) vl_shed = 1;
    if (pct > 100u) vl.late++;
    gov_tick(pct > (mc_two() ? GOV_OVER_PCT : GOV_OVER_PCT_1C), pct > (mc_two() ? GOV_HOT_PCT : GOV_HOT_PCT_1C),
             pct < (mc_two() ? 60u : 50u), vl_shed);
    if (!vl_on)
        return;
    vl.halves++;
    vl.load_sum += pct;
    if (pct > vl.load_max) vl.load_max = pct;
    vl.tight += load_tight;
    if (gov_cap < c0) {
        if (vl_shed) vl.gov_shed++;
        else if (pct > (mc_two() ? GOV_HOT_PCT : GOV_HOT_PCT_1C)) vl.gov_hot++;
        else vl.gov_over++;
    }
    vl.gov_low += voice_cap() < (mc_two() ? VOICES_2CORE : NVOICE);
}

static void vl_sample(void)
{
    uint32_t k, i, tot = 0;
    for (k = 0; k < NTRK; k++) {
        uint32_t n = 0;
        for (i = 0; i < NVOICE; i++)
            n += trk[k].v[i].active && trk[k].v[i].stage != 4u;
        vl.busy_sum[k] += n;
        if (n > vl.busy_max[k]) vl.busy_max[k] = n;
        tot += n;
    }
    vl.tot_sum += tot;
    if (tot > vl.tot_max) vl.tot_max = tot;
    vl.cap_sum += voice_cap();
    if (voice_cap() < vl.cap_min) vl.cap_min = voice_cap();
    vl.blocks++;
}

/* ------------------------------------------------------------------------------------------- the scenes --- */
static void vl_fresh(void)
{
    uint32_t k;
    ui_power_on();
    load_depth++;
    power_on_parts();                                    /* (today's power-on: T1 DRUM-X .. T6 FM6, ui_power_on has 0.9.8's) */
    load_depth--;
    for (k = 0; k < NTRK; k++) memset(fg_set[k], 0, sizeof fg_set[k]);
    ui_rec_prefs |= PREF_SIX >> 8;
    ui_load = 0;
    rp_apply();                                          /* TRACKS 6, LOAD FULL */
    gov_cap = 16u; gov_lite = 0; gov_hot = gov_calm = gov_win = gov_lcalm = 0; gov_sheds = gov_floor = 0;
    load_tight = 0; vl_tcalm = vl_sover = vl_shed = 0; vl_units = 0; vl_rng = 1u;
    song.g[G_DJF] = 0;
}
static uint32_t vl_eng(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES && !str_eq(ENGINES[e]->name, n); e++)
        ;
    return e;
}
static void vl_sound(uint32_t k, const char *eng, const char *pre)
{
    track_t *t = &trk[k];
    uint32_t e = vl_eng(eng), i;
    set_engine_of(t, e);
    for (i = 0; i < ENGINES[e]->npresets; i++)
        if (str_eq(ENGINES[e]->presets[i].name, pre)) { apply_preset_to(t, i); break; }
}
/* a track's steps: len steps of DIV div, each nn notes (a chord up from root) every every steps, the rest empty */
static void vl_steps(uint32_t k, uint32_t len, uint32_t every, uint32_t nn, const uint8_t *roots, uint32_t nr, uint32_t gate)
{
    track_t *t = &trk[k];
    uint32_t i, j;
    static const uint8_t CH[4] = {0, 4, 7, 11};
    load_begin(t, LOAD_PAT);
    t->p[P_SLEN] = (int16_t)len; t->p[P_SGATE] = (int16_t)gate; t->p[P_MUTE] = 0;
    for (i = 0; i < NSTEP; i++) memset(&t->step[i], 0, sizeof t->step[i]);
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        s->time = ST_NOTE; s->vel = 100;
        if (i % every) continue;
        for (j = 0; j < nn && j < 4u; j++) s->note[j] = (uint8_t)(roots[(i / every) % nr] + CH[j]);
        s->n = (uint8_t)(nn < 4u ? nn : 4u);
    }
    load_end(t);
}
static void vl_drums(uint32_t k, uint32_t busy)
{
    track_t *t = &trk[k];
    uint32_t i;
    load_begin(t, LOAD_PAT);
    t->p[P_SLEN] = 16; t->p[P_MUTE] = 0;
    for (i = 0; i < NSTEP; i++) memset(&t->step[i], 0, sizeof t->step[i]);
    for (i = 0; i < 16u; i++) {
        step_t *s = &t->step[i];
        s->time = ST_NOTE; s->vel = 100;
        s->hit = (uint8_t)((i % 4u == 0u ? 1u << DV_KICK : 0u) | (i % 8u == 4u ? 1u << 1 : 0u) |
                           (i % 2u == 0u ? 1u << DV_HATC : 0u) | (i % 4u == 2u ? 1u << DV_HATO : 0u) |
                           (busy && i % 3u == 1u ? 1u << 5 : 0u) | (busy && i % 8u == 6u ? 1u << 2 : 0u) |
                           (busy && i % 5u == 3u ? 1u << 7 : 0u) | (busy && i % 7u == 5u ? 1u << 6 : 0u));
    }
    load_end(t);
}
static const uint8_t R_BASS[4] = {36, 36, 41, 43}, R_MEL[8] = {60, 62, 64, 67, 69, 67, 64, 62}, R_CH[4] = {57, 53, 60, 55};
static void sc_poweron(void)                            /* the power-on parts as they are, each playing */
{
    vl_fresh();
    vl_drums(0, 0);
    vl_steps(1, 16, 2, 1, R_MEL, 8, 64);                 /* VOICE: a line in 8ths */
    vl_steps(2, 16, 8, 1, R_MEL, 8, 64);                 /* ANIMAL: a call each half bar */
    vl_steps(3, 16, 1, 1, R_MEL, 8, 64);                 /* LOFI: 16ths */
    vl_steps(4, 16, 2, 1, R_BASS, 4, 64);                /* ANALOG: the bass in 8ths */
    vl_steps(5, 16, 4, 4, R_CH, 4, 100);                 /* FM6: chords of 4 a beat */
    song.g[G_BPM] = 120;
}
static void sc_gen(uint32_t genre, uint32_t seed)
{
    uint32_t k;
    vl_fresh();
    gp.genre = (uint8_t)genre; gp.seed = (uint16_t)seed; gp.energy = 80; gp.form = 0;
    for (k = 0; k < GP_N; k++) gp.amt[k] = GA_AUTO;
    gp.complex = 70; gp.swing = GA_AUTO; gp.human = 30;
    mc_host = 1;
    gen_go();
}
static void sc_pad(void)                                /* a pad's long release, chords of four on every beat */
{
    vl_fresh();
    vl_drums(0, 1);
    vl_sound(4, "ANALOG", "JUNO PAD");
    trk[4].p[P_REL] = 110; trk[4].p[P_VOICE] = V_POLY;
    vl_steps(4, 16, 4, 4, R_CH, 4, 90);
    vl_steps(5, 16, 8, 4, R_CH, 4, 120);
    trk[5].p[P_REL] = 100;
    vl_steps(3, 16, 2, 1, R_MEL, 8, 50);
    trk[1].p[P_MUTE] = trk[2].p[P_MUTE] = 1;
    song.g[G_BPM] = 110;
}
static void sc_arp(void)                                /* arpeggiated chords (ARP 1/16 on the steps) on two tracks */
{
    uint32_t k, i;
    vl_fresh();
    vl_drums(0, 1);
    vl_sound(4, "ANALOG", "ARP PLUCK");
    vl_steps(4, 16, 4, 4, R_CH, 4, 120);
    vl_steps(5, 16, 4, 4, R_CH, 4, 120);
    vl_steps(3, 16, 2, 1, R_BASS, 4, 60);
    for (k = 4; k < 6u; k++)
        for (i = 0; i < 16u; i++)
            if (trk[k].step[i].n) { trk[k].step[i].comp = CP_ARPQ; trk[k].step[i].carg = CP_ARG(1u, ARP_RCODE[2], 1u); }
    trk[1].p[P_MUTE] = trk[2].p[P_MUTE] = 1;
    song.g[G_BPM] = 128;
}
static void sc_ratch(void)                              /* ratchets x4 on the drums and the lines, rolls */
{
    uint32_t k, i;
    vl_fresh();
    vl_drums(0, 1);
    vl_steps(3, 16, 1, 1, R_MEL, 8, 80);
    vl_steps(4, 16, 2, 1, R_BASS, 4, 80);
    vl_steps(5, 16, 4, 3, R_CH, 4, 100);
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < 16u; i++)
            if (i % 4u == 3u) trk[k].step[i].flags |= (uint8_t)(3u << SF_RATCH_SH);
    trk[1].p[P_MUTE] = trk[2].p[P_MUTE] = 1;
    song.g[G_BPM] = 140;
}
static void sc_fugue(void)                              /* PLAYHEADS: three more heads on two melodic tracks */
{
    uint32_t h;
    vl_fresh();
    vl_drums(0, 0);
    vl_steps(3, 16, 1, 1, R_MEL, 8, 70);
    vl_steps(5, 16, 2, 2, R_CH, 4, 100);
    vl_steps(4, 16, 2, 1, R_BASS, 4, 64);
    for (h = 0; h < FG_HEADS; h++) {
        fg_set[3][h].rate = (uint8_t)(2u + h); fg_set[3][h].dir = (uint8_t)h; fg_set[3][h].ivl = (int8_t)(2 + 2 * (int)h);
        fg_set[5][h].rate = (uint8_t)(3u + h); fg_set[5][h].dir = (uint8_t)(3u - h); fg_set[5][h].ivl = (int8_t)(-3 - (int)h);
    }
    trk[1].p[P_MUTE] = trk[2].p[P_MUTE] = 1;
    song.g[G_BPM] = 120;
}
static void sc_unison(void)                             /* a UNISON lead over a pad and the drums */
{
    vl_fresh();
    vl_drums(0, 1);
    vl_sound(3, "ANALOG", "SAW LEAD");
    trk[3].p[P_VOICE] = V_UNISON; trk[3].p[P_DETUNE] = 40;
    vl_steps(3, 16, 2, 1, R_MEL, 8, 90);
    vl_sound(4, "ANALOG", "SOFT PAD");
    trk[4].p[P_REL] = 100;
    vl_steps(4, 16, 8, 4, R_CH, 4, 110);
    vl_steps(5, 16, 4, 3, R_CH, 4, 80);
    trk[1].p[P_MUTE] = trk[2].p[P_MUTE] = 1;
    song.g[G_BPM] = 124;
}

static void vl_play(uint32_t secs)
{
    uint32_t b, nb = secs * FS / CTL;
    int32_t o[2 * CTL];
    song.master_q12 = 4096;
    mc_host = 1;
    transport_req = 1;
    for (b = 0; b < 400u; b++) {                         /* (the sound loads over, the first bar: not counted) */
        if (b % 32u == 0u) { ui_input(); song_poll(); host_ticks += 20000u; fm1_ms += 20u; }
        mix_block(o, CTL);
        vl_block_units();
        if ((b + 1u) % (HALF_FRAMES / CTL) == 0u) vl_half();
    }
    memset(&vl, 0, sizeof vl);
    vl.cap_min = 99;
    vl_on = 1;
    for (b = 0; b < nb; b++) {
        if (b % 32u == 0u) { ui_input(); song_poll(); host_ticks += 20000u; fm1_ms += 20u; }
        mix_block(o, CTL);
        vl_sample();
        vl_block_units();
        if ((b + 1u) % (HALF_FRAMES / CTL) == 0u) vl_half();
    }
    vl_on = 0;
    transport_req = 2;
    ui_input();
    for (b = 0; b < 64u; b++) mix_block(o, CTL);
    mc_host = 0;
}

static const char *vl_ename(uint32_t k) { return ENGINES[trk[k].engine % NENGINES]->name; }
static uint32_t vl_tot(const uint32_t *a) { uint32_t k, s = 0; for (k = 0; k < NTRK; k++) s += a[k]; return s; }
static void vl_print(const char *name)
{
    uint32_t k;
    printf("%-14s req %5u held %4u cut %4u quiet %4u trim %4u deny %4u own %3u pcut %4u | busy %4.1f max %2u cap %4.1f min %2u | "
           "load %3.0f%% max %3u%% tight %3.0f%% gov<cap %3.0f%% (over %u hot %u shed %u) sheds %u late %u\n",
           name, vl_tot(vl.req), vl_tot(vl.held), vl_tot(vl.cut), vl_tot(vl.quiet), vl_tot(vl.trim), vl_tot(vl.deny), vl_tot(vl.own), vl_tot(vl.pcut),
           (double)vl.tot_sum / vl.blocks, vl.tot_max, (double)vl.cap_sum / vl.blocks, vl.cap_min,
           (double)vl.load_sum / vl.halves, vl.load_max, 100.0 * vl.tight / vl.halves, 100.0 * vl.gov_low / vl.halves,
           vl.gov_over, vl.gov_hot, vl.gov_shed, vl.sheds, vl.late);
    for (k = 0; k < NTRK; k++)
        if (vl.req[k] || vl.busy_max[k])
            printf("    T%u %-7s req %5u held %4u cut %4u quiet %4u trim %4u deny %4u pcut %4u | busy %4.1f max %u\n", k + 1u,
                   vl_ename(k), vl.req[k], vl.held[k], vl.cut[k], vl.quiet[k], vl.trim[k], vl.deny[k], vl.pcut[k], (double)vl.busy_sum[k] / vl.blocks,
                   vl.busy_max[k]);
}

static const struct { const char *name; uint32_t genre, seed; void (*f)(void); } VL_SC[] = {
    {"POWER-ON", 0, 0, sc_poweron}, {"PAD LONG REL", 0, 0, sc_pad}, {"ARP CHORDS", 0, 0, sc_arp},
    {"RATCHETS", 0, 0, sc_ratch}, {"PLAYHEADS", 0, 0, sc_fugue}, {"UNISON LEAD", 0, 0, sc_unison},
    {"GEN HOUSE", 1, 7, 0}, {"GEN TECHNO", 6, 11, 0}, {"GEN TRANCE", 11, 3, 0}, {"GEN DNB", 32, 5, 0},
    {"GEN AMBIENT", 38, 9, 0}, {"GEN JAZZ", 35, 2, 0}, {"GEN IDM", 42, 4, 0},
    {"EXPLORE 1", GEN_EXPLORE, 101, 0}, {"EXPLORE 2", GEN_EXPLORE, 202, 0}, {"EXPLORE 3", GEN_EXPLORE, 303, 0},
};
#define VL_NSC (sizeof VL_SC / sizeof VL_SC[0])
static vl_t vl_res[VL_NSC];

int main(void)
{
    uint32_t s;
    for (s = 0; s < VL_NSC; s++) {
        if (VL_SC[s].f) VL_SC[s].f(); else sc_gen(VL_SC[s].genre, VL_SC[s].seed);
        vl_play(VL_SECS);
        vl_res[s] = vl;
        vl_print(VL_SC[s].name);
    }
    return 0;
}
