# JIANT FM tekno rework

**Modified JIANT with six new techno-family genres for M-VAVE FM-1**

## What Changed
- Replaced BROKEN, AFROBEAT, BOSSA, JAZZ, BOOM BAP, STEPPERS with TRIBE TEKNO, MENTAL TEKNO, ACID CORE, HARDTEKK, TEKNO, PSY TRANCE
- All original 43 genres preserved (PSY TRANCE in slot 42)
- Added 11 new recipes and 8 playable ANALOG presets designed for tribe, mental, acidcore, hardtekk, and psytrance

## Try It
- **Browser Emulator**: https://MarcoCasanova00.github.io/JIANT-tekno-rework/emu/
- **How to Publish on GitHub Pages**: see [README-PAGES.md](README-PAGES.md) for step-by-step setup instructions.

## How Genre Generation Works
1. Navigate to the generator: on HOME, tap **HOME** to cycle groups to **GEN 1**, or hold **HOME** and turn **SELECT**.
2. **KNOB 1** (or **PRESETS**) chooses the genre. Turning the knob updates the screen but does **not** regenerate music immediately.
3. Press **OCT+** (or keyboard `]` / `+`) to **commit and generate** the piece.
4. **Seed Persistence**: When you switch genres, JIANT intentionally preserves the current **SEED** so you can hear the same composition re-orchestrated under the new genre's synth patches and tempo range. Because tekno genres (Tekno, Acid Core, Hardtekk) share 4-on-the-floor kicks and modal progressions, keeping the same seed will produce similar sequence notes. **To generate a completely fresh sequence and new seed, simply press OCT+ a second time.**

## Flashing & Device Safety
- **Coming from another custom firmware (Felucca, baudgirl, Skiff)**: **You MUST roll back to official V15 first** before installing JIANT. Writing JIANT directly over a different custom firmware will brick the FM-1 due to differing flash partition tables.
- **Coming from stock JIANT (v0.9.x) or official V15**: Safe to install directly via the web installer. The package preserves the exact identity (`FM-1_900`) and partition layout, and leaves 14,892 B of verified free headroom in the app slot without modifying the hardware bootloader.
- **Recovery**: If a flash is interrupted mid-transfer, the FM-1 enters `WL80UBOOT` recovery mode and can be cleanly re-flashed without opening the hardware.

## Documentation
- [README-PAGES.md](README-PAGES.md) - How to enable and host the emulator on GitHub Pages
- [docs/TODO-EXPORT-TO-STOCK.md](docs/TODO-EXPORT-TO-STOCK.md) - Specification and roadmap for exporting generated projects to stock FM-1

## License
GPL-3.0-only
