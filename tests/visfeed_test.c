/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* (0.9.19) VIS FEED (firmware/src/visfeed.c, docs/VISFEED.md) against the sound, the sequencer and the USB-MIDI rings of
 * the host build, decoded as an app would (tests/visfeed_decode.h): nothing without a subscription and MIDI OUT byte
 * for byte the same with or without it; HELLO (and OFF in the MENU); the whole state within 2 s and each field what the
 * synth has; the rate, change detection and keyframes; NOTE / RATCH / MUTATE / FX / SECTION / TRANSPORT / BEAT / SOUND
 * events; the lapse and UNSUB; the budget (frames a pass, the SysEx ring at most half full, never waiting), the
 * editor still answered, seq without a gap; its cost. Writes build/host/visfeed.bin (the stream) and visfeed.json
 * (what this decoder made of it) for web/visfeed_test.mjs. */
#define EDITOR_TEST_NO_MAIN
#include "editor_test.c"
#include "../firmware/src/visfeed.c"
#include "visfeed_decode.h"
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>                          /* (the instructions of a pass, under callgrind) */
#else
#define CALLGRIND_TOGGLE_COLLECT
#endif

static int vcheck(const char *what, int ok)
{
    printf("visfeed: %-96s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}
static vfd_t D;
static uint32_t ev_n[16][8], ev_last[16][8][2];
static uint32_t pk_max, ring_max, sam;
static uint8_t cap[1u << 20];
static uint32_t cap_n;
static uint32_t mo_log[4096], mo_n;
static int drain_on = 1, verbose = 1;
static uint32_t tot[3];                                  /* frames, events, records of the whole stream captured */

static void on_ev(void *ctx, const vfd_event_t *e)
{
    (void)ctx;
    ev_n[e->kind & 15u][e->trk & 7u]++;
    ev_last[e->kind & 15u][e->trk & 7u][0] = e->a;
    ev_last[e->kind & 15u][e->trk & 7u][1] = e->b;
}
static void vdrain(void)                                 /* the app reading EP 0x81 */
{
    uint32_t n = 0;
    if (so_w - so_r > ring_max) ring_max = so_w - so_r;
    while (drain_on && so_r != so_w) {
        uint32_t p = sx_out_q[so_r++ % SXQ];
        uint8_t b[4] = {(uint8_t)p, (uint8_t)(p >> 8), (uint8_t)(p >> 16), (uint8_t)(p >> 24)};
        vfd_packet(&D, b);
        if (cap_n + 4u <= sizeof cap) { memcpy(cap + cap_n, b, 4); cap_n += 4; }
        n++;
    }
    while (mo_r != mo_w) {                               /* MIDI OUT's ring, logged */
        if (mo_n < NELEM(mo_log)) mo_log[mo_n++] = midi_out_q[mo_r % MQ];
        mo_r++;
    }
    if (n > pk_max) pk_max = n;
}
static void vsub(uint32_t rate, uint32_t flags)
{
    static uint32_t seq;
    uint8_t m[12];
    uint32_t i, n = vfd_sub(m, seq++, rate, flags);
    for (i = 0; i < n; i++) sysex_byte(m[i]);
}
/* the music for ms: audio blocks, a main-loop pass every 16 ms (VIS FEED's), the app reading after each */
static void play_ms(uint32_t ms, int feed, uint32_t sub_every)
{
    uint32_t end = fm1_ms + ms, next = fm1_ms, subt = fm1_ms;
    int32_t o[2 * CTL];
    while (fm1_ms < end) {
        mix_block(o, CTL);
        sam += CTL;
        fm1_ms = (uint32_t)((uint64_t)sam * 1000u / FS);
        if (sub_every && fm1_ms - subt >= sub_every) { subt = fm1_ms; vsub(30, 0); }
        if (feed && fm1_ms >= next) { next = fm1_ms + 16u; vf_service(); }
        vdrain();
    }
}
static void pattern(void)                                /* four tracks: a 16th line each, track 3 a chord, DRUM on 4 */
{
    uint32_t k, i;
    for (k = 0; k < 4u; k++) {
        if (k == 3u) { set_engine_of(&trk[k], ENGI_DRUM); apply_preset_to(&trk[k], 0); trk[k].engine = trk[k].eng_req; }
        trk[k].p[P_SLEN] = (int16_t)(8 + 4 * k);
        for (i = 0; i < NSTEP; i++) {
            uint8_t n[3] = {(uint8_t)(48 + 12 * (k % 3u) + i % 5u), (uint8_t)(52 + 12 * (k % 3u)), (uint8_t)(55 + 12 * (k % 3u))};
            if (k == 3u) n[0] = i % 2u ? 42 : 36;
            put_step(&trk[k], i, k == 2u ? 3u : 1u, n, ST_NOTE, 0);
        }
        trk[k].p[P_VOICE] = V_POLY;
    }
}
static void vreset(void)
{
    reset();
    memset(&vf, 0, sizeof vf); memset(&vf_q, 0, sizeof vf_q);
    tot[0] += D.frames; tot[1] += D.events; tot[2] += D.records;
    vfd_init(&D); D.on_event = on_ev;
    memset(ev_n, 0, sizeof ev_n);
    pk_max = ring_max = 0; mo_n = 0; mo_r = mo_w = 0; drain_on = 1;
    sam = (uint32_t)((uint64_t)fm1_ms * FS / 1000u);
    trk_live = 4;
}
/* every field the decoder holds equals what the synth has now (vf_track / vf_global) */
static uint32_t mismatches(void)
{
    int16_t v[VF_TN];
    uint32_t k, i, bad = 0, mut = mut_enabled();
    for (k = 0; k < NTRK; k++) {
        vf_track(k, v, mut);
        for (i = 0; i < VF_TN; i++) {
            uint32_t id = vf_tid(i);
            int32_t want = id >= 0x40u ? v[i] + 8192 : vf_c7(v[i]), got = D.trk[k][id];
            if (want > 16383) want = 16383;
            if (want < 0) want = 0;
            if (got != want && verbose) printf("visfeed:   T%u field %02X: app %d, synth %d\n", k + 1u, id, got, want);
            bad += got != want;
        }
    }
    vf_global(v);
    for (i = 0; i < VF_GN; i++) {
        uint32_t id = vf_gid(i);
        int32_t want = id >= 0x40u ? v[i] : vf_c7(v[i]);
        if (D.glo[id] != want && verbose) printf("visfeed:   global %02X: app %d, synth %d\n", id, D.glo[id], want);
        bad += D.glo[id] != want;
    }
    return bad;
}

static void (*volatile vf_worst)(void) = vf_service;   /* (out of line: callgrind can count it) */
int main(void)
{
    int bad = 0, ok;
    uint32_t k, i, a, b, f0, by0, rec0, keys, frames_state, t0;

    /* 1. no subscription: nothing, and MIDI OUT ALL the same packets with and without the feed (each run a child from
     * the same state: the parent's) */
    {
        static uint32_t ref[3][4096];
        uint32_t rn[3], fr[3], sw[3];
        for (i = 0; i < 3u; i++) {
            int fd[2];
            pid_t pid;
            if (pipe(fd)) return 1;
            pid = fork();
            if (!pid) {
                uint32_t hdr[3];
                close(fd[0]);
                vreset(); pattern(); mo_lvl = MO_ALL; transport_req = 1;
                if (i == 2u) vsub(30, 0);
                play_ms(3000, i != 0u, i == 2u ? 1000u : 0u);
                hdr[0] = mo_n; hdr[1] = D.frames; hdr[2] = so_w;
                if (write(fd[1], hdr, sizeof hdr) != (ssize_t)sizeof hdr || write(fd[1], mo_log, sizeof mo_log) < 0) _exit(1);
                _exit(0);
            }
            close(fd[1]);
            {
                uint32_t hdr[3];
                size_t got = 0;
                ssize_t r;
                if (read(fd[0], hdr, sizeof hdr) != (ssize_t)sizeof hdr) return 1;
                while (got < sizeof ref[i] && (r = read(fd[0], (char *)ref[i] + got, sizeof ref[i] - got)) > 0) got += (size_t)r;
                rn[i] = hdr[0]; fr[i] = hdr[1]; sw[i] = hdr[2];
            }
            close(fd[0]);
            waitpid(pid, 0, 0);
        }
        ok = sw[1] == 0u && fr[1] == 0u && rn[1] == rn[0] && rn[0] > 50u && !memcmp(ref[0], ref[1], rn[0] * 4u);
        bad += vcheck("no SUB: no byte of it; MIDI OUT ALL packet for packet as without the feed", ok);
        printf("visfeed: (%u MIDI OUT packets in 3 s; subscribed: %u, and %u VIS FEED frames)\n", rn[0], rn[2], fr[2]);
        ok = rn[2] == rn[0] && !memcmp(ref[0], ref[2], rn[0] * 4u) && fr[2] > 50u;
        bad += vcheck("subscribed: MIDI OUT ALL still packet for packet the same (its own ring)", ok);
    }
    /* 2. MENU > MIDI > VIS FEED OFF: HELLO says so, nothing else */
    vreset();
    vis_feed_off = 1;
    vsub(30, 0); vf_service(); vdrain();
    f0 = D.frames;
    play_ms(2000, 1, 500);
    ok = D.hello && D.status == 1u && f0 == 1u && D.frames == 4u + 1u;   /* (a HELLO for each SUB, nothing else) */
    bad += vcheck("VIS FEED OFF: each SUB answered HELLO status 1, nothing streamed", ok);
    ok = menu_get(MI_VISFEED) == 1u && str_eq(menu_vname(MI_VISFEED, 0), "ON") && menu_step(MI_VISFEED, 1) == 0u &&
         menu_page_of(MI_VISFEED) == menu_page_of(MI_MIDIOUT) && mi_tab(MI_VISFEED) == MTAB_MIDI;
    menu_put(MI_VISFEED, 0);
    ok &= vis_feed_off == 0u;
    bad += vcheck("MENU > MIDI > VIS FEED: KNOB 4 of MIDI, ON / OFF, a step up is ON, stored", ok);

    /* 3. subscribed, stopped: HELLO, the whole state, each field as the synth has it */
    vreset(); pattern();
    vsub(30, 0);
    play_ms(2100, 1, 1000);
    vf.st_ms = fm1_ms - 1000u; fm1_ms += 16u; vf_service(); vdrain();   /* (a period now: what the app has is now's) */
    ok = D.hello && D.status == 0u && D.ver == 1u && D.ntrk == NTRK && D.rate == 30u && !strcmp(D.fw, "TEST") &&
         D.live == 4u;
    bad += vcheck("SUB: HELLO status 0, v1, 6 tracks (4 playing), 30 Hz, the firmware's version", ok);
    a = mismatches();
    ok = !a && D.trk[3][0x00] == ENGI_DRUM && D.trk[0][0x18] == 8 && D.glo[0x40] == 1200 && D.trk[5][0x40] != VFD_UNSET;
    bad += vcheck("within 2 s every field of every track and the globals, equal to the synth's (DRUM, LEN, BPM x 10)", ok);
    if (a) printf("visfeed: %u fields differ\n", a);
    /* change detection: frozen (no audio), only keyframes: one unit's whole state every 250 ms */
    rec0 = D.records; by0 = D.frames;
    t0 = fm1_ms;
    for (i = 0; i < 125u; i++) { fm1_ms += 16u; sam = (uint32_t)((uint64_t)fm1_ms * FS / 1000u); vf_service(); vdrain(); }
    keys = (fm1_ms - t0) / VF_KEY_MS;
    b = D.records - rec0;
    ok = b >= (keys - 1u) * VF_GN && b <= (keys + 1u) * VF_TN;
    bad += vcheck("nothing changing: no record but the keyframes (a unit's state every 250 ms)", ok);
    printf("visfeed: frozen 2 s: %u records in %u frames (%u keyframes)\n", b, D.frames - by0, keys);

    /* 4. playing: the rate, every field right after each period, NOTE events = MIDI OUT's notes */
    vreset(); pattern(); mo_lvl = MO_ALL;
    vsub(30, 0); play_ms(500, 1, 0);
    transport_req = 1;
    {
        uint32_t periods = 0, wrong = 0, end, next, subt, notes[NTRK] = {0}, st_frames = 0, lastms = 0xFFFFFFFFu;
        uint32_t by = D.frames, t1 = fm1_ms, pkt0 = cap_n, ev0 = D.events;
        int32_t o[2 * CTL];
        end = fm1_ms + 8000u; next = subt = fm1_ms;
        while (fm1_ms < end) {
            mix_block(o, CTL); sam += CTL; fm1_ms = (uint32_t)((uint64_t)sam * 1000u / FS);
            if (fm1_ms - subt >= 1000u) { subt = fm1_ms; vsub(30, 0); }
            if (fm1_ms >= next) {
                uint32_t st0 = vf.st_ms, fr = D.frames;
                next = fm1_ms + 16u;
                vf_service(); vdrain();
                if (vf.st_ms != st0) { periods++; if (!vf.due) wrong += mismatches() != 0u; }
                if (D.frames != fr && D.ms != lastms) { st_frames++; lastms = D.ms; }
            } else
                vdrain();
        }
        for (i = 0; i < mo_n; i++)                       /* MIDI OUT ALL's note-ons, by channel */
            if (((mo_log[i] >> 8) & 0xF0u) == 0x90u && (mo_log[i] >> 24)) notes[(mo_log[i] >> 8) & 15u]++;
        ok = periods >= 8u * 30u * 9u / 10u && periods <= 8u * 31u && !wrong;
        bad += vcheck("playing, 30 Hz: a state period every 33 ms, each one leaving the app's state = the synth's", ok);
        printf("visfeed: %u periods in 8 s, %u with a field behind; %u frames, %u B/s, %u events; at most %u packets a "
               "pass, the ring at most %u of %u\n", periods, wrong, D.frames - by, (cap_n - pkt0) / 4u * 3u / 8u,
               D.events - ev0, pk_max, ring_max, SXQ);
        ok = 1;
        for (k = 0; k < 4u; k++) {
            int32_t d = (int32_t)ev_n[VFE_NOTE][k] - (int32_t)notes[k];
            ok &= notes[k] > 30u && d >= -3 && d <= 3;   /* (MIDI OUT's are sent as the notes start: the same) */
            printf("visfeed: track %u: %u NOTE events, MIDI OUT %u note-ons\n", k + 1u, ev_n[VFE_NOTE][k], notes[k]);
        }
        bad += vcheck("NOTE events: every note each track started (as MIDI OUT ALL counts them)", ok);
        ok = ev_n[VFE_TRANSPORT][7] == 1u && ev_last[VFE_TRANSPORT][7][0] == 1u &&
             ev_n[VFE_BEAT][7] >= 15u && ev_n[VFE_BEAT][7] <= 17u;
        bad += vcheck("TRANSPORT PLAY once; a BEAT event every beat (16 in 8 s at 120)", ok);
        ok = pk_max <= VF_PASS * 16u && ring_max <= SXQ / 2u + 16u && D.lost == 0u && D.bad == 0u;
        bad += vcheck("a pass: at most 4 frames of 16 packets, the ring at most half full + a frame, no seq gap", ok);
        (void)frames_state; (void)st_frames;
    }
    /* 5. events: MUTATE, DICE, GEN (marked by the main loop), FX, SECTION, SOUND, RATCH, ROLL, STOP */
    {
        memset(ev_n, 0, sizeof ev_n);
        vf_mark(VFE_MUTATE, 2, 2, 64);
        vf_mark(VFE_DICE, 1, 1, 0);
        vf_mark(VFE_DICE, 7, 1234u >> 7, 1234u);
        perf_act |= PF_BIT(PF_GLT);
        live.cur = 1;
        trk[2].preset = (uint8_t)(trk[2].preset + 1u);
        fm1_ms += 16u; vf_service(); vdrain();
        ok = ev_n[VFE_MUTATE][2] == 1u && ev_last[VFE_MUTATE][2][0] == 2u && ev_last[VFE_MUTATE][2][1] == 64u &&
             ev_n[VFE_DICE][1] == 1u && ev_n[VFE_DICE][7] == 1u &&
             (ev_last[VFE_DICE][7][0] << 7 | ev_last[VFE_DICE][7][1]) == 1234u &&
             ev_n[VFE_FX][7] == 1u && ev_last[VFE_FX][7][0] == PF_GLT && ev_last[VFE_FX][7][1] == 1u &&
             ev_n[VFE_SECTION][7] == 1u && ev_last[VFE_SECTION][7][0] == 1u && ev_n[VFE_SOUND][2] == 1u;
        bad += vcheck("events: MUTATE, DICE, GEN's seed, FX GLITCH on, SECTION B, SOUND, on the next pass", ok);
        fm1_ms += 40u; vf_service(); vdrain();
        ok = (D.glo[0x41] >> PF_GLT) & 1u;
        perf_act &= ~PF_BIT(PF_GLT);
        fm1_ms += 40u; vf_service(); vdrain();
        ok &= ev_n[VFE_FX][7] == 2u && ev_last[VFE_FX][7][1] == 0u && !((D.glo[0x41] >> PF_GLT) & 1u);
        bad += vcheck("FX: FXON0's bit while it runs; off, an FX event and the bit clear", ok);
        memset(ev_n, 0, sizeof ev_n);
        transport_req = 2; play_ms(100, 1, 0);
        ok = ev_n[VFE_TRANSPORT][7] == 1u && ev_last[VFE_TRANSPORT][7][0] == 0u && !(D.glo[0x01] & 1);
        bad += vcheck("STOP: a TRANSPORT event, TRANSPORT's bit 0 clear", ok);
        trk[1].sp_left = 3; trk[1].sp_inst++; trk[3].ls_on = 0x05;
        fm1_ms += 40u; vf_service(); vdrain();
        ok = ev_n[VFE_RATCH][1] == 1u && ev_last[VFE_RATCH][1][0] == 3u && ev_n[VFE_ROLL][3] == 1u &&
             ev_last[VFE_ROLL][3][0] == 5u;
        bad += vcheck("RATCH (hits to come) and ROLL (the lanes) events", ok);
        trk[1].sp_left = 0; trk[3].ls_on = 0;
    }
    /* 6. the budget: the app not reading for 1 s: never past half the ring + a frame, never waiting; then it heals */
    {
        uint32_t w0;
        drain_on = 0;
        transport_req = 1;
        play_ms(1000, 1, 0);
        w0 = so_w - so_r;
        ok = w0 <= SXQ / 2u + 16u && ring_max <= SXQ / 2u + 16u;
        drain_on = 1; vdrain();
        play_ms(2500, 1, 1000);
        vf.st_ms = fm1_ms - 1000u; fm1_ms += 16u; vf_service(); vdrain();
        ok &= !mismatches();
        bad += vcheck("the app not reading: the ring at most half full + a frame, nothing waits; reading again: in sync", ok);
        {   /* the editor still answered while the feed runs */
            uint8_t z = 0;
            uint32_t n = request(ED_PING, &z, 0);
            ok = n >= 6u;
            bad += vcheck("the web editor's PING answered while the feed runs", ok);
        }
    }
    /* 7. the lapse (no SUB for 3 s) and UNSUB */
    {
        uint32_t fr;
        play_ms(1000, 1, 0);
        play_ms(2500, 1, 0);
        fr = D.frames;
        play_ms(1500, 1, 0);
        ok = !vf.sub && D.frames == fr && !vf_q.on;
        bad += vcheck("no SUB for 3 s: the feed stops by itself", ok);
        vsub(30, 0); play_ms(500, 1, 0);
        ok = vf.sub && D.status == 0u;
        vsub(0, 0); vf_service(); vdrain();
        fr = D.frames;
        play_ms(1000, 1, 0);
        ok &= !vf.sub && D.status == 2u && D.frames == fr;
        bad += vcheck("SUB again: back; rate 0: HELLO status 2, nothing more", ok);
        vsub(60, VF_FTRK); play_ms(300, 1, 0);
        ok = vf.rate == 60u && vf.flags == VF_FTRK;
        vsub(0, 0); vf_service(); vdrain();
        bad += vcheck("SUB's rate (60) and flags (tracks only) taken", ok);
    }
    /* 8. the cost: a busy pass (6 tracks playing, every field moving), host ns */
    {
        uint64_t t = 0, n = 0;
        vreset(); pattern(); trk_live = 6;
        vsub(60, 0); transport_req = 1;
        play_ms(500, 1, 0);
        for (i = 0; i < 400u; i++) {
            int32_t o[2 * CTL];
            uint64_t s0;
            for (k = 0; k < 22u; k++) { mix_block(o, CTL); sam += CTL; }
            fm1_ms = (uint32_t)((uint64_t)sam * 1000u / FS);
            vf.st_ms = fm1_ms - 1000u;                   /* (every pass a period: the worst) */
            vf.sub_ms = fm1_ms;
            s0 = now_ns(); CALLGRIND_TOGGLE_COLLECT; vf_worst(); CALLGRIND_TOGGLE_COLLECT; t += now_ns() - s0; n++;
            vdrain();
        }
        printf("visfeed: a busy pass (every unit due) %.2f us on the host\n", (double)t / (double)n / 1000.0);
        t = n = 0;
        for (i = 0; i < 400u; i++) {                     /* the worst: every field of every unit (a keyframe of all) */
            int32_t o[2 * CTL];
            uint64_t s0;
            for (k = 0; k < 22u; k++) { mix_block(o, CTL); sam += CTL; }
            fm1_ms = (uint32_t)((uint64_t)sam * 1000u / FS);
            vf.st_ms = fm1_ms - 1000u;
            vf.keyed = (uint8_t)((1u << (NTRK + 1u)) - 1u);
            vf.sub_ms = fm1_ms;
            s0 = now_ns(); CALLGRIND_TOGGLE_COLLECT; vf_worst(); CALLGRIND_TOGGLE_COLLECT; t += now_ns() - s0; n++;
            vdrain();
        }
        printf("visfeed: the worst pass (every field of every unit, 4 frames) %.2f us on the host\n",
               (double)t / (double)n / 1000.0);
    }
    {   /* the stream for web/visfeed_test.mjs and what this decoder made of it */
        FILE *f = fopen("build/host/visfeed.bin", "wb");
        if (f) { fwrite(cap, 1, cap_n, f); fclose(f); }
        f = fopen("build/host/visfeed.json", "w");
        if (f) {
            fprintf(f, "{\"frames\":%u,\"events\":%u,\"records\":%u,\"trk\":[", tot[0] + D.frames, tot[1] + D.events,
                    tot[2] + D.records);
            for (k = 0; k < NTRK; k++) {
                fprintf(f, "%s[", k ? "," : "");
                for (i = 0; i < 0x70u; i++) fprintf(f, "%s%d", i ? "," : "", D.trk[k][i] == VFD_UNSET ? -1 : D.trk[k][i]);
                fprintf(f, "]");
            }
            fprintf(f, "],\"glo\":[");
            for (i = 0; i < 0x70u; i++) fprintf(f, "%s%d", i ? "," : "", D.glo[i] == VFD_UNSET ? -1 : D.glo[i]);
            fprintf(f, "]}\n");
            fclose(f);
        }
    }
    printf("%s\n", bad ? "VIS FEED TEST FAILED" : "vis feed test passed");
    return bad != 0;
}
