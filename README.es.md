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

[English](README.md) | **Español**

### Pasa música del PC a tu Nintendo 3DS / 2DS escaneando un QR

**Sound3DS Share** manda canciones desde el PC a la consola por Wi-Fi, sin sacar la tarjeta SD. Eliges las canciones en el PC, revisas sus álbumes y carátulas, y la consola las descarga al escanear un QR. Quedan listas en **[Sound3DS](https://github.com/PeppiDev/sound3ds) > Local**, cada una en su álbum dentro del cover flow.

Son dos programas que trabajan juntos:

| | Dónde corre | Qué hace |
|---|---|---|
| **Sound3DS Share** | PC (Linux o Windows) | Explora tus archivos, arma la lista por álbum y genera el QR |
| **Sound3DS QR** | 3DS / 2DS / New 3DS | Escanea el QR con la cámara y descarga las canciones a `sdmc:/music` |

<p align="center">
  <img src="docs/share-pc.png" alt="Sound3DS Share: explorador de archivos y canciones agrupadas por álbum" width="760">
</p>

---

## 📥 Instalación

### En la consola: Sound3DS QR
<p align="center">
  <a href="https://github.com/PeppiDev/sound3ds-share/releases/latest/download/sound3ds-qr.cia">
    <img src="docs/qr-cia.png" alt="QR para instalar sound3ds-qr.cia con FBI" width="200">
  </a>
</p>

En la consola abre **FBI → Remote Install → Scan QR Code** y escanea el código: instala el `.cia` de la última versión. Al terminar, Sound3DS QR aparece en el menú HOME. Requiere una consola con CFW (Luma3DS) y conexión Wi-Fi.

También puedes bajar `sound3ds-qr.cia` (o `sound3ds-qr.3dsx` para el Homebrew Launcher) desde [Releases](https://github.com/PeppiDev/sound3ds-share/releases/latest).

Para escuchar las canciones necesitas **[Sound3DS](https://github.com/PeppiDev/sound3ds)**, que se instala igual, con su propio QR.

### En el PC: Sound3DS Share
Baja el ejecutable de tu sistema desde [Releases](https://github.com/PeppiDev/sound3ds-share/releases/latest):

| Sistema | Archivo | Cómo abrirlo |
|---|---|---|
| **Windows 10 / 11** | `Sound3DS-Share.exe` | Doble clic. La primera vez Windows pregunta por el firewall: marca **Redes privadas** y toca **Permitir**, si no la consola no podrá conectarse. Si aparece SmartScreen ("Windows protegió su PC"), toca **Más información → Ejecutar de todas formas**. |
| **Linux** | `Sound3DS-Share` | Dale permiso de ejecución (`chmod +x Sound3DS-Share` o Propiedades → Permisos → "Permitir ejecutar") y ábrelo. Funciona en casi cualquier distro de los últimos años: Ubuntu 20.04+, Debian 11+, Fedora, Arch, Mint, openSUSE, Pop!_OS... |

No hay que instalar nada más. La interfaz se abre como ventana propia si tienes Edge, Chrome, Chromium o Brave (Windows siempre trae Edge); si no, se abre en tu navegador predeterminado.

<details>
<summary>Linux: agregarla al menú de aplicaciones (opcional)</summary>

```bash
install -Dm755 Sound3DS-Share ~/.local/bin/Sound3DS-Share
cat > ~/.local/share/applications/sound3ds-share.desktop <<EOF
[Desktop Entry]
Type=Application
Name=Sound3DS Share
Comment=Envía canciones a la consola con Sound3DS QR
Exec=$HOME/.local/bin/Sound3DS-Share %F
Icon=audio-x-generic
Terminal=false
Categories=AudioVideo;Audio;
EOF
```
</details>

---

## 🚀 Cómo se usa

1. **En el PC**, abre Sound3DS Share.
2. **Explora tus carpetas** a la izquierda. Marca canciones y toca **Agregar**, o usa **+ carpeta** para agregar una carpeta entera (también sirve doble clic sobre una canción).
3. **Revisa la lista** de la derecha. Las canciones se agrupan por álbum con la carátula y las etiquetas que traen. Corrige lo que quieras (ver más abajo).
4. Toca **Generar QR**.
5. **En la consola**, abre Sound3DS QR y apunta la cámara de afuera al QR, a unos 20 cm.
6. La consola descarga las canciones y el PC muestra el avance de cada una. Al terminar, ábrelas en **Sound3DS > Local**.

<p align="center">
  <img src="docs/share-qr.png" alt="QR generado y avance de la descarga en el PC" width="620">
</p>

<p align="center">
  <img src="docs/qr-scan.png" alt="Sound3DS QR buscando el código" width="300">
  &nbsp;
  <img src="docs/qr-done.png" alt="Sound3DS QR con las 9 canciones descargadas" width="300">
  <br>
  <em>Sound3DS QR buscando el código y al terminar la descarga</em>
</p>

---

## 💿 Álbumes, carátulas y etiquetas

Sound3DS ordena tu música local por la **etiqueta de álbum** de cada canción, no por carpetas. Sound3DS Share te deja dejarlo bien antes de enviar.

**Se detecta solo:**
- Título, artista, álbum, artista del álbum y número de pista desde las etiquetas ID3.
- La carátula incrustada en la canción. Si no trae, se usa `cover.jpg`, `folder.jpg` o `front.jpg` de su carpeta.
- Si falta el título o el artista, se toman del nombre del archivo (`01. Artista - Título.mp3`).

**Puedes cambiar:**
- **Nombre del álbum** y **artista del álbum**: se aplica a todas las canciones del álbum. Si escribes el nombre de otro álbum de la lista, se juntan.
- **Número, título y artista** de cada canción, directamente en la tabla.
- **Mover a**: pasa una canción a otro álbum de la lista o a uno nuevo.
- **Carátula**: toca el recuadro para elegir una imagen (JPG o PNG). Si un álbum no tiene carátula, el recuadro sale naranja. Si sus canciones traen carátulas distintas, toca el aviso **unificar** para usar la principal en todas.

Las canciones del PC **no se modifican**. Al enviarlas se les pone una etiqueta nueva con lo que ves en pantalla y la carátula reducida a 500 px, el tamaño que mejor maneja la consola. En la consola se guardan como `Artista - Título.mp3`.

Se pueden enviar archivos `.mp3` y `.aac`, que son los que reproduce Sound3DS.

---

## 🎮 Sound3DS QR en la consola

- La imagen de la cámara se ve en la pantalla de arriba con un visor. Al reconocer el código, las esquinas se ponen verdes.
- Mientras descarga, arriba se ve el avance total, la canción actual, MB y velocidad. Abajo, la lista con el estado de cada canción.
- **(B)**: cancela la descarga. **HOME** funciona mientras descarga y cerrar la tapa no la corta.
- **(A)**: escanea otro QR al terminar. **START**: sale.
- Las canciones que ya estaban en la SD se saltan.
- Usa el mismo idioma que elegiste en Sound3DS (inglés o español).

El QR también puede ser directamente la URL de un `.mp3`/`.aac`, o la de una lista de texto con una URL por línea.

---

## 🛠️ Problemas comunes

| Problema | Qué revisar |
|---|---|
| La consola lee el QR pero no descarga | Que el PC y la consola estén en la **misma red Wi-Fi** y que el firewall permita Sound3DS Share (en Windows, "Redes privadas"). Algunos routers de invitados aíslan los dispositivos entre sí. |
| El PC tiene varias redes (VPN, Tailscale, cable + Wi-Fi) | Arriba, junto al interruptor del servicio, elige la dirección IP de la red de tu casa y genera el QR de nuevo. |
| La cámara no reconoce el código | Acércate o aléjate un poco, evita reflejos en la pantalla del PC y sube el brillo. |
| No aparece la ventana | La app abre tu navegador en `http://127.0.0.1:8764`. Puedes abrir esa dirección a mano. |

**Puertos:** la interfaz usa `127.0.0.1:8764` y solo es accesible desde tu PC, porque permite explorar tus archivos. La consola se conecta al puerto `8765` (o al siguiente libre), que solo entrega las canciones de la lista.

---

## 🔧 Compilación

### PC
Desde el código, con Python 3.9 o más nuevo:
```bash
cd pc
pip install -r requirements.txt
python3 sound3ds_share.py
```

Ejecutables (requieren `podman`):
```bash
./pc/build_linux.sh     # pc/dist/linux/Sound3DS-Share (compilado sobre Debian 11, glibc 2.31)
./pc/build_windows.sh   # pc/dist/windows/Sound3DS-Share.exe (Python para Windows bajo Wine)
```

### Consola
Con la imagen de devkitARM en `podman`:
```bash
./3ds/build.sh          # 3ds/sound3ds-qr.3dsx
./3ds/build_cia.sh      # 3ds/sound3ds-qr.cia (requiere makerom y bannertool)
```
`build_cia.sh` busca las herramientas en `~/.local/share/3ds-tools`. Puedes cambiar la ruta con `CTR_TOOLS_DIR`, o indicar cada una con `MAKEROM` y `BANNERTOOL`.

### Estructura
```
pc/                 Sound3DS Share (Python: servidor + interfaz web en pc/web)
3ds/                Sound3DS QR (C, libctru + citro2d)
  source/camera.c   captura continua de la cámara en su propio hilo
  source/job.c      lectura de la lista y descargas con libcurl
  source/ui.c       interfaz de ambas pantallas
  source/quirc/     decodificador de QR (licencia ISC)
docs/               imágenes de este README
```

---

## 🙌 Créditos

- [quirc](https://github.com/dlbeer/quirc) (ISC) para leer los códigos QR en la consola.
- [mutagen](https://github.com/quodlibet/mutagen), [Pillow](https://python-pillow.org) y [python-qrcode](https://github.com/lincolnloop/python-qrcode) en el PC.
- [devkitPro](https://devkitpro.org) (libctru, citro2d, libcurl) para la app de la consola.
