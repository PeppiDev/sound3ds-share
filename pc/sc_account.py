"""Conexión de la cuenta de SoundCloud para Sound3DS.

El usuario inicia sesión en la página oficial de SoundCloud, en una ventana de
Edge / Chrome / Chromium / Brave con un perfil temporal. Por el protocolo de
depuración del navegador (CDP) se leen las cookies `oauth_token` y
`oauth_refresh_token`; la contraseña nunca pasa por este código. Después se
cierra la ventana, se borra el perfil y los tokens quedan solo en memoria hasta
que la consola los pide una vez por un enlace de un solo uso (el QR lleva el
enlace, nunca el token).
"""
import base64
import json
import os
import secrets
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

API_CLIENT_ID = "A0cxVCVBD6HAZsm0P3nxKaPaZtrGqkXN"   # el mismo que usa Sound3DS
SIGNIN_URL = "https://soundcloud.com/signin"
LOGIN_TIMEOUT = 600   # segundos para iniciar sesión
CODE_TTL = 600        # segundos que vale el enlace para la consola
UA = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/130.0.0.0 Safari/537.36")


def browser_candidates():
    out = []
    if os.name == "nt":
        for env in ("PROGRAMFILES(X86)", "PROGRAMFILES", "LOCALAPPDATA"):
            base = os.environ.get(env)
            if base:
                out += [os.path.join(base, "Microsoft", "Edge", "Application", "msedge.exe"),
                        os.path.join(base, "Google", "Chrome", "Application", "chrome.exe"),
                        os.path.join(base, "BraveSoftware", "Brave-Browser", "Application", "brave.exe")]
        return [p for p in out if os.path.isfile(p)]
    for name in ("chromium", "chromium-browser", "google-chrome", "google-chrome-stable",
                 "brave-browser", "brave", "microsoft-edge", "microsoft-edge-stable", "vivaldi"):
        p = shutil.which(name)
        if p:
            out.append(p)
    return out


