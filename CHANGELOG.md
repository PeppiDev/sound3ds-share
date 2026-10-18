# Historial de cambios

## [1.0.0] - 2026-10-18

Primera versión pública.

### Sound3DS Share (PC)
- Interfaz gráfica para Linux y Windows: explorador de archivos con accesos a Música, Descargas, Escritorio y discos, filtro, selección múltiple y "+ carpeta".
- Canciones agrupadas por álbum según su etiqueta, con carátula incrustada o de la carpeta (`cover.jpg`, `folder.jpg`, `front.jpg`).
- Edición de álbum, artista del álbum, número, título y artista; mover canciones entre álbumes; elegir o unificar carátulas.
- Al enviar, cada canción lleva una etiqueta ID3v2.3 nueva con la carátula reducida a 500 px; los archivos del PC no se modifican.
- QR con el avance de cada descarga en vivo, selector de red si el PC tiene varias e interruptor del servicio.
- Interfaz en español e inglés.
- Ejecutable único: Linux (glibc 2.31 o más nueva) y Windows 10/11.

### Sound3DS QR (consola)
- La cámara captura en su propio hilo y se relanza sola si se detiene, así la imagen no se queda pegada.
- Lectura del QR en segundo plano, también de códigos en espejo.
- Descargas en segundo plano con avance total, por canción, MB y velocidad; (B) cancela, HOME funciona y cerrar la tapa no corta la descarga.
- Nueva interfaz con visor animado, pasos, lista de descargas y resumen.
- Usa el idioma elegido en Sound3DS.
