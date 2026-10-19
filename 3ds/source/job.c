#include "job.h"
#include <curl/curl.h>
#include <jansson.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000
#define URL_LEN 512

static u32* s_soc_buf;
static bool s_net_ok = false;

static LightLock s_lock;
static Thread s_thread;
static volatile bool s_cancel;
static JobStatus s_st;
static JobItem s_items[JOB_MAX];
static char s_urls[JOB_MAX][URL_LEN];
static char s_qr[1024];

bool job_net_init(void) {
    LightLock_Init(&s_lock);
    s_soc_buf = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    s_net_ok = s_soc_buf && R_SUCCEEDED(socInit(s_soc_buf, SOC_BUFFERSIZE));
    curl_global_init(CURL_GLOBAL_ALL);
    return s_net_ok;
}

void job_net_exit(void) {
    job_cancel();
    job_reset();
    curl_global_cleanup();
    if (s_net_ok) socExit();
    free(s_soc_buf);
    s_net_ok = false;
}

// ---------------------------------------------------------------------------
// Utilidades de URL
// ---------------------------------------------------------------------------

typedef struct {
    char* data;
    size_t size, cap;
} Mem;

static size_t mem_write(void* p, size_t sz, size_t n, void* user) {
    Mem* m = (Mem*)user;
    size_t len = sz * n;
    if (m->size + len + 1 > 256 * 1024) return 0;   // una lista no debería pasar de esto
    if (m->size + len + 1 > m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 8192;
        while (nc < m->size + len + 1) nc *= 2;
        char* g = realloc(m->data, nc);
        if (!g) return 0;
        m->data = g;
        m->cap = nc;
    }
    memcpy(m->data + m->size, p, len);
    m->size += len;
    m->data[m->size] = 0;
    return len;
}

static void common_opts(CURL* c, const char* url) {
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Sound3DS-QR/1.1");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
}

static bool has_audio_ext(const char* s, size_t len) {
    return len > 4 && (strncasecmp(s + len - 4, ".mp3", 4) == 0 || strncasecmp(s + len - 4, ".aac", 4) == 0);
}

// Largo de la URL sin "?consulta" ni "#fragmento"
static size_t path_len(const char* url) { return strcspn(url, "?#"); }

// Nombre de archivo desde la URL: último tramo de la ruta, %XX decodificado y sin
// caracteres que FAT no acepta
static void file_name_from_url(const char* url, char* out, size_t out_sz) {
    size_t n = path_len(url);
    const char* start = url;
    for (size_t i = 0; i < n; i++)
        if (url[i] == '/') start = url + i + 1;
    size_t o = 0;
    for (const char* p = start; p < url + n && o + 1 < out_sz; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '%' && p + 2 < url + n) {
            char hex[3] = {p[1], p[2], 0};
            c = (unsigned char)strtol(hex, NULL, 16);
            p += 2;
        } else if (c == '+') {
            c = ' ';
        }
        if (c < 0x20 || strchr("\\/:*?\"<>|", c)) c = '_';
        out[o++] = (char)c;
    }
    out[o] = 0;
    if (o == 0) snprintf(out, out_sz, "cancion.mp3");
}

// Convierte una línea en URL absoluta (relativa a la URL de la lista si hace falta)
static void absolute_url(const char* base, const char* line, char* out, size_t out_sz) {
    if (strncmp(line, "http://", 7) == 0 || strncmp(line, "https://", 8) == 0) {
        snprintf(out, out_sz, "%s", line);
        return;
    }
    const char* host_end = strstr(base, "://");
    host_end = host_end ? strchr(host_end + 3, '/') : NULL;
    if (line[0] == '/') {
        int n = host_end ? (int)(host_end - base) : (int)strlen(base);
        snprintf(out, out_sz, "%.*s%s", n, base, line);
    } else {
        const char* slash = strrchr(base, '/');
        int n = slash && slash > strstr(base, "://") + 2 ? (int)(slash - base + 1) : (int)strlen(base);
        snprintf(out, out_sz, "%.*s%s%s", n, base, (slash ? "" : "/"), line);
    }
}

static void add_url(const char* url) {
    LightLock_Lock(&s_lock);
    int n = s_st.count;
    if (n < JOB_MAX) {
        snprintf(s_urls[n], URL_LEN, "%s", url);
        char name[160];
        file_name_from_url(url, name, sizeof(name));
        snprintf(s_items[n].name, sizeof(s_items[n].name), "%s", name);
        s_items[n].state = IT_PENDING;
        s_st.count = n + 1;
    }
    LightLock_Unlock(&s_lock);
}

static int cancel_cb(void* user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)user; (void)a; (void)b; (void)c; (void)d;
    return s_cancel ? 1 : 0;
}

