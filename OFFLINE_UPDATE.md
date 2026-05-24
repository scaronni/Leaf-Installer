# CFW offline-update flow

Leaf Installer can install and update the major custom firmware components (`Atmosphère`, `hekate + Nyx`, `Ultrahand overlay`, `sys-patch`, `emuiibo`) without you ever having to plug your SD card into a PC. But it does so in two phases instead of one — this document explains why, what happens on screen, and how to recover if anything goes wrong.

## Prerequisites

Before the first time you use this flow, make sure your SD card has:

- `sdmc:/switch/Leaf-Installer/Leaf-Installer.nro` — the homebrew binary you launch from hbmenu.
- `sdmc:/bootloader/hekate_ipl.ini` — your hekate config, **with a `[Leaf Update]` section pointing at the payload** (the bundled sample already has this; details below).
- `sdmc:/bootloader/payloads/leaf-updater.bin` — the offline-update payload itself.

The easiest way to get all three at once is to extract `Leaf-Installer.zip` straight to the root of your SD card. The release zip's layout already matches what's expected:

```
sdmc:/
├── bootloader/
│   ├── hekate_ipl.ini
│   └── payloads/
│       └── leaf-updater.bin
└── switch/
    └── Leaf-Installer/
        └── Leaf-Installer.nro
```

If you grabbed only the bare `Leaf-Installer.nro` (e.g. you updated by replacing the homebrew binary by hand) and tried to use the offline-update flow, Leaf will refuse to start with a "Leaf Updater payload missing" dialog — that's a signal to re-extract the full release zip.

## Steps for the impatient

For Switch systems modified with a boot chip that boots Hekate directly:

- Boot your system with Hekate and then Atmosphere.
- Launch Leaf Installer.
- Install all custom firmware components.
- Let it reboot.
- System reboots into Hekate and the Leaf payload updates Atmosphere and everything else.
- System reboots into Hekate and then Atmosphere.

For Switch systems that use an RCM injector:

- Boot your system with Hekate as a payload via RCM.
- Boot Atmosphere.
- Launch Leaf Installer.
- Install all custom firmware components.
- Select to restart later and power off system.
- Boot your system with Hekate as a payload via RCM.
- The Leaf payload updates Atmosphere and everything else.
- Select power off.
- Boot your system with Hekate as a payload via RCM.
- Boot Atmosphere.

## Why it isn't a normal "download and replace" install

