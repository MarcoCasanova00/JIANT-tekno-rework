# JIANT VIS FEED — protocol v1 (firmware 0.9.19)

Status: **v1 FINAL** (firmware `firmware/src/visfeed.c`, branch `vis-feed`, pushed as `wip/vis-feed`; the same text
lives in the repo as `docs/VISFEED.md`). Readers: the JIANT Stems app's visuals agent (app_needs.md read and
reconciled: see §9, "Reply to the app").

Decoders you can build against: `tests/visfeed_decode.h` (C, the reference, used by `tests/visfeed_test.c` against the
real firmware code) and `web/visfeed.mjs` (JavaScript, the same, checked by `web/test_web.mjs` against the stream the
C test records to `build/host/visfeed.bin`). Kotlin: port either line for line.

## 0. What already goes out of the FM-1 (audit, 0.9.18)

| Path | What | Notes |
| --- | --- | --- |
| USB-MIDI EP1 IN (0x81, bulk 64) | **MENU › MIDI › MIDI OUT** = KEYS (default): the panel keys' note on/off on the selected track's channel (track k → channel k+1). CLOCK: + F8 clock 24 ppq, FA start, FC stop (only when the tempo is internal). ALL: + every note the FM-1 plays (sequencer, arp, FUGUE heads, punch-in repeats, OCT/RANDOM applied), each track on its channel, note-offs guaranteed. OFF: nothing. | Produced in the audio ISR (seq.c `mo_*`), ring of 64 packets, dropped when full. **No CC, no program change, no SPP, no knob moves, no punch-in FX, no section changes are sent.** |
| USB-MIDI EP1 IN, SysEx | Editor replies/pushes (F0 7D 46 4C …, web/EDITOR_PROTOCOL.md) only when the web editor asks/WATCHes; M-UPGRADE handshake (F0 … 00 59 …). | Main loop, separate 64-packet SysEx ring, sent before channel messages. |
| TRS jack | Input only (hardware). No MIDI out on TRS. | |
| BLE MIDI | Does not exist (the chip's BT needs JieLi's closed SDK). | |
| USB audio | STEMS (8 ch: T1–T6, FX L R) / STEMS+IN (10 ch). | Interface 2 (iso EP 0x84). |

So before 0.9.19 a visual app could only see notes (MIDI OUT ALL) and clock. VIS FEED adds everything else.

### Opening USB-MIDI while streaming stems (Android)

