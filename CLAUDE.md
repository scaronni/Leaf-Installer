## Build

Always test a build after performing changes (sandbox must be disabled for podman):

podman run --rm -it -v "$PWD":/Leaf-Installer:Z -w /Leaf-Installer docker.io/devkitpro/devkita64:latest bash -c "git config --system --add safe.directory '*' && make -j\"\$(nproc)\" all"

A successful build produces `Leaf-Installer.nro` at the repo root. The Makefile lists every file under `$(ROMFS)` as a prerequisite of the `.nro` (see the `ROMFS_FILES := $(shell find ...)` block), so romfs-only edits trigger a rebuild — no need to touch a `.cpp` or delete the `.nro` first.

`APP_VERSION` (Makefile) reaches the code only through `inst::config::appVersion`, defined once in `source/util/config.cpp`; the inner Makefile makes `config.o` and the NACP depend on the Makefile so a version bump rebuilds them. Don't reintroduce a `static const` copy in a header — every object would bake in its own version string and stale objects in `build/` would show an old version and make the update check prompt forever.

To build the full **release zip** (NRO + payload + ini), use `make release` — same `devkita64` image, which ships devkitARM too so the payload can build in the same container:

`podman run --rm -it -v "$PWD":/Leaf-Installer:Z -w /Leaf-Installer docker.io/devkitpro/devkita64:latest bash -c "git config --system --add safe.directory '*' && make -j\"\$(nproc)\" release"`

Output: `Leaf-Installer.zip` at the repo root, containing `switch/Leaf-Installer/Leaf-Installer.nro`, `bootloader/payloads/leaf-updater.bin`, and `bootloader/hekate_ipl.ini`.

## Project layout

This is a Switch homebrew app (forked from Awoo Installer). It uses devkitpro/devkitA64 + Plutonium for graphics.

- `source/` / `include/` — C++ sources. Subfolders: `ui/`, `install/`, `nx/`, `data/`, `util/`, `drive/`.
- `source/ui/` — one `*Page.cpp` per screen. Each page is a `pu::ui::Layout` registered in `MainApplication`.
- `romfs/lang/` — translation JSON files, one per language (12 total).
- `include/Plutonium/`, `include/libusbhsfs/`, `include/hekate/` — vendored submodules. `Plutonium` and `libusbhsfs` are built by the top-level Makefile. `hekate` is BPMP-side code; only the `bdk/` subtree is linked into the payload at `payload/`.
- `payload/` — stand-alone hekate-BDK payload (ARMv4T) that runs after a reboot to apply the staged CFW components. Built by `make payload`. See the "CFW install flow + offline-update payload" section below.
- `release/` — files bundled verbatim into the release zip (currently `bootloader/hekate_ipl.ini`).
- `shortcut/` — generator for the HOME Menu forwarder template (`romfs/shortcut/template.bin`). See the "HOME Menu shortcut" section below.
- `Makefile` line 42 lists `SOURCES` (directories scanned for `*.cpp`/`*.c`) — new source dirs must be added there. New `.cpp` files in an existing source dir are picked up automatically.

## Adding a new UI page

1. Header in `include/ui/<name>Page.hpp` — class extends `pu::ui::Layout`, uses `PU_SMART_CTOR`.
2. Implementation in `source/ui/<name>Page.cpp` — model on `source/ui/sdInstPage.cpp` (background image, top/info/bot rectangles, logo, version text, storage info blocks, menu, button hint text).
3. Register in `include/ui/MainApplication.hpp` (add `#include` + `<name>Page::Ref` member) and `source/ui/MainApplication.cpp` (`::New()` + `SetOnInput` bind).
4. Navigate to it with `mainApp->LoadLayout(mainApp-><name>page)`.

`source/ui/instPage.cpp` is the shared progress-bar screen — call `instPage::loadInstallScreen()` / `setInstBarPerc()` / `setInstInfoText()` / `setTopInstInfoText()` etc. from any long-running install action.

## Translations — ALWAYS translate, never leave English fallbacks

`util::lang::LanguageEntry()` does NOT fall back to English. A missing key renders literally as `"didn't find: <key>"` on screen. Every string added to `romfs/lang/en.json` MUST be added to **all** of these files with a real translation:

  de.json, en.json, es-419.json, fr.json, it.json, jp.json, ko.json, pt.json, ru.json, zh-CN.json, zh-Hant.json, zh-TW.json

When changing the meaning of an existing key (e.g. the user-visible flow changes), update the translation in every language too — do not leave stale wording. The user explicitly requires this for every round.

Strings are referenced in code via the `"key.path"_lang` user-defined literal, with `.` as the nesting separator (see `source/util/lang.cpp::GetRelativeJson`).

