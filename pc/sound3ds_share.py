#!/usr/bin/env python3
"""Sound3DS Share: pasa canciones del PC a la consola con la app Sound3DS QR.

Abre una interfaz (ventana de Edge/Chrome/Chromium en modo app, o el navegador
predeterminado) para explorar los archivos, elegir canciones, revisar sus
álbumes, carátulas y etiquetas, y generar el QR que escanea la consola.

Dos servidores HTTP:
  - interfaz: solo en 127.0.0.1 (puede explorar los archivos del PC)
  - servicio para la consola: en la red local, sirve solo las canciones elegidas

Las canciones no se copian: al enviarlas se les antepone una etiqueta ID3v2.3
nueva (título, artista, álbum, pista y carátula reducida a 500 px) y se manda
el audio original a continuación.
"""
import hashlib
import io
import json
import os
import re
import shutil
import socket
import string
import subprocess
import sys
import threading
import time
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from mutagen.id3 import APIC, ID3, TALB, TIT2, TPE1, TPE2, TRCK
import qrcode
import qrcode.image.svg

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # sin Pillow las carátulas se envían tal cual
    HAVE_PIL = False

APP_NAME = "Sound3DS Share"
VERSION = "1.0"
UI_PORT = 8764
SHARE_PORT = 8765
AUDIO_EXT = (".mp3", ".aac")
IMAGE_EXT = (".jpg", ".jpeg", ".png")
FOLDER_COVERS = ("cover", "folder", "front", "album", "albumart", "albumartsmall", "portada")
COVER_MAX = 500
MAX_SCAN = 1000


def resource_dir():
    # PyInstaller descomprime los recursos en _MEIPASS
    return getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))


# ---------------------------------------------------------------------------
# Carátulas
# ---------------------------------------------------------------------------

COVERS = {}          # id -> bytes JPEG/PNG listos para incrustar
COVERS_LOCK = threading.Lock()


def add_cover(data):
    """Normaliza una imagen (JPEG de 500 px como máximo) y devuelve su id."""
    if not data:
        return None
    out, mime = data, "image/jpeg"
    if HAVE_PIL:
        try:
            im = Image.open(io.BytesIO(data))
            im = im.convert("RGB")
            im.thumbnail((COVER_MAX, COVER_MAX), Image.LANCZOS)
            buf = io.BytesIO()
            # JPEG básico (no progresivo): lo decodifica cualquier versión de la app
            im.save(buf, "JPEG", quality=88, optimize=True, progressive=False)
            out = buf.getvalue()
        except Exception:
            return None
    elif data[:8] == b"\x89PNG\r\n\x1a\n":
        mime = "image/png"
    cid = hashlib.sha1(out).hexdigest()[:16]
    with COVERS_LOCK:
        COVERS[cid] = (out, mime)
    return cid


def folder_cover(directory):
    try:
        names = os.listdir(directory)
    except OSError:
        return None
    best = None
    for n in names:
        base, ext = os.path.splitext(n.lower())
        if ext in IMAGE_EXT and base in FOLDER_COVERS:
            best = n
            if base in ("cover", "folder", "front"):
                break
    if not best:
        return None
    try:
        with open(os.path.join(directory, best), "rb") as f:
            return add_cover(f.read())
    except OSError:
        return None


# ---------------------------------------------------------------------------
# Metadatos
# ---------------------------------------------------------------------------

def _num(s):
    m = re.match(r"\s*(\d+)", s or "")
    return int(m.group(1)) if m else 0


