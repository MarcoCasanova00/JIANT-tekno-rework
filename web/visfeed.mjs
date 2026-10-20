// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
//
// (0.9.19) VIS FEED: a decoder of the FM-1's state stream for an app's visuals (docs/VISFEED.md; the firmware side is
// firmware/src/visfeed.c, the C reference tests/visfeed_decode.h: this is the same in JavaScript, so a web page or a
// Node tool can read what the JIANT Stems app reads). No dependencies.
//   const d = new VisFeed(ev => ...);   d.packet(usbMidiEventPacket4Bytes) or d.sysex(bytesF0toF7)
//   d.trk[k][id], d.glo[id]: the fields by id (null until sent); d.hello, d.frames, d.lost, d.events
//   VisFeed.sub(seq, rate, flags): the SUB message a host sends (and again every second)

export const VF = {
  VER: 1, NTRK: 6, SUB: 0x00, HELLO: 0x01, BYE: 0x02, STATE: 0x10, EVENTS: 0x30,
  F_TRK: 1, F_GLO: 2, F_EV: 4, F_NOTE: 8,
  EV: ["NOTE", "RATCH", "ROLL", "MUTATE", "DICE", "BEAT", "TRANSPORT", "SECTION", "FX", "SOUND"],
  T1: ["ENG", "FLAGS", "PRESET", "LEVEL", "PAN", "CUT", "RES", "ENV", "VOICES", "LFO", "TRANS", "ROOT", "SCALE", "ARP",
       "ARATE", "AORD", "AOCT", "ASTEP", "ANOTE", "CHORD", "CVOI", "CHN", "MORPH", "STEP", "SLEN", "HEAD1", "HEAD2",
       "HEAD3", "GHOST", "RATCH", "MUTON", "MAMT", "PFX", "DIST", "CHOR", "DLY", "REV", "LOOPS", "COMP", "COMP2",
       "LRATE", "LWAVE", "LPHASE", "MS", "ATK", "DEC", "SUS", "REL"],
  T2: ["E0", "E1", "E2", "E3", "E4", "E5", "E6", "E7"],
  G1: ["SEL", "TRANSPORT", "BEAT", "PHASE", "BAR", "SECTION", "ROW", "KOCT", "FXSTEPS", "FXTRANS", "FXMUT", "FXFILT",
       "FXAMT", "CRUSH", "THROW", "DJF", "FXTGT", "LIVE", "CPU", "TAPETIME", "TAPECURVE", "DMIX", "DFDBK", "RSIZE",
       "SLMODE", "SLDEP", "PUNCH"],
  G2: ["BPM", "FXON0", "FXON1"],
  ENG: { 0: "ANALOG", 2: "PHASE", 3: "LOFI", 5: "VOICE", 7: "WHEEL", 10: "DRUM-X", 11: "NOISE", 12: "FM6", 14: "ANIMAL" },
  PF: ["REPEAT 1/8", "REPEAT 1/16", "REPEAT 1/32", "GLITCH", "TAPE STOP", "OCT-", "OCT+", "1/2 TEMPO", "DEC-", "DEC+",
       "STUTTER 1/8", "STUTTER 1/16", "ATK+", "ARP", "RANDOM", "SHUFFLE", "CRUSH", "THROW", "FREEZE",
       "MUTE T1", "MUTE T2", "MUTE T3", "MUTE T4", "MUTE T5", "MUTE T6"],
};

