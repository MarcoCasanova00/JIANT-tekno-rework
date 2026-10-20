/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* (JIANT 0.9.19) Fuzz harness: the whole instrument played at random, the audio rendered all along, on one core or
 * two (mc_host: core 1's parts and buses on its own buffers), under ASan / UBSan (run_tests.sh: bounds on too). Each
 * frame any of: buttons pressed, held, let go, several at once; keys; every knob turned (1..12 detents); the transport;
 * USB-MIDI packets and TRS MIDI bytes, any status and data (notes, CC, NRPN, program change, bend, aftertouch, clock,
 * SysEx, garbage); a dialog answered; the tempo, the parameters of a part (its own and its engine's) and the song's
 * set anywhere in their ranges (what MOD, LFOs, MIDI CC and a project can reach); a part's engine changed; the clock
 * jumping. A crash, a sanitizer report or a NaN-free check failing is the bug (the random cases: SEED).
 *   fuzz_ui [FRAMES [SEED]] */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
static uint32_t fz = 0x9E3779B9u;
static uint32_t frnd(void) { fz ^= fz << 13; fz ^= fz >> 17; fz ^= fz << 5; return fz; }
#define FR(n) (frnd() % (uint32_t)(n))

static int32_t in_range(int32_t lo, int32_t hi)
{
    if (hi < lo) { int32_t t = lo; lo = hi; hi = t; }
    switch (FR(6u)) {
    case 0: return lo;
    case 1: return hi;
    default: return lo + (int32_t)FR((uint32_t)(hi - lo) + 1u);
    }
}
static void poke_part(track_t *t)                         /* one parameter of the part, anywhere in its range */
{
    uint32_t i = FR(P_COUNT);
    if (i >= P_E0 && i < P_E0 + 8u) {
        const engine_t *e = trk_engp(t);                 /* (the engine asked for: the UI's, MIDI's, a fade's next) */
        const param_desc_t *d = e->desc ? e->desc(t, i - P_E0) : 0;
        d = d ? d : &e->edit[i - P_E0];
        t->p[i] = (int16_t)in_range(d->min, d->max);
    } else {
        t->p[i] = (int16_t)in_range(TP[i].min, TP[i].max);
    }
}
static void midi_garbage(void)
{
    static const uint8_t ST[] = {0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0xF1, 0xF2, 0xF3, 0xF6, 0xF7, 0xF8,
                                 0xFA, 0xFB, 0xFC, 0xFE, 0xFF};
    uint32_t st = ST[FR(sizeof ST)], ch = FR(4u) ? FR(4u) : FR(16u), d1 = FR(8u) ? FR(128u) : FR(256u),
             d2 = FR(8u) ? FR(128u) : FR(256u), cin;
    if (st < 0xF0)
        st |= ch;
    if (st == 0xB0u + ch && FR(3u) == 0u)               /* the CCs that do something: bank, mod, NRPN, sustain, all off */
        d1 = (uint32_t)((const uint8_t[]){0, 1, 6, 7, 10, 11, 32, 38, 64, 74, 98, 99, 100, 101, 120, 121, 123}[FR(17u)]);
    if (FR(2u)) {                                       /* USB-MIDI: a packet, its CIN mostly the status's */
        cin = FR(8u) ? (st < 0xF0 ? st >> 4 : st == 0xF0 ? 4u : 15u) : FR(16u);
        midi_enqueue(cin | st << 8 | d1 << 16 | d2 << 24, 1u);
    } else {                                            /* TRS: the bytes */
        um_byte(st);
        if (FR(4u)) um_byte(d1 & 0x7Fu);
        if (FR(4u)) um_byte(d2 & 0x7Fu);
        if (st == 0xF0u) { uint32_t k = FR(40u); while (k--) um_byte(FR(128u)); um_byte(0xF7u); }
    }
}

/* every parameter of every part inside its ranges (the engine's own and, for MOD, its edit table's): what is out names
 * the step that put it there (a report, then on) */
static uint32_t out_seen;
static void ranges(uint32_t f, const char *what)
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        const engine_t *e = trk_engp(t);
        for (i = 0; i < P_COUNT; i++) {
            int32_t lo, hi, v = t->p[i];
            if (i >= P_E0 && i < P_E0 + 8u) {
                const param_desc_t *d = e->desc ? e->desc(t, i - P_E0) : 0, *ed = &e->edit[i - P_E0];
                d = d ? d : ed;
                lo = d->min < ed->min ? d->min : ed->min;
                hi = d->max > ed->max ? d->max : ed->max;
            } else {
                lo = TP[i].min;
                hi = TP[i].max;
                if (i == P_LRATE) lo = lo < -128 ? lo : -128;
            }
            if ((v < lo || v > hi) && out_seen < 40u) {
                out_seen++;
                printf("fuzz_ui: frame %u after %s: T%u %s p[%u] (%s) = %d outside %d..%d\n", f, what, k + 1u, e->name, i,
                       i >= P_E0 && i < P_E0 + 8u ? e->edit[i - P_E0].label : TP[i].label, v, lo, hi);
            }
        }
    }
}

