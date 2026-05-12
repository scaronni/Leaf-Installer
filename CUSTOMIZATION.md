# Customization

Awoo Installer ships with default graphics and sounds, but the user can override any of them at runtime by dropping replacement files into the app's data folder on the SD card.

## Where to put the files

All customization files go in:

```
sdmc:/switch/Awoo-Installer/
```

This is the same folder where `config.json` (Awoo Installer's settings) lives. The folder is created automatically the first time you launch the app, so you can drop replacements in afterwards.

## Files you can override

| Filename | Replaces | Where it appears | Render size | Format |
|---|---|---|---|---|
| `background.png` | `romfs:/images/background.jpg` | Full-screen background on every page | **1920 × 1080** | **PNG-24** (no transparency) or PNG-32 |
| `logo.png` | *(no override)* — bundled only | Top-bar "Awoo Installer" wordmark | **720 × 140** | PNG-32 (RGBA — needs transparency) |
| `awoo_main.png` | `romfs:/images/awoos/5bbdbcf9a5625cd307c9e9bc360d78bd.png` | Decorative figure on the right side of the **main menu** | **1296 × 732** | **PNG-32** (RGBA) |
| `awoo_inst.png` | `romfs:/images/awoos/7d8a05cddfef6da4901b20d2698d5a71.png` | Decorative figure on the right side of the **install / progress screen** | **1146 × 831** | **PNG-32** (RGBA) |
| `awoo.wav` | `romfs:/audio/awoo.wav` | Plays on **successful install / Amiibo generation completion** | n/a | **WAV** (signed 16-bit PCM, mono or stereo, 22050 or 44100 Hz) |
| `bark.wav` | `romfs:/audio/bark.wav` | Plays on **install failure** and **NCA signature-verification warning** | n/a | **WAV** (same as above) |

Notes on each:

- **Render size matters.** The image is stretched to the listed dimensions at render time using bilinear filtering. Matching the listed size exactly is sharpest; oversized PNGs waste space without improving quality; undersized PNGs look soft.
- **PNG-32 vs PNG-24.** Use PNG-32 when the image has transparent regions (the awoo figures and the logo need this — the dark-red bar should show through the alpha). Use PNG-24 or even PNG-8 for fully opaque content like the background.
- **Background is JPG by default**, but the override file must be named `background.png`. JPG is bigger-for-equivalent-quality for natural images, but PNG is what Awoo looks for at the override path.
- **Sound format.** The `SDL2_mixer` runtime accepts 16-bit signed PCM WAV at standard sample rates (22050 / 44100 Hz). MP3 / OGG / FLAC are *not* loaded from these override paths.

## How override resolution works

For each asset, Awoo Installer checks if the override file exists in `sdmc:/switch/Awoo-Installer/` first. If it does, that file is used. Otherwise the bundled romfs default loads. There is no need to delete the bundled file — your replacement simply takes priority.

## Interactions with Settings

The Settings menu has a **"Remove anime"** toggle (the `gayMode` flag in `config.json`) that turns off the decorative content even if you've replaced it:

- The decorative awoo figure (`awoo_main.png` / `awoo_inst.png`) is hidden on every page.
- Success and failure sounds (`awoo.wav` / `bark.wav`) are silenced.
- The "Awoo Installer" wordmark slides to the left to leave room for the version text.

## Building your own assets

Best results come from authoring at the exact target render size. Recommendations:

- **Photographs / painted backgrounds** → 1920×1080, PNG-24 quality. If file size matters, export JPG quality 85 and rename to `background.png`.
- **Character art with transparency** → 1296×732 (main) or 1146×831 (install), PNG-32 quality.
- **Logos / wordmarks** → 720×140, PNG-32. Anti-alias the edges and leave a couple of pixels of transparent padding to avoid hard edges against the top bar.
- **Sounds** → 16-bit PCM WAV. Keep them short (1–3 seconds). Loudness should be normalized to peak around -3 dBFS so they're audible without being jarring.

## Restoring defaults

To revert any single asset, just delete (or rename) the override file in `sdmc:/switch/Awoo-Installer/`. The bundled romfs asset will be used on the next launch.
