# Tekno generator core

This note covers the second pass over the six tekno-family rows (TRIBE TEKNO, TEKNO, MENTAL TEKNO, ACID CORE,
HARDTEKK, PSY TRANCE). The first patch replaced six genre slots and added recipes, presets, drum maps and bass
archetypes. That made the genre names appear, but several musical decisions were still being overwritten after the
fact. This pass makes the dedicated material survive the generator and adds a small, deterministic articulation layer.

## Scope and safety

The change is generator-only:

- `firmware/src/gen_tables.c`: recipe-envelope helper, six genre rows, one acoustic-core recipe swap.
- `firmware/src/gen.c`: final-envelope policy, recipe throw policy, tekno bass articulation pass.
- `tests/gen_test.c`: genre-aware regression tests.
- Documentation only elsewhere.

No bootloader, OTA/update path, recovery path, partition table, flash layout, storage format or project format was
touched. The host emulator path still boots and renders (`web/emu/native_check.c`). See **Validation** for the exact
host checks and the one limitation: no JieLi toolchain/SDK is available in this workspace, so a full firmware image
was not built here.

## What was wrong

Measured over eight seeds per genre before the change:

| Genre | bass release | kick / bass mode | bass slides (8 seeds) |
|---|---:|---:|---:|
| TRIBE TEKNO | 40..40 | ANSWER | 16 |
| TEKNO | 40..40 | SUSTAIN | 28 |
| MENTAL TEKNO | 0..40 | SUSTAIN | 36 |
| ACID CORE | 40..40 | SUSTAIN | 33 |
| HARDTEKK | 40..43 | SUSTAIN | **0** |
| PSY TRANCE | 40..40 | SUSTAIN | **0** |

The ACID CORE bass recipe asks for a 18..32 release, but `gen_sound()` clamped every role to `GREL[role][0]` (40 for
bass). Dedicated lead/arp recipes were then overwritten by the generic lead/arp envelope shapes. The six rows also
shared essentially one kick/bass mode, and two bass archetypes never produced a slide.

## Envelope policy

`gtekno_voice(ri)` names the dedicated tekno recipes. For those recipes:

- the generic lead/arp ADSR shape is skipped;
- the final release floor is **24**, not the role floor (still inside `GREL[role][1]`).

Twenty-four is short enough to preserve a 303 / tekk gesture and long enough to stay clear of a click at the genre
tempos. Everything else keeps the existing role policy. `gtekno_genre(gi)` identifies the six rows, and the
neighbour-recipe throw (`SR_ALT`) is skipped for them, so a TRIBE throw cannot replace its tribe bass with a rock
bass and silently invalidate the envelope agreement. EXPLORE and mutant pieces still use the old throw rules.

Final measured releases after the change (non-FM6 tracks):

| Genre | final bass release | final lead | final arp |
|---|---:|---:|---:|
| TRIBE TEKNO | 24..26 | 45..68 | 24..35 |
| TEKNO | 24..30 | 40..69 | 24..33 |
| MENTAL TEKNO | 24..28 | 43..70 | 41..64 |
| ACID CORE | 24..32 | 24..31 | 24..35 |
| HARDTEKK | 24..26 | 35..58 | 24..35 |
| PSY TRANCE | 24..35 | 41..59 | 31..31 |

FM6 uses the carriers' R4 for its release (`fm6_rel_r4`); those tracks are excluded from the release assertion rather
than forced onto a value the engine does not use.

## Articulation pass

`gtekno_artic()` runs at the end of `gen_post()` for the six genres only (not EXPLORE, not mutants). It uses its own
deterministic PRNG seeded from the piece, so it does not move the shared generator stream or another part's gestures.

- If the bass has no slides, it adds one or two hinges into a following note, skipping ties. It usually avoids the
  downbeat so the kick keeps its weight.
- If the bass has fewer than two accents, it adds offbeat accents, never on a quarter.
- MENTAL TEKNO may hold one phrase tail into a following rest, but never over a chord change or a kick.

Measured after the change: 2267 bass notes, 468 accents, 140 slides across 48 pieces. Every piece has at least two
accents and one slide. Before the pass HARDTEKK and PSY had zero slides.

This is deliberately a grammar, not a fixed pattern: it only fills a missing gesture and leaves existing accents and
slides alone.

## Kick / bass handovers

The six rows now use four distinct modes:

| Genre | Mode | Behaviour |
|---|---|---|
| TRIBE TEKNO | `KM_INTERLOCK` | the bass answers the step after the kick |
| TEKNO | `KM_DOUBLE` | the bass doubles the kick when it rests |
| MENTAL TEKNO | `KM_SUSTAIN` | the bass sustains through the kick, ducked |
| ACID CORE | `KM_ANSWER` | the bass stops on the kick's step |
| HARDTEKK | `KM_DOUBLE` | hard doubling against the kick |
| PSY TRANCE | `KM_SUSTAIN` | rolling sustained bass under the kick |

ACID CORE's arp recipe moved from `SR_ACIDCORE` to `SR_TEK_ARP`; the bass and lead still use the dedicated 303
recipe, but the third acid layer no longer stacks the same envelope a third time.

The regression test requires four distinct modes, no empty downbeat, at least 87 % of quarters on each row (ACID
CORE's last-bar fill and TRIBE TEKNO's half-time throw are the deliberate holes) and at least 94 % over all 48
pieces. The holes are not smoothed away.

## Determinism and rollback

- Same settings still produce the same piece. The articulation stream is local and seeded from `gp.seed`, `gi` and a
  fixed salt.
- No new global random draws were inserted into the shared streams except the already existing recipe throw draw,
  which is still consumed but ignored for the six rows.
- To roll back, revert the three source files (`gen_tables.c`, `gen.c`, `gen_test.c`) and this note. There is no
  migration, stored-format change or one-way action.
- Existing non-tekno genres keep their old code paths.

## Validation

Host tests (this workspace):

```sh
mkdir -p build/gen build/host build/emu
# generate the headers into build/gen as documented in tests/gen_test.c
cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/gen_test tests/gen_test.c -lm
./build/host/gen_test
```

Result: `gen: all passed` (72 checks).

Host emulator boot/render:

```sh
cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o build/emu/native_check web/emu/native_check.c -lm
./build/emu/native_check /tmp/after.f32
```

Result: boots, renders 176400 float samples, peak 0.271, RMS 0.058; no clipping. This exercises the firmware sources
through the emulator's host path, but it is not a hardware test.

Not verified here:

- a full FM-1 firmware image (no JieLi toolchain / AC79 SDK in the workspace);
- listening tests, matched-loudness A/B, or hardware voice-count/timing checks;
- on-device flash/update behaviour (intentionally out of scope).

Run `tests/run_tests.sh` in an environment with the firmware build artefacts before publishing a release image.

## Design notes

The intent is a role and relationship, not a mandatory pattern: a kick/bass handover, a small accent/slide vocabulary,
and a recipe that owns its envelope. More notes are not the goal. The next useful step is an audition pass at matched
loudness on hardware, followed by a preset-reachability check for the named tekno voices if the preset bank changes.
