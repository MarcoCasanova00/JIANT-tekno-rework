/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* (0.9.19) VIS FEED: the synth's state for the JIANT Stems app's visuals, over USB-MIDI (docs/VISFEED.md, the protocol:
 * version 1). Opt-in twice: MENU > MIDI > VIS FEED ON (the default) only allows it; nothing goes out until a host
 * subscribes with JIANT's own SysEx (F0 7D 4A 56 01 00 rate flags F7) and it lapses VF_SUB_MS after the host's last
 * one, so a DAW, a drum machine or a MIDI monitor never sees a byte of it.
 *   F0 7D 4A 56 ver type seq tlo thi payload F7     (ver 1; seq +1 a frame; t: fm1_ms & 0x3FFF, 7 bits each)
 * Out: HELLO (the answer to every SUB), BYE, STATE (records: a marker 0x70 + k picks track k, 0x77 the globals; then
 * id value, a byte below 0x40, two (14 bit, + 8192, LSB first) from 0x40 to 0x6F; only what changed, 7-bit quantised,
 * at the subscribed rate, and a keyframe of one track or the globals every VF_KEY_MS), EVENTS (3 bytes each:
 * kind << 3 | track (7: global), a, b, on the next pass).
 * Cost: the main loop only (main.c, never the audio ISR); every value is a word or a byte read as it is (a value may
 * be a control block old, a pair of them from two blocks: visuals, nothing acts on it); no allocation; per pass at
 * most VF_PASS frames of at most VF_FRAME bytes (16 USB-MIDI packets), each only while the SysEx ring is at most half
 * full (the editor's replies keep their room; ep1_tx sends a frame in one poll, 0.5 ms). Idle (nobody subscribed):
 * one test a pass */
#define VF_VER 1u
#define VF_ID1 0x4Au                                     /* "JV" after the non-commercial ID 7D */
#define VF_ID2 0x56u
#define VF_SUB_MS 3000u                                  /* a subscription lapses this long after the host's last SUB */
#define VF_KEY_MS 250u                                   /* a keyframe: one unit's whole state (7 units: ~1.75 s round) */
#define VF_GAP_MS 4u                                     /* (passes closer than this: the main loop's waiting, nothing) */
#define VF_FRAME 48u                                     /* bytes a frame, F0 .. F7: 16 USB-MIDI packets */
#define VF_PASS 3u                                       /* frames a pass */
#define VF_RATE 30u                                      /* the state's rate when the host asks none (Hz) */
#define VF_T1 0x28u                                      /* a track's 1-byte fields (0x00 ..), 2-byte ones (0x40 ..) */
#define VF_T2 8u
#define VF_G1 0x13u                                      /* the globals' */
#define VF_G2 3u
#define VF_TN (VF_T1 + VF_T2)
#define VF_GN (VF_G1 + VF_G2)
#define VF_MARK 0x70u                                    /* + k: the records after it are track k's; + 7 the globals' */
#define VF_NEV 24u                                       /* events a pass (two frames of them) */
#define VF_EVF 12u                                       /* .. a frame */
#define VF_NONE 0x7FFF                                   /* (a field never sent) */
enum { VFT_SUB, VFT_HELLO, VFT_BYE, VFT_STATE = 0x10, VFT_EVENTS = 0x30 };
enum { VFE_NOTE, VFE_RATCH, VFE_ROLL, VFE_MUTATE, VFE_DICE, VFE_BEAT, VFE_TRANSPORT, VFE_SECTION, VFE_FX, VFE_SOUND };
enum { VF_FTRK = 1, VF_FGLO = 2, VF_FEV = 4, VF_FNOTE = 8 };   /* SUB's flags: what the host wants (0: all) */
_Static_assert(VF_T1 <= 0x40u && VF_T2 <= 0x30u && VF_G1 <= 0x40u && VF_G2 <= 0x30u, "VIS FEED ids");
_Static_assert(SXQ >= 2u * (VF_FRAME + 2u) / 3u, "a frame fits the half of the SysEx ring left");

static struct {
    uint8_t sub, flags, rate, seq, rr, key, play, beat, bar, due, keyed, hello;
    int8_t sec;
    uint32_t sub_ms, st_ms, key_ms, pass_ms, fx;
    uint8_t nr[NTRK];                                    /* the notes read of each track's ring (core.h vf_nw) */
    uint8_t sp[NTRK], spi[NTRK], ls[NTRK], eng[NTRK], pre[NTRK];
    int16_t tv[NTRK][VF_TN], gv[VF_GN];                  /* what the host has (VF_NONE: nothing yet) */
    uint8_t ev[VF_NEV][3], nev;
    uint8_t f[VF_FRAME];
    uint32_t n, pass_left;
    int16_t *pp[VF_FRAME / 2u], pv[VF_FRAME / 2u];      /* a STATE frame's values, the host's once it has gone */
    uint32_t np;
    uint8_t fmask;                                       /* the units with records in the STATE frame being filled */
    uint32_t frames, bytes, lost;                        /* (the tests, the console) */
} vf;

static const uint8_t VF_CHN[12] = {0, 3, 4, 4, 3, 3, 3, 3, 4, 4, 4, 2};   /* the notes of each SCL > CHORD (params.c N_CHRD) */

static int32_t vf_c7(int32_t v) { return v < 0 ? 0 : v > 127 ? 127 : v; }

/* ---------------------------------------------------------------------------------------------- frames --- */
static void vf_begin(uint32_t type)
{
    vf.f[0] = 0xF0; vf.f[1] = 0x7D; vf.f[2] = VF_ID1; vf.f[3] = VF_ID2; vf.f[4] = VF_VER;
    vf.f[5] = (uint8_t)type;
    vf.f[6] = (uint8_t)(vf.seq & 127u);
    vf.f[7] = (uint8_t)(fm1_ms & 127u);
    vf.f[8] = (uint8_t)((fm1_ms >> 7) & 127u);
    vf.n = 9;
    vf.np = 0;
    vf.fmask = 0;
}
static __attribute__((noinline)) void vf_put(uint32_t b) { vf.f[vf.n++] = (uint8_t)b; }
static uint32_t vf_room(void) { return VF_FRAME - 1u - vf.n; }   /* bytes left before the F7 */
/* the frame into the SysEx ring, all of it or nothing: 0 = it went (the ring at most half full, a budget left) */
static int vf_send(void)
{
    uint32_t i = 0, w = so_w, k;
    if (!vf.pass_left || !usb.config || so_w - so_r > SXQ / 2u) {
        vf.np = 0;                                       /* (what it held is sent again: still changed, due) */
        vf.due |= vf.fmask;
        vf.fmask = 0;
        return -1;
    }
    vf_put(0xF7);
    while (i < vf.n) {
        uint32_t pkt;
        k = vf.n - i >= 3u ? 3u : vf.n - i;
        pkt = (k == 3u && i + 3u < vf.n ? 4u : 4u + k) | (uint32_t)vf.f[i] << 8 |
              (k > 1u ? (uint32_t)vf.f[i + 1u] << 16 : 0u) | (k > 2u ? (uint32_t)vf.f[i + 2u] << 24 : 0u);
        sx_out_q[w++ % SXQ] = pkt;
        i += k;
    }
    RING_PUBLISH();
    so_w = w;                                            /* (the whole message at once: no note inside it) */
    for (i = 0; i < vf.np; i++)
        *vf.pp[i] = vf.pv[i];
    vf.np = 0;
    vf.seq++;
    vf.frames++;
    vf.bytes += vf.n;
    vf.pass_left--;
    return 0;
}

/* ---------------------------------------------------------------------------------------------- values --- */
static uint32_t vf_tid(uint32_t i) { return i < VF_T1 ? i : 0x40u + (i - VF_T1); }
static uint32_t vf_gid(uint32_t i) { return i < VF_G1 ? i : 0x40u + (i - VF_G1); }

/* the fields that are a parameter as it is (0..127; PAN + 64), by field id; 0xFF: worked out below */
static const uint8_t VF_TP[VF_T1] = {
    0xFF, 0xFF, 0xFF, P_LEVEL, P_PAN, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, P_ROOT, P_SCALE, P_AMODE, P_ARATE, P_AORDER,
    P_AOCT, 0xFF, 0xFF, 0xFF, P_CVOI, 0xFF, 0xFF, 0xFF, P_SLEN, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, P_MAMT,
    0xFF, P_DIST, P_CHOR, P_DLY, P_REV, 0xFF, 0xFF, 0xFF};
/* track k's fields as they are now, in v[] (field index order: VF_T1 one-byte ids, then the two-byte ones) */
static void vf_track(uint32_t k, int16_t *v, uint32_t mut)
{
    const track_t *t = &trk[k];
    const step_t *s;
    uint32_t i, n = 0, a = 0, st = 0, ch = (uint32_t)t->p[P_CHRD], e = trk_eng(t), an = e == ENGI_ANALOG;
    int32_t env = 0, lfo, cut;
    for (i = 0; i < VF_T1; i++)
        v[i] = (int16_t)(VF_TP[i] == 0xFFu ? 0 : vf_c7(t->p[VF_TP[i]] + (VF_TP[i] == P_PAN ? 64 : 0)));
    for (i = 0; i < 8u; i++)
        v[VF_T1 + i] = t->p[P_E0 + i];
    for (i = 0; i < NVOICE; i++) {
        const voice_t *x = &t->v[i];
        if (!x->active)
            continue;
        n++;
        if (x->stage && x->stage < 4u && (x->env >> 9) > env)
            env = x->env >> 9;                           /* (Q15, as voice.c's ENV -> FILTER reads it) */
        if (x->age >= a) {
            a = x->age;
            st = x->stage;
        }
    }
    lfo = (int32_t)(((int64_t)t->lfo_val * t->lfo_fade) >> 15);
    cut = t->p[an ? P_E4 : P_FCUT] * 256 + ((lfo * t->p[P_LD_FLT]) >> 7) + ((env * t->p[P_ED_FLT]) >> 7);
    ch = ch < 12u ? ch : 0u;
    v[0x00] = (int16_t)e;
    v[0x01] = (int16_t)((t->p[P_MUTE] != 0) | (k >= trk_live) << 1 | ((trk_hush >> k) & 1u) << 2 |
                        ((song.rec >> k) & 1u) << 3 | (song.sel == k) << 4 | (e == ENGI_DRUM) << 5 | LV_ON(k) << 6);
    v[0x02] = (int16_t)(t->preset & 127u);
    v[0x05] = (int16_t)vf_c7(cut >> 8);
    v[0x06] = (int16_t)vf_c7(t->p[an ? P_E5 : P_FRES]);
    v[0x07] = (int16_t)(env >> 8);
    v[0x08] = (int16_t)((st > 4u ? 4u : st) << 4 | n);
    v[0x09] = (int16_t)vf_c7(64 + (lfo >> 9));
    v[0x0A] = (int16_t)vf_c7(64 + t->p[P_TRANS] + t->trn + t->pfx_pit / 16);
    v[0x11] = (int16_t)(t->arp_idx & 127u);
    v[0x12] = (int16_t)(t->arp_note & 127u);
    v[0x13] = (int16_t)ch;
    v[0x15] = VF_CHN[ch];
    v[0x16] = (int16_t)vf_c7(t->mo_cur >> 8);
    v[0x17] = (int16_t)(t->seq_idx < NSTEP ? t->seq_idx : 127u);
    for (i = 0; i < FG_HEADS; i++)                       /* PLAYHEADS (fugue.c): each head's step, 127 off */
        v[0x19 + i] = (int16_t)(fg_set[k][i].rate && fg_head[k][i].idx < NSTEP ? fg_head[k][i].idx : 127u);
    v[0x1C] = (int16_t)(LV_ON(k) && lv.gidx[k] < NSTEP ? lv.gidx[k] : 127u);
    v[0x1D] = (int16_t)vf_c7(t->sp_left);
    v[0x1E] = (int16_t)((mut >> k) & 1u);
    v[0x20] = (int16_t)(t->pfx & 31u);
    v[0x25] = (int16_t)(t->loops & 127u);
    s = &seq_steps(t)[seq_src(t, t->seq_idx < NSTEP ? t->seq_idx : 0u)];
    v[0x26] = (int16_t)(s->comp & 127u);
    v[0x27] = (int16_t)(s->comp2 & 127u);
}
static uint32_t vf_fxon(void) { return (perf_act | (pfx_qon & PF_MIDI)) & 0x0FFFFFFFu; }
static void vf_global(int16_t *v)
{
    uint32_t b = beat_samples(), fx = vf_fxon(), i;
    v[0x00] = song.sel;
    v[0x01] = (int16_t)((song.playing != 0) | (song.rec != 0) << 1 | (live.mode != 0) << 2 | (chain.running != 0) << 3 |
                        (song.g[G_CLOCK] != 0) << 4);
    v[0x02] = (int16_t)(beat_n & 3u);
    v[0x03] = (int16_t)vf_c7(b ? (int32_t)((uint64_t)beat_pos * 128u / b) : 0);
    v[0x04] = (int16_t)(bar_n & 127u);
    v[0x05] = (int16_t)(live.cur + 1);
    v[0x06] = (int16_t)(chain.row & 127u);
    v[0x07] = (int16_t)vf_c7(song.octave + 64);
    for (i = 0; i < 4u; i++)
        v[0x08 + i] = perf_m[i];
    v[0x0C] = (int16_t)vf_c7(pf.w >> 8);
    v[0x0D] = (int16_t)vf_c7(pf.ss[0] >> 8);
    v[0x0E] = (int16_t)vf_c7(pf.ss[1] >> 8);
    v[0x0F] = (int16_t)vf_c7(song.g[G_DJF] + 64);
    v[0x10] = pfx_tgt;
    v[0x11] = (int16_t)(lv.act & 127u);
    v[0x12] = (int16_t)vf_c7((int32_t)(song.cpu_q8 * 100u >> 8));
    v[VF_G1 + 0] = (int16_t)(b ? (uint32_t)FS * 600u / b : 0u);   /* BPM x 10, as heard */
    v[VF_G1 + 1] = (int16_t)(fx & 0x3FFFu);             /* (PF bits 0..13, 14..27) */
    v[VF_G1 + 2] = (int16_t)(fx >> 14);
}

/* one unit's records into the STATE frame (u: track 0..5, NTRK the globals): what changed since the host's (all of it
 * keyed); frames sent as they fill. 0: all of it went, -1: the budget ran out first (the rest next pass) */
static int vf_unit(uint32_t u, uint32_t mut, int *open)
{
    int16_t cur[VF_TN > VF_GN ? VF_TN : VF_GN], *had = u < NTRK ? vf.tv[u] : vf.gv;
    uint32_t i, nf = u < NTRK ? VF_TN : VF_GN, keyed = (vf.keyed >> u) & 1u, marked = 0;
    if (u < NTRK) vf_track(u, cur, mut);
    else vf_global(cur);
    for (i = 0; i < nf; i++) {
        uint32_t id = u < NTRK ? vf_tid(i) : vf_gid(i), w = id >= 0x40u ? 3u : 2u;
        if (!keyed && cur[i] == had[i])
            continue;
        if (*open && vf_room() < w + !marked) {          /* full: it goes, the next one goes on */
            if (vf_send())
                return -1;
            *open = 0;
        }
        if (!*open) {
            if (!vf.pass_left)
                return -1;
            vf_begin(VFT_STATE);
            *open = 1;
            marked = 0;
        }
        if (!marked) {
            vf_put(VF_MARK + (u < NTRK ? u : 7u));
            vf.fmask |= (uint8_t)(1u << u);
            marked = 1;
        }
        vf_put(id);
        if (w == 3u) {                                   /* (a track's E values signed, + 8192; the globals' as they are) */
            int32_t x = cur[i] + (u < NTRK ? 8192 : 0);
            x = x < 0 ? 0 : x > 16383 ? 16383 : x;
            vf_put((uint32_t)x & 127u);
            vf_put((uint32_t)x >> 7);
        } else
            vf_put((uint32_t)vf_c7(cur[i]));
        vf.pp[vf.np] = &had[i];
        vf.pv[vf.np++] = cur[i];
    }
    vf.keyed &= (uint8_t)~(1u << u);
    return 0;
}

/* ---------------------------------------------------------------------------------------------- events --- */
static void vf_ev(uint32_t kind, uint32_t k, uint32_t a, uint32_t b)
{
    if (vf.nev >= VF_NEV) {
        vf.lost++;
        return;
    }
    vf.ev[vf.nev][0] = (uint8_t)((kind & 15u) << 3 | (k & 7u));
    vf.ev[vf.nev][1] = (uint8_t)(a & 127u);
    vf.ev[vf.nev++][2] = (uint8_t)(b & 127u);
}
/* what happened since the last pass: the transport, the beat, the section, the effects; the main loop's (vf_q); each
 * track's sound, ratchets, rolls and new notes (the ring trk_note_on fills: every note a track starts, whoever played
 * it: the sequencer, the arp, PLAYHEADS, the keys, MIDI IN, a repeat) */
static void vf_events(int want, int notes)
{
    uint32_t k, i, fx = vf_fxon(), d;
    if (want) {
        if ((song.playing != 0) != vf.play)
            vf_ev(VFE_TRANSPORT, 7, song.playing != 0, 0);
        if (song.playing && (beat_n != vf.beat || (bar_n & 127u) != vf.bar))
            vf_ev(VFE_BEAT, 7, bar_n, beat_n);
        if (live.cur != vf.sec && live.cur >= 0)
            vf_ev(VFE_SECTION, 7, (uint32_t)live.cur, chain.row);
        for (d = fx ^ vf.fx; d; d &= d - 1u) {
            i = (uint32_t)__builtin_ctz(d);
            vf_ev(VFE_FX, 7, i, (fx >> i) & 1u);
        }
        for (i = 0; i < vf_q.n; i++)
            if (vf.nev < VF_NEV) {
                for (k = 0; k < 3u; k++)
                    vf.ev[vf.nev][k] = vf_q.ev[i][k];
                vf.nev++;
            }
    }
    vf.play = song.playing != 0;
    vf.beat = (uint8_t)beat_n;
    vf.bar = (uint8_t)(bar_n & 127u);
    vf.sec = (int8_t)live.cur;
    vf.fx = fx;
    vf_q.n = 0;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        uint32_t e = trk_eng(t), ls = t->ls_on, w = vf_nw[k], r = vf.nr[k];
        if (want && (e != vf.eng[k] || t->preset != vf.pre[k]))
            vf_ev(VFE_SOUND, k, e, t->preset);
        vf.eng[k] = (uint8_t)e;
        vf.pre[k] = t->preset;
        if (want && t->sp_left && (t->sp_left > vf.sp[k] || t->sp_inst != vf.spi[k]))
            vf_ev(VFE_RATCH, k, t->sp_left, t->sp_arp != 0);
        vf.sp[k] = t->sp_left;
        vf.spi[k] = t->sp_inst;
        if (want && (ls & ~vf.ls[k]))
            vf_ev(VFE_ROLL, k, ls & ~vf.ls[k], 0);
        vf.ls[k] = (uint8_t)ls;
        if ((uint8_t)(w - r) > 8u) {                     /* (more than the ring holds since the last pass: the last 8) */
            vf.lost += (uint8_t)(w - r) - 8u;
            r = (uint8_t)(w - 8u);
        }
        RING_PUBLISH();
        for (; (uint8_t)(w - r); r++)
            if (want && notes)
                vf_ev(VFE_NOTE, k, vf_nr[k][r & 7u] & 127u, vf_nr[k][r & 7u] >> 8);
        vf.nr[k] = (uint8_t)w;
    }
}
static void vf_events_send(void)
{
    uint32_t i = 0, j;
    while (i < vf.nev) {
        vf_begin(VFT_EVENTS);
        for (j = 0; j < VF_EVF && i < vf.nev; j++, i++) {
            vf_put(vf.ev[i][0]);
            vf_put(vf.ev[i][1]);
            vf_put(vf.ev[i][2]);
        }
        if (vf_send()) {
            vf.lost += vf.nev - i + j;                   /* (the ring full: these are gone, the next ones not late) */
            break;
        }
    }
    vf.nev = 0;
}

