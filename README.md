# Leaf Installer
An NSP, NSZ, XCI, and XCZ Installer for Nintendo Switch.

Derived from [Awoo Installer](https://github.com/Huntereb/Awoo-Installer).

![Leaf Installer Main Menu](/images/main.jpg)

## Features
- Uses [XorTroll's Plutonium](https://github.com/XorTroll/Plutonium) for a pretty graphical interface
- Installs NSP/NSZ/XCI/XCZ files:
  - Over LAN or USB from tools such as [NS-USBloader](https://github.com/developersu/ns-usbloader)
  - From the SD card or USB mass storage devices.
  - Over the internet by URL or Google Drive
- Verifies NCAs by header signature before they're installed
- Auto updates from the Github releases page.
- Installs and manages the latest custom firmware components via an [offline-update flow](OFFLINE_UPDATE.md) (HOS stages the files, a bundled hekate payload applies them on next boot):
  - [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)
  - [hekate & Nyx](https://github.com/CTCaer/hekate)
  - [Ultrahand overlay](https://github.com/ppkantorski/Ultrahand-Overlay)
  - [sys-patch](https://github.com/impeeza/sys-patch)
  - [emuiibo](https://github.com/xortroll/emuiibo)
- Generates [emuiibo](https://github.com/xortroll/emuiibo)-compatible Amiibo data directly on the console — no PC required. See [Amiibo data generation](#amiibo-data-generation) below.
- Auto-update endpoints for Leaf Installer itself and the Amiibo API are configurable under `Settings`. The CFW component release URLs are pinned to upstream and not user-editable.
- Show storage information in the main window. It is also updated:
  - After every NSP install for network and SD installs.
  - At the end of the NSP install batch for USB installs (NS-USBLoader protocol limitation).

## Amiibo data generation

Leaf can populate emuiibo's library straight from the console — you don't need a PC, an existing Amiibo dump folder, or any prior setup beyond a working internet connection. Inspired by [Slluxx/AmiiboGenerator](https://github.com/Slluxx/AmiiboGenerator).

### What it does

From Leaf's main menu, choose **Generate Amiibo data**:

![Leaf Installer Generate Amiibo data](/images/amiibo-confirmation.jpg)

Leaf will:

1. Download the full Amiibo database as JSON from a configurable endpoint (defaults to [amiiboapi.org](https://amiiboapi.org/api/amiibo/) — you can repoint it under `Settings` if the public API ever goes away).
2. For every Amiibo in the database, create a folder under `sdmc:/emuiibo/amiibo/<series>/<name>/` containing:
   - `amiibo.json` — the metadata emuiibo reads to recognise the virtual Amiibo.
   - `amiibo.flag` — emuiibo's marker file that says "this directory is a valid Amiibo."
   - A 150-pixel-tall PNG icon downloaded from the API and resized on-device so emuiibo's overlay can show it.
3. Skip any Amiibo whose folder already exists, so it's safe to re-run after the upstream database is refreshed — only newly-released Amiibos get generated on the second pass, the existing ones are left untouched.

### Flow on screen

1. **Main menu → Generate Amiibo data.** A confirmation dialog summarises what'll happen and asks for permission to start. Cancel here is harmless — nothing has been written yet.
2. **Database download.** A short progress message; the API response is a few MB of JSON.
3. **Generation loop.** A progress page shows the current Amiibo being generated and a running counter. On a typical run with the full upstream database this takes a few minutes the first time; subsequent runs that just pick up newly-released Amiibos finish in seconds.

![Leaf Installer progress generating Amiibo data](/images/amiibo-progress.jpg)

4. **Completion summary.** A dialog reports how many entries were newly generated vs. skipped (already present).

![Leaf Installer finished generating Amiibo data](/images/amiibo-finished.jpg)

If the JSON download or icon fetch fails partway through, Leaf shows an error dialog and stops. The files it has already generated stay on the SD card and will be picked up as "already present" on the next run.

## CFW offline-update flow

CFW components are installed via a two-phase offline-update flow that works around Atmosphère's runtime file locks. See [OFFLINE_UPDATE.md](OFFLINE_UPDATE.md) for the full walkthrough, the bundled `hekate_ipl.ini` reference, and recovery instructions.

## Building with Podman
The GitHub Actions workflow builds Leaf Installer inside the official devkitPro container image (`devkitpro/devkita64:latest`). You can reproduce the exact same build locally with Podman — no need to install devkitPro, devkitA64, or any Switch toolchain on your host.

### Prerequisites
- [Podman](https://podman.io/) installed on your system
- Clone fresh with submodules:
  ```sh
  git clone --recursive https://github.com/dragonflylee/Leaf-Installer.git
  cd Leaf-Installer
  ```
  
  Or, if you already cloned without `--recursive`, populate the submodules from inside the repository:
  ```sh
  git submodule update --init --recursive
  ```

### Build

From the root of the repository, run:
```sh
podman run --rm -it \
    -v "$PWD":/Leaf-Installer:Z \
    -w /Leaf-Installer \
    docker.io/devkitpro/devkita64:latest \
    bash -c "git config --system --add safe.directory '*' && make -j\"\$(nproc)\" release"
```

What this does:
- `--rm` removes the container when the build finishes.
- `-v "$PWD":/Leaf-Installer:Z` mounts the current directory into the container. The mount path matters — the Makefile names the artifact after the working directory (`$(notdir $(CURDIR))`), so mounting at `/Leaf-Installer` produces `Leaf-Installer.nro` (and `Leaf-Installer.zip`) just like CI. The `:Z` suffix relabels the volume for SELinux — drop it on systems without SELinux.
- `-w /Leaf-Installer` sets the working directory inside the container.
- `git config --system --add safe.directory '*'` mirrors the workflow step that lets git operate on the bind-mounted tree (it is owned by a different UID inside the container).
- `make -j"$(nproc)" release` runs the same build command as CI. The `release` target builds the NRO, then the `leaf-updater.bin` BPMP payload via devkitARM (already present in the `devkita64` image), and bundles them together with the sample `hekate_ipl.ini`.

Output: `Leaf-Installer.zip` at the repo root — extract it to the SD card. The standalone `Leaf-Installer.nro` is also produced in the repo root if you only want to update the homebrew binary itself.

### Clean build
To start from scratch:
```sh
podman run --rm -it \
    -v "$PWD":/Leaf-Installer:Z \
    -w /Leaf-Installer \
    docker.io/devkitpro/devkita64:latest \
    make clean
```

### Note on Docker
The same commands work with Docker — just replace `podman` with `docker` and omit the `:Z` SELinux relabel suffix if your system does not use SELinux.

## Thanks to
- [Plutonium](https://github.com/XorTroll/Plutonium) graphics library
- Original [Awoo Installer](https://github.com/Huntereb/Awoo-Installer)
- Fork of [Awoo Installer](https://github.com/harmonsir/Awoo-Installer) with HTTP and USB hard drive support
- [hekate](https://github.com/CTCaer/hekate) bootloader and BDK used by the Leaf Updater payload
- [Claude](https://claude.ai/) for the incredible speed on iterating over features
