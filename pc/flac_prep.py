"""FLAC listo para la 3DS.

Sound3DS lee las etiquetas propias de FLAC (comentarios Vorbis y bloque PICTURE),
no ID3, así que a los FLAC no se les antepone una etiqueta: se prepara una copia
con las etiquetas y la carátula elegidas en la app.

Si el FLAC pasa de 48 kHz se convierte a FLAC de 16 bits a 44,1 o 48 kHz. Un Old
3DS no alcanza a decodificar 24 bits a 192 kHz y la consola sale a ~32,7 kHz de
todos modos: suena igual, pesa unas 5 veces menos y gasta menos batería.

La conversión usa soundfile (libsndfile) y soxr; si faltan, el FLAC se envía sin
convertir. Las copias quedan en una carpeta temporal y se reutilizan.
"""
import hashlib
import json
import os
import shutil
import tempfile

from mutagen.flac import FLAC, Picture

MAX_RATE = 48000
CACHE_DIR = os.path.join(tempfile.gettempdir(), "sound3ds-share-flac")
BLOCK = 65536


def stream_info(path):
    """(frecuencia, bits) del FLAC, o (0, 0) si no se puede leer."""
    try:
        info = FLAC(path).info
        return info.sample_rate, info.bits_per_sample
    except Exception:
        return 0, 0


def can_convert():
    try:
        import numpy, soundfile, soxr  # noqa: F401
        return True
    except Exception:
        return False


def target_rate(rate):
    # Múltiplos de 44,1 kHz (88,2 / 176,4) bajan a 44,1; el resto (96 / 192) a 48
    return 44100 if rate % 44100 == 0 else 48000


def convert(src_path, dst_path):
    """Remuestrea a 44,1/48 kHz y escribe FLAC de 16 bits con dither triangular."""
    import numpy as np
    import soundfile as sf
    import soxr

    rng = np.random.default_rng()
    lsb = 1.0 / 32768.0
    top = 32767.0 / 32768.0
    with sf.SoundFile(src_path) as src:
        rate, ch = src.samplerate, src.channels
        out_rate = target_rate(rate)
        rs = soxr.ResampleStream(rate, out_rate, ch, dtype="float32", quality="HQ")
        with sf.SoundFile(dst_path, "w", out_rate, ch, "PCM_16", format="FLAC") as dst:
            def write(y):
                if len(y):
                    y = y + (rng.random(y.shape, dtype=np.float32) - rng.random(y.shape, dtype=np.float32)) * lsb
                    dst.write(np.clip(y, -1.0, top))
            for block in src.blocks(blocksize=BLOCK, dtype="float32", always_2d=True):
                write(rs.resample_chunk(block))
            write(rs.resample_chunk(np.zeros((0, ch), dtype="float32"), last=True))


def write_tags(path, song, cover):
    f = FLAC(path)
    f.clear_pictures()
    if f.tags is not None:
        f.tags.clear()
    else:
        f.add_tags()
    for key, field in (("TITLE", "title"), ("ARTIST", "artist"), ("ALBUM", "album"), ("ALBUMARTIST", "album_artist")):
        if song.get(field):
            f[key] = song[field]
    if int(song.get("track") or 0) > 0:
        f["TRACKNUMBER"] = str(int(song["track"]))
    if cover:
        data, mime = cover
        pic = Picture()
        pic.type, pic.mime, pic.data = 3, mime, data
        f.add_picture(pic)
    f.save()


def prepare(path, song, cover):
    """Ruta de la copia lista para enviar (se crea si no existe). Lanza excepción si falla."""
    st = os.stat(path)
    rate, _ = stream_info(path)
    convert_it = rate > MAX_RATE and can_convert()
    meta = {k: song.get(k) for k in ("title", "artist", "album", "album_artist", "track")}
    key = hashlib.sha1(json.dumps([path, st.st_size, st.st_mtime_ns, meta, convert_it,
                                   hashlib.sha1(cover[0]).hexdigest() if cover else ""]).encode()).hexdigest()
    os.makedirs(CACHE_DIR, exist_ok=True)
    out = os.path.join(CACHE_DIR, key + ".flac")
    if os.path.isfile(out):
        return out
    tmp = out + ".part"
    try:
        if convert_it:
            convert(path, tmp)
        else:
            shutil.copyfile(path, tmp)
        write_tags(tmp, song, cover)
        os.replace(tmp, out)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)
    return out
