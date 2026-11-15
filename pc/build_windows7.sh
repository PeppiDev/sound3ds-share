#!/usr/bin/env bash
# Sound3DS-Share-Win7.exe para Windows 7 SP1 (y posteriores), de 32 bits.
# Windows 7 no ejecuta Python >= 3.9, así que se compila con Python 3.8 para Windows
# (imagen tobix/pywine:3.8, que trae Python 3.8 de 32 bits bajo Wine). Un .exe de
# 32 bits corre igual en Windows 7 de 32 y de 64 bits.
# Versiones fijadas a las últimas con ruedas cp38 win32 que siguen funcionando en Win7;
# PyInstaller 5.13.2 es la última 5.x (la 6.x ya no declara soporte para Windows 7).
# "Conectar SoundCloud" necesita Chrome 109 (el último para Windows 7); sin navegador
# compatible la interfaz se abre en el navegador predeterminado.
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
podman run --rm --security-opt label=disable -v "$DIR:/src:Z" -w /src docker.io/tobix/pywine:3.8 bash -c '
  set -e
  wine python -m pip install -q \
    pyinstaller==5.13.2 pefile==2023.2.7 pywin32-ctypes==0.2.3 altgraph setuptools \
    mutagen==1.47.0 qrcode==7.4.2 pillow==10.4.0 certifi numpy==1.24.4 \
    soundfile==0.13.1 soxr==0.3.5 2>/dev/null
  wine pyinstaller --noconfirm --clean --onefile --noconsole --name Sound3DS-Share-Win7 \
    --add-data "Z:\\src\\web;web" --icon "Z:\\src\\icon.ico" \
    --distpath dist/windows7 --workpath build/windows7 --specpath build/windows7 sound3ds_share.py 2>&1 | tail -3
'
ls -la "$DIR/dist/windows7/Sound3DS-Share-Win7.exe"
