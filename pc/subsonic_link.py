"""Conectar un servidor propio (Navidrome, Airsonic, Gonic... con la API de Subsonic).

El usuario escribe la dirección, el usuario y la contraseña en esta app. Aquí se calcula
el token de Subsonic (md5(contraseña + sal)), se prueba con ping y se arma un enlace de un
solo uso para el QR: la consola recibe la URL, el usuario, el token y la sal, nunca la
contraseña. El enlace vence a los 10 minutos.
"""

import hashlib
import json
import secrets
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

from sc_account import ssl_context

CODE_TTL = 600
LOCAL_HOSTS = {"localhost", "127.0.0.1", "::1", "0.0.0.0"}


def normalize_url(raw):
    """'nas:4533/' -> 'http://nas:4533'. None si no se entiende."""
    raw = (raw or "").strip()
    if not raw:
        return None
    if "://" not in raw:
        raw = "http://" + raw
    u = urllib.parse.urlsplit(raw)
    if u.scheme not in ("http", "https") or not u.hostname:
        return None
    path = u.path.rstrip("/")
    # Quien pegue la dirección de la web de Navidrome con /app o /rest al final
    for tail in ("/app", "/rest"):
        if path.endswith(tail):
            path = path[: -len(tail)]
    return urllib.parse.urlunsplit((u.scheme, u.netloc, path, "", ""))


def for_console(url, pc_ip):
    """La consola no puede usar 'localhost' (sería ella misma): se cambia por la IP del PC."""
    u = urllib.parse.urlsplit(url)
    if u.hostname in LOCAL_HOSTS and pc_ip:
        netloc = pc_ip + (f":{u.port}" if u.port else "")
        return urllib.parse.urlunsplit((u.scheme, netloc, u.path, "", ""))
    return url


def ping(url, user, token, salt, timeout=8):
    """(ok, error, nombre del servidor). error: 'net', 'auth', 'notsubsonic'."""
    q = urllib.parse.urlencode({"u": user, "t": token, "s": salt, "v": "1.16.1", "c": "Sound3DS-Share", "f": "json"})
    req = urllib.request.Request(f"{url}/rest/ping?{q}", headers={"User-Agent": "Sound3DS-Share"})
    try:
        with urllib.request.urlopen(req, timeout=timeout, context=ssl_context()) as r:
            data = json.loads(r.read(64 * 1024).decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        return False, "auth" if e.code in (401, 403) else "notsubsonic", ""
    except (urllib.error.URLError, OSError, TimeoutError):
        return False, "net", ""
    except ValueError:
        return False, "notsubsonic", ""
    resp = data.get("subsonic-response") if isinstance(data, dict) else None
    if not isinstance(resp, dict):
        return False, "notsubsonic", ""
    if resp.get("status") != "ok":
        code = (resp.get("error") or {}).get("code")
        return False, "auth" if code in (40, 41, 50) else "notsubsonic", ""
    return True, "", str(resp.get("type") or "Subsonic")


class ServerLink:
    def __init__(self):
        self.lock = threading.Lock()
        self._reset()

    def _reset(self):
        self.state = "idle"        # idle, ready, delivered, error
        self.error = ""
        self.payload = None
        self.code = ""
        self.code_expires = 0.0
        self.label = ""

    def status(self):
        with self.lock:
            if self.state == "ready" and time.time() > self.code_expires:
                self.payload = None
                self.code = ""
                self.state, self.error = "error", "expired"
            st = {"state": self.state, "error": self.error, "label": self.label}
            if self.state == "ready":
                st["code"] = self.code
                st["expires_in"] = max(0, int(self.code_expires - time.time()))
            return st

    def connect(self, url, user, password, pc_ip):
        url = normalize_url(url)
        user = (user or "").strip()
        if not url or not user or not password:
            with self.lock:
                self._reset()
                self.state, self.error = "error", "fields"
            return self.status()
        salt = secrets.token_hex(8)
        token = hashlib.md5((password + salt).encode("utf-8")).hexdigest()
        ok, err, name = ping(url, user, token, salt)
        with self.lock:
            self._reset()
            if not ok:
                self.state, self.error = "error", err
            else:
                console_url = for_console(url, pc_ip)
                self.payload = {"url": console_url, "u": user, "t": token, "s": salt, "name": name}
                self.code = secrets.token_urlsafe(24)
                self.code_expires = time.time() + CODE_TTL
                host = urllib.parse.urlsplit(console_url).netloc
                self.label = f"{user} @ {host}"
                self.state = "ready"
        print(f"Servidor {'listo' if ok else 'rechazado (' + err + ')'}: {url}", flush=True)
        return self.status()

    def take(self, code):
        """El enlace del QR: entrega los datos una sola vez y los borra de la memoria."""
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
        with self.lock:
            self._reset()


SERVER = ServerLink()