def read_meta(path):
    title = artist = album = album_artist = ""
    track = 0
    cover = None
    cover_src = ""
    tags = None
    try:
        tags = ID3(path)
    except Exception:
        tags = None
    if tags is not None:
        def t(key):
            fr = tags.get(key)
            return str(fr.text[0]).strip() if fr is not None and fr.text else ""
        title, artist, album, album_artist = t("TIT2"), t("TPE1"), t("TALB"), t("TPE2")
        track = _num(t("TRCK"))
        apics = tags.getall("APIC")
        apic = next((a for a in apics if a.type == 3), apics[0] if apics else None)
        if apic is not None:
            cover = add_cover(apic.data)
            cover_src = "embedded" if cover else ""

    # Lo que falte sale del nombre del archivo: "01. Artista - Título.mp3"
    stem = os.path.splitext(os.path.basename(path))[0]
    if not track:
        track = _num(stem)
    clean = re.sub(r"^\s*\d+\s*[.\-_)]*\s*", "", stem) or stem
    if not title:
        if " - " in clean:
            a, b = clean.split(" - ", 1)
            title = b.strip()
            artist = artist or a.strip()
        else:
            title = clean.strip()
    if not cover:
        cover = folder_cover(os.path.dirname(path))
        cover_src = "folder" if cover else ""

    try:
        size = os.path.getsize(path)
    except OSError:
        size = 0
    return {
        "path": path, "size": size, "ext": os.path.splitext(path)[1].lower(),
        "title": title, "artist": artist, "album": album, "album_artist": album_artist,
        "track": track, "cover": cover, "cover_src": cover_src,
    }


def build_tag(song, cover):
    tags = ID3()
    # UTF-16 con BOM: la versión 2.3 no admite UTF-8 y así caben tildes y eñes
    if song.get("title"):
        tags.add(TIT2(encoding=1, text=song["title"]))
    if song.get("artist"):
        tags.add(TPE1(encoding=1, text=song["artist"]))
    if song.get("album"):
        tags.add(TALB(encoding=1, text=song["album"]))
    if song.get("album_artist"):
        tags.add(TPE2(encoding=1, text=song["album_artist"]))
    if int(song.get("track") or 0) > 0:
        tags.add(TRCK(encoding=0, text=str(int(song["track"]))))
    if cover:
        data, mime = cover
        tags.add(APIC(encoding=0, mime=mime, type=3, desc="", data=data))
    buf = io.BytesIO()
    tags.save(buf, v2_version=3, padding=lambda info: 0)
    return buf.getvalue()


def audio_range(path):
    """Inicio y fin del audio sin la etiqueta ID3v2 del principio ni la ID3v1 del final."""
    size = os.path.getsize(path)
    start, end = 0, size
    with open(path, "rb") as f:
        head = f.read(10)
        if len(head) == 10 and head[:3] == b"ID3":
            n = (head[6] << 21) | (head[7] << 14) | (head[8] << 7) | head[9]
            start = 10 + n + (10 if head[5] & 0x10 else 0)
        if size >= 128:
            f.seek(size - 128)
            if f.read(3) == b"TAG":
                end = size - 128
    return start, max(start, end)


def safe_name(s, limit=90):
    s = re.sub(r'[\\/:*?"<>|\x00-\x1f]', "_", s).strip(" .")
    return s[:limit].rstrip(" .") or "cancion"


# ---------------------------------------------------------------------------
# Archivos del PC
# ---------------------------------------------------------------------------

def user_dir(key, fallback):
    """Carpetas de usuario de XDG (Música, Descargas...) en Linux."""
    home = os.path.expanduser("~")
    cfg = os.path.join(os.environ.get("XDG_CONFIG_HOME", os.path.join(home, ".config")), "user-dirs.dirs")
    try:
        with open(cfg, encoding="utf-8") as f:
            for line in f:
                if line.startswith(f"XDG_{key}_DIR="):
                    v = line.split("=", 1)[1].strip().strip('"').replace("$HOME", home)
                    if os.path.isdir(v):
                        return v
    except OSError:
        pass
    p = os.path.join(home, fallback)
    return p if os.path.isdir(p) else None


def places():
    home = os.path.expanduser("~")
    out = [{"id": "home", "path": home}]
    for pid, key, fb in (("music", "MUSIC", "Music"), ("downloads", "DOWNLOAD", "Downloads"),
                         ("desktop", "DESKTOP", "Desktop")):
        p = user_dir(key, fb)
        if p:
            out.append({"id": pid, "path": p})
    if os.name == "nt":
        for letter in string.ascii_uppercase:
            d = f"{letter}:\\"
            if os.path.exists(d):
                out.append({"id": "drive", "path": d, "label": f"{letter}:"})
    else:
        out.append({"id": "root", "path": "/"})
        # Discos montados: /media/<usuario>/<disco>, /run/media/<usuario>/<disco>, /mnt/<disco>
        user = os.path.basename(home)
        for base in (f"/media/{user}", f"/run/media/{user}", "/mnt"):
            try:
                for n in sorted(os.listdir(base)):
                    p = os.path.join(base, n)
                    if os.path.isdir(p) and not n.startswith("."):
                        out.append({"id": "drive", "path": p, "label": n})
            except OSError:
                pass
    return out


