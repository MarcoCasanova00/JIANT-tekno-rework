# TODO: export modified-emulator projects into stock JIANT without flashing

**Status:** idea only. Nothing below is implemented.

## Goal

Generate a piece with the modified JIANT firmware in the browser emulator, export that
piece, and load it into the **stock, unmodified FM-1** through the web editor. No firmware
flash and no replacement of the stock firmware.

This would let the new genres (TRIBE TEKNO, MENTAL TEKNO, ACID CORE, HARDTEKK, TEKNO,
PSY TRANCE) be used as **sequence and sound generators** even though the stock firmware does
not contain their genre rows or generator recipes.

The stock firmware would not gain the new genres. It would receive the generated music:
tracks, steps, sounds, tempo, section data and — when present — motion, song chain, FM6
patches and the DRUM-X kit.

## What the stock editor can and cannot do today

Checked in the fork's `web/editor.html`, `web/fm1backup.js`, `web/EDITOR_PROTOCOL.md` and
`firmware/src/editor_backup.c`.

### 1. Sound-tab "Save to file" / "Load file"

This is a **single-track patch**, not a project.

- The file is JSON with `format: "felucca-patch"`, `version: 1`.
- It contains one selected track's engine, preset, parameters and 16 sequencer steps.
- Loading it sends `PRESET`, then visible `P_*` values, then `STEP_SET` for the steps.
- It does **not** carry: the other tracks, song chain, motion, FM6 patch, DRUM-X kit,
  project name, drum-grid hit/accent masks, chance, ratchet or lane modes.
- It is useful for a one-track sound/pattern sketch, but not for exporting a complete genre
  arrangement.

### 2. Projects tab

- Slots A–D are loaded and saved **on the device**.
- There is no local project-file picker and no "import `.funb` here" button.
- `PROJECT` SysEx op codes are only load/save/query; they do not carry project bytes.

### 3. Complete backup

- The editor can download a `felucca-backup` version 1 JSON file and restore one.
- The file contains every backup object, including:
  - `0`: current live music
  - `1`: settings
  - `2..5`: current song's project sections A–D
  - `10`: song index (arrangements/scenes)
  - `40..71`: the other songs' sections
  - user presets and their FM6 patches
- Restore writes **all** objects. It is not a selective project import.
- This is still the cleanest stock-editor-only import route if a generated project object is
  merged into a backup taken from the stock device.

### 4. Single-object import is possible in the firmware

`firmware/src/editor_backup.c` accepts `BACKUP_PUT` for object IDs `2..5` and writes only
that project section. So a small helper page could import one section without touching
settings, presets, songs or the other sections.

The stock editor does not expose that as a UI action, but it is a safer route than a full
backup restore.

## Recommended export unit

Use a **FUNB project object**: 3,840 bytes, magic `46 55 4E 42` (`FUNB`).

A project section is self-contained:

- every track's parameters, engine, preset and packed steps
- song chain
- motion/automation and parameter locks
- all four FM6 patches
- the DRUM-X kit
- punch-in lane, macros, lyrics and project name

That is exactly what the stock firmware needs to play the generated piece. The stock
firmware does not need the genre tables to play an exported project.

## Proposed workflow

### A. In the modified browser emulator

Add a small generator/export panel to `web/emu/index.html` (or a separate page):

1. Genre dropdown populated from `web_gen_name()` / `web_gen_count()`.
2. Seed, energy and form controls (`LOOP`, `PARTS`, `SONG`).
3. **Generate** calls `web_gen(genre, seed, energy, form)`.
4. **Export project** captures the current music as a FUNB object.

The WASM build already exposes the needed pieces:

- `web_gen`, `web_gen_last`, `web_gen_count`, `web_gen_name`
- `web_bk_buf`, `web_bk_max`
- `web_bk_len(0)` — current live music, packed as a FUNB project
- `web_bk_len(40..43)` — current song's sections A–D (the browser build exposes
  `40..43`, not the editor backup IDs `2..5`)
- `web_bk_len(10)` — song index

A first implementation can export only the live section (`web_bk_len(0)`) to keep the
compatibility surface small. Multi-section `PARTS`/`SONG` exports need all used sections
plus the chain.

Output options:

1. **Raw project:** `genre-seed.funb` (3,840 bytes), for inspection and tests.
2. **Stock-merge backup:** a copy of a stock backup with one object `2..5` replaced by the
   FUNB bytes, its `size` set to 3,840, and its CRC-32 recomputed. Every other stock object
   stays byte-for-byte unchanged.
3. **Single-object payload:** a small JSON containing one object ID, size, CRC-32 and base64
   data, for a future one-object importer.

### B. In the stock editor

Preferred low-risk route:

1. Connect the stock FM-1 and make a **Complete backup** first.
2. Run an offline merge tool on that backup and the exported FUNB:
   - replace only the chosen section object (`2..5`)
   - preserve all other objects, settings, presets, songs and sections
   - recompute the backup object's CRC-32
3. In the stock editor's Projects tab, restore the merged backup.
4. Load slot A–D and play.
5. If anything is wrong, restore the original stock backup.

Alternative, even less invasive route: a tiny local page that uses the existing
`BACKUP_PUT` protocol to write only object `2..5`. This avoids rewriting settings and other
music, but it is new tooling rather than the stock editor UI.

### C. Do not use the Sound-tab patch file for this

The `felucca-patch` route cannot preserve a whole arrangement, the drum grid, motion, the
FM6 patch or the DRUM-X kit. It is not the right transport for a generated genre piece.