export class VisFeed {
  constructor(onEvent) {
    this.onEvent = onEvent || null;
    this.trk = Array.from({ length: VF.NTRK }, () => new Array(0x70).fill(null));
    this.glo = new Array(0x70).fill(null);
    this.hello = null;                  // {status, ver, ntrk, live, rate, nt1, nt2, ng1, ng2, fw}
    this.frames = 0; this.lost = 0; this.bad = 0; this.events = 0; this.records = 0;
    this.seq = -1; this.ms = 0;
    this.sx = []; this.sxOn = false;
  }
  static sub(seq, rate = 30, flags = 0) {
    return Uint8Array.from([0xF0, 0x7D, 0x4A, 0x56, VF.VER, VF.SUB, seq & 127, 0, 0, rate & 127, flags & 127, 0xF7]);
  }
  /** one USB-MIDI event packet (4 bytes: cable << 4 | CIN, then 3) */
  packet(p) {
    const cin = p[0] & 15;
    if (cin < 4 || cin > 7) return;
    const k = cin === 4 || cin === 7 ? 3 : cin === 6 ? 2 : 1;
    for (let i = 0; i < k; i++) {
      const b = p[1 + i];
      if (b === 0xF0) { this.sx = []; this.sxOn = true; }
      if (!this.sxOn) continue;
      if (this.sx.length < 64) this.sx.push(b); else this.sxOn = false;
      if (b === 0xF7) { this.sysex(this.sx); this.sxOn = false; }
    }
  }
  /** one SysEx message F0 .. F7: 1 a VIS FEED frame, 0 someone else's, -1 malformed */
  sysex(f) {
    const n = f.length - 1;
    if (f.length < 10 || f[0] !== 0xF0 || f[n] !== 0xF7 || f[1] !== 0x7D || f[2] !== 0x4A || f[3] !== 0x56) return 0;
    for (let i = 1; i < n; i++) if (f[i] & 0x80) { this.bad++; return -1; }
    if (f[4] !== VF.VER) { this.bad++; return -1; }
    const type = f[5];
    if (this.seq >= 0 && f[6] !== ((this.seq + 1) & 127)) this.lost += (f[6] - this.seq - 1) & 127;
    this.seq = f[6];
    this.ms = f[7] | f[8] << 7;
    this.frames++;
    let i = 9;
    if (type === VF.HELLO) {
      if (n < i + 9) { this.bad++; return -1; }
      const h = { status: f[i], ver: f[i + 1], ntrk: f[i + 2], live: f[i + 3], rate: f[i + 4],
                  nt1: f[i + 5], nt2: f[i + 6], ng1: f[i + 7], ng2: f[i + 8], fw: "" };
      for (i += 9; i < n && f[i]; i++) h.fw += String.fromCharCode(f[i]);
      this.hello = h;
    } else if (type === VF.STATE) {
      let v = null;
      while (i < n) {
        const id = f[i++];
        if (id >= 0x70) { const u = id - 0x70; v = u < VF.NTRK ? this.trk[u] : u === 7 ? this.glo : null; continue; }
        if (id < 0x40) { if (i >= n) { this.bad++; return -1; } if (v) v[id] = f[i]; i++; }
        else { if (i + 1 >= n) { this.bad++; return -1; } if (v) v[id] = f[i] | f[i + 1] << 7; i += 2; }
        this.records++;
      }
    } else if (type === VF.EVENTS) {
      for (; i + 3 <= n; i += 3) {
        const e = { kind: f[i] >> 3, trk: f[i] & 7, a: f[i + 1], b: f[i + 2], ms: this.ms };
        this.events++;
        if (this.onEvent) this.onEvent(e);
      }
    }
    return 1;
  }
  /** a track's field by name: the value as the synth has it (E0..E7 signed, PAN / TRANS / LFO centred on 0) */
  track(k, name) {
    let id = VF.T1.indexOf(name);
    if (id >= 0) {
      const v = this.trk[k][id];
      return v === null ? null : name === "PAN" || name === "TRANS" || name === "LFO" || name === "MS" ? v - 64 : v;
    }
    id = VF.T2.indexOf(name);
    if (id < 0) return undefined;
    const v = this.trk[k][0x40 + id];
    return v === null ? null : v - 8192;
  }
  global(name) {
    let id = VF.G1.indexOf(name);
    if (id >= 0) { const v = this.glo[id]; return v === null ? null : name === "DJF" || name === "KOCT" || name === "TAPECURVE" ? v - 64 : v; }
    id = VF.G2.indexOf(name);
    if (id < 0) return undefined;
    const v = this.glo[0x40 + id];
    return v === null ? null : name === "BPM" ? v / 10 : v;
  }
  /** the punch-in / FX-layer effects running now, by name (FXON0 / FXON1) */
  fxOn() {
    const a = this.glo[0x41] || 0, b = this.glo[0x42] || 0, m = a | b << 14;
    return VF.PF.filter((_, i) => (m >> i) & 1);
  }
}