**Button-hint strings (the bottom bar)** must include Switch PUA glyphs for the controller button icons, otherwise the icons render as nothing. Mapping (from existing `inst.sd.buttons`, `main.buttons`, etc.):
- `` — A button
- `` — B button
- `` — X button
- `` — Y button
- `` — Plus (+) button

Pattern is `<glyph> <label>    <glyph> <label>    …` (4 spaces between items). Easy to miss because text editors render the PUA codepoints as blank — hex-dump the string if in doubt.

After editing all JSON files, validate them: `for f in romfs/lang/*.json; do python3 -c "import json; json.load(open('$f'))"; done`

## Config

`source/util/config.cpp` holds the persisted JSON config. When adding a new setting:
- Declare it in `inst::config::` (top of `config.cpp`).
- Serialize it in `setConfig()`.
- Parse it in `parseConfig()`.
- Provide a default in the `catch (...)` block at the bottom of `parseConfig()`.
The defaults block also runs when any single key is missing, so always supply one.

## GitHub release fetching

`inst::util::fetchLatestRelease(releasesPageUrl, assetNameSubstring = "")` in `source/util/util.cpp:405` takes a `https://github.com/<owner>/<repo>/releases` URL, queries the GitHub API for the latest release, and returns `{tag, asset_download_url}`. By default it picks the first `.zip` asset (or the first asset if none are zips). When `assetNameSubstring` is non-empty, it only matches `.zip` assets whose name contains that substring — used for hekate, whose release ships both `hekate_ctcaer_X.Y.Z.zip` (no Nyx GUI) and `hekate_ctcaer_X.Y.Z_Nyx_A.B.C.zip` (with Nyx) and we want the latter. Empty vector means failure.

Component release URLs (Atmosphère, hekate, Ultrahand, sys-patch, emuiibo) live in `inst::config::` with hardcoded defaults — they're persisted in `config.json` so a power user can hand-edit them, but the Settings UI does NOT expose them. Only Leaf Installer's own update URL and the Amiibo API URL are editable in-app.

## CFW install flow + offline-update payload

The flow is split between HOS (the .nro) and BPMP (the payload) because Atmosphère's `ams.mitm` holds `/atmosphere/package3` and `stratosphere.romfs` open at runtime — they can't be replaced while HOS is running. The .nro stages files, the payload applies them after a reboot.

### HOS side (`source/cfwInstall.cpp`, namespace `cfw`)

- `cfw::startCfwInstall()` — refuses to proceed if `sdmc:/bootloader/payloads/leaf-updater.bin` is missing (means the user updated the NRO by hand instead of unpacking the release zip; shows `cfw.missing_payload_title` dialog). Otherwise: shows confirm dialog, fetches releases, populates `cfwinstpage` with `CfwComponent` entries.
- `cfw::installSelectedComponents(components)` — wipes `sdmc:/leaf-offline-update/`, then downloads + extracts each selected component **into the staging dir** (not the SD root). Per-component `postProcess` hooks run inside the staging dir (`emuiibo_sdout` flattens `SdOut/`, `hekate_payload` renames `hekate_ctcaer_*.bin` to `payload.bin`). After all components are staged, `armHekateAutoboot()` parses `bootloader/hekate_ipl.ini`, finds the index of `[Leaf Update]` and the current `autoboot=` value, snapshots the latter to `sdmc:/leaf-offline-update/.autoboot-prev`, rewrites `autoboot=` to the Leaf Update index. Prompts user to reboot.
- `armHekateAutoboot()` returns negative values for distinct failure modes (ini missing, `[Leaf Update]` section missing, `autoboot=` line missing, write failed). On any failure, the user gets the `cfw.arm_failed` message telling them to launch the payload manually from hekate's Payloads menu — staging still completed successfully.

`CfwComponent` struct (in `include/ui/cfwInstPage.hpp`) fields: `displayName`, `version` (latest available), `url`, `zipName`, `postProcess` (tag for per-component post-extract hook), `installedVersion` (filled from `inst::config::cfwInstalled` before showing the page), `defaultSelected` (default true; per-component knob — currently nothing overrides).

**Installed-version display**: each row shows the available version. If `displayName` is present in `inst::config::cfwInstalled` (a `std::map<std::string, std::string>` persisted in `config.json`, updated and `setConfig()`'d after every successful install), the row shows `installed  →  available`. We do NOT scan `/atmosphere/contents/*/toolbox.json` — that was tried and removed because most components don't ship one and Ultrahand bundles several sysmodules together, making the readout misleading.

The lang section for this flow is keyed `"cfw"` in all `romfs/lang/*.json` files (menu label at `main.menu.cfw`).

### Payload side (`payload/`)

Stand-alone BPMP binary built from `payload/source/{start.S,main.c,gfx.c,gfx.h,ffconf.h,diskio.c}` plus a curated subset of hekate's BDK (`payload/Makefile`'s `BDK_SUBDIRS` lists which directories get globbed in — `soc mem display power rtc utils sec storage libs/fatfs`). Linked at `0xC0000000` (the EXT_PAYLOAD_ADDR hekate uses when chainloading), ARMv4T / ARM7TDMI.