/* -------------------------------------------------------------------------------------------- the host --- */
static void vf_hello(uint32_t status)
{
    static const char V[] = FELUCCA_VERSION;
    uint32_t i;
    vf.pass_left = 1;
    vf_begin(VFT_HELLO);
    vf_put(status);
    vf_put(VF_VER);
    vf_put(NTRK);
    vf_put(trk_live);
    vf_put(vf.rate);
    vf_put(VF_T1); vf_put(VF_T2); vf_put(VF_G1); vf_put(VF_G2);
    for (i = 0; V[i] && i < 15u; i++)
        vf_put((uint32_t)V[i] & 127u);
    vf_put(0);
    vf.hello = (uint8_t)(vf_send() != 0);                /* (the ring full: again on the next pass) */
}
static void vf_start(void)                               /* subscribed (again): everything is new to the host */
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < VF_TN; i++)
            vf.tv[k][i] = VF_NONE;
    for (i = 0; i < VF_GN; i++)
        vf.gv[i] = VF_NONE;
    vf.keyed = 0;
    vf.due = (uint8_t)((1u << (NTRK + 1u)) - 1u);
    vf.st_ms = vf.key_ms = fm1_ms;
    vf.nev = 0;
    vf_q.n = 0;
    vf_events(0, 0);                                     /* (what is now is no event: the ages, the beat seen) */
}
/* a SysEx frame (7-bit bytes between F0 and F7) for us: SUB. 1 = it was ours */
static int vf_rx(const uint8_t *p, uint32_t n)
{
    if (n < 5u || p[0] != 0x7Du || p[1] != VF_ID1 || p[2] != VF_ID2)
        return 0;
    if (p[3] >= 1u && p[4] == VFT_SUB) {
        uint32_t rate = n > 5u ? p[5] : VF_RATE, was = vf.sub;
        vf.flags = (uint8_t)(n > 6u && (p[6] & 15u) ? p[6] & 15u : 15u);
        vf.rate = (uint8_t)(rate > 60u ? 60u : rate);
        vf.sub = (uint8_t)(rate && !vis_feed_off);
        vf.sub_ms = fm1_ms;
        if (vf.sub && !was)
            vf_start();
        vf.hello = 1;                                    /* (answered below, status: streaming / OFF / unsubscribed) */
        vf_hello(vis_feed_off ? 1u : vf.sub ? 0u : 2u);
    }
    return 1;
}
static void vf_take(void)
{
    const uint8_t *p;
    uint32_t n;
    if (!ota_frame_get(&p, &n))
        return;
    if (vf_rx(p, n))
        ota_frame_done();                                /* (not ours: the update handshake's, ota_service) */
}

