# Awoo Installer
A No-Bullshit NSP, NSZ, XCI, and XCZ Installer for Nintendo Switch

![Awoo Installer Main Menu](https://i.imgur.com/q5Qff0R.jpg)

## Features
- Installs NSP/NSZ/XCI/XCZ files and split NSP/XCI files from your SD card
- Installs NSP/NSZ/XCI/XCZ files over LAN or USB from tools such as [NS-USBloader](https://github.com/developersu/ns-usbloader)
- Installs NSP/NSZ/XCI/XCZ files over the internet by URL or Google Drive
- Verifies NCAs by header signature before they're installed
- Installs and manages the latest signature patches quickly and easily
- Based on [Adubbz Tinfoil](https://github.com/Adubbz/Tinfoil)
- Uses [XorTroll's Plutonium](https://github.com/XorTroll/Plutonium) for a pretty graphical interface
- Just werks

## Why?
Because Goldleaf tends to not "Just werk" when installing NSP files. I wanted a *free software* solution that installs, looks pretty, and doesn't make me rip my hair out whenever I want to put software on my Nintendo Switch. Awoo Installer does exactly that. It installs software. That's about it!

If you want to do other things like manage installed tickets, titles, and user accounts, check out [Goldleaf](https://github.com/XorTroll/Goldleaf)!

## Building with Podman
The GitHub Actions workflow builds Awoo Installer inside the official devkitPro container image (`devkitpro/devkita64:latest`). You can reproduce the exact same build locally with Podman — no need to install devkitPro, devkitA64, or any Switch toolchain on your host.

### Prerequisites
- [Podman](https://podman.io/) installed on your system
- Clone fresh with submodules:
  ```sh
  git clone --recursive https://github.com/dragonflylee/Awoo-Installer.git
  cd Awoo-Installer
  ```
  
  Or, if you already cloned without `--recursive`, populate the submodules from inside the repository:
  ```sh
  git submodule update --init --recursive
  ```

### Build
From the root of the repository, run:
```sh
podman run --rm -it \
    -v "$PWD":/Awoo-Installer:Z \
    -w /Awoo-Installer \
    docker.io/devkitpro/devkita64:latest \
    bash -c "git config --system --add safe.directory '*' && make -j\"\$(nproc)\" all"
```

What this does:
- `--rm` removes the container when the build finishes.
- `-v "$PWD":/Awoo-Installer:Z` mounts the current directory into the container. The mount path matters — the Makefile names the artifact after the working directory (`$(notdir $(CURDIR))`), so mounting at `/Awoo-Installer` produces `Awoo-Installer.nro` just like CI. The `:Z` suffix relabels the volume for SELinux — drop it on systems without SELinux.
- `-w /Awoo-Installer` sets the working directory inside the container.
- `git config --system --add safe.directory '*'` mirrors the workflow step that lets git operate on the bind-mounted tree (it is owned by a different UID inside the container).
- `make -j"$(nproc)" all` runs the same build command as CI.

When the build completes, `Awoo-Installer.nro` will be in the repository root, ready to copy to your Switch's SD card under `/switch/Awoo-Installer/`.

### Clean build
To start from scratch:
```sh
podman run --rm -it -v "$PWD":/Awoo-Installer:Z -w /Awoo-Installer docker.io/devkitpro/devkita64:latest make clean
```

### Note on Docker
The same commands work with Docker — just replace `podman` with `docker` and omit the `:Z` SELinux relabel suffix if your system does not use SELinux.

## Thanks to
- HookedBehemoth for A LOT of contributions
- Adubbz and other contributors for [Tinfoil](https://github.com/Adubbz/Tinfoil)
- XorTroll for [Plutonium](https://github.com/XorTroll/Plutonium) and [Goldleaf](https://github.com/XorTroll/Goldleaf)
- blawar (wife strangulator) and nicoboss for [NSZ](https://github.com/nicoboss/nsz) support
- The kind folks at the AtlasNX Discuck (or at least some of them)
- The also kind folks at the RetroNX Discuck (of no direct involvement)
- [namako8982](https://www.pixiv.net/member.php?id=14235616) for the Momiji art
- TheXzoron for being a baka