## Compatibility risks to check

1. **Project format version.** The stock firmware must accept FUNB (3,840 bytes). Check
   `INFO` and the stock source before promising anything. If it is older, either export a
   format it accepts (FUN9/FUNA/FUN8/FUN7) or require a stock firmware version that reads
   FUNB. The genre work itself does not change the project format.
2. **Preset numbers.** The new ANALOG presets are appended. A FUNB stores each track's raw
   parameters, so the sound should survive even if the stock preset byte wraps to an
   existing preset. Verify:
   - the preset byte is normalized modulo the stock engine's preset count;
   - no new parameter IDs or engine changes are required by the new recipes;
   - no new sample ROM or kit data is required.
3. **Genre names.** A project does not store the genre row. Stock will show the project name,
   not "PSY TRANCE". Set a useful project name on export.
4. **Unsupported features.** Verify the stock firmware supports every feature the generated
   project uses: motion, parameter locks, chance, ratchet, lane modes, FM6 patches, DRUM-X
   kit, punch-in lane, macros and chain. If not, strip unsupported data on export.
5. **Sections and chain.** `LOOP` uses one live section. `PARTS`/`SONG` store B–D in project
   slots and set an arrangement. Importing only slot A loses the arrangement. Either export
   every used section plus the song index, or collapse the export to one section.
6. **Full-backup restore is destructive.** It replaces stock settings, user presets, all
   songs and all sections. Always take a stock backup first, or use a single-object import.
7. **Stock editor version.** The deployed GitHub editor may differ from the fork. Confirm
   `INFO` fields (`P_COUNT`, `G_COUNT`, `CHAIN_ROWS`, backup caps) and the project format
   support before relying on a particular UI path.
8. **Checksums.** A FUNB has its own internal FNV-1a checksum; a backup object also has a
   CRC-32. Do not hand-edit either. Use the packing helpers.

## Implementation checklist

- [ ] Add a FUNB capture/export helper to the emulator build (`web/emu/felucca_web.c` or a
      small JS wrapper around `web_bk_len` / `web_bk_buf`).
- [ ] Add the generator/export UI to `web/emu/index.html`.
- [ ] Add a reusable JS module for:
  - FUNB size/magic validation;
  - stock-backup JSON parsing;
  - replacing one object and recomputing its CRC-32;
  - optional single-object import payload.
- [ ] Add a command-line merge tool (for example `web/emu/stock_project.mjs`) so the
  workflow can be tested without the browser UI.
- [ ] If the single-object route is chosen, add a minimal `BACKUP_PUT` importer using the
  existing protocol. Do not add a firmware update path.
- [ ] Add tests:
  - every new genre exports a 3,840-byte FUNB;
  - a FUNB exported from the modified emulator imports into a **fresh modified emulator**
    via `stems.js` / `deviceFromBackup` and renders the same piece;
  - the merge tool changes only the intended object;
  - a stock backup restored after the test still has the original settings and presets;
  - no firmware-update SysEx or app-area write is sent (project-slot flash writes are
    expected and are the point).
- [ ] Document the exact stock firmware versions that can read the exported format.
- [ ] Add a one-page "export → merge → restore → load slot" guide with screenshots or exact
  button names.
- [ ] Do not flash the FM-1 or push the fork until hardware validation is complete.

## Validation procedure

1. Generate a known piece in the modified emulator:
   - one `LOOP` per new genre, fixed seed and energy;
   - at least one `PARTS` piece to test sections and the chain.
2. Export the FUNB and verify:
   - size is 3,840;
   - magic is `FUNB`;
   - the project checksum passes;
   - track count, engine bytes and step count are present.
3. Reload the export into a fresh modified emulator and render it. Compare with the original
   render (track mute states, tempo, pattern length and audible result).
4. On the stock FM-1:
   - stop playback;
   - download and keep a Complete backup;
   - merge one exported section into slot A;
   - restore the merged backup;
   - confirm the editor shows slot A as used;
   - load slot A and play.
5. Check:
   - BPM and pattern length;
   - all six tracks and their presets/sounds;
   - drum grid, motion, chance/ratchet if used;
   - chain/arrangement for `PARTS`/`SONG`;
   - no stock settings, user presets or other songs changed.
6. Restore the original stock backup and confirm the device is exactly as before.
7. Record the stock firmware version, editor version and result. Only then consider
   publishing the workflow.

## Open decisions

- Export only live section A first, or support full `PARTS`/`SONG` arrangements immediately?
- Use the stock editor's full-backup restore, or add a one-object importer?
- Keep the exporter inside the emulator page, or keep it as a Node CLI / test tool first?
- Should the exporter strip unsupported features automatically, or refuse to export with a
  clear list?
- Which stock firmware version is the minimum target?

## References

- `web/EDITOR_PROTOCOL.md` — SysEx commands, backup object IDs, FUNB layout.
- `web/editor.html` — Sound-tab patch file and Projects/backup UI.
- `web/fm1backup.js` — `felucca-backup` JSON, CRC-32, restore order.
- `firmware/src/editor_backup.c` — `BACKUP_PUT` object validation and commit.
- `firmware/src/project.c` — FUNB packing, `project_capture`, project loading.
- `web/emu/felucca_web.c` — `web_gen`, `web_bk_len`, `web_bk_put`.
- `web/emu/stems.js` — `deviceFromBackup`, backup-object round-trip helper.
- `web/emu/stems_test.mjs` — example of making a piece and capturing its backup objects.
- `tests/project_test.c`, `tests/backup_test.c` — format and backup coverage.