In the STEMS / STEMS+IN modes there is no CDC console, so the device is: IF 0 AudioControl, **IF 1 MIDIStreaming
(class 1 / subclass 3, EP 0x01 OUT bulk 64, EP 0x81 IN bulk 64)**, IF 2 AudioStreaming (EP 0x84 iso), (+IF 3 the
host's audio out with STEMS+IN). The app already holds a `UsbDeviceConnection` and force-claims the streaming
interface. Force-claiming any interface of the card makes Linux `snd-usb-audio` disconnect the **whole** ALSA card,
MIDI included, so `android.media.midi` will NOT see the FM-1 while the app records. Therefore:

* claim IF 1 on the same connection: `conn.claimInterface(midiIf, true)` (find it by class 1 / subclass 3);
* read EP 0x81 with a queued `UsbRequest` (64-byte buffers, keep 2–4 queued) or `bulkTransfer(ep81, buf, 64, 50)`
  on its own thread; write EP 0x01 with `bulkTransfer` (the subscribe message below);
* each 4-byte group is a USB-MIDI event packet: byte 0 = cable<<4 | CIN; SysEx: CIN 4 (3 bytes, continues),
  5/6/7 (1/2/3 bytes, ends). Reassemble F0 … F7, drop anything not starting `F0 7D 4A 56`.

The device side is independent of audio: EP1 is polled by the same 2 kHz USB task as the iso endpoint, nothing in
the audio path changes.

## 1. Principles

* **Opt-in twice**: MENU › MIDI › **VIS FEED ON / OFF** (default ON) *allows* it; nothing is ever sent until a host
  subscribes with the JIANT-specific SysEx below, and the subscription lapses 3 s after the host's last SUB.
  A DAW, a drum machine or a MIDI monitor never sees a byte of it. With VIS FEED OFF the FM-1 answers a SUB with a
  HELLO whose status says OFF and sends nothing else.
* Manufacturer SysEx, non-commercial ID 0x7D, sub-ID "JV" (0x4A 0x56) — distinct from the editor's 7D 46 4C.
* Sent from the main loop (never the audio interrupt), state read without locks (word reads; a value may be one
  control block old), no allocation, a bounded budget per main-loop pass (≤ 2 frames, ≤ 32 USB-MIDI packets), and
  only while the SysEx ring is at most half full (editor replies and channel MIDI are never starved).
* Events go out on the next main-loop pass (≤ ~16–33 ms after they happen); continuous values at the subscribed rate
  (default 30 Hz, 1..60) **only when they changed** (7-bit quantised), plus a slow round-robin keyframe (one
  frame's full state every 250 ms: everything is re-sent within ~2 s) so a dropped frame heals itself.
* Every frame carries the protocol version, a sequence number (drops) and a 14-bit millisecond timestamp (the
  FM-1's clock, wraps every 16.384 s) so events can be placed against each other and against the audio.
* Forward compatible: unknown frame types are skipped; inside a state frame every field's length is given by its
  id (`id < 0x40` one byte, `id >= 0x40` two); events are always 3 bytes. New fields / events only get new ids.

## 2. Framing

```
F0 7D 4A 56  ver  type  seq  tlo thi  payload…  F7
```
* `ver` = 1. `seq` 0..127, +1 per frame sent (any type). `tlo | thi << 7` = `fm1_ms & 0x3FFF`.
* All bytes 0..127. A 2-byte field is 14 bits, LSB first (`u = b0 | b1 << 7`). **A track's** 2-byte fields (E0..E7)
  are signed and sent `u = v + 8192` (as the editor protocol); **the globals'** 2-byte fields (BPM, FXON0/1) are
  unsigned and sent as they are (BPM = `u`, tempo × 10; FXON = the raw bit mask).
* A frame is at most 48 bytes (16 USB-MIDI packets).

### Host → FM-1

| type | name | payload |
| --- | --- | --- |
| 0x00 | SUB | the same header (`seq` the host's own count, `tlo thi` 0), then `rate` (Hz 1..60; **0 = unsubscribe**), `flags` (bit0 track state, bit1 global state, bit2 events, bit3 note events inside events; **0 = all**) |

So a SUB is exactly `F0 7D 4A 56 01 00 seq 00 00 rate flags F7` (12 bytes). Send it at start and then every ~1 s
(keepalive; the subscription lapses 3000 ms after the last one). Each SUB gets a HELLO back; the first one (or one
after a lapse) also makes the FM-1 re-send its whole state. The FM-1 drops its subscription at once when VIS FEED is
turned OFF in the MENU (it sends BYE) or when USB is unplugged.

### FM-1 → host

| type | name | payload |
| --- | --- | --- |
| 0x01 | HELLO | `status` (0 streaming, 1 VIS FEED is OFF in the MENU, 2 unsubscribed), `ver`(1), `ntrk`(6), `tracks playing` (4 or 6: MENU › TRACKS), `rate`, `nt1` (track 1-byte fields: 0x30), `nt2` (track 2-byte fields: 8), `ng1` (0x1B), `ng2` (3), then the firmware version ASCII, 0-terminated (e.g. `v0.9.19`) |
| 0x02 | BYE | (none) — VIS FEED was turned OFF while subscribed |
| 0x10 | STATE | records: a **marker** `0x70 + k` says the records after it are track k's (`0x77`: the globals'); then `id value` (1 byte for `id < 0x40`, 2 for `0x40..0x6F`). Only the fields that changed, or all of a unit's in a keyframe. One frame may hold several units; a unit's state may span several frames. |
| 0x30 | EVENTS | 3-byte records `kt a b`: `kt = kind << 3 | trk` (`trk` 0..5, 7 = global) |

A STATE unit is sent whole (all its fields) in a keyframe: one unit every 250 ms in a round of 7 (6 tracks + globals),
so the whole state is re-sent within 1.75 s; it is also sent whole right after a (re)subscription.

## 3. Track fields (TRACK frames)

1-byte fields (0..127):

| id | name | meaning |
| --- | --- | --- |
| 0x00 | ENG | engine id (below) |
| 0x01 | FLAGS | bit0 MUTE, bit1 asleep (past MENU › TRACKS 4), bit2 silenced by solo / an FX mute key, bit3 REC armed, bit4 selected, bit5 DRUM kit, bit6 modified by GLO › LIVE |
| 0x02 | PRESET | the sound's preset number (& 127) |
| 0x03 | LEVEL | the track's LEVEL knob 0..127 |
| 0x04 | PAN | PAN + 64 (0 left, 64 centre, 127 right) |
| 0x05 | CUT | filter cutoff **as heard** 0..127: the knob (ANALOG's CUT, otherwise the part filter's CUT) + LFO→FILTER × LFO + ENV→FILTER × envelope (the modulation matrix not included) |
| 0x06 | RES | resonance 0..127 |
| 0x07 | ENV | amplitude envelope now: the loudest sounding voice, 0..127 |
| 0x08 | VOICES | `stage << 4 | n`: n voices sounding (0..8), stage of the newest (0 off, 1 attack, 2 decay/sustain, 3 release, 4 fading) |
| 0x09 | LFO | LFO output now, after FADE: 64 + value (1..127 ≈ −1..+1) |
| 0x0A | TRANS | 64 + semitones the track is moved by: TRANS + the arp's TRNS keys + punch-in OCT−/OCT+ |
| 0x0B | ROOT | 0..11 (C..B) |
| 0x0C | SCALE | scale id (below) |
| 0x0D | ARP | ARP MODE (0 OFF; below) |
| 0x0E | ARATE | ARP RATE (division id, below) |
| 0x0F | AORD | ARP ORDER (0 NOTE, 1 PLAY) |
| 0x10 | AOCT | ARP OCT 1..4 |
| 0x11 | ASTEP | the arp's step counter (& 127) |
| 0x12 | ANOTE | the note the arp sounds (0 none) |
| 0x13 | CHORD | SCL › CHORD type (0 OFF; below) |
| 0x14 | CVOI | voicing (0 CLOSE, 1 LEAD, 2 OPEN, 3 +BASS) |
| 0x15 | CHN | notes in that chord (0 off, 2..4) |
| 0x16 | MORPH | MORPH as the engine plays it (slewed) 0..127 |
| 0x17 | STEP | main playhead: the step playing 0..63 |
| 0x18 | SLEN | pattern length 1..64 |
| 0x19 | HEAD1 | PLAYHEADS (SEQ › PLAYHEADS / FUGUE) extra head 1: its step, 127 = off |
| 0x1A | HEAD2 | head 2 |
| 0x1B | HEAD3 | head 3 |
| 0x1C | GHOST | GLO › LIVE's ghost playhead step while the track is modified, 127 = none |
| 0x1D | RATCH | ratchet / ARP-spill hits still to come (0 none) |
| 0x1E | MUTON | MUTATE on (1) / off (0) |
| 0x1F | MAMT | MUTATE AMOUNT 0..127 |
| 0x20 | PFX | punch-in effects acting on the track now: bit0 RANDOM, bit1 1/2 TEMPO, bit2 a repeat (STUTTER/ARP), bit3 a decay change (DEC−/DEC+), bit4 ATK+ |
| 0x21 | DIST | DIST send |
| 0x22 | CHOR | CHORUS send |
| 0x23 | DLY | DELAY send |
| 0x24 | REV | REVERB send |
| 0x25 | LOOPS | the pattern's loops since PLAY (& 127) |
| 0x26 | COMP | component of the step playing (CP id, below; 0 none) |
| 0x27 | COMP2 | its second component |
| 0x28 | LRATE | LFO RATE: 0..11 in time with the tempo (4BAR 2BAR 1BAR 1/2 1/4 1/8D 1/4T 1/8 1/8T 1/16 1/16T 1/32: `11 − v` of params.c N_LSYNC), 12 + the free rate knob (0..127, capped at 127: the knob's Hz curve is F_LFOHZ) |
| 0x29 | LWAVE | LFO WAVE (0 SIN 1 TRI 2 SAW 3 SQR 4 S&H …) |
| 0x2A | LPHASE | the LFO's phase now, 0..127 = one cycle: run the same LFO locally and re-lock on each frame |
| 0x2B | MS | the modulation sequence's level now, 64 + value |
| 0x2C | ATK | the amp envelope's ATK 0..127 |
| 0x2D | DEC | DEC |
| 0x2E | SUS | SUS |
| 0x2F | REL | REL |

2-byte fields (14-bit, value + 8192):

| id | name | meaning |
| --- | --- | --- |
| 0x40..0x47 | E0..E7 | the engine's 8 EDIT macros (P_E0..P_E7; their meaning per engine: the editor's DESC, e.g. ANALOG E0 WAVE … E4 CUT E5 RES) |

## 4. Global fields (GLOBAL frame)

1-byte:

| id | name | meaning |
| --- | --- | --- |
| 0x00 | SEL | selected track 0..5 |
| 0x01 | TRANSPORT | bit0 playing, bit1 a track REC armed, bit2 SONG mode, bit3 the song / chain running, bit4 external clock |
| 0x02 | BEAT | beat of the bar 0..3 |
| 0x03 | PHASE | position in the beat 0..127 |
| 0x04 | BAR | bars since PLAY & 127 |
| 0x05 | SECTION | section playing: 0 none, 1..4 = A..D |
| 0x06 | ROW | the song row playing (0..) |
| 0x07 | KOCT | the keyboard's octave + 64 |
| 0x08 | FXSTEPS | FX-layer macro STEPS 0..12 (0 off) |
| 0x09 | FXTRANS | FX-layer macro TRANSPOSE 0..21 |
| 0x0A | FXMUT | FX-layer macro MUTATE 0..100 |
| 0x0B | FXFILT | FX-layer macro FILTER 0..100 |
| 0x0C | FXAMT | the buffer effect's (REPEAT / GLITCH / TAPE / SHUFFLE / FREEZE) wet share 0..127 |
| 0x0D | CRUSH | the CRUSH sweep 0..127 |
| 0x0E | THROW | the THROW sweep 0..127 |
| 0x0F | DJF | DJ FILTER + 64 (below 64 low-pass, above high-pass) |
| 0x10 | FXTGT | the punch-in MIDI effects' tracks: 0 ALL, 1 SYN, 2 DRM |
| 0x11 | LIVE | GLO › LIVE: a bit per track modified |
| 0x12 | CPU | audio load % |
| 0x13 | TAPETIME | TAPE STOP's TIME knob 0..4 (1/64 1/32 1/16 1/8 1/4 of a bar: the stop's length) |
| 0x14 | TAPECURVE | its CURVE + 64 (−: drops at once, +: holds then drops; the curve is (1 − x)(1 + c·x)) |
| 0x15 | DMIX | the delay's MIX |
| 0x16 | DFDBK | the delay's FEEDBACK (0..120) |
| 0x17 | RSIZE | the reverb's SIZE |
| 0x18 | SLMODE | the SLICER's mode (0 OFF 1 GATE 2 STUT) |
| 0x19 | SLDEP | its DEPTH |
| 0x1A | PUNCH | the drum bus's PUNCH 0..100 |

2-byte:

| id | name | meaning |
| --- | --- | --- |
| 0x40 | BPM | tempo × 10, raw (as heard: an external clock's too) |
| 0x41 | FXON0 | punch-in / FX-layer effects running, raw bits 0..13 (PF ids below) |
| 0x42 | FXON1 | raw bits 14..27 (bit b here = PF id 14 + b) |

## 5. Events (EVENTS frame, 3 bytes each: `kind << 3 | trk`, a, b)

| kind | name | trk | a | b |
| --- | --- | --- | --- | --- |
| 0 | NOTE | 0..5 | note | velocity (every note a track starts: sequencer, arp, keys, MIDI in, PLAYHEADS, repeats; DRUM: the lane's note) |
| 1 | RATCH | 0..5 | hits to come | 0 a step's RATCH, 1 an ARP's spill |
| 2 | ROLL | 0..5 | lanes (bit per DRUM lane) rolling x2/x4/x8 | 0 |
| 3 | MUTATE | 0..5 | bit0 the sound, bit1 the pattern, bit2 the FX-layer MUTATE macro | AMOUNT |
| 4 | DICE | 0..5 | 0 sound/kit, 1 sequence, 2 LFO, 3 FX | 0 |
| 4 | GEN | 7 | seed >> 7 | seed & 127 (a GEN piece / DICE ALL thrown: everything changed) |
| 5 | BEAT | 7 | bar & 127 | beat 0..3 (on every beat while playing) |
| 6 | TRANSPORT | 7 | 1 PLAY, 0 STOP | 0 |
| 7 | SECTION | 7 | section 0..3 (A..D) | song row |
| 8 | FX | 7 | PF id (below) | 1 on, 0 off |
| 9 | SOUND | 0..5 | engine | preset (a sound loaded / engine changed) |

Up to 24 events are sent per pass (two EVENTS frames); notes are kept in a ring of 8 per track between passes, so a
burst of more than 8 notes on one track within ~16 ms loses the oldest (a RATCH x8 at 1/32 is the one case; the RATCH
event still says how many). NOTE events come from the audio engine's note-on, whoever played the note (sequencer, arp,
keys, MIDI IN, PLAYHEADS, repeats), so they match MIDI OUT ALL note for note.

## 6. Tables

Engines (ENG): 0 ANALOG, 2 PHASE, 3 LOFI, 5 VOICE, 7 WHEEL, 10 DRUM-X, 11 NOISE, 12 FM6, 14 ANIMAL (1, 4, 6, 8, 9,
13 retired: never sent).

SCALE: 0 CHR 1 MAJ 2 MIN 3 DOR 4 MIX 5 PEN 6 MPEN 7 HARM 8 PHRY 9 LYD 10 LOC 11 MEL 12 BLUES 13 WHOLE 14 DIMHW 15 DIMWH.

ARP MODE: 0 OFF 1 UP 2 DN 3 UPDN 4 RND 5 ORD 6 REPEAT 7 DNUP 8 UP+8 9 CONV 10 DIVG 11 PINKY 12 THUMB 13 WALK
14 CHORD 15 TRNS. ARATE: 0 1/4 1/8 1/16 1/32 8T 16T 1/2 1/1 2BAR 9 4BAR.

CHORD: 0 OFF 1 TRIAD 2 7TH 3 ADD9 4 SUS2 5 SUS4 6 MAJ 7 MIN 8 7 9 MAJ7 10 MIN7 11 POWER (CHN gives the size).

COMP (CP): 0 none 1 ARP(notes) 2 ARP(seq) 3 ARP(scale) 4 TRIG 5 RAND 6 OCT 7 TONAL 8 BEND 9 RATCH 10 PULSE 11 JUMP
12 REV 13 FILL 14 KILL 15 WALK 16 CLIMB 17 TOUCH.

PF ids (FXON bits, FX events): 0 REPEAT 1/8, 1 REPEAT 1/16, 2 REPEAT 1/32, 3 GLITCH, 4 TAPE STOP, 5 OCT−, 6 OCT+,
7 1/2 TEMPO, 8 DEC−, 9 DEC+, 10 STUTTER 1/8, 11 STUTTER 1/16, 12 ATK+, 13 ARP, 14 RANDOM, 15 SHUFFLE, 16 CRUSH sweep,
17 THROW sweep, 18 FREEZE, 19..24 mute T1..T6.

## 7. Budget and timing (measured, tests/visfeed_test.c)

* 4 tracks all playing 16ths at 120 BPM, 30 Hz, everything on: ~2.4–2.7 KB/s (≈550 frames / 8 s, 400 events). Stopped
  and still: ~20 B/s (keyframes only). Events reach the wire on the next main-loop pass (≤ 16 ms); state values at
  most one period late.
* The firmware sends at most 4 frames (64 USB-MIDI packets, 256 B) per pass and only while its SysEx ring is at most
  half full, so the web editor's replies, the update protocol and MIDI OUT (its own ring) are never delayed; if the
  host stops reading, the stream stalls at ≤ ½ ring and resumes in sync (changed fields stay marked).
* A worst pass (every field of every unit) is ~10.5 k instructions on the host (callgrind) — a few µs on the FM-1's
  main loop, 0 in the audio interrupt; the audio ISR only stores one 16-bit word per note-on.

## 8. Decoder reference

`tests/visfeed_decode.h` (C, ~120 lines) and `web/visfeed.mjs` (JS, the same API):

```js
import { VisFeed, VF } from "./visfeed.mjs";
const d = new VisFeed(e => { /* e = {kind, trk, a, b, ms}; VF.EV[e.kind] is its name */ });
// write to EP 0x01 at start and every second:
conn.bulkTransfer(epOut, VisFeed.sub(seq++, 30, 0), 12, 50);
// for every 4-byte USB-MIDI packet read from EP 0x81:
d.packet(pkt);
// then
d.track(2, "CUT")      // 0..127 as heard, null until sent
d.track(2, "E0")       // signed
d.global("BPM")        // 120.0
d.fxOn()               // ["GLITCH", "OCT+"]
d.hello, d.frames, d.lost, d.bad
```

The C one: `vfd_init(&d); d.on_event = cb; vfd_packet(&d, pkt4)` or `vfd_sysex(&d, f, n)`; `d.trk[k][id]`,
`d.glo[id]` (`VFD_UNSET` until sent), `vfd_sub(out, seq, rate, flags)` builds the SUB.

## 9. Reply to the app (app_needs.md, 2026-10-20)

1. **Global 2-byte fields are raw**: BPM = `u` (tempo × 10, not `u − 8192`); FXON0/1 raw masks. Only a track's E0..E7
   are `+ 8192`. (Flip your BPM read.)
2. **SUB framing: yes**, exactly `F0 7D 4A 56 01 00 seq 00 00 rate flags F7`; the firmware reads rate/flags at bytes
   9/10, keeps the subscription 3 s per SUB and answers each with HELLO.
3. Wishes, all in v1 now: the LFO's **LRATE / LWAVE / LPHASE** (0x28..0x2A: run the LFO locally, re-lock on each
   frame's phase — no reset events needed, the phase itself tells you), **MS** (0x2B), **ATK DEC SUS REL** (0x2C..0x2F,
   on change like any field), **TAPETIME / TAPECURVE** (global 0x13/0x14), delay MIX/FEEDBACK, reverb SIZE, SLICER
   mode/depth, PUNCH (global 0x15..0x1A). The arp's **ASTEP** is a running counter (its cycle length is the number of
   held/chord notes × AOCT octaves, in the ORDER; not a fixed 8) and **ANOTE** the note it sounds, both at the rate;
   there is no note-off event (use VOICES: n sounding, newest stage). Arp/chord changes: read ARP/ARATE/CHORD/CHN as
   fields (they change rarely; the keyframe and change detection cover them).
4. Your event map is right. Playheads: HEAD1..3 / STEP are fields at the rate (a 1/16 at 120 BPM is 125 ms, four
   state periods), their rate/direction are not sent (the app can infer the rate from consecutive steps); a RATCH event
   carries the hits to come (`a`) and whether it is an ARP spill (`b`); the step's duration is the track's division:
   1/16 at BPM unless the track's DIV differs (not sent in v1 — ask if you need it).
5. BLE MIDI does not exist on the FM-1 (closed SDK); only USB. On USB the budget is fine at 30 Hz (≈2.5 KB/s busy);
   use `flags` to drop what you do not draw, or `rate` 15–20 on a slow phone.
6. The punch-in lane's length/resolution and "next part" are not sent (the lane is the section's and the song's next
   row is only known the bar before); SECTION events + ROW carry the part changes.