Steps the payload performs: `hw_init` → `display_init` → `gfx_init` → `sd_mount` → read `/leaf-offline-update/.autoboot-prev` (if present) → recursive merge-move of `/leaf-offline-update/*` to `/` (uses `f_rename` for cheap metadata moves when the target doesn't exist; recurses + per-file rename when it does, so we never clobber unrelated SD content) → rewrite `autoboot=` in `bootloader/hekate_ipl.ini` to the snapshotted value → `f_unlink` the staging dir → `power_set_state(POWER_OFF_REBOOT)`.

Gotchas worth remembering for next round:
- The payload links at **`0x40010000`** (IRAM run address), NOT at the DRAM staging address. Hekate's `_launch_payload()` copies the .bin bytes to `EXT_PAYLOAD_ADDR=0xC0000000` in DRAM as a staging buffer, then injects a relocator stub that copies them into IRAM at `PATCHED_RELOC_ENTRY=0x40010000` and jumps there. Linking against `0xC0000000` produces a binary whose absolute pointer loads all point at wrong memory once it actually runs from IRAM — silent crash, black screen, requires a 10-second power-button hold to recover. The bug-hunt for this took several rounds; do not regress.
- BDK's `sd_mount()` registers the SD as FATFS **volume `0:`**, NOT `sd:`. Using `f_stat("sd:/...")` etc. silently returns `NOT_ENABLED` and the payload takes the "nothing to do" fallback. Use bare paths (the default volume is `0:`) — that's the pattern hekate's own bootloader uses (e.g. `f_stat("bootloader", ...)`).
- Hekate's `gfx_printf` supports `%c %s %d %x %X %p %P %k %K %%` — it does NOT support `%u` or `%lu`. Unknown specifiers fall through to a literal `%` + the char. Use `%d` (cast to `int`) for u32 counts/sizes.
- `GFX_INC` and `FFCFG_INC` defines are required so hekate's `gfx_utils.h` and FATFS pick up our `payload/source/gfx.h` / `payload/source/ffconf.h`. Don't define `IPL_LOAD_ADDR` via `-D` — `memory_map.h` sets it, redefining triggers warnings on every BDK file.
- Compile C as **ARM** mode, not Thumb. Our `start.S` uses `.arm` and calls `BL main` (24-bit immediate ARM branch with no mode switch — ARMv4T has no `BLX label` form). If C is Thumb-compiled the CPU jumps into Thumb code while in ARM state and silently faults. The Makefile uses `-mthumb-interwork` but NOT `-mthumb` for that reason.
- The Makefile globs every `.c` in `BDK_SUBDIRS` and lets `--gc-sections` drop what isn't called. Adding new BDK call sites might pull in additional `undefined reference` errors — the fix is usually adding the source dir to `BDK_SUBDIRS`.
- After `display_init` + `display_init_window_a_pitch` + `gfx_init_ctxt` + `gfx_con_init`, calling `display_backlight_pwm_init()` alone is not enough — you also have to call `display_backlight_brightness(N, 1000)` to actually light up the panel.
- `btn_wait_timeout(ms, mask)` does NOT mean "wait for any of the buttons in `mask`." It waits for `res == mask`, i.e. **all** the bits in `mask` to be pressed at the same time. If you pass a multi-bit mask you'll wait forever (or until timeout, which then returns whatever buttons happen to be held in that instant). For "first of these buttons to be tapped," hand-roll a polling loop using `btn_read()` + edge detection (rising edges = `curr & ~prev`) with a `msleep(20)` tick. See `payload/source/main.c::show_exit_menu`.
- **The `include/hekate` submodule is pinned to v6.5.2 specifically because the payload's curated BDK build needs it.** hekate v6.5.3 rewrote `bdk/storage/sdmmc.c` (~515 lines) and the payload's `sd_mount()` hangs at runtime against it (links clean, then deadlocks on the new SD init path because our `BDK_SUBDIRS` subset doesn't pull in everything the rewrite expects). This pin only affects the BDK the **payload** compiles against — the hekate that gets *installed* to the user's SD card is fetched at runtime from `inst::config::hekateUrl` (GitHub), so users still get the latest hekate regardless. Do NOT bump this submodule past v6.5.2 without re-testing the payload's SD mount on real hardware.
- The payload renders **landscape** via software rotation in `payload/source/gfx.c` (ported from Lockpick_RCM_Pro / TegraExplorer): framebuffer stays portrait 720x1280, but `gfx_con_setpos`/`getpos` apply an `x ↔ 1279-y` transform and the 16px `gfx_putc` draws each glyph rotated 90°. Only the 16px font path is rotated; `gfx_clear_partial_grey`'s coordinate semantics do NOT map to text rows under rotation, so the exit menu repaints in place (reposition + reprint, relying on `fillbg=1`) instead of partial-clearing.

Historical note: the CFW module was previously named `sig` / "signature patches" / "Install Ultrahand and overlays" before being renamed to reflect its broader purpose. Do NOT reintroduce `sig*` symbols for this flow. Note that "signature patches" still legitimately appears in `source/nx/fs.cpp` for the unrelated NCA signature-patches concept — that one stays.

## HOME Menu shortcut

Settings → "Add Leaf Installer to the HOME Menu" installs a forwarder application (title ID `0x01004C4541460000`, "LEAF") that boots `sdmc:/switch/Leaf-Installer/Leaf-Installer.nro`. Approach ported from NSteamLink (https://github.com/kxn/nsteamlink); needs signature patches (sys-patch) to launch.

- **Template (build time, committed)**: `shortcut/build-shortcut.py` fetches nx-hbloader + hacBrewPack at pinned commits into `build/shortcut/`, patches hbloader (target path → our NRO; exit instead of falling back to hbmenu/aborting), builds it with `shortcut/application.json` as the NPDM (application permissions + hbloader's full syscall set from `hbl.json`, for parity with what Leaf gets under hbmenu), packs plaintext NCAs with a throwaway key, decrypts the headers again and writes `romfs/shortcut/template.bin` (`LEAFSC01` magic, 3×u32 sizes, then program/control/meta NCAs). No real keys involved. Regenerate with `make shortcut` in the devkita64 container (needs network; installs python3-cryptography/python3-pil via apt) only when icon/name/target path change — the NACP version is fixed at 1.0.0 so releases don't require it.
- **Runtime (`source/shortcutInstall.cpp`, namespace `shortcut`)**: checks the NRO exists at the canonical path, refuses if the title ID is already present (ns records incl. archived, SD + NAND meta DBs, content IDs), then seals each NCA header with the console header key (`Crypto::Keys()` → SPL), recomputes the dependent hashes (CNMT content records + digest, PFS0 block hashes, master hash, FS header hash), registers the 3 NCAs on SD via NCM, sets + commits the meta DB entry and pushes the application record. Any failure rolls back what was registered. Wrap calls in `inst::util::initInstallServices()` / `deinitInstallServices()`.
- The shortcut never needs reinstalling when Leaf updates — it just launches whatever NRO is at the path. Users remove it from System Settings → Data Management.
- **Exit handshake**: `shortcut::configureHomeExit()` (first thing in `main()`) sets `__nx_applet_exit_mode = 1` when `!envIsNso()` and our program ID is the shortcut's. libnx's NRO default (0) returns to the loader without `SelfController::Exit`; under hbmenu that's fine, but the forwarder owns an application process, so AM sees it die unannounced and HOME shows a crash on every exit. Don't remove it or apply it unconditionally (it would break hbmenu/title-takeover returns).
- The NCA header content-type byte (0x205: Program=0, Meta=1, Control=2) uses different numbering from libnx's `NcmContentType_*` (Meta=0, Program=1, Control=3). Mixing them up makes the template check fail with `template (0x00000000)`.
- Include `nx/ipc/tin_ipc.h` (has the `extern "C"` wrapper), not `nx/ipc/ns_ext.h` directly, from C++ — otherwise `nsPushApplicationRecord` fails to link.

## Conventions

- The render thread is single-threaded — long work runs synchronously from the click handler and pushes progress via `instPage::setInstBarPerc()` / `setInstInfoText()` (each calls `mainApp->CallForRender()`).
- Audio playback uses `std::thread(inst::util::playAudio, path)` and joins after the dialog closes (see `sdInstall.cpp`).
- Storage info blocks: call `inst::util::addStorageInfoBlocks(this, 1900, 25)` in any page constructor that should show free/total memory in the top-right.
- The `mainApp` global is declared `extern` in each `ui/*Page.cpp`.
- Coordinates: screen is 1920×1080. Top bar at y=0–141, info rect at 142–232, content area below, button hint at y=1017.
- Selection checkbox icons: `romfs:/images/icons/checkbox-blank-outline.png` (unchecked) and `check-box-outline.png` (checked).

## Don'ts

- Don't add the `-it` flag to the podman command in automated builds — it requires a TTY.
- Don't `mkdir` `include/Plutonium/` or `include/libusbhsfs/` contents; they're vendored submodules/copies and the Makefile rebuilds them.
- Don't introduce English-only strings; see translations section above.
