// Sound3DS QR: escanea con la cámara un QR que muestra el PC y descarga las
// canciones a sdmc:/music, la carpeta que lee Sound3DS. Así no hace falta sacar
// la tarjeta SD.
//
// Hilos:
//   - cámara (camera.c): captura sin parar y publica el último cuadro
//   - decodificador (aquí): busca un QR en los cuadros con quirc, sin frenar la vista
//   - descargas (job.c): lee la lista y baja las canciones
// El hilo principal solo dibuja y atiende los botones, así HOME, cancelar y las
// animaciones responden siempre.

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "camera.h"
#include "job.h"
#include "ui.h"
#include "quirc/quirc.h"

#define FOUND_ANIM_S 0.45f   // cuánto se muestra "QR detectado" antes de empezar
#define CAM_STALL_MS 2500    // sin cuadros por este tiempo: relanzar la cámara

// ---------------------------------------------------------------------------
// Decodificador
// ---------------------------------------------------------------------------

static struct quirc* s_quirc;
static Thread s_dec_thread;
static volatile bool s_dec_quit = false;
static volatile bool s_dec_enabled = false;
static volatile bool s_dec_found = false;
static LightLock s_dec_lock;
static char s_dec_text[1024];

static void decode_thread(void* arg) {
    (void)arg;
    while (!s_dec_quit) {
        LightEvent_Wait(camera_frame_event());
        if (s_dec_quit) break;
        if (!s_dec_enabled || s_dec_found) continue;

        int w, h;
        uint8_t* img = quirc_begin(s_quirc, &w, &h);
        const u16* f = camera_lock_frame();
        for (int i = 0; i < CAM_W * CAM_H; i++) {
            u16 p = f[i];
            int r = (p >> 11) << 3, g = ((p >> 5) & 0x3F) << 2, b = (p & 0x1F) << 3;
            img[i] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
        }
        camera_unlock_frame();
        quirc_end(s_quirc);

        int n = quirc_count(s_quirc);
        for (int i = 0; i < n; i++) {
            struct quirc_code code;
            struct quirc_data data;
            quirc_extract(s_quirc, i, &code);
            if (quirc_decode(&code, &data) != QUIRC_SUCCESS) {
                quirc_flip(&code);   // por si la cámara entrega la imagen en espejo
                if (quirc_decode(&code, &data) != QUIRC_SUCCESS) continue;
            }
            LightLock_Lock(&s_dec_lock);
            snprintf(s_dec_text, sizeof(s_dec_text), "%.*s", data.payload_len, (const char*)data.payload);
            s_dec_found = true;
            LightLock_Unlock(&s_dec_lock);
            break;
        }
    }
}

static bool decoder_start(void) {
    LightLock_Init(&s_dec_lock);
    s_quirc = quirc_new();
    if (!s_quirc || quirc_resize(s_quirc, CAM_W, CAM_H) < 0) return false;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    // Menos prioridad que la interfaz: usa el tiempo que le sobra a cada cuadro
    s_dec_thread = threadCreate(decode_thread, NULL, 0x8000, prio + 2, -2, false);
    return s_dec_thread != NULL;
}

static void decoder_stop(void) {
    if (s_dec_thread) {
        s_dec_quit = true;
        LightEvent_Signal(camera_frame_event());
        threadJoin(s_dec_thread, U64_MAX);
        threadFree(s_dec_thread);
    }
    if (s_quirc) quirc_destroy(s_quirc);
}

// ---------------------------------------------------------------------------
// Cámara: se apaga al ir a HOME o dormir y se relanza al volver
// ---------------------------------------------------------------------------

static bool s_want_camera = false;
static u32 s_cam_base_seq;      // contador al arrancar: hay imagen cuando cambia
static int s_cam_restarts;      // relanzamientos seguidos sin recibir ningún cuadro

static void scan_camera_on(void) {
    s_want_camera = true;
    if (!camera_running()) {
        s_cam_base_seq = camera_frame_seq();
        camera_start();
    }
}

static void scan_camera_off(void) {
    s_want_camera = false;
    camera_stop();
}

static void apt_hook(APT_HookType type, void* param) {
    (void)param;
    if (type == APTHOOK_ONSUSPEND || type == APTHOOK_ONSLEEP) {
        camera_stop();
    } else if ((type == APTHOOK_ONRESTORE || type == APTHOOK_ONWAKEUP) && s_want_camera) {
        s_cam_base_seq = camera_frame_seq();
        camera_start();
    }
}

