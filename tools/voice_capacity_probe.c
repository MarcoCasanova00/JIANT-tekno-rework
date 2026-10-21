/* SPDX-License-Identifier: GPL-3.0-only */
/* Allocation / scheduling diagnostic; host time is not FM1 CPU time. One scene per process. */
#define main hostsim_main
#include "../tests/hostsim.c"
#undef main
int main(int argc, char **argv)
{
    uint32_t p, i, b, sounding[NPART] = {0}, mask, c0 = 0, c1 = 0;
    int32_t out[CTL * 2];
    uint32_t e = argc > 1 && !strcmp(argv[1], "voice") ? ENGI_VOICE : ENGI_FM6;
    uint32_t parts = argc > 2 ? (uint32_t)atoi(argv[2]) : 6u;
    uint32_t notes = argc > 3 ? (uint32_t)atoi(argv[3]) : 1u;
    uint32_t dual = argc > 4 ? (uint32_t)atoi(argv[4]) : 1u;
    uint32_t cap = argc > 5 ? (uint32_t)atoi(argv[5]) : 12u;
    if (parts < 1 || parts > NPART || notes < 1 || notes > 4 || cap < 4 || cap > 12) return 2;
    host_tracks_init(); mc_host = !!dual; gov_cap = cap;
    for (p = 0; p < parts; p++) {
        host_preset(&trk[p], e, 0);
        trk[p].p[P_VOICE] = notes == 1 ? V_MONO : V_POLY;
        trk[p].p[P_SUS] = 127;
        trk[p].p[P_CHOR] = trk[p].p[P_DLY] = trk[p].p[P_REV] = 0;
        for (i = 0; i < notes; i++) trk_note_on(&trk[p], 48u + p + 3u * i, 100);
    }
    for (b = 0; b < 128; b++) mix_block(out, CTL);
    mask = mc_split(4u * MC_VW);
    for (p = 0; p < parts; p++) {
        for (i = 0; i < NVOICE; i++) sounding[p] += trk[p].v[i].active && trk[p].v[i].stage != 4u;
        if (mask >> p & 1u) c1 += sounding[p]; else c0 += sounding[p];
    }
    printf("%s parts=%u notes=%u cores=%u cap=%u busy=%u killed=%u per_track=",
           e == ENGI_VOICE ? "VOICE" : "FM6", parts, notes, dual ? 2 : 1, voice_cap(), voices_busy(), voice_kills);
    for (p = 0; p < parts; p++) printf("%s%u", p ? "," : "", sounding[p]);
    printf(" scheduler_mask=0x%02x voices_by_core=%u/%u\n", mask, c0, c1);
    return 0;
}
