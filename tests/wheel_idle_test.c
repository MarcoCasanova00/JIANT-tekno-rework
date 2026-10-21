/* SPDX-License-Identifier: GPL-3.0-only
 * Compare with the unmodified CPU5 wheel_block using the same harness.
 * Move controls while silent, then check restart audio and rotor continuity.
 */
#define main hostsim_main
#include "hostsim.c"
#undef main
static uint64_t wh(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b=p;
    while(n--) h=(h^*b++)*1099511628211ull;
    return h;
}
int main(void)
{
    uint32_t b, i, idle=0, active=0;
    uint64_t h=1469598103934665603ull;
    int32_t out[2*CTL]; track_t *t=&trk[0];
    host_tracks_init(); host_preset(t,ENGI_WHEEL,0);
    t->p[P_VOICE]=V_POLY; t->p[P_ATK]=t->p[P_REL]=0; t->p[P_SUS]=127;
    for(b=0;b<12000;b++) {
        uint32_t live=0;
        t->p[P_E0]=(b/31)%16; t->p[P_E1]=(int32_t)((b/17)%17)-8;
        t->p[P_E2]=(int32_t)((b/19)%17)-8; t->p[P_E3]=(int32_t)((b/23)%17)-8;
        t->p[P_E4]=(b/37)%7; t->p[P_E5]=(b/13)%128; t->p[P_E6]=(b/11)%128;
        t->p[P_E7]=(b/241)%3; t->p[P_WRDEP]=(b/29)%128; t->p[P_WDUST]=(b/43)%128;
        if(b%600==300) { trk_note_on(t,48,100); trk_note_on(t,67,80); }
        if(b%600==400) { trk_note_off(t,48); trk_note_off(t,67); }
        for(i=0;i<NVOICE;i++) live |= t->v[i].active;
        track_render(t,out,CTL);
        if(live) { active++; h=wh(h,&drw_t[0],sizeof drw_t[0]); }
        else idle++;
        h=wh(h,out,sizeof(int32_t)*CTL);
        h=wh(h,drw_t[0].rph,sizeof drw_t[0].rph);
        h=wh(h,drw_t[0].rsp,sizeof drw_t[0].rsp);
        h=wh(h,&drw_t[0].rwet,sizeof drw_t[0].rwet);
        h=wh(h,drw_v[0],sizeof drw_v[0]);
    }
    if(idle<4000 || active<1000) return 1;
    printf("wheel-idle: hash=%016llx idle=%u active=%u\n",(unsigned long long)h,idle,active);
    return 0;
}
