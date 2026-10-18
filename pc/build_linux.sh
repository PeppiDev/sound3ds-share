#!/usr/bin/env bash
# Ejecutable único para Linux. Se compila sobre Debian 11 (glibc 2.31) para que
# funcione en casi cualquier distro de los últimos años (Ubuntu 20.04+, Fedora,
# Arch, Mint, openSUSE, Debian 11+...). Solo requiere un navegador instalado.
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
podman run --rm --security-opt label=disable -v "$DIR:/src:Z" -w /src docker.io/library/python:3.11-slim-bullseye bash -c '
  set -e
  apt-get update -qq && apt-get install -y -qq binutils >/dev/null
  pip install -q --root-user-action=ignore pyinstaller -r requirements.txt
  pyinstaller --noconfirm --clean --onefile --name Sound3DS-Share \
    --add-data /src/web:web --icon /src/icon.png \
    --distpath dist/linux --workpath build/linux --specpath build/linux sound3ds_share.py
'
ls -la "$DIR/dist/linux/Sound3DS-Share"
