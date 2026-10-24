"""Conexión de la cuenta de Bandcamp (fan) para Sound3DS.

Igual que la de SoundCloud: el usuario inicia sesión en la página oficial de
Bandcamp en una ventana del navegador con un perfil temporal, y por el protocolo
de depuración se lee la cookie de sesión `identity`. La contraseña nunca pasa por
este código. La consola recibe la cookie, su fan_id y el nombre de usuario por un
enlace de un solo uso, y con eso ve su colección (lo que compró).
"""
import json
import secrets
import time
import urllib.error
import urllib.request

from sc_account import CODE_TTL, Account, ssl_context

SIGNIN_URL = "https://bandcamp.com/login"
SUMMARY_URL = "https://bandcamp.com/api/fan/2/collection_summary"
UA = "Sound3DS (Nintendo 3DS)"   # Bandcamp responde a un navegador con un desafío JavaScript


def collection_summary(identity):
    """{fan_id, username} de la sesión, o None si la cookie no es de una sesión iniciada."""
    req = urllib.request.Request(SUMMARY_URL, headers={"Cookie": f"identity={identity}", "User-Agent": UA,
                                                       "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=20, context=ssl_context()) as r:
            data = json.loads(r.read())
    except (urllib.error.URLError, ValueError, OSError) as e:
        print("Bandcamp collection_summary:", type(e).__name__, getattr(e, "code", "") or getattr(e, "reason", ""),
              flush=True)
        return None
    if not isinstance(data, dict) or data.get("error") or not data.get("fan_id"):
        return None
    summary = data.get("collection_summary") or {}
    return {"fan_id": data["fan_id"], "username": summary.get("username") or ""}


class BandcampAccount(Account):
    SIGNIN_URL = SIGNIN_URL
    PROFILE_PREFIX = "sound3ds-bc-"

    def _reset(self):
        super()._reset()
        self.checked = {}   # cookie identity -> resumen (o None): no volver a consultar la misma

    def pick_cookies(self, cookies):
        # La cookie identity puede existir antes de iniciar sesión: solo vale cuando
        # Bandcamp la reconoce como una sesión con colección
        for c in cookies:
            if c.get("name") != "identity" or "bandcamp.com" not in c.get("domain", ""):
                continue
            value = c.get("value") or ""
            if value and value not in self.checked:
                self.checked[value] = collection_summary(value)
            if self.checked.get(value):
                return value, None
        return None, None

    def _finish(self, token, refresh):
        info = (self.checked.get(token) if token else None) or (collection_summary(token) if token else None)
        with self.lock:
            if not info:
                self.state, self.error = "error", "invalid"
                return False
            self.username = info["username"] or str(info["fan_id"])
            self.payload = {"identity": token, "fan_id": info["fan_id"], "username": self.username,
                            "obtained_at": int(time.time())}
            self.code = secrets.token_urlsafe(16)
            self.code_expires = time.time() + CODE_TTL
            self.state = "ready"
            self.checked = {}
        return True


BC_ACCOUNT = BandcampAccount()