def find_browser():
    c = browser_candidates()
    return c[0] if c else None


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Cdp:
    """Cliente WebSocket mínimo para el protocolo de depuración de Chromium."""

    def __init__(self, ws_url, timeout=10):
        u = urllib.parse.urlsplit(ws_url)
        self.sock = socket.create_connection((u.hostname, u.port), timeout=timeout)
        key = base64.b64encode(secrets.token_bytes(16)).decode()
        req = (f"GET {u.path} HTTP/1.1\r\nHost: {u.hostname}:{u.port}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
        self.sock.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("CDP: sin respuesta")
            resp += chunk
        if b" 101 " not in resp.split(b"\r\n", 1)[0]:
            raise ConnectionError("CDP: " + resp.split(b"\r\n", 1)[0].decode(errors="replace"))
        self.buf = resp.split(b"\r\n\r\n", 1)[1]
        self.next_id = 1

    def _recv_exact(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("CDP: conexión cerrada")
            self.buf += chunk
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def _recv_message(self):
        parts = []
        while True:
            b0, b1 = self._recv_exact(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._recv_exact(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._recv_exact(8))[0]
            mask = self._recv_exact(4) if b1 & 0x80 else None
            payload = self._recv_exact(n)
            if mask:
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
            opcode = b0 & 0x0F
            if opcode == 8:
                raise ConnectionError("CDP: cerrado")
            if opcode in (0, 1, 2):
                parts.append(payload)
                if b0 & 0x80:
                    return b"".join(parts)

    def call(self, method, params=None):
        mid = self.next_id
        self.next_id += 1
        data = json.dumps({"id": mid, "method": method, "params": params or {}}).encode()
        mask = secrets.token_bytes(4)
        head = bytes([0x81])
        n = len(data)
        if n < 126:
            head += bytes([0x80 | n])
        elif n < 65536:
            head += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            head += bytes([0x80 | 127]) + struct.pack(">Q", n)
        self.sock.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))
        while True:
            msg = json.loads(self._recv_message())
            if msg.get("id") == mid:
                return msg.get("result", {})

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def jwt_claims(token):
    try:
        part = token.split(".")[1]
        part += "=" * (-len(part) % 4)
        return json.loads(base64.urlsafe_b64decode(part))
    except (IndexError, ValueError):
        return {}


def api_me(token):
    req = urllib.request.Request(f"https://api-v2.soundcloud.com/me?client_id={API_CLIENT_ID}",
                                 headers={"Authorization": f"OAuth {token}", "User-Agent": UA,
                                          "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.loads(r.read())
    except (urllib.error.URLError, ValueError, OSError):
        return None


def _kill_by_profile(profile):
    """Linux: cierra los procesos del navegador que usan ese perfil (por si el lanzador no lo hizo)."""
    if os.name == "nt" or not os.path.isdir("/proc"):
        return
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                if profile.encode() in f.read():
                    os.kill(int(pid), 15)
        except OSError:
            pass


class Account:
    """Estado de la conexión: idle → waiting (inicio de sesión) → ready (QR) → delivered."""

    def __init__(self):
        self.lock = threading.Lock()
        self.cancel_ev = threading.Event()
        self._reset()

    def _reset(self):
        self.state = "idle"
        self.error = ""
        self.username = ""
        self.payload = None
        self.code = ""
        self.code_expires = 0.0
        self.proc = None
        self.profile = None
        self.keep_profile = False
        self.thread = None

    def status(self):
        with self.lock:
            if self.state == "ready" and time.time() > self.code_expires:
                self.state, self.error, self.payload, self.code = "error", "expired", None, ""
            return {"state": self.state, "error": self.error, "username": self.username, "code": self.code,
                    "expires_in": max(0, int(self.code_expires - time.time())) if self.state == "ready" else 0,
                    "has_browser": find_browser() is not None}

    # --- Inicio de sesión en el navegador ---

    def start(self, profile=None, keep_profile=False):
        self.cancel()
        browser = find_browser()
        with self.lock:
            self._reset()
            if not browser:
                self.state, self.error = "error", "no_browser"
                return False
            self.state = "waiting"
            self.keep_profile = keep_profile
            self.profile = profile or tempfile.mkdtemp(prefix="sound3ds-sc-")
            port = free_port()
            kw = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
            if os.name == "nt":
                kw["creationflags"] = 0x00000200   # CREATE_NEW_PROCESS_GROUP
            else:
                kw["start_new_session"] = True
            try:
                self.proc = subprocess.Popen([browser, f"--user-data-dir={self.profile}",
                                              f"--remote-debugging-port={port}", "--no-first-run",
                                              "--no-default-browser-check", "--window-size=1000,800",
                                              SIGNIN_URL], **kw)
            except OSError:
                self.state, self.error = "error", "no_browser"
                return False
            self.cancel_ev.clear()
            self.thread = threading.Thread(target=self._capture, args=(port,), daemon=True)
            self.thread.start()
        return True

    def _capture(self, port):
        cdp = None
        try:
            deadline = time.time() + 20
            ws = None
            while time.time() < deadline and not self.cancel_ev.is_set():
                try:
                    with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/version", timeout=2) as r:
                        ws = json.loads(r.read())["webSocketDebuggerUrl"]
                        break
                except (urllib.error.URLError, OSError, ValueError, KeyError):
                    time.sleep(0.5)
            if not ws:
                self._fail("browser")
                return
            cdp = Cdp(ws)
            deadline = time.time() + LOGIN_TIMEOUT
            token = refresh = None
            seen_token_at = 0.0
            while time.time() < deadline and not self.cancel_ev.is_set():
                cookies = cdp.call("Storage.getCookies").get("cookies", [])
                for c in cookies:
                    if "soundcloud.com" not in c.get("domain", ""):
                        continue
                    if c.get("name") == "oauth_token":
                        token = c.get("value")
                    elif c.get("name") == "oauth_refresh_token":
                        refresh = c.get("value")
                if token and not seen_token_at:
                    seen_token_at = time.time()
                # Con los dos, o con el token y unos segundos sin que llegue el de renovación
                if token and (refresh or time.time() - seen_token_at > 6):
                    break
                time.sleep(2)
            if self.cancel_ev.is_set():
                return
            if not token:
                self._fail("timeout")
                return
            try:
                cdp.call("Browser.close")
            except (OSError, ConnectionError, ValueError):
                pass
            cdp.close()
            cdp = None
            self._close_browser()
            self._finish(token, refresh)
        except (OSError, ConnectionError, ValueError):
            # El usuario cerró la ventana antes de iniciar sesión
            if not self.cancel_ev.is_set():
                self._fail("closed")
        finally:
            if cdp:
                cdp.close()
            if self.cancel_ev.is_set() or self.state != "waiting":
                self._close_browser()

    def _close_browser(self):
        proc, profile, keep = self.proc, self.profile, self.keep_profile
        self.proc = None
        if proc and proc.poll() is None:
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.terminate()
        if profile:
            _kill_by_profile(profile)
            if not keep:
                time.sleep(1)
                shutil.rmtree(profile, ignore_errors=True)
                self.profile = None

    def _fail(self, code):
        with self.lock:
            if self.state == "waiting":
                self.state, self.error = "error", code
        self._close_browser()

    # --- Token pegado a mano (opción avanzada) ---

    def manual(self, token, refresh=""):
        self.cancel()
        token = (token or "").strip()
        if token.lower().startswith("oauth "):
            token = token[6:].strip()
        with self.lock:
            self._reset()
            self.state = "waiting"
        return self._finish(token, (refresh or "").strip() or None)

    def _finish(self, token, refresh):
        me = api_me(token) if token else None
        with self.lock:
            if not me or "id" not in me:
                self.state, self.error = "error", "invalid"
                return False
            claims = jwt_claims(token)
            self.username = me.get("username") or ""
            self.payload = {
                "username": self.username,
                "user_id": me["id"],
                "access_token": token,
                "refresh_token": refresh or "",
                "auth_client_id": claims.get("client_id", ""),
                "obtained_at": int(time.time()),
            }
            self.code = secrets.token_urlsafe(16)   # 128 bits
            self.code_expires = time.time() + CODE_TTL
            self.state = "ready"
        return True

    # --- Entrega a la consola ---

    def take(self, code):
        """El enlace del QR: entrega los tokens una sola vez y los borra de la memoria."""
        with self.lock:
            if (self.state != "ready" or not code or not secrets.compare_digest(code, self.code)
                    or time.time() > self.code_expires):
                return None
            data = json.dumps(self.payload).encode()
            self.payload = None
            self.code = ""
            self.state = "delivered"
            return data

    def cancel(self):
        self.cancel_ev.set()
        t = self.thread
        if t and t.is_alive() and t is not threading.current_thread():
            t.join(timeout=8)
        self._close_browser()
        with self.lock:
            self.payload = None
            self.code = ""
            if self.state in ("waiting", "ready"):
                self.state = "idle"


ACCOUNT = Account()
