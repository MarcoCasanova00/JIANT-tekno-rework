/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* (0.9.19) A VIS FEED decoder (docs/VISFEED.md), the reference for an app (web/visfeed.mjs is the same in JavaScript):
 * feed it USB-MIDI event packets (vfd_packet) or whole SysEx messages (vfd_sysex); it keeps the state the device sent
 * and hands each event to a callback. Plain C99, no allocation. tests/visfeed_test.c decodes the firmware's stream
 * with it. */
#ifndef VISFEED_DECODE_H
#define VISFEED_DECODE_H
#include <stdint.h>
#include <string.h>

#define VFD_NTRK 6
#define VFD_UNSET (-32768)
typedef struct { uint8_t kind, trk, a, b; uint16_t ms; } vfd_event_t;
typedef struct {
    int32_t trk[VFD_NTRK][0x70];   /* a track's fields by id (0x00.. one byte, 0x40.. two), VFD_UNSET until sent */
    int32_t glo[0x70];             /* the globals' */
    uint8_t status, ver, ntrk, live, rate, hello;   /* the last HELLO (hello: one seen) */
    char fw[16];
    uint32_t frames, lost, bad, events, records;    /* frames decoded, frames lost (seq gaps), malformed ones */
    int32_t seq;                   /* the last seq seen, -1 none */
    uint16_t ms;                   /* the last frame's 14-bit timestamp */
    uint8_t sx[64];                /* SysEx being assembled from packets */
    uint32_t sxn, sxon;
    void (*on_event)(void *ctx, const vfd_event_t *e);
    void *ctx;
} vfd_t;

static void vfd_init(vfd_t *d)
{
    uint32_t k, i;
    memset(d, 0, sizeof *d);
    for (i = 0; i < 0x70u; i++) {
        d->glo[i] = VFD_UNSET;
        for (k = 0; k < VFD_NTRK; k++)
            d->trk[k][i] = VFD_UNSET;
    }
    d->seq = -1;
}
/* the subscription a host sends (and again every second): F0 7D 4A 56 01 00 seq 00 00 rate flags F7 (the frame header
 * both ways: the host's seq counts its SUBs, its timestamp 0); rate 0 unsubscribes */
static uint32_t vfd_sub(uint8_t *out, uint32_t seq, uint32_t rate, uint32_t flags)
{
    static const uint8_t H[6] = {0xF0, 0x7D, 0x4A, 0x56, 1, 0};
    memcpy(out, H, 6);
    out[6] = (uint8_t)(seq & 127u);
    out[7] = out[8] = 0;
    out[9] = (uint8_t)(rate & 127u);
    out[10] = (uint8_t)(flags & 127u);
    out[11] = 0xF7;
    return 12;
}
/* one SysEx message, F0 .. F7. 1: a VIS FEED frame (decoded), 0: someone else's, -1: malformed */
static int vfd_sysex(vfd_t *d, const uint8_t *f, uint32_t n)
{
    uint32_t i, type;
    int32_t *v = 0;
    if (n < 10u || f[0] != 0xF0u || f[n - 1u] != 0xF7u || f[1] != 0x7Du || f[2] != 0x4Au || f[3] != 0x56u)
        return 0;
    for (i = 1; i + 1u < n; i++)
        if (f[i] & 0x80u) { d->bad++; return -1; }
    if (f[4] != 1u) { d->bad++; return -1; }      /* (a later version: another decoder) */
    type = f[5];
    if (d->seq >= 0 && f[6] != (uint32_t)((d->seq + 1) & 127))
        d->lost += (f[6] - (uint32_t)d->seq - 1u) & 127u;
    d->seq = f[6];
    d->ms = (uint16_t)(f[7] | f[8] << 7);
    d->frames++;
    i = 9;
    n--;                                           /* (the F7) */
    if (type == 0x01u) {                           /* HELLO */
        uint32_t k = 0;
        if (n < i + 9u) { d->bad++; return -1; }
        d->status = f[i]; d->ver = f[i + 1]; d->ntrk = f[i + 2]; d->live = f[i + 3]; d->rate = f[i + 4];
        for (i += 9; i < n && f[i] && k + 1u < sizeof d->fw; i++)
            d->fw[k++] = (char)f[i];
        d->fw[k] = 0;
        d->hello = 1;
    } else if (type == 0x10u) {                    /* STATE: a marker, then id value(s) */
        while (i < n) {
            uint32_t id = f[i++];
            if (id >= 0x70u) {
                id -= 0x70u;
                v = id < VFD_NTRK ? d->trk[id] : id == 7u ? d->glo : 0;
                continue;
            }
            if (id < 0x40u) {
                if (i >= n) { d->bad++; return -1; }
                if (v) v[id] = f[i];
                i++;
            } else {
                if (i + 1u >= n) { d->bad++; return -1; }
                if (v) v[id] = f[i] | f[i + 1u] << 7;   /* (a track's E0..E7 are signed: - 8192) */
                i += 2;
            }
            d->records++;
        }
    } else if (type == 0x30u) {                    /* EVENTS: 3 bytes each */
        for (; i + 3u <= n; i += 3) {
            vfd_event_t e = {(uint8_t)(f[i] >> 3), (uint8_t)(f[i] & 7u), f[i + 1], f[i + 2], d->ms};
            d->events++;
            if (d->on_event)
                d->on_event(d->ctx, &e);
        }
    }                                              /* (BYE, an unknown type: nothing to keep) */
    return 1;
}
/* one USB-MIDI event packet (byte 0 = cable << 4 | CIN, then 3 bytes), as read from EP 0x81 */
static void vfd_packet(vfd_t *d, const uint8_t *p)
{
    uint32_t cin = p[0] & 15u, k, i;
    if (cin < 4u || cin > 7u)
        return;                                    /* (notes, clock: not SysEx) */
    k = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
    for (i = 0; i < k; i++) {
        uint8_t b = p[1 + i];
        if (b == 0xF0u) { d->sxn = 0; d->sxon = 1; }
        if (!d->sxon) continue;
        if (d->sxn < sizeof d->sx) d->sx[d->sxn++] = b;
        else d->sxon = 0;                          /* (too long: not a frame of ours) */
        if (b == 0xF7u) {
            (void)vfd_sysex(d, d->sx, d->sxn);
            d->sxon = 0;
        }
    }
}
#endif
