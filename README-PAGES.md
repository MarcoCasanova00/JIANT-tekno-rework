# Publishing the Emulator to GitHub Pages

This guide explains how to publish and host the WebAssembly browser emulator of this custom firmware directly on **GitHub Pages** for your fork ([MarcoCasanova00/JIANT-tekno-rework](https://github.com/MarcoCasanova00/JIANT-tekno-rework)).

---

## Method 1: Automated Deployment via GitHub Actions (Recommended)

The repository already includes a pre-configured GitHub Actions workflow (`.github/workflows/emulator.yml`) that compiles the firmware to WebAssembly and deploys the emulator and landing site automatically on every push to `main`.

Because GitHub disables GitHub Actions and Pages on new forks by default, follow these steps to enable it:

### Step 1: Enable GitHub Actions
1. Go to your repository on GitHub:
   [https://github.com/MarcoCasanova00/JIANT-tekno-rework](https://github.com/MarcoCasanova00/JIANT-tekno-rework)
2. Click **Settings** (top navigation bar) -> **Actions** -> **General** (left sidebar).
3. Under **Actions permissions**, select **Allow all actions and reusable workflows**.
4. Scroll down to **Workflow permissions** and select **Read and write permissions**.
5. Click **Save**.

### Step 2: Configure GitHub Pages Source
1. In repository **Settings**, click **Pages** (under the "Code and automation" section on the left sidebar).
2. Under **Build and deployment** -> **Source**, change the dropdown from *Deploy from a branch* to:
   **GitHub Actions**

### Step 3: Trigger the Workflow
1. Click the **Actions** tab at the top of the repository.
2. If prompted with *"Workflows aren't being run on this forked repository"*, click the green button **I understand my workflows, go ahead and enable them**.
3. In the left sidebar, click the **emulator** workflow.
4. Click the **Run workflow** dropdown button on the right, ensure branch `main` is selected, and click **Run workflow**.
5. Wait for the workflow run to finish (usually ~3-4 minutes). When it succeeds, it will show a green checkmark.

### Step 4: Access Your Emulator
Once deployed, the emulator is accessible at:
- **Emulator**: `https://MarcoCasanova00.github.io/JIANT-tekno-rework/emu/`
- **Front Landing Page**: `https://MarcoCasanova00.github.io/JIANT-tekno-rework/`

---

## Method 2: Direct Branch Deployment (Prebuilt Files)

If you prefer to deploy the prebuilt WebAssembly emulator without waiting for GitHub Actions runners to compile it from source, you can publish the already-built emulator files to a dedicated `gh-pages` branch.

### 1. Create and push the `gh-pages` branch from WSL:
```bash
cd /home/maltomare/JIANT-fork

# Create an orphan branch with no history
git checkout --orphan gh-pages

# Clear working tree
git rm -rf .

# Copy prebuilt emulator files to root
cp -r /home/maltomare/fm1-firmware/emulator/* .

# Commit and push
git add .
git commit -m "Publish prebuilt emulator to GitHub Pages"
git push -u origin gh-pages --force

# Return to your main branch
git checkout main
```

### 2. Configure GitHub Pages:
1. In your repository on GitHub, go to **Settings** -> **Pages**.
2. Under **Build and deployment** -> **Source**, select **Deploy from a branch**.
3. Under **Branch**, select `gh-pages` and `/ (root)`, then click **Save**.

With this method, the emulator will be served directly at:
- `https://MarcoCasanova00.github.io/JIANT-tekno-rework/`

---

## Testing & Usage Notes

- **Audio Playback**: Modern web browsers require user interaction before web audio starts. When opening the emulator, click the **power switch** / **GO** button to start audio playback.
- **Controls**:
  - Keys: Computer keyboard rows `Z`?`'` and `Q`?`O`.
  - Buttons: `1` to `=` (FX to REC).
  - Octaves: `[` (OCT-) and `]` (OCT+).
  - Navigation: `UP` / `DOWN` (SELECT), `LEFT` / `RIGHT` (ALGORITHM).
  - Knobs & Dials: Drag with mouse or trackpad.
- **Tekno Genres**: Navigate to the GEN / EXPLORE screen to audition all six new genres: **TRIBE TEKNO**, **MENTAL TEKNO**, **ACID CORE**, **HARDTEKK**, **TEKNO**, and **PSY TRANCE**.