// Reúne las URLs de canciones a partir del contenido del QR
static bool collect_urls(CURL* c, const char* qr) {
    bool list_failed = false;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", qr);
    char* save = NULL;
    for (char* line = strtok_r(buf, "\r\n", &save); line && !s_cancel; line = strtok_r(NULL, "\r\n", &save)) {
        while (*line == ' ') line++;
        if (strncmp(line, "http", 4) != 0) continue;
        if (has_audio_ext(line, path_len(line))) {
            add_url(line);
            continue;
        }
        // No es una canción: se trata como una lista que hay que descargar
        Mem m = {0};
        curl_easy_reset(c);
        common_opts(c, line);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, cancel_cb);
        if (curl_easy_perform(c) != CURLE_OK || !m.data) {
            list_failed = true;
            free(m.data);
            continue;
        }
        char* save2 = NULL;
        for (char* l = strtok_r(m.data, "\r\n", &save2); l; l = strtok_r(NULL, "\r\n", &save2)) {
            while (*l == ' ') l++;
            if (!*l || *l == '#') continue;
            char abs[URL_LEN];
            absolute_url(line, l, abs, sizeof(abs));
            if (has_audio_ext(abs, path_len(abs))) add_url(abs);
        }
        free(m.data);
    }
    return !list_failed;
}

// ---------------------------------------------------------------------------
// Cuenta de SoundCloud
// ---------------------------------------------------------------------------

// ¿El QR es el enlace de un solo uso de Sound3DS Share: http://<ip>:<puerto>/account/<código>?
static bool is_account_url(const char* qr) {
    if (strncmp(qr, "http://", 7) != 0) return false;
    const char* p = strstr(qr + 7, "/account/");
    if (!p || p != strchr(qr + 7, '/')) return false;
    p += 9;
    size_t n = strspn(p, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-");
    return n >= 16 && n <= 64 && (p[n] == 0 || p[n] == '\r' || p[n] == '\n');
}

static const char* json_str(json_t* o, const char* key, size_t max) {
    const char* v = json_string_value(json_object_get(o, key));
    return v && *v && strlen(v) < max ? v : NULL;
}

// Baja la cuenta y la guarda en ACCOUNT_PATH. Solo se copian los campos
// conocidos, así un archivo raro no termina en la SD. Nada de esto se registra:
// los tokens dan acceso a la cuenta.
static void receive_account(CURL* c, const char* url) {
    char clean[URL_LEN];
    snprintf(clean, sizeof(clean), "%.*s", (int)strcspn(url, "\r\n"), url);
    Mem m = {0};
    curl_easy_reset(c);
    common_opts(c, clean);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, cancel_cb);
    CURLcode res = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);

    JobError err = JERR_NONE;
    char user[64] = "";
    if (res != CURLE_OK || !m.data) {
        // 404: el enlace ya se usó o venció; lo demás es de red
        err = (res == CURLE_HTTP_RETURNED_ERROR && http == 404) ? JERR_ACCOUNT : JERR_LIST;
    } else {
        json_t* in = json_loads(m.data, 0, NULL);
        const char* name = json_str(in, "username", 64);
        const char* access = json_str(in, "access_token", 4096);
        const char* refresh = json_str(in, "refresh_token", 512);
        const char* client = json_str(in, "auth_client_id", 128);
        json_t* uid = json_object_get(in, "user_id");
        // Con el token pegado a mano puede no venir refresh ni client_id: entonces
        // Sound3DS no puede renovarla y pide volver a conectar cuando vence
        if (!refresh || !client) refresh = client = "";
        if (!name || !access || !json_is_integer(uid)) {
            err = JERR_ACCOUNT;
        } else {
            json_t* out = json_pack("{s:s, s:I, s:s, s:s, s:s, s:I}",
                                    "username", name, "user_id", json_integer_value(uid),
                                    "access_token", access, "refresh_token", refresh,
                                    "auth_client_id", client,
                                    // Hora de la consola, no la del PC: Sound3DS calcula la
                                    // edad del token con este mismo reloj (que puede no estar en UTC)
                                    "obtained_at", (json_int_t)time(NULL));
            mkdir("sdmc:/3ds", 0777);
            mkdir(ACCOUNT_DIR, 0777);
            // Primero a un temporal y luego se renombra: un corte no deja el archivo a medias
            if (out && json_dump_file(out, ACCOUNT_PATH ".tmp", JSON_COMPACT) == 0) {
                remove(ACCOUNT_PATH);
                if (rename(ACCOUNT_PATH ".tmp", ACCOUNT_PATH) == 0) snprintf(user, sizeof(user), "%s", name);
                else err = JERR_SD;
            } else {
                remove(ACCOUNT_PATH ".tmp");
                err = JERR_SD;
            }
            json_decref(out);
        }
        json_decref(in);
    }
    if (m.data) memset(m.data, 0, m.size);   // no dejar los tokens en la memoria libre
    free(m.data);

    LightLock_Lock(&s_lock);
    s_st.error = s_cancel ? JERR_NONE : err;
    s_st.cancelled = s_cancel;
    snprintf(s_st.username, sizeof(s_st.username), "%s", user);
    s_st.phase = JOB_DONE;
    LightLock_Unlock(&s_lock);
}