/* the main loop's pass (main.c, after the editor, before the update handshake) */
static void vf_service(void)
{
    uint32_t now = fm1_ms, k, mut, u;
    int open = 0;
    vf_take();
    if (vf.sub && (!usb.config || vis_feed_off || now - vf.sub_ms > VF_SUB_MS)) {   /* unplugged, OFF, the host gone */
        if (vis_feed_off && usb.config) {
            vf.pass_left = 1;
            vf_begin(VFT_BYE);
            (void)vf_send();
        }
        vf.sub = 0;
    }
    vf_q.on = vf.sub && (vf.flags & VF_FEV);
    if (!vf.sub || now - vf.pass_ms < VF_GAP_MS)
        return;
    vf.pass_ms = now;
    vf.pass_left = VF_PASS;
    if (vf.hello)
        vf_hello(0);
    vf_events((vf.flags & VF_FEV) != 0, (vf.flags & VF_FNOTE) != 0);
    vf_events_send();
    k = 1000u / (vf.rate ? vf.rate : VF_RATE);
    if (now - vf.st_ms >= k) {                           /* the state's period: every unit due (on average at the rate) */
        vf.st_ms = now - vf.st_ms >= 2u * k ? now : vf.st_ms + k;
        vf.due = (uint8_t)(((vf.flags & VF_FTRK) ? (1u << NTRK) - 1u : 0u) | ((vf.flags & VF_FGLO) ? 1u << NTRK : 0u));
    }
    if (now - vf.key_ms >= VF_KEY_MS) {                  /* a keyframe: the next unit's whole state */
        vf.key_ms = now;
        vf.key = (uint8_t)((vf.key + 1u) % (NTRK + 1u));
        if (((vf.key < NTRK) ? VF_FTRK : VF_FGLO) & vf.flags) {
            vf.keyed |= (uint8_t)(1u << vf.key);
            vf.due |= (uint8_t)(1u << vf.key);
        }
    }
    if (!vf.due)
        return;
    mut = mut_enabled();
    for (k = 0; k <= NTRK; k++) {                        /* round robin: the one cut short first next time */
        u = (vf.rr + k) % (NTRK + 1u);
        if (!((vf.due >> u) & 1u))
            continue;
        if (vf_unit(u, mut, &open)) {
            vf.rr = (uint8_t)u;
            open = 0;                                    /* (the frame begun stays unsent: its fields again next pass) */
            break;
        }
        vf.due &= (uint8_t)~(1u << u);
    }
    if (open)
        (void)vf_send();                                 /* (the ring full: its fields again, still changed) */
}
