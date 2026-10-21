# WHEEL silent-block preparation, based on 0.9.9 cpu5

Four-track hardware feedback showed DRUM, LOFI, WHEEL and VOICE, with WHEEL reporting zero active voices but a nonzero smoothed render time. These measurements include track/modulation work and are not pure engine profiles; the reported WHEEL time is not all removable. FX was the largest measured group; this patch is independent of the continuing ROOM/delay/chorus investigation.

WHEEL prepared nine drawbar gains, percussion/click/drive settings and rotor AM/FM coefficients even when no voice could consume them. Skip that preparation while all voices are inactive. Keep both rotor speeds and phases, rotary wet smoothing and the dust-carrier/noise initialization logic running exactly as before. Do not stop modulation clocks, track control, distortion tails, filters or the scheduler. No persistent state or buffers added. The first active block reconstructs every skipped value before rendering; note-on itself does not consume those cached values.

A dedicated differential test compares the original CPU5 implementation with the new code over 12000 blocks, repeatedly moving registration, drawbar trims, percussion, click, drive, rotary speed/depth and dust during silence and playing two-note bursts. It hashes audio, all wheel voice state, rotor phase/speed/wet state every block, and the complete per-track engine state whenever voices are active. Silent cached coefficients intentionally differ. Existing 157 golden renders stay unchanged.

This source branch is an unpublished candidate. The beta installer still points to CPU5. No measured hardware CPU improvement or complete dropout fix is claimed. The failing project's exported data is needed to reproduce ROOM settings, all sends and timbres before changing the FX path.