// ---------------------------------------------------------------------------
// Descarga de cada canción
// ---------------------------------------------------------------------------

typedef struct {
    u64 window_ms;
    s64 window_bytes;
} Speed;

static int progress_cb(void* user, curl_off_t total, curl_off_t now, curl_off_t ut, curl_off_t un) {
    (void)ut; (void)un;
    if (s_cancel) return 1;
    Speed* sp = (Speed*)user;
    u64 t = osGetTime();
    LightLock_Lock(&s_lock);
    s_st.now = now;
    s_st.total = total;
    if (t - sp->window_ms >= 500) {
        s_st.speed = (u32)((now - sp->window_bytes) * 1000 / (s64)(t - sp->window_ms));
        sp->window_ms = t;
        sp->window_bytes = now;
    }
    LightLock_Unlock(&s_lock);
    return 0;
}

static ItemState download_one(CURL* c, const char* url, const char* name) {
    char path[256], part[264];
    snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, name);
    snprintf(part, sizeof(part), "%s.part", path);

    FILE* exists = fopen(path, "rb");
    if (exists) {
        fclose(exists);
        return IT_EXISTS;
    }
    FILE* f = fopen(part, "wb");
    if (!f) return IT_FAIL;
    // Escribir a la SD en bloques grandes: con los 16 KB por defecto de curl va mucho más lento
    setvbuf(f, NULL, _IOFBF, 128 * 1024);

    Speed sp = { osGetTime(), 0 };
    curl_easy_reset(c);
    common_opts(c, url);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 64L * 1024);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &sp);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 512L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
    CURLcode res = curl_easy_perform(c);
    bool closed = fclose(f) == 0;

    if (res == CURLE_OK && closed && rename(part, path) == 0) return IT_OK;
    remove(part);
    return res == CURLE_ABORTED_BY_CALLBACK ? IT_CANCEL : IT_FAIL;
}

static void set_item(int i, ItemState st) {
    LightLock_Lock(&s_lock);
    s_items[i].state = st;
    if (st == IT_OK) s_st.ok++;
    else if (st == IT_EXISTS) s_st.skipped++;
    else if (st == IT_FAIL) s_st.failed++;
    LightLock_Unlock(&s_lock);
}

static void job_thread(void* arg) {
    (void)arg;
    CURL* c = curl_easy_init();
    if (c && is_account_url(s_qr)) {
        receive_account(c, s_qr);
        curl_easy_cleanup(c);
        return;
    }
    bool list_ok = c && collect_urls(c, s_qr);

    LightLock_Lock(&s_lock);
    s_st.phase = JOB_DOWNLOAD;
    int count = s_st.count;
    if (!c) s_st.error = JERR_LIST;
    else if (count == 0) s_st.error = list_ok ? JERR_EMPTY : JERR_LIST;
    LightLock_Unlock(&s_lock);

    for (int i = 0; i < count && !s_cancel; i++) {
        LightLock_Lock(&s_lock);
        s_st.current = i;
        s_st.now = s_st.total = 0;
        s_st.speed = 0;
        s_items[i].state = IT_ACTIVE;
        LightLock_Unlock(&s_lock);
        set_item(i, download_one(c, s_urls[i], s_items[i].name));
    }
    if (c) curl_easy_cleanup(c);

    LightLock_Lock(&s_lock);
    if (s_cancel) {
        s_st.cancelled = true;
        for (int i = 0; i < s_st.count; i++)
            if (s_items[i].state == IT_PENDING || s_items[i].state == IT_ACTIVE) s_items[i].state = IT_CANCEL;
    }
    s_st.phase = JOB_DONE;
    LightLock_Unlock(&s_lock);
}

bool job_start(const char* qr_text) {
    if (s_thread) return false;
    memset(&s_st, 0, sizeof(s_st));
    s_st.phase = JOB_LIST;
    s_cancel = false;
    snprintf(s_qr, sizeof(s_qr), "%s", qr_text);
    s_st.account = is_account_url(s_qr);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    // Un poco menos de prioridad que la interfaz para que las animaciones no se traben
    s_thread = threadCreate(job_thread, NULL, 0x10000, prio + 1, -2, false);
    if (!s_thread) {
        s_st.phase = JOB_DONE;
        s_st.error = JERR_LIST;
        return false;
    }
    return true;
}

void job_cancel(void) { s_cancel = true; }

void job_reset(void) {
    if (s_thread) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    memset(&s_st, 0, sizeof(s_st));
}

void job_status(JobStatus* out) {
    LightLock_Lock(&s_lock);
    *out = s_st;
    LightLock_Unlock(&s_lock);
}

int job_items(JobItem* out, int max) {
    LightLock_Lock(&s_lock);
    int n = s_st.count < max ? s_st.count : max;
    memcpy(out, s_items, n * sizeof(JobItem));
    LightLock_Unlock(&s_lock);
    return n;
}
