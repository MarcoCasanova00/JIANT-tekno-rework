# Installing the Tekno Rework on Your FM-1

> **READ THE ENTIRE GUIDE BEFORE STARTING. Follow each step in order.**

---

## Step 0: Prerequisites

### Hardware
| Item | Notes |
|------|-------|
| M-WAVE FM-1 | Must be powered by USB (not battery-only) |
| USB-C cable | Data cable, not charge-only |
| Computer | Windows 10+, macOS 12+, or Linux (WSL works) |

### Browser
- **Chrome or Edge** (WebMIDI support required).
- Firefox does **not** support WebMIDI for flashing.

### Files
All files are in `/home/maltomare/fm1-firmware/`:

| File | Purpose |
|------|---------|
| `felucca-tekno.fwsc` | The Tekno Rework firmware (to install) |
| `felucca-stock.fwsc` | Official V15 fallback (for safety rollback) |
| `site/webapp/installer/` | The web installer page |
| `site/firmware/` | Contains both `.fwsc` packages |
| `SHA256SUMS` | Verify checksums |

---

## Step 1 (MANDATORY): Roll back to official V15 stock first

> **Skip this step ONLY if your FM-1 is currently on stock JIANT v0.9.x or official V15 firmware.**  
> **If you have ANY other custom firmware (Felucca, baudgirl, Skiff, etc.), you MUST do this step.**

### Why?
Different custom firmwares partition the NOR flash differently. Flashing JIANT directly over another custom firmware causes flash table collisions and **will brick** the device.

### How to roll back:
1. Open a terminal:
   ```bash
   cd /home/maltomare/fm1-firmware/site
   python3 -m http.server 8000
   ```
2. Open in Chrome/Edge: `http://localhost:8000/webapp/installer/`
3. In the installer page, select **"stock"** in the Version dropdown.
4. Click **"Install"**.
5. Wait for the progress bar to complete (≈2-3 minutes).
6. **Power-cycle** the FM-1 (unplug USB, wait 3s, plug back in).

Verify: The splash should now show the official V15 boot screen (no "patch" attribution).

---

## Step 2: Verify checksums (optional but recommended)

In the terminal:
```bash
cd /home/maltomare/fm1-firmware
sha256sum -c SHA256SUMS
```
Expected output:
```
felucca-tekno.fwsc: OK
emulator/felucca.wasm: OK
```

The **stock fallback** should match:
```
3d49d987718093128e277a1daf840d1e48a159eff656d7cdc22f891004d22b22  felucca-stock.fwsc
```

---

## Step 3: Try the emulator first (no risk)

Before touching hardware, verify the sound in the browser:

```bash
cd /home/maltomare/fm1-firmware
python3 -m http.server 8790 -d emulator
```

Open: `http://localhost:8790/`

**Audition the new genres:**
1. Tap **HOME** until you see `GEN 1` on screen.
2. Turn **KNOB 1** to select a genre:
   - `TRIBE TEKNO`, `TEKNO`, `MENTAL TEKNO`, `ACID CORE`, `HARDTEKK`, `PSY TRANCE`
3. Press **OCT+** (or `]` on keyboard) to generate a piece.
4. Listen — you should hear the new synth patches and drum patterns.

---

## Step 4: Flash the Tekno Rework to the device

### Connect the FM-1
1. Plug in the FM-1 via USB-C to your computer.
2. Power it on (press **GO/POWER** button).
3. The screen should show the splash and then HOME.

### Run the web installer
1. In the terminal (same directory as Step 1):
   ```bash
   cd /home/maltomare/fm1-firmware/site
   python3 -m http.server 8000
   ```
2. Open in Chrome/Edge: `http://localhost:8000/webapp/installer/`
3. Select **"dev"** in the Version dropdown.
4. Verify the package shows: **570,392 B** image size.
5. Click **"Install"**.
6. **Do NOT unplug the USB cable** until the installer reports completion (≈2-3 minutes).
7. When done, click **"Done"** (if shown).

### Power-cycle and verify
1. Unplug the USB cable from the FM-1.
2. Wait 3 seconds.
3. Plug it back in.
4. The splash screen should now show:
   ```
   v0.9.18
   by Juan Jiant
   MarcoCasanova00 patch
   ```

---

## Step 5: Roll back to stock V15 (if needed)

If you ever need to return to stock:

```bash
cd /home/maltomare/fm1-firmware/site
python3 -m http.server 8000
# Open: http://localhost:8000/webapp/installer/
# Select "stock" and click Install
```

---

## Troubleshooting

| Symptom | Solution |
|---------|----------|
| Installer says "Device not found" | Use Chrome or Edge; ensure FM-1 is powered on and shows HOME screen |
| Flash fails mid-way | Do NOT panic — power-cycle, re-enter installer, retry |
| Stuck on splash screen | Re-flash via installer (WL80UBOOT recovery mode auto-triggers) |
| "Port already in use" on port 8000 | Use a different port: `python3 -m http.server 8080` |

### Emergency Recovery
If the device does not respond after a failed flash:
1. Unplug USB cable.
2. Hold down **OCT-** + **GO** buttons.
3. While holding, plug in USB.
4. The device enters **WL80UBOOT** recovery mode (screen stays dark).
5. Re-run the installer and flash the **"stock"** package first.

---

## Safety Checklist

- ✅ Currently on stock V15 or official JIANT v0.9.x (or have rolled back)
- ✅ Using Chrome or Edge browser
- ✅ Using a data USB cable (not charge-only)
- ✅ Device shows HOME screen before flashing
- ✅ Installer shows 570,392 B image size before clicking Install
- ✅ Not unplugging USB during flashing

---

## What You Get

The firmware replaces **6 of 43 genres**:

| Old Genre | New Genre |
|-----------|-----------|
| BROKEN | PSY TRANCE |
| AFROBEAT | TEKNO |
| BOSSA | MENTAL TEKNO |
| JAZZ | ACID CORE |
| BOOM BAP | HARDTEKK |
| STEPPERS | TRIBE TEKNO |

All synths are genre-specific. The new genres occupy the exact old slots, so presets and sequences for the remaining 37 genres are 100% unchanged.