int main(int argc, char **argv)
{
    static int32_t o[CTL * 2];
    uint32_t frames_n = argc > 1 ? (uint32_t)atoi(argv[1]) : 200000u, f, k, held = 0, blocks = 0, notes = 0, midi = 0;
    if (argc > 2) fz = (uint32_t)atoi(argv[2]) * 2654435761u | 1u;
    setvbuf(stdout, NULL, _IONBF, 0);
    ui_power_on();
    ui_prefs &= ~PREF_ACCEL_OFF;                         /* (the device's knob acceleration: big jumps) */
    for (f = 0; f < frames_n; f++) {
        uint32_t r = FR(1000u);
        const char *what = "a frame";
        if (r < 60u) { what = "a button";                                  /* a button down or up, sometimes several */
            uint32_t b = 1u << panel.btn[FR(NB)];
            if (held & b) held &= ~b; else { held |= b; host_pressed |= b; }
            if (FR(20u) == 0u) held = 0;
        } else if (r < 160u) { what = "a knob";                          /* a knob */
            uint32_t e = FR(NE);
            int32_t s = (int32_t)(FR(5u) ? 1u + FR(3u) : 1u + FR(12u));
            host_enc[panel.enc[e]] += FR(2u) ? s : -s;
        } else if (r < 220u) { what = "a key";                          /* a key down or up */
            uint32_t n = 1u << FR(24u);
            if (fm1_in.notes & n) fm1_in.notes &= ~n; else { fm1_in.notes |= n; host_notes |= n; notes++; }
        } else if (r < 300u) { what = "MIDI";                          /* MIDI in */
            k = 1u + FR(6u);
            while (k--) { midi_garbage(); midi++; }
        } else if (r < 306u) {
            transport_req = song.playing ? 2u : 1u;
        } else if (r < 330u) { what = "a poke";
            poke_part(&trk[FR(NTRK)]);
        } else if (r < 340u) {
            uint32_t g = FR(G_COUNT);
            song.g[g] = (int16_t)in_range(GP[g].min, GP[g].max);
        } else if (r < 342u) { what = "an engine change";
            track_t *t = &trk[FR(NTRK)];                 /* (as the UI does it: each its own load, ui.c set_engine_of) */
            set_engine_of(t, FR(NENGINES));
            apply_preset_to(t, FR(trk_engp(t)->npresets ? trk_engp(t)->npresets : 1u));
        } else if (r < 344u) {
            mc_host = (uint8_t)!mc_host;                 /* one core / two */
        } else if (r < 346u) {
            fm1_ms += 1000u + FR(60000u);                /* the clock jumps (a long frame, a wrap) */
        }
        if (ui.confirm && FR(8u) == 0u) {
            if (FR(2u)) { held |= 1u << panel.btn[B_OCTUP]; host_pressed |= 1u << panel.btn[B_OCTUP]; }
            else { ui.confirm = 0; ui.force = 1; }
        }
        if (held & (1u << panel.btn[B_OCTDN]) && held & (1u << panel.btn[B_OCTUP]) && FR(4u))
            held &= ~(1u << panel.btn[B_OCTDN]);         /* (both OCT held long: the setup / UBOOT, not the UI's) */
        fm1_in.buttons = held;
        host_ticks += 16000u;
        fm1_ms += 16u;
        ui_input();
        ranges(f, what);
        for (k = 0; k < FS * 16u / 1000u / CTL; k++) {   /* the audio of the frame */
            uint32_t i;
            mix_block(o, CTL);
            blocks++;
            for (i = 0; i < 2u * CTL; i++)
                if (o[i] > 0x7FFFFFF || o[i] < -0x7FFFFFF) {
                    printf("fuzz_ui: frame %u: an output sample %d out of any range\n", f, o[i]);
                    return 1;
                }
        }
        ranges(f, "the audio");
        ui_leds();
        ui_draw();
        if (FR(20000u) == 0u) {                          /* now and then from the start */
            stop_transport();
            held = 0;
            fm1_in.buttons = fm1_in.notes = 0;
            ui_power_on();
            xf_snap_t = 0xFFu;                           /* (the device's power-on: .data) */
            ui_prefs &= ~PREF_ACCEL_OFF;
        }
    }
    printf("fuzz_ui ok: %u frames, %u audio blocks, %u key presses, %u MIDI messages\n", frames_n, blocks, notes, midi);
    return 0;
}