Once Atmosphère is running, its `ams.mitm` sysmodule [deliberately keeps these two files open at boot](https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/ams_mitm/source/amsmitm_initialization.cpp#L137-L144):

- `sdmc:/atmosphere/package3`
- `sdmc:/atmosphere/stratosphere.romfs`

That open file handle tells the SD-card driver to refuse any other process trying to delete, rename, or write those paths. The intent on Atmosphère's side is "don't let users brick themselves by replacing the running kernel mid-session." The side effect is that a homebrew updater running inside Atmosphère **cannot** overwrite Atmosphère's own boot files — every `fopen("wb")` or `rename()` against them comes back with `EIO` / `EEXIST`.

The workaround Leaf uses: do everything in two stages.

1. **Stage** — while HOS is running, download all the new files into a staging directory on the SD card that isn't protected by `ams.mitm`. Nothing in `/atmosphere/` is touched at this point.
2. **Apply** — reboot into a tiny standalone payload that runs *outside* HOS (so `ams.mitm` isn't loaded and the protected files are free). The payload moves the staged tree into its final place at the SD root and reboots back into Atmosphère.

You don't have to think about any of this — Leaf orchestrates both phases for you. The next sections walk through what you'll see.

## Step-by-step walkthrough

### 1. Open the CFW component installer

From Leaf Installer's main menu, choose **Install custom firmware components**.

Leaf first checks that `sdmc:/bootloader/payloads/leaf-updater.bin` is present. If it's missing, you'll see the "Leaf Updater payload missing" dialog explaining what to do, and the flow aborts — no work is done.

![Leaf Installer firmware components menu](/images/cfw-confirmation.jpg)

### 2. Confirm the check

A confirmation dialog explains that Leaf will go fetch the latest release of each configured component from GitHub. This step doesn't write anything yet — it's just a heads-up that network requests are about to fly.

Press **Continue** to go on, or **Cancel** to back out.

### 3. Pick which components to install

After the latest release versions come back from GitHub, you land on the selection page. One row per component, each showing the available version:

![Leaf Installer firmware components first install](/images/cfw-list-new.jpg)

If you've previously installed that component via Leaf, the row will also show what version you had: `1.7.0  →  1.7.1` style:

![Leaf Installer firmware components update](/images/cfw-list-update.jpg)

- **A** toggles a component on/off.
- **Y** toggles all components at once.
- **+** starts the install with whatever is selected.
- **B** cancels back to the main menu.

By default every component is selected; deselect anything you don't want updated. There's no harm in including components that are already at the latest version — they'll just be re-downloaded and re-staged.

### 4. Staging

Leaf wipes the staging directory `sdmc:/leaf-offline-update/`, then for each selected component:

1. Downloads the release zip into Leaf's app directory (`sdmc:/switch/Leaf-Installer/`).
2. Extracts the zip into `sdmc:/leaf-offline-update/`.
3. Runs a per-component post-extract step if needed — currently emuiibo's `SdOut/` wrapper is flattened, and hekate's `hekate_ctcaer_X.Y.Z.bin` is renamed to `payload.bin` (so any modchip that boots `sdmc:/payload.bin` — PicoFly, HWFLY, etc. — will pick up the new hekate next time).
4. Records the installed version in Leaf's `config.json` so future runs can show the upgrade arrow.

After all components are staged, Leaf prepares the reboot:

![Leaf Installer firmware components reboot](/images/cfw-reboot.jpg)

1. It reads `sdmc:/bootloader/hekate_ipl.ini`, finds the entry numbered as `[Leaf Update]`, and notes its index (in the bundled ini, that's `3`).
2. It snapshots the *current* `autoboot=` value to `sdmc:/leaf-offline-update/.autoboot-prev`. With the bundled ini this is `1` (`[Atmosphere]`), but Leaf doesn't assume — whatever value was there is what gets restored after the update finishes.
3. It rewrites `autoboot=` to the `[Leaf Update]` index. From here on, the next reboot will go straight into the payload.

If any of those ini steps fail (typically because your hekate config doesn't have a `[Leaf Update]` section), Leaf still finishes successfully — it just shows a message saying "I couldn't arm hekate, launch the Leaf Updater payload manually from the Payloads menu" and skips the auto-reboot.

At this point the SD card looks like:

```
sdmc:/
├── atmosphere/                     (your current install, untouched)
├── bootloader/
│   ├── hekate_ipl.ini              (autoboot= flipped to [Leaf Update])
│   └── payloads/leaf-updater.bin   (was already here)
├── leaf-offline-update/            (new! staged tree mirroring final layout)
│   ├── atmosphere/                 (the new atmosphere/ to be moved into place)
│   ├── bootloader/                 (if hekate was selected)
│   ├── payload.bin                 (if hekate was selected, renamed from hekate_ctcaer_*)
│   ├── switch/                     (if Ultrahand was selected)
│   └── .autoboot-prev              (single line: the value to restore after update)
└── switch/Leaf-Installer/...
```

### 5. Reboot

The completion dialog summarises what was staged and offers a **Restart** button. Pick it and Leaf calls into the BPC service for a clean reboot.

If anything was skipped (for example, you selected hekate but its GitHub release was unreachable), the skipped components are listed below the success message.

> **How hekate actually boots** depends on your console's mod state — see [How hekate gets to run on your console](#how-hekate-gets-to-run-on-your-console) below. Either way, the next thing the user-visible system does after this reboot is to **land in hekate**, which then reads `hekate_ipl.ini` and auto-boots the Leaf Updater payload because Leaf flipped `autoboot=` to the `[Leaf Update]` index in step 4.

### 6. The payload runs

Hekate reads `hekate_ipl.ini`, sees `autoboot=` pointing at `[Leaf Update]`, and chainloads `bootloader/payloads/leaf-updater.bin`.

The payload takes over and:

1. Brings up the Tegra hardware (clocks, display, gfx console).
2. Mounts the SD card via FATFS.
3. Reads `.autoboot-prev` so it knows what value to put back in `hekate_ipl.ini` when it's done.
4. Recursively walks `sdmc:/leaf-offline-update/` and moves every entry into its final place at `sdmc:/`. The move uses FATFS `f_rename`, which is a metadata-only operation on the same partition — so even for a 60 MB `package3`, the "copy" is essentially instant.
5. When the source and destination directories collide (e.g. you already have `/atmosphere/contents/`, and the staged tree also has `/atmosphere/contents/`), the payload recurses into them and merges file-by-file rather than wiping anything. Files that exist in both places are overwritten with the staged version.
6. Rewrites `bootloader/hekate_ipl.ini`'s `autoboot=` line back to the value from `.autoboot-prev`. The rest of the ini (comments, ordering, other entries) is preserved.
7. Deletes the now-empty `sdmc:/leaf-offline-update/`.
8. Shows an exit menu: **Reboot** (default) or **Power off**, with a 5-second auto-reboot countdown. Touching either volume key cancels the countdown and lets you pick at your own pace; **POWER** confirms the highlighted option. The **Power off** path is the one to take on a non-modchip Switch — a cold reboot on those consoles always lands back in stock HOS instead of in hekate, so you want to shut down here and re-inject hekate via RCM yourself on the next boot.

On screen during all this you'll see a status text and a per-file progress line, then the final tally and the exit menu.

![Leaf Installer payload](/images/payload.jpg)

### 7. Back in CFW

The reboot brings up hekate again, which now reads the restored `autoboot=1` and chainloads Atmosphère as if nothing happened. You're back in HOS, but running the freshly-installed CFW components.

The whole second phase, from "press Restart" to "back in HOS," is typically under 20 seconds on a modchipped Mariko. On an Erista (or any console that needs RCM injection between boots), the wall-clock time depends on how fast you can drive the injection.

### 8. Allow system updates

Once back in Atmosphère, enter the UltraHand overaly menu (ZL + ZR + Down on the pad) and disable the `blockfirmwareupdates_12.0.0+` patch under `sys-patch`. Reboot the system, and you can let the Switch update itself to the latest HOS version.

Once the update has happened, re-enable the patch and reboot to disable again checking for updates automatically.

![sys-patch block updates](/images/firmware-block.jpg)

---

## How hekate gets to run on your console

The whole offline-update flow assumes hekate is the first thing the SoC's BPMP runs after every reboot. *How* that happens depends on what kind of console-side mod you have:

- **Modchipped Mariko (PicoFly, HWFLY, etc.).** The modchip patches Mariko's bootrom every cold boot so that it loads `sdmc:/payload.bin` from the SD card and executes it. That `payload.bin` is hekate itself. Leaf never touches `/payload.bin` during the offline update — only the staged `hekate_ipl.ini` and the `[Leaf Update]` entry — so the chip just keeps doing its job on the next reboot and you land in hekate automatically. This is the "warm path": pick **Restart** from the exit menu, the Switch power-cycles, the modchip injects hekate, hekate auto-boots Leaf Updater, the move runs, hekate auto-boots Atmosphère, you're back. No human input between steps.

- **Erista / unpatched Switch via RCM.** An Erista console with no modchip has no automatic way to start hekate — every cold boot, the bootrom enters RCM and waits for a USB host to push a payload. You'd normally use a USB-attached PC (with TegraRcmGUI, `fusee-launcher`, etc.) or a USB-OTG jig to inject hekate's `.bin` over USB. The same applies here: between **Restart** (or, on this kind of console, **Power off** from the payload's exit menu) and the next stage, you have to re-inject hekate yourself. Once hekate is up it sees the modified `hekate_ipl.ini` and continues the flow exactly the same way.

This is why the payload's exit menu has both **Reboot** and **Power off** — modchipped users want to keep going (Reboot), Erista users want to stop cleanly (Power off) so they can use their RCM injection rig at their own pace.

If your console doesn't have either of those (e.g. a patched Mariko without a modchip), you can't run the offline-update flow at all — there's no way to get hekate (or any homebrew payload) onto a fresh-booted patched Mariko without a modchip.

---

## Remembering what's installed

Leaf records the version of each component it has successfully staged in `sdmc:/switch/Leaf-Installer/config.json`, in a `cfwInstalled` object keyed by component display name:

```json
"cfwInstalled": {
    "Atmosphère": "1.7.1",
    "hekate - Nyx": "6.5.2",
    "Ultrahand Overlay": "1.5.5",
    "sys-patch": "1.5.0",
    "emuiibo": "0.7.0"
}
```

On the **next** run of the install flow, after Leaf fetches the latest versions from GitHub, the selection page renders each row as **`installed  →  available`** when the component is present in that map. Same version on both sides means "already up to date" — there's no harm in selecting it again, you'll just re-download and re-stage the same files. Different versions mean "Leaf would install this upgrade if you keep the row selected."

Notes:
- The map is only updated for components that **Leaf** staged. A component you installed by extracting a release zip on a PC won't show up there until you've run it through Leaf at least once.
- Toolbox-based detection (scanning `/atmosphere/contents/*/toolbox.json`) was tried earlier and removed — most components don't ship one, and Ultrahand bundles several sysmodules together, which made the readout misleading. The `cfwInstalled` map is the authoritative source.
- If you want to clear the memory (e.g. to force every row to show no installed version on the next run), open `config.json` on a PC and either set `cfwInstalled` to `{}` or delete the whole file — Leaf re-creates it with defaults on next start.

---

## The bundled `bootloader/hekate_ipl.ini`

The release zip drops a sample ini at `bootloader/hekate_ipl.ini`:

```
[config]
autoboot=1
bootwait=3

[Atmosphere]
payload=bootloader/payloads/fusee.bin
icon=bootloader/res/icon_payload.bmp
kip1patch=nosigchk

[Stock]
pkg3=atmosphere/package3
stock=1
emummc_force_disable=1
icon=bootloader/res/icon_switch.bmp

[Leaf Update]
payload=bootloader/payloads/leaf-updater.bin
icon=bootloader/res/icon_payload.bmp
```

`autoboot=1` selects `[Atmosphere]`, so a normal boot goes straight into Atmosphère after a three-second pause. The `[Leaf Update]` section is what Leaf temporarily flips `autoboot=` to during an offline update.

**If you already have a working hekate setup that you don't want to replace, you only need to add the `[Leaf Update]` block** — copy the three lines from the sample into your existing ini and leave everything else alone. The order of sections matters because hekate counts them: Leaf re-scans your ini at install time to figure out which numbered entry corresponds to `[Leaf Update]`, so wherever you put it is fine.

---

## The `leaf-updater.bin` payload

The payload at `bootloader/payloads/leaf-updater.bin` is a small (~60 KB) hekate-BDK binary. Built from source under `payload/` in this repository (devkitARM, ARMv4T, runs on the Tegra X1 BPMP), it has no external dependencies beyond the hekate BDK headers and is licensed GPL-2.0 like hekate itself.

You can also launch it manually any time from hekate's main menu: `Payloads → leaf-updater.bin`. When started this way the payload behaves identically except for one detail: if `.autoboot-prev` is missing it skips the `hekate_ipl.ini` rewrite (because Leaf didn't arm anything, there's nothing to undo). This is the recommended way to recover from a partial update — see below.

---

## Recovery

### "Leaf couldn't arm hekate" message during staging

The staging finished successfully, but Leaf couldn't find a `[Leaf Update]` section in your `hekate_ipl.ini` (or the file is missing entirely). The staged tree under `sdmc:/leaf-offline-update/` is still intact and waiting.

Fix: add the `[Leaf Update]` block from the sample ini into your hekate config, reboot to hekate, then either reboot again (with `autoboot=<Leaf Update index>` set in the ini) or just pick `Payloads → leaf-updater.bin` from hekate's menu. The payload will apply the staged update.

### The payload errored partway through

Rare — would require either an SD card I/O failure or a power loss during the move. The payload performs each move atomically (FATFS `f_rename` is a single metadata write), but a power loss between two consecutive moves could leave the staging tree partially drained.

Fix: boot back to hekate, choose `Payloads → leaf-updater.bin`. The move is idempotent — any entries that were already moved are simply absent from the staging tree on the second run, and the payload skips them. After a clean re-run the staging tree is empty and the payload deletes it.

### `hekate_ipl.ini` ended up broken

Could happen if the SD card's filesystem is corrupted while Leaf was rewriting `autoboot=`, or if the user-supplied ini has unusual line endings the payload's parser didn't anticipate.

Fix: mount the SD card on a PC and edit `bootloader/hekate_ipl.ini` by hand. The only value that needs touching is `autoboot=` in the `[config]` section — set it to the index of `[Atmosphere]` (typically `1`). The bundled sample is also a good baseline to start over from.

### Boot loop into the Leaf Updater

Should never happen — the payload always restores `autoboot=` before rebooting, and on a clean re-run with an empty staging dir it falls through to the "nothing to do, reboot" path immediately. If you somehow get into a loop, mount the SD on a PC and either delete `bootloader/payloads/leaf-updater.bin` (hekate will fall back to the menu when the autoboot entry's payload is missing) or edit `hekate_ipl.ini` to fix `autoboot=`.

---

## File layout cheat-sheet

What lives where while an offline update is in flight:

| Path                                            | When            | Purpose                                                   |
| ----------------------------------------------- | --------------- | --------------------------------------------------------- |
| `sdmc:/leaf-offline-update/`                    | Stage + Apply   | Staging tree — created by Leaf, drained by the payload    |
| `sdmc:/leaf-offline-update/.autoboot-prev`      | Stage + Apply   | Single integer — the autoboot value to restore            |
| `sdmc:/bootloader/hekate_ipl.ini`               | Always          | Hekate config; `autoboot=` is temporarily flipped         |
| `sdmc:/bootloader/payloads/leaf-updater.bin`    | Always          | The standalone payload                                    |
| `sdmc:/switch/Leaf-Installer/Leaf-Installer.nro`| Always          | The Leaf homebrew binary                                  |
| `sdmc:/switch/Leaf-Installer/config.json`       | Always          | Leaf's settings, including the list of installed versions |