// Idioma de la app principal (sdmc:/3ds/sound3ds/settings.ini): 0 español, 1 inglés
static bool read_lang_en(void) {
    bool en = true;
    FILE* f = fopen("sdmc:/3ds/sound3ds/settings.ini", "r");
    if (!f) return en;
    char line[64];
    int v;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "lang=%d", &v) == 1) en = v != 0;
    fclose(f);
    return en;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    osSetSpeedupEnable(true);
    ui_init();
    mkdir("sdmc:/music", 0777);
    bool ac_ok = R_SUCCEEDED(acInit());
    bool net_ok = job_net_init();
    bool dec_ok = decoder_start();

    aptHookCookie cookie;
    aptHook(&cookie, apt_hook, NULL);

    static JobItem items[JOB_MAX];
    UiModel m;
    memset(&m, 0, sizeof(m));
    m.en = read_lang_en();
    m.net_ok = net_ok;
    m.wifi_ok = true;
    m.found_t = -1;
    m.screen = SCR_SCAN;
    m.items = items;
    m.cam_failed = !dec_ok;

    char qr[1024] = "";
    u32 shown_seq = 0;
    u64 last_ms = osGetTime(), wifi_ms = 0;

    scan_camera_on();
    s_dec_enabled = true;

#ifdef QR_AUTOTEST
    // Prueba en el emulador (sin cámara): el texto del QR sale de un archivo
    {
        FILE* f = fopen("sdmc:/3ds/sound3ds/qr_autotest.txt", "r");
        if (f) {
            if (fgets(qr, sizeof(qr), f)) {
                qr[strcspn(qr, "\r\n")] = 0;
                s_dec_enabled = false;
                m.found_t = 0;
            }
            fclose(f);
        }
    }
#endif

    while (aptMainLoop()) {
        u64 now_ms = osGetTime();
        float dt = (now_ms - last_ms) / 1000.0f;
        last_ms = now_ms;

        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld();
        touchPosition touch;
        hidTouchRead(&touch);
        UiButton tb = (held & KEY_TOUCH) ? ui_button_at(m.screen, touch.px, touch.py) : BTN_NONE;
        UiButton tap = (down & KEY_TOUCH) ? tb : BTN_NONE;
        m.pressed = tb;

        // Estado del Wi-Fi una vez por segundo
        if (ac_ok && now_ms - wifi_ms > 1000) {
            u32 st = 0;
            m.wifi_ok = R_SUCCEEDED(ACU_GetWifiStatus(&st)) && st != 0;
            wifi_ms = now_ms;
        }

        bool quit = (down & KEY_START) || tap == BTN_START;

        if (m.screen == SCR_SCAN) {
            if (quit) break;
            // Hay imagen desde que llega el primer cuadro de esta sesión de la cámara
            bool live = camera_frame_seq() != s_cam_base_seq;
            if (live) {
                m.has_frame = true;
                s_cam_restarts = 0;
                m.cam_failed = !dec_ok;
            }
            // Si la captura se cuelga, relanzar la cámara completa
            if (camera_running() && camera_ms_since_frame() > CAM_STALL_MS) {
                camera_stop();
                s_cam_base_seq = camera_frame_seq();
                camera_start();
                if (++s_cam_restarts >= 3 && !m.has_frame) m.cam_failed = true;
            }

            if (m.found_t < 0 && s_dec_found) {
                LightLock_Lock(&s_dec_lock);
                snprintf(qr, sizeof(qr), "%s", s_dec_text);
                LightLock_Unlock(&s_dec_lock);
                s_dec_enabled = false;
                m.found_t = 0;
            } else if (m.found_t >= 0) {
                m.found_t += dt;
                if (m.found_t > FOUND_ANIM_S) {
                    scan_camera_off();
                    job_start(qr);
                    aptSetSleepAllowed(false);   // que cerrar la tapa no corte la descarga
                    m.screen = SCR_WORK;
                    m.cancelling = false;
                }
            }
        } else if (m.screen == SCR_WORK) {
            if ((down & KEY_B) || tap == BTN_B || quit) {
                job_cancel();
                m.cancelling = true;
            }
            job_status(&m.job);
            m.item_count = job_items(items, JOB_MAX);
            if (quit) {
                job_reset();
                break;
            }
            if (m.job.phase == JOB_DONE) {
                aptSetSleepAllowed(true);
                m.screen = SCR_DONE;
                m.scroll = 0;
            }
        } else {
            if (quit) break;
            int max_scroll = m.item_count > UI_LIST_ROWS ? m.item_count - UI_LIST_ROWS : 0;
            if ((down & KEY_DOWN) && m.scroll < max_scroll) m.scroll++;
            if ((down & KEY_UP) && m.scroll > 0) m.scroll--;
            if ((down & KEY_A) || tap == BTN_A) {
                job_reset();
                m.item_count = 0;
                memset(&m.job, 0, sizeof(m.job));
                m.found_t = -1;
                m.has_frame = false;
                m.screen = SCR_SCAN;
                s_dec_found = false;
                s_dec_enabled = true;
                scan_camera_on();
            }
        }

        ui_begin();
        if (m.screen == SCR_SCAN && m.found_t < 0) {
            u32 seq = camera_frame_seq();
            if (seq != shown_seq && seq != s_cam_base_seq) {
                ui_upload_camera(camera_lock_frame());
                camera_unlock_frame();
                shown_seq = seq;
            }
        }
        ui_draw(&m);
        ui_end();
    }

    aptSetSleepAllowed(true);
    aptUnhook(&cookie);
    scan_camera_off();
    decoder_stop();
    job_net_exit();
    if (ac_ok) acExit();
    ui_exit();
    return 0;
}
