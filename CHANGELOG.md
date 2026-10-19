# Historial de cambios

## [Sin publicar]

### Sound3DS Share (PC)
- **Conectar SoundCloud**: botón en la barra superior. Abre la página oficial de inicio de sesión en una ventana aparte (Edge, Chrome, Chromium o Brave con un perfil temporal), detecta solo cuando el usuario entra (con Google, Apple o verificación en dos pasos) y muestra un QR para la consola. La contraseña nunca pasa por la app.
- La sesión se lee por el protocolo de depuración del navegador, sin dependencias nuevas. Al terminar se cierra la ventana y se borra el perfil temporal; los tokens quedan solo en memoria.
- El QR lleva un enlace de un solo uso (código aleatorio de 128 bits) que vence en 10 minutos; nunca lleva el token.
- Antes de iniciar sesión se muestra la advertencia de seguridad sobre guardar la sesión en la SD.
- Opción avanzada, oculta en Configuración, para pegar el token a mano si no hay un navegador compatible.

### Sound3DS QR (consola)
- Reconoce el QR de **Conectar SoundCloud**: baja la cuenta del enlace de un solo uso y la guarda en `sdmc:/3ds/sound3ds/account.json` (escrita a un temporal y luego renombrada, solo con los campos conocidos y la hora de la consola). Muestra "Cuenta conectada" con el usuario y, una vez, la advertencia de seguridad. Si el QR ya se usó o venció lo explica y no toca la cuenta guardada.

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
