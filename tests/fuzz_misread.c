/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* (JIANT 0.9.19) The second core's known fault (hal/fm1_multicore.h, Melodee #17): now and then a 32-bit read gives
 * 0x00200000 where the word holds 0 (bit 21 set), more often under load. What core 1 then writes back lands in the
 * state of the parts it renders (a track, its voices, its engine's and filters' state), and core 0 renders those parts
 * the next block it takes them (fx.c mc_split): a wrong word there must give a wrong sound for a while, never a read
 * or a write outside a table. This harness plays the power-on song on two cores' worth of parts, and between blocks
 * ORs bit 21 (or another single bit) into a random word of a part's state, then renders on, under ASan / UBSan (bounds
 * on): every site that indexes with such a word unchecked is reported.
 *   fuzz_misread [ITERATIONS [SEED]] */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
static uint32_t mz = 0x2545F491u;
static uint32_t mrnd(void) { mz ^= mz << 13; mz ^= mz >> 17; mz ^= mz << 5; return mz; }

/* the parts' state core 1 writes as it renders them (voice.c, the engines, sing.c, mod.c's matrix): (base, bytes) */
static struct { void *p; uint32_t n; const char *name; } REG[24];
static uint32_t nreg;
static void reg(void *p, uint32_t n, const char *name) { REG[nreg].p = p; REG[nreg].n = n; REG[nreg++].name = name; }

int main(int argc, char **argv)
{
    static int32_t o[2 * CTL];
    uint32_t iters = argc > 1 ? (uint32_t)atoi(argv[1]) : 20000u, it, k, r, hits[24] = {0};
    if (argc > 2) mz = (uint32_t)atoi(argv[2]) * 2654435761u | 1u;
    setvbuf(stdout, NULL, _IONBF, 0);
    reg(trk, sizeof trk, "trk (the tracks, their voices)");
    reg(sing, sizeof sing, "sing (VOICE's words)");
    reg(sing_x, sizeof sing_x, "sing_x");
    reg(sing_w, sizeof sing_w, "sing_w");
    reg(lyc, sizeof lyc, "lyc");
    reg(drw_t, sizeof drw_t, "drw_t (WHEEL)");
    reg(drw_v, sizeof drw_v, "drw_v (WHEEL's voices)");
    reg(comb_w, sizeof comb_w, "comb_w (COMB)");
    reg(tf_ic1, sizeof tf_ic1, "tf_ic1");
    reg(tf_xb, sizeof tf_xb, "tf_xb");
    reg(mod_c, sizeof mod_c, "mod_c (the matrix, a core each)");
    reg(fm6_lfo_v, sizeof fm6_lfo_v, "fm6_lfo_v");
    reg(&fx, sizeof fx, "fx (the buses: core 1 runs them)");
    reg(&dlo, sizeof dlo, "dlo (the delay's rate filters)");
    reg(cl_g, sizeof cl_g, "cl_g (CLOUD's grains)");
    reg(fm6_bus_c, sizeof fm6_bus_c, "fm6_bus_c");
    reg(mp_lvl, sizeof mp_lvl, "mp_lvl (the parts' level ramps)");
    for (it = 0; it < iters; it++) {
        uint32_t w, bit;
        if (it % 400u == 0u) {                          /* a song: the power-on's, playing, every track live; the buses
                                                         * on: each reverb model, the delay long (its line at a lower rate) */
            ui_power_on();
            song.g[G_RTYPE] = (int16_t)(it / 400u % 5u);
            song.g[G_DTIME] = (int16_t)(GP[G_DTIME].min + (int32_t)(mrnd() % (uint32_t)(GP[G_DTIME].max - GP[G_DTIME].min + 1)));
            for (k = 0; k < NTRK; k++) { trk[k].p[P_DLY] = 60; trk[k].p[P_REV] = 70; trk[k].p[P_CHOR] = 40; }
            xf_snap_t = 0xFFu;
            trk_live = NTRK;
            transport_req = 1;
            for (k = 0; k < 8u; k++)
                mix_block(o, CTL);
        }
        if (mrnd() % 6u == 0u)                           /* a note now and then (voices start and end) */
            trk_note_on(&trk[mrnd() % NTRK], 36u + mrnd() % 48u, 1u + mrnd() % 127u);
        r = mrnd() % nreg;
        if (getenv("MR_ONLY"))                           /* (one region's names: MR_ONLY=fx,dlo) */
            while (!strstr(getenv("MR_ONLY"), REG[r].name[0] == 'f' && REG[r].name[1] == 'x' ? "fx" :
                           REG[r].name[0] == 'd' ? "dlo" : REG[r].name[0] == 'c' && REG[r].name[1] == 'l' ? "cl_g" : "-"))
                r = mrnd() % nreg;
        w = (mrnd() % (REG[r].n / 4u)) * 4u;
        bit = mrnd() % 4u ? 1u << 21 : 1u << (mrnd() % 32u);
        {
            uint32_t v;
            memcpy(&v, (uint8_t *)REG[r].p + w, 4);
            v |= bit;
            memcpy((uint8_t *)REG[r].p + w, &v, 4);
        }
        hits[r]++;
        mc_host = 1;
        for (k = 0; k < 4u; k++) {
            mix_block(o, CTL);
            if (mrnd() % 64u == 0u) ui_input();
        }
    }
    for (r = 0; r < nreg; r++)
        printf("  %-36s %u words hit\n", REG[r].name, hits[r]);
    printf("fuzz_misread ok: %u words hit\n", iters);
    return 0;
}
