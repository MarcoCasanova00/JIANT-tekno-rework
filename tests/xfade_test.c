/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* (JIANT 0.9.19) A sound load's fade (voice.c track_render: what sounds fades out on the sound it had, xf_snap, while
 * the engine switch waits): the old engine must never render with another engine's values.
 *   1  PRESETS crossing into another engine's sounds (ui.c preset_go: the engine, then the preset: two loads) and a
 *      MIDI PROGRAM CHANGE to another engine's sound (midi_pc_poll, the same two), with notes sounding and no audio
 *      block between the two loads (the main loop runs both in one frame; the audio ISR every 0.7 ms): until 0.9.18 the
 *      second load took the snapshot again, of the first load's values, the new engine's, and the old engine faded
 *      out on them (ANIMAL read past its HYST table, a DIGITAL / FM6 algorithm past its own: tests/fuzz_ui.c found it)
 *   2  a snapshot left from a fade that never ran (a load with nothing sounding, then an engine fade later): not used
 *   3  every engine rendered with every other engine's preset values for a fade's length (what the bug did, and what a
 *      wrong word from the second core could do): no read or write outside its tables (ASan / UBSan, bounds on)
 * Built and run by tests/run_tests.sh (with and without the sanitizers). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int xf_check(const char *what, int ok)
{
    printf("xfade: %-92s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
static void blocks(uint32_t n)
{
    static int32_t o[2 * CTL];
    while (n--)
        mix_block(o, CTL);
}
/* the values the old engine renders with in the fade's first block, against its own (the old sound) */
static int fade_on_old(uint32_t ti, const int16_t *old_e)
{
    const track_t *t = &trk[ti];
    uint32_t i;
    int16_t got[8];
    if (!t->xf_on)
        return 1;                                    /* (nothing faded: nothing rendered on any values) */
    for (i = 0; i < 8u; i++)
        got[i] = xf_snap_t == ti && xf_snap_e == t->engine ? xf_snap[P_E0 + i] : t->pe_old[i];
    return !memcmp(got, old_e, sizeof got);
}
/* the first list index (PRESETS) of a sound of engine e */
static uint32_t list_of(uint32_t e)
{
    uint32_t n, all, k;
    preset_all_pos(&all);
    for (n = 0; n < all; n++)
        if (preset_all_at(n, &k) == e)
            return n;
    return 0;
}

static int test_two_loads(void)
{
    static const uint8_t PAIRS[][2] = {{ENGI_ANALOG, ENGI_ANIMAL}, {ENGI_ANIMAL, ENGI_FM6}, {ENGI_FM6, ENGI_LOFI},
                                       {ENGI_LOFI, ENGI_ANALOG}, {ENGI_PHASE, ENGI_WHEEL}, {ENGI_WHEEL, ENGI_ANIMAL},
                                       {ENGI_NOISE, ENGI_PHASE}, {ENGI_VOICE, ENGI_FM6}};
    uint32_t p, way, bad_ui = 0, bad_pc = 0, faded = 0;
    for (way = 0; way < 2u; way++)
        for (p = 0; p < NELEM(PAIRS); p++) {
            track_t *t;
            int16_t old_e[8];
            ui_power_on();
            xf_snap_t = 0xFFu;
            song.sel = 1;
            t = TSEL;
            load_depth++;
            set_engine_of(t, PAIRS[p][0]);
            load_depth--;
            t->engine = t->eng_req;
            blocks(4);
            trk_note_on(t, 60, 100);
            trk_note_on(t, 64, 100);
            blocks(8);
            memcpy(old_e, &t->p[P_E0], sizeof old_e);
            if (!way) {
                preset_go(list_of(PAIRS[p][1]));     /* PRESETS: the engine, then its sound, in one frame */
            } else {
                midi_pc_req[1] = (uint8_t)(1u + list_of(PAIRS[p][1]));   /* PROGRAM CHANGE on T2's channel */
                midi_pc_poll();
            }
            blocks(1);                               /* the fade's first block (panic_req: trk_fade_out) */
            faded += t->xf_on;
            if (!fade_on_old(1, old_e))
                *(way ? &bad_pc : &bad_ui) += 1;
            blocks(40);
            if (t->engine != PAIRS[p][1])
                *(way ? &bad_pc : &bad_ui) += 1;
        }
    ui_power_on();
    return xf_check("PRESETS into another engine's sounds, notes sounding: the old engine fades on its own values",
                    !bad_ui && faded >= NELEM(PAIRS)) +
           xf_check("PROGRAM CHANGE to another engine's sound: the same", !bad_pc);
}

/* the fade of T2 from ANIMAL into FM6 without a load (a song's section: seq.c sec_apply), with a snapshot of another
 * engine's sound left (stale: 1) or none; its audio (the fade's blocks) */
static uint64_t stale_run(int stale, int *faded)
{
    static int32_t o[2 * CTL];
    track_t *t;
    uint32_t i, k;
    uint64_t h = 1469598103934665603u;
    ui_power_on();
    xf_snap_t = 0xFFu;
    song.sel = 1;
    t = TSEL;
    load_depth++;
    set_engine_of(t, ENGI_ANIMAL);
    load_depth--;
    t->engine = t->eng_req;
    blocks(4);
    trk_note_on(t, 60, 100);
    trk_note_on(t, 64, 100);
    blocks(8);
    if (stale) {
        memcpy(xf_snap, t->p, sizeof xf_snap);
        for (i = 0; i < 8u; i++)
            xf_snap[P_E0 + i] = ENG_ANALOG.presets[0].e[i];
        xf_snap[P_E4] = 90;
        xf_snap_t = 1;
        xf_snap_e = ENGI_ANALOG;
    }
    fm1_irq_off();
    t->eng_req = ENGI_FM6;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = ENG_FM6.edit[i].def;
    fm1_irq_on();
    for (k = 0; k < XF_ENG + 2u; k++) {
        mix_block(o, CTL);
        *faded |= t->xf_on;
        for (i = 0; i < CTL; i++)                    /* (the mix before MASTER: the test's power-on has it at 0) */
            h = (h ^ (uint32_t)mix_l[i] ^ (uint32_t)mix_r[i] << 7) * 1099511628211u;
    }
    blocks(40);
    *faded &= t->engine == ENGI_FM6;
    ui_power_on();
    xf_snap_t = 0xFFu;
    return h;
}
static uint64_t stale_fork(int stale, int *faded)   /* (each from the same state: the generators of every engine too) */
{
    int fd[2], st;
    uint64_t r[2] = {0, 0};
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (!pid) {
        int f = 0;
        r[0] = stale_run(stale, &f);
        r[1] = (uint64_t)f;
        if (write(fd[1], r, sizeof r) != (ssize_t)sizeof r) _exit(1);
        _exit(0);
    }
    if (read(fd[0], r, sizeof r) != (ssize_t)sizeof r) r[0] = r[1] = 0;
    waitpid(pid, &st, 0);
    close(fd[0]);
    close(fd[1]);
    *faded = (int)r[1];
    return r[0];
}
static int test_stale(void)
{
    int f0 = 0, f1 = 0;
    uint64_t a = stale_fork(0, &f0), b = stale_fork(1, &f1);
    return xf_check("a snapshot of another engine's sound left, then an engine fade without a load: as with none", f0 && f1 && a == b);
}

static int test_cross(void)
{
    uint32_t a, b, k, n = 0;
    for (a = 0; a < NENGINES; a++) {
        if (eng_gone(a) || a == ENGI_DIGITAL)
            continue;
        for (b = 0; b < NENGINES; b++) {
            if (eng_gone(b) || b == ENGI_DIGITAL || b == a)
                continue;
            for (k = 0; k < ENGINES[b]->npresets; k++) {
                track_t *t = &trk[1];
                uint32_t i;
                ui_power_on();
                xf_snap_t = 0xFFu;
                load_depth++;
                set_engine_of(t, a);
                load_depth--;
                t->engine = t->eng_req;
                trk_note_on(t, 48u + k % 24u, 100);
                trk_note_on(t, 67, 90);
                blocks(3);
                for (i = 0; i < 8u; i++)                 /* engine a's voices on b's sound */
                    t->p[P_E0 + i] = ENGINES[b]->presets[k].e[i];
                for (i = 0; i < 8u; i++)
                    t->pe_old[i] = t->p[P_E0 + i];
                blocks(XF_ENG);
                n++;
            }
        }
    }
    ui_power_on();
    printf("  %u renders of an engine on another's sound\n", n);
    return xf_check("every engine on every other engine's preset values, a fade long: nothing outside its tables", n > 100u);
}

int main(void)
{
    int bad = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    bad += test_two_loads();
    bad += test_stale();
    bad += test_cross();
    printf(bad ? "xfade: %d FAILED\n" : "xfade: all passed\n", bad);
    return bad ? 1 : 0;
}
