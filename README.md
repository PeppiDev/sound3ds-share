> [!WARNING]
> **Rama `development`: Sound3DS Share 1.1.0 beta para testers.** Puede tener errores; la versión estable está en la rama `main`.
> **`development` branch: Sound3DS Share 1.1.0 beta for testers.** It may have bugs; the stable version is on the `main` branch.

**Descargar la beta:** [pre-release v1.1.0-beta.1](https://github.com/PeppiDev/sound3ds-share/releases/tag/v1.1.0-beta.1)
- Windows 10/11: [`Sound3DS-Share.exe`](https://github.com/PeppiDev/sound3ds-share/releases/download/v1.1.0-beta.1/Sound3DS-Share.exe)
- Linux: [`Sound3DS-Share`](https://github.com/PeppiDev/sound3ds-share/releases/download/v1.1.0-beta.1/Sound3DS-Share) (dale permiso de ejecución)

Va con **Sound3DS 0.7.0 beta** en la consola: [rama development de Sound3DS](https://github.com/PeppiDev/sound3ds/tree/development) (ahí está el QR para instalarla).
Novedades: botón **Conectar SoundCloud** para usar tu cuenta en la consola, y se escanea desde **Sound3DS → Configuración → Recibir desde el PC**.
**Si algo falla:** anota tu sistema (Windows/Linux) y qué hacías.

---

# Sound3DS Share 📲🎵

**English** | [Español](README.es.md)

### Send music from your PC to your Nintendo 3DS / 2DS by scanning a QR code

**Sound3DS Share** sends songs from your PC to your console over Wi-Fi, without taking out the SD card. You pick the songs on your PC, check their albums and cover art, and the console downloads them when you scan a QR code. They end up ready in **[Sound3DS](https://github.com/PeppiDev/sound3ds) > Local**, each one in its album in the cover flow.

It's two programs that work together:

| | Runs on | What it does |
|---|---|---|
| **Sound3DS Share** | PC (Linux or Windows) | Browses your files, builds the list by album and generates the QR code |
| **Sound3DS QR** | 3DS / 2DS / New 3DS | Scans the QR code with the camera and downloads the songs to `sdmc:/music` |

<p align="center">
  <img src="docs/share-pc.png" alt="Sound3DS Share: file browser and songs grouped by album" width="760">
</p>

---

## 📥 Installation

### On the console: Sound3DS QR
<p align="center">
  <a href="https://github.com/PeppiDev/sound3ds-share/releases/latest/download/sound3ds-qr.cia">
    <img src="docs/qr-cia.png" alt="QR code to install sound3ds-qr.cia with FBI" width="200">
  </a>
</p>

On your console, open **FBI → Remote Install → Scan QR Code** and scan the code: it installs the `.cia` of the latest release. When it finishes, Sound3DS QR shows up on the HOME menu. Requires a console with CFW (Luma3DS) and Wi-Fi.

You can also download `sound3ds-qr.cia` (or `sound3ds-qr.3dsx` for the Homebrew Launcher) from [Releases](https://github.com/PeppiDev/sound3ds-share/releases/latest).

To listen to the songs you need **[Sound3DS](https://github.com/PeppiDev/sound3ds)**, which installs the same way, with its own QR code.

### On the PC: Sound3DS Share
Download the executable for your system from [Releases](https://github.com/PeppiDev/sound3ds-share/releases/latest):

| System | File | How to open it |
|---|---|---|
| **Windows 10 / 11** | `Sound3DS-Share.exe` | Double-click it. The first time, Windows asks about the firewall: check **Private networks** and click **Allow access**, otherwise the console won't be able to connect. If SmartScreen shows up ("Windows protected your PC"), click **More info → Run anyway**. |
| **Linux** | `Sound3DS-Share` | Make it executable (`chmod +x Sound3DS-Share` or Properties → Permissions → "Allow executing") and open it. Works on almost any recent distro: Ubuntu 20.04+, Debian 11+, Fedora, Arch, Mint, openSUSE, Pop!_OS... |

Nothing else to install. The interface opens as its own window if you have Edge, Chrome, Chromium or Brave (Windows always ships Edge); otherwise it opens in your default browser.

<details>
<summary>Linux: add it to your application menu (optional)</summary>

```bash
install -Dm755 Sound3DS-Share ~/.local/bin/Sound3DS-Share
cat > ~/.local/share/applications/sound3ds-share.desktop <<EOF
[Desktop Entry]
Type=Application
Name=Sound3DS Share
Comment=Send songs to your console with Sound3DS QR
Exec=$HOME/.local/bin/Sound3DS-Share %F
Icon=audio-x-generic
Terminal=false
Categories=AudioVideo;Audio;
EOF
```
</details>

---

## 🚀 How to use it

1. **On the PC**, open Sound3DS Share.
2. **Browse your folders** on the left. Check songs and click **Add**, or use **+ folder** to add a whole folder (double-clicking a song works too).
3. **Review the list** on the right. Songs are grouped by album with the cover art and tags they have. Fix whatever you want (see below).
4. Click **Generate QR**.
5. **On the console**, open Sound3DS QR and point the outer camera at the QR code, about 20 cm (8 in) away.
6. The console downloads the songs and the PC shows the progress of each one. When it's done, open them in **Sound3DS > Local**.

<p align="center">
  <img src="docs/share-qr.png" alt="Generated QR code and download progress on the PC" width="620">
</p>

<p align="center">
  <img src="docs/qr-scan.png" alt="Sound3DS QR looking for the code" width="300">
  &nbsp;
  <img src="docs/qr-done.png" alt="Sound3DS QR with the 9 songs downloaded" width="300">
  <br>
  <em>Sound3DS QR looking for the code, and after the download</em>
</p>

---

## 💿 Albums, cover art and tags

Sound3DS sorts your local music by each song's **album tag**, not by folders. Sound3DS Share lets you get that right before sending.

**Detected automatically:**
- Title, artist, album, album artist and track number from the ID3 tags.
- The cover art embedded in the song. If it has none, `cover.jpg`, `folder.jpg` or `front.jpg` from its folder is used.
- If the title or artist is missing, they're taken from the file name (`01. Artist - Title.mp3`).

**You can change:**
- **Album name** and **album artist**: applied to every song in the album. If you type the name of another album in the list, they're merged.
- **Track number, title and artist** of each song, right in the table.
- **Move to**: moves a song to another album in the list or to a new one.
- **Cover art**: click the square to pick an image (JPG or PNG). If an album has no cover, the square is shown in orange. If its songs have different covers, click the **unify** badge to use the main one for all of them.

Your songs on the PC are **not modified**. When they're sent, they get a new tag with what you see on screen and the cover art scaled down to 500 px, the size the console handles best. On the console they're saved as `Artist - Title.mp3`.

You can send `.mp3` and `.aac` files, which are the formats Sound3DS plays.

---

## 🎮 Sound3DS QR on the console

- The camera image is shown on the top screen with a viewfinder. When the code is recognized, the corners turn green.
- While downloading, the top screen shows the overall progress, the current song, MB and speed. The bottom screen shows the list with the status of each song.
- **(B)**: cancels the download. **HOME** works while downloading, and closing the lid doesn't interrupt it.
- **(A)**: scan another QR code when it's done. **START**: exit.
- Songs that are already on the SD card are skipped.
- It uses the same language you picked in Sound3DS (English or Spanish).

The QR code can also be the URL of a single `.mp3`/`.aac`, or of a text list with one URL per line.

---

## 🛠️ Troubleshooting

| Problem | What to check |
|---|---|
| The console reads the QR code but doesn't download | That the PC and the console are on the **same Wi-Fi network** and that the firewall allows Sound3DS Share (on Windows, "Private networks"). Some guest networks isolate devices from each other. |
| The PC has several networks (VPN, Tailscale, cable + Wi-Fi) | At the top, next to the service switch, pick your home network's IP address and generate the QR code again. |
| The camera doesn't recognize the code | Move a bit closer or farther, avoid reflections on the PC screen and turn up the brightness. |
| The window doesn't show up | The app opens your browser at `http://127.0.0.1:8764`. You can open that address by hand. |

**Ports:** the interface uses `127.0.0.1:8764` and is only reachable from your PC, since it can browse your files. The console connects to port `8765` (or the next free one), which only serves the songs in the list.

---

## 🔧 Building

### PC
From source, with Python 3.9 or newer:
```bash
cd pc
pip install -r requirements.txt
python3 sound3ds_share.py
```

Executables (require `podman`):
```bash
./pc/build_linux.sh     # pc/dist/linux/Sound3DS-Share (built on Debian 11, glibc 2.31)
./pc/build_windows.sh   # pc/dist/windows/Sound3DS-Share.exe (Windows Python under Wine)
```

### Console
With the devkitARM image on `podman`:
```bash
./3ds/build.sh          # 3ds/sound3ds-qr.3dsx
./3ds/build_cia.sh      # 3ds/sound3ds-qr.cia (requires makerom and bannertool)
```
`build_cia.sh` looks for the tools in `~/.local/share/3ds-tools`. You can change the path with `CTR_TOOLS_DIR`, or point to each one with `MAKEROM` and `BANNERTOOL`.

### Layout
```
pc/                 Sound3DS Share (Python: server + web interface in pc/web)
3ds/                Sound3DS QR (C, libctru + citro2d)
  source/camera.c   continuous camera capture on its own thread
  source/job.c      list reading and downloads with libcurl
  source/ui.c       interface for both screens
  source/quirc/     QR decoder (ISC license)
docs/               images for this README
```

---

## 🙌 Credits

- [quirc](https://github.com/dlbeer/quirc) (ISC) to read QR codes on the console.
- [mutagen](https://github.com/quodlibet/mutagen), [Pillow](https://python-pillow.org) and [python-qrcode](https://github.com/lincolnloop/python-qrcode) on the PC.
- [devkitPro](https://devkitpro.org) (libctru, citro2d, libcurl) for the console app.
