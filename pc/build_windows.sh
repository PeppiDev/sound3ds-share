#!/usr/bin/env bash
# Sound3DS-Share.exe para Windows 10/11, compilado con Python para Windows bajo Wine.
# Abre la interfaz en Edge (viene con Windows) como ventana de app.
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
podman run --rm --security-opt label=disable -v "$DIR:/src:Z" -w /src docker.io/tobix/pywine:3.11 bash -c '
  set -e
  # NumPy 2 llama a ucrtbase.crealf, que el Wine de la imagen no tiene: la 1.26 basta
  wine python -m pip install -q pyinstaller -r requirements.txt "numpy<2" 2>/dev/null
  wine pyinstaller --noconfirm --clean --onefile --noconsole --name Sound3DS-Share \
    --add-data "Z:\\src\\web;web" --icon "Z:\\src\\icon.ico" \
    --distpath dist/windows --workpath build/windows --specpath build/windows sound3ds_share.py 2>&1 | tail -3
'
ls -la "$DIR/dist/windows/Sound3DS-Share.exe"
