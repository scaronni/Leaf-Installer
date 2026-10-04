#!/usr/bin/env python3
"""Build the HOME Menu shortcut template (romfs/shortcut/template.bin).

The template is a key-free, plaintext forwarder package — NOT an installable
NSP. It holds the program, control and meta NCAs of a patched nx-hbloader that
launches sdmc:/switch/Leaf-Installer/Leaf-Installer.nro. Leaf Installer seals
the NCA headers at runtime with the console's own header key (derived through
SPL) and installs the result via NCM, so no keys ever ship with the app.

Approach adapted from NSteamLink (https://github.com/kxn/nsteamlink, GPLv3).

Run inside the devkitpro/devkita64 image via `make shortcut`. Needs network
access (fetches nx-hbloader and hacBrewPack) and python3-cryptography +
python3-pil. The output only has to be regenerated when the forwarder itself
changes (icon, name, target path) — it is committed to the repo.
"""
import argparse
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

HBLOADER = ('https://github.com/switchbrew/nx-hbloader.git', '82b95122c5ae8dc059bf23893ba7623c72c86773')
HACBREWPACK = ('https://github.com/dragonflylee/hacBrewPack.git', '745b16ecfc9ce055743067d200572204cb2aac6c')
MAGIC = b'LEAFSC01'


def checkout(repo, revision, dest):
    if not (dest / '.git').exists():
        subprocess.run(['git', 'init', '-q', str(dest)], check=True)
        subprocess.run(['git', '-C', str(dest), 'remote', 'add', 'origin', repo], check=True)
    subprocess.run(['git', '-C', str(dest), 'fetch', '-q', '--depth', '1', 'origin', revision], check=True)
    subprocess.run(['git', '-C', str(dest), 'checkout', '-q', '--detach', 'FETCH_HEAD'], check=True)


def pfs_files(data):
    magic, count, strings, _ = struct.unpack_from('<4sIII', data)
    assert magic == b'PFS0' and count < 100
    base = 16 + count * 24 + strings
    for i in range(count):
        offset, size, _, _ = struct.unpack_from('<QQII', data, 16 + i * 24)
        assert base + offset + size <= len(data)
        yield data[base + offset:base + offset + size]


def main():
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from PIL import Image

    root = Path(__file__).resolve().parent.parent
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, default=root / 'romfs/shortcut/template.bin')
    p.add_argument('--nro', default='sdmc:/switch/Leaf-Installer/Leaf-Installer.nro')
    p.add_argument('--name', default='Leaf Installer')
    p.add_argument('--author', default='slaanesh')
    p.add_argument('--version', default='1.0.0')
    p.add_argument('--icon', type=Path, default=root / 'icon.jpg')
    a = p.parse_args()

    config = json.loads((root / 'shortcut/application.json').read_text())
    tid = config['program_id'].removeprefix('0x')

    work = root / 'build/shortcut'
    deps = work / 'deps'
    deps.mkdir(parents=True, exist_ok=True)
    checkout(*HBLOADER, deps / 'nx-hbloader')
    checkout(*HACBREWPACK, deps / 'hacBrewPack')
    hbp = deps / 'hacBrewPack'
    shutil.copyfile(hbp / 'config.mk.template', hbp / 'config.mk')
    subprocess.run(['make', '-C', str(hbp), f'-j{os.cpu_count()}'], check=True, stdout=subprocess.DEVNULL)

    # Patched hbloader: boot our NRO instead of hbmenu, and exit instead of
    # falling back to hbmenu (or aborting) when the NRO returns or is missing.
    loader = work / 'loader'
    if loader.exists():
        shutil.rmtree(loader)
    (loader / 'source').mkdir(parents=True)
    hbl = deps / 'nx-hbloader'
    src = (hbl / 'source/main.c').read_text()
    assert src.count('"sdmc:/hbmenu.nro"') == 1
    src = src.replace('"sdmc:/hbmenu.nro"', json.dumps(a.nro))
    needle = "if (g_nextNroPath[0] == '\\0')\n    {"
    assert src.count(needle) == 1
    src = src.replace(needle, "static bool launched = false;\n    " + needle +
                      "\n        if (launched) svcExitProcess();\n        launched = true;")
    missing = 'diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 3));'
    assert src.count(missing) == 1
    src = src.replace(missing, 'svcExitProcess();')
    (loader / 'source/main.c').write_text(src)
    shutil.copyfile(hbl / 'source/trampoline.s', loader / 'source/trampoline.s')
    shutil.copyfile(hbl / 'Makefile', loader / 'Makefile')
    (loader / 'hbl.json').write_text(json.dumps(config))
    subprocess.run(['make', f'-j{os.cpu_count()}'], cwd=loader, check=True, stdout=subprocess.DEVNULL)

    for name in ('exefs', 'control', 'out'):
        (loader / name).mkdir(exist_ok=True)
    shutil.copyfile(loader / 'hbl.nso', loader / 'exefs/main')
    shutil.copyfile(loader / 'hbl.npdm', loader / 'exefs/main.npdm')
    tools = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro')) / 'tools/bin'
    subprocess.run([str(tools / 'nacptool'), '--create', a.name, a.author, a.version,
                    str(loader / 'control/control.nacp'), f'--titleid={tid}'], check=True)
    # HOME Menu only renders 256x256 baseline JPEGs.
    icon = io.BytesIO()
    Image.open(a.icon).convert('RGB').resize((256, 256)).save(icon, 'JPEG', quality=95, progressive=False)
    (loader / 'control/icon_AmericanEnglish.dat').write_bytes(icon.getvalue())

    # Throwaway public key so hacBrewPack can run; headers are decrypted again
    # below and re-sealed on the console with its real header key.
    fake_key = bytes(range(32))
    (loader / 'synthetic.keys').write_text('header_key = ' + fake_key.hex() +
                                           '\nkey_area_key_application_00 = ' + bytes(range(16)).hex() + '\n')
    subprocess.run([str(hbp / 'hacbrewpack'), '-k', str(loader / 'synthetic.keys'), '--titleid', tid,
                    '--nspdir', str(loader / 'out'), '--noromfs', '--nologo', '--plaintext',
                    '--keygeneration', '1'], cwd=loader, check=True, stdout=subprocess.DEVNULL)
    parts = {}
    for file in pfs_files((loader / 'out' / (tid + '.nsp')).read_bytes()):
        data = bytearray(file)
        for sector in range(6):
            # Nintendo's XTS tweak is the big-endian sector number.
            dec = Cipher(algorithms.AES(fake_key), modes.XTS(sector.to_bytes(16, 'big'))).decryptor()
            data[sector * 512:(sector + 1) * 512] = dec.update(data[sector * 512:(sector + 1) * 512]) + dec.finalize()
        assert data[0x200:0x204] == b'NCA3'
        assert struct.unpack_from('<Q', data, 0x210)[0] == int(tid, 16)
        assert data[0x404] == 1  # section 0 is unencrypted
        data[0x300:0x340] = bytes(64)  # plaintext sections have no key area
        parts[data[0x205]] = data
    assert set(parts) == {0, 1, 2}  # program, meta, control
    order = (0, 2, 1)  # program, control, meta
    bundle = MAGIC + struct.pack('<III', *(len(parts[k]) for k in order))
    bundle += b''.join(parts[k] for k in order)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(bundle)
    # The synthetic NSP is not a distributable artifact.
    (loader / 'out' / (tid + '.nsp')).unlink()
    print(f'{a.output}: {len(bundle)} bytes (title {tid}, target {a.nro})')


if __name__ == '__main__':
    main()