def list_dir(path):
    path = os.path.abspath(path or os.path.expanduser("~"))
    entries = []
    try:
        with os.scandir(path) as it:
            for e in it:
                if e.name.startswith("."):
                    continue
                try:
                    if e.is_dir():
                        entries.append({"name": e.name, "path": e.path, "type": "dir"})
                    elif e.name.lower().endswith(AUDIO_EXT):
                        entries.append({"name": e.name, "path": e.path, "type": "audio",
                                        "size": e.stat().st_size})
                except OSError:
                    continue
    except OSError as ex:
        return {"path": path, "parent": os.path.dirname(path), "entries": [], "error": str(ex)}
    entries.sort(key=lambda e: (e["type"] != "dir", e["name"].lower()))
    parent = os.path.dirname(path)
    return {"path": path, "parent": parent if parent != path else None, "sep": os.sep, "entries": entries}


def expand(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, names in os.walk(p):
                dirs[:] = sorted(d for d in dirs if not d.startswith("."))
                for n in sorted(names):
                    if n.lower().endswith(AUDIO_EXT):
                        files.append(os.path.join(root, n))
                        if len(files) >= MAX_SCAN:
                            return files
        elif p.lower().endswith(AUDIO_EXT) and os.path.isfile(p):
            files.append(p)
    return files


# ---------------------------------------------------------------------------
# Servicio para la consola
# ---------------------------------------------------------------------------

def lan_ips():
    ips = []
    for probe in ("192.168.0.1", "10.0.0.1", "8.8.8.8"):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect((probe, 1))   # no envía nada: solo elige la interfaz
            ip = s.getsockname()[0]
            if ip not in ips and not ip.startswith("127."):
                ips.append(ip)
        except OSError:
            pass
        finally:
            s.close()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if ip not in ips and not ip.startswith("127."):
                ips.append(ip)
    except OSError:
        pass
    # Primero las de redes domésticas
    ips.sort(key=lambda ip: (not ip.startswith(("192.168.", "10.", "172.")), ip.startswith("100.")))
    return ips or ["127.0.0.1"]


class Share:
    def __init__(self):
        self.lock = threading.Lock()
        self.server = None
        self.thread = None
        self.port = SHARE_PORT
        self.ip = lan_ips()[0]
        self.items = []          # canciones publicadas
        self.error = ""
        self.last_activity = 0.0
        self.list_reads = 0

    @property
    def on(self):
        return self.server is not None

    def url(self):
        return f"http://{self.ip}:{self.port}/sound3ds.txt"

    def start(self):
        if self.server:
            return True
        self.error = ""
        for port in range(SHARE_PORT, SHARE_PORT + 10):
            try:
                srv = ThreadingHTTPServer(("0.0.0.0", port), ConsoleHandler)
            except OSError:
                continue
            srv.daemon_threads = True
            self.server, self.port = srv, port
            self.thread = threading.Thread(target=srv.serve_forever, daemon=True)
            self.thread.start()
            return True
        self.error = "port"
        return False

    def stop(self):
        if self.server:
            self.server.shutdown()
            self.server.server_close()
            self.server = None

    def publish(self, songs):
        items, used = [], set()
        for s in songs:
            path = s["path"]
            if not os.path.isfile(path):
                continue
            ext = os.path.splitext(path)[1].lower()
            artist = s.get("artist") or s.get("album_artist") or ""
            base = safe_name(f"{artist} - {s.get('title') or 'Pista'}" if artist else (s.get("title") or "Pista"))
            name, k = base + ext, 2
            while name.lower() in used:
                name = f"{safe_name(base, 84)} ({k}){ext}"
                k += 1
            used.add(name.lower())
            with COVERS_LOCK:
                cover = COVERS.get(s.get("cover") or "")
            tag = build_tag(s, cover)
            start, end = audio_range(path)
            items.append({
                "id": len(items), "name": name, "path": path, "tag": tag,
                "start": start, "end": end, "size": len(tag) + end - start,
                "sent": 0, "state": "pending", "title": s.get("title", ""), "album": s.get("album", ""),
            })
        with self.lock:
            self.items = items
            self.list_reads = 0
        return items

    def listing(self):
        base = f"http://{self.ip}:{self.port}"
        return "".join(f"{base}/f/{it['id']}/{urllib.parse.quote(it['name'])}\n" for it in self.items)


SHARE = Share()


class ConsoleHandler(BaseHTTPRequestHandler):
    server_version = "Sound3DSShare/" + VERSION

    def log_message(self, *args):
        pass

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        SHARE.last_activity = time.time()
        if path == "/sound3ds.txt":
            body = SHARE.listing().encode()
            with SHARE.lock:
                SHARE.list_reads += 1
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        m = re.match(r"^/f/(\d+)/", path)
        item = None
        if m:
            i = int(m.group(1))
            with SHARE.lock:
                item = SHARE.items[i] if i < len(SHARE.items) else None
        if not item:
            self.send_error(404)
            return
        mime = "audio/mpeg" if item["name"].lower().endswith(".mp3") else "audio/aac"
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(item["size"]))
        self.end_headers()
        item["state"], item["sent"] = "sending", 0
        try:
            self.wfile.write(item["tag"])
            item["sent"] = len(item["tag"])
            with open(item["path"], "rb") as f:
                f.seek(item["start"])
                left = item["end"] - item["start"]
                while left > 0:
                    chunk = f.read(min(65536, left))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    left -= len(chunk)
                    item["sent"] += len(chunk)
                    SHARE.last_activity = time.time()
            item["state"] = "done" if left == 0 else "error"
        except (BrokenPipeError, ConnectionResetError, OSError):
            item["state"] = "error"


# ---------------------------------------------------------------------------
# Interfaz (solo en este equipo)
# ---------------------------------------------------------------------------

class App:
    preload = []         # rutas pasadas al abrir (argumentos o arrastrar sobre el ejecutable)
    last_ping = 0.0
    seen_ui = False
    bye_at = 0.0
    quit = threading.Event()


def qr_svg(text):
    img = qrcode.make(text, image_factory=qrcode.image.svg.SvgPathImage, border=2, box_size=10)
    buf = io.BytesIO()
    img.save(buf)
    return buf.getvalue().decode()


class UIHandler(BaseHTTPRequestHandler):
    server_version = "Sound3DSShareUI/" + VERSION

    def log_message(self, *args):
        pass

    def _json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def _local_only(self):
        # Defensa extra: rechaza peticiones que lleguen con otro Host (DNS rebinding)
        host = (self.headers.get("Host") or "").split(":")[0]
        if host not in ("127.0.0.1", "localhost"):
            self.send_error(403)
            return False
        return True

    def do_GET(self):
        if not self._local_only():
            return
        u = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(u.query)
        if u.path in ("/", "/index.html"):
            with open(os.path.join(resource_dir(), "web", "index.html"), "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        elif u.path == "/api/places":
            self._json({"places": places(), "home": os.path.expanduser("~"), "os": os.name})
        elif u.path == "/api/list":
            self._json(list_dir(q.get("path", [""])[0]))
        elif u.path.startswith("/api/cover/"):
            with COVERS_LOCK:
                c = COVERS.get(u.path.rsplit("/", 1)[1])
            if not c:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", c[1])
            self.send_header("Content-Length", str(len(c[0])))
            self.send_header("Cache-Control", "max-age=86400")
            self.end_headers()
            self.wfile.write(c[0])
        elif u.path == "/api/state":
            App.last_ping = time.time()
            App.seen_ui = True
            App.bye_at = 0
            with SHARE.lock:
                items = [{"id": it["id"], "name": it["name"], "size": it["size"], "sent": it["sent"],
                          "state": it["state"]} for it in SHARE.items]
                reads = SHARE.list_reads
            self._json({"on": SHARE.on, "ip": SHARE.ip, "ips": lan_ips(), "port": SHARE.port,
                        "url": SHARE.url(), "error": SHARE.error, "items": items, "list_reads": reads,
                        "version": VERSION})
        else:
            self.send_error(404)

    def do_POST(self):
        if not self._local_only():
            return
        u = urllib.parse.urlsplit(self.path)
        if u.path == "/api/add":
            data = json.loads(self._body() or b"{}")
            files = expand(data.get("paths", []))
            self._json({"songs": [read_meta(p) for p in files], "limited": len(files) >= MAX_SCAN})
        elif u.path == "/api/cover":
            cid = add_cover(self._body())
            self._json({"cover": cid} if cid else {"error": "image"}, 200 if cid else 400)
        elif u.path == "/api/service":
            data = json.loads(self._body() or b"{}")
            if data.get("ip"):
                SHARE.ip = data["ip"]
            if "on" in data:
                SHARE.start() if data["on"] else SHARE.stop()
            self._json({"on": SHARE.on, "port": SHARE.port, "error": SHARE.error})
        elif u.path == "/api/share":
            data = json.loads(self._body() or b"{}")
            items = SHARE.publish(data.get("songs", []))
            if not SHARE.start():
                self._json({"error": "port"}, 500)
                return
            self._json({"url": SHARE.url(), "svg": qr_svg(SHARE.url()), "count": len(items),
                        "bytes": sum(i["size"] for i in items)})
        elif u.path == "/api/preload":
            paths, App.preload = App.preload, []
            files = expand(paths)
            self._json({"songs": [read_meta(p) for p in files]})
        elif u.path == "/api/bye":
            App.bye_at = time.time() + 5   # se cierra salvo que la página vuelva (recarga)
            self._json({})
        elif u.path == "/api/quit":
            self._json({})
            App.quit.set()
        else:
            self.send_error(404)


def open_window(url):
    """Abre la interfaz como ventana de app si hay un navegador Chromium; si no, en el predeterminado."""
    candidates = []
    if os.name == "nt":
        for env in ("PROGRAMFILES(X86)", "PROGRAMFILES", "LOCALAPPDATA"):
            base = os.environ.get(env)
            if base:
                candidates += [os.path.join(base, "Microsoft", "Edge", "Application", "msedge.exe"),
                               os.path.join(base, "Google", "Chrome", "Application", "chrome.exe"),
                               os.path.join(base, "BraveSoftware", "Brave-Browser", "Application", "brave.exe")]
    else:
        for name in ("chromium", "chromium-browser", "google-chrome", "google-chrome-stable",
                     "brave-browser", "brave", "microsoft-edge", "microsoft-edge-stable", "vivaldi"):
            p = shutil.which(name)
            if p:
                candidates.append(p)
    for exe in candidates:
        if os.path.isfile(exe):
            try:
                kw = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
                if os.name == "nt":
                    kw["creationflags"] = 0x00000008  # DETACHED_PROCESS
                else:
                    kw["start_new_session"] = True
                subprocess.Popen([exe, f"--app={url}", "--window-size=1240,820"], **kw)
                return
            except OSError:
                continue
    webbrowser.open(url)


def already_running(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.5):
            return True
    except OSError:
        return False


def main():
    url = f"http://127.0.0.1:{UI_PORT}/"
    App.preload = [os.path.abspath(a) for a in sys.argv[1:] if not a.startswith("--")]
    if already_running(UI_PORT):
        open_window(url)   # ya hay una abierta: solo mostrar la ventana
        return
    ui = ThreadingHTTPServer(("127.0.0.1", UI_PORT), UIHandler)
    ui.daemon_threads = True
    threading.Thread(target=ui.serve_forever, daemon=True).start()
    if "--no-window" not in sys.argv:
        open_window(url)
    print(f"{APP_NAME} {VERSION}: {url}")

    # Se cierra al cerrar la ventana (o si deja de responder un buen rato),
    # pero nunca en medio de una descarga de la consola
    while not App.quit.wait(1.0):
        now = time.time()
        busy = now - SHARE.last_activity < 30
        if App.bye_at and now > App.bye_at and not busy:
            break
        if App.seen_ui and now - App.last_ping > 180 and not busy:
            break
    SHARE.stop()
    ui.shutdown()


if __name__ == "__main__":
    main()
