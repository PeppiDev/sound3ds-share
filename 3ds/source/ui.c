#include "ui.h"
#include "camera.h"
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

// Paleta (morado de la mascota, como el banner de la app)
#define COL_BG        C2D_Color32(24, 14, 42, 255)
#define COL_BG2       C2D_Color32(44, 22, 78, 255)
#define COL_HEADER    C2D_Color32(52, 26, 92, 255)
#define COL_PANEL     C2D_Color32(40, 26, 68, 255)
#define COL_PANEL_HI  C2D_Color32(66, 42, 110, 255)
#define COL_ACCENT    C2D_Color32(190, 110, 255, 255)
#define COL_CYAN      C2D_Color32(70, 220, 230, 255)
#define COL_OK        C2D_Color32(80, 214, 128, 255)
#define COL_WARN      C2D_Color32(255, 184, 72, 255)
#define COL_ERR       C2D_Color32(255, 96, 112, 255)
#define COL_TEXT      C2D_Color32(246, 240, 255, 255)
#define COL_DIM       C2D_Color32(170, 154, 204, 255)
#define COL_INK       C2D_Color32(28, 16, 48, 255)

static struct {
    C3D_RenderTarget* top;
    C3D_RenderTarget* bot;
    C2D_TextBuf text;
    C3D_Tex cam_tex;
    bool cam_tex_ok;
    u32 row_base[CAM_H];
    u32 col_base[CAM_W];
    u64 start_ms;
    bool en;
    float z;
} s;

#define T(es_, en_) (s.en ? (en_) : (es_))

// Citro2D usa prueba de profundidad: cada dibujo recibe un z apenas mayor que el
// anterior para que se apilen en el orden en que se dibujan
static float zn(void) { return s.z += 0.00005f; }

static float now_s(void) { return (float)((osGetTime() - s.start_ms) % 3600000ULL) / 1000.0f; }

bool ui_init(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    s.top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s.bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    fontEnsureMapped();
    s.text = C2D_TextBufNew(4096);
    s.start_ms = osGetTime();

    // Textura de 512x256 para el cuadro de 400x240, en el orden de mosaico de la GPU:
    // bloques de 8x8 con los píxeles en orden Morton. El desplazamiento se separa en
    // una parte por fila y otra por columna para convertir cada cuadro rápido.
    s.cam_tex_ok = C3D_TexInit(&s.cam_tex, 512, 256, GPU_RGB565);
    if (s.cam_tex_ok) {
        C3D_TexSetFilter(&s.cam_tex, GPU_LINEAR, GPU_LINEAR);
        memset(s.cam_tex.data, 0, s.cam_tex.size);
    }
    for (int x = 0; x < CAM_W; x++) {
        int i = x & 7;
        s.col_base[x] = (x >> 3) * 64 + ((i & 1) | ((i & 2) << 1) | ((i & 4) << 2));
    }
    for (int y = 0; y < CAM_H; y++) {
        int i = y & 7;
        s.row_base[y] = (y >> 3) * (512 / 8) * 64 + (((i & 1) << 1) | ((i & 2) << 2) | ((i & 4) << 3));
    }
    return s.text != NULL;
}

void ui_exit(void) {
    if (s.cam_tex_ok) C3D_TexDelete(&s.cam_tex);
    if (s.text) C2D_TextBufDelete(s.text);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

void ui_begin(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(s.text);
}

void ui_upload_camera(const u16* frame) {
    if (!s.cam_tex_ok || !frame) return;
    u16* dst = (u16*)s.cam_tex.data;
    for (int y = 0; y < CAM_H; y++) {
        const u16* src = frame + y * CAM_W;
        u16* row = dst + s.row_base[y];
        for (int x = 0; x < CAM_W; x++) row[s.col_base[x]] = src[x];
    }
    C3D_TexFlush(&s.cam_tex);
}

void ui_end(void) { C3D_FrameEnd(0); }

// ---------------------------------------------------------------------------
// Primitivas
// ---------------------------------------------------------------------------

static u32 alpha(u32 c, float a) {
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    return (c & 0x00FFFFFF) | ((u32)(((c >> 24) & 0xFF) * a) << 24);
}

static float text_width(const char* str, float scale) {
    C2D_Text t;
    C2D_TextParse(&t, s.text, str);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

static void text(float x, float y, float scale, u32 color, const char* str) {
    if (!str || !*str) return;
    C2D_Text t;
    C2D_TextParse(&t, s.text, str);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, zn(), scale, scale, color);
}

static void text_c(float cx, float y, float scale, u32 color, const char* str) {
    text(cx - text_width(str, scale) / 2, y, scale, color, str);
}

// Recorta con "..." para que quepa en max_w (mide con la tabla de anchos de la fuente)
static void fit(const char* src, float scale, float max_w, char* out, size_t out_sz) {
    float dots = 3.0f * fontGetCharWidthInfo(NULL, fontGlyphIndexFromCodePoint(NULL, '.'))->charWidth * scale;
    float w = 0;
    size_t fit_len = 0, fit_dots = 0;
    const uint8_t* p = (const uint8_t*)src;
    while (*p) {
        uint32_t cp;
        ssize_t units = decode_utf8(&cp, p);
        if (units <= 0) break;
        w += fontGetCharWidthInfo(NULL, fontGlyphIndexFromCodePoint(NULL, cp))->charWidth * scale;
        if (w > max_w) break;
        p += units;
        fit_len = (size_t)(p - (const uint8_t*)src);
        if (w + dots <= max_w) fit_dots = fit_len;
    }
    if (!*p) {
        snprintf(out, out_sz, "%s", src);
        return;
    }
    size_t n = fit_dots < out_sz - 4 ? fit_dots : out_sz - 4;
    memcpy(out, src, n);
    memcpy(out + n, "...", 4);
}

static void text_fit(float x, float y, float scale, u32 color, const char* str, float max_w) {
    char buf[200];
    fit(str, scale, max_w, buf, sizeof(buf));
    text(x, y, scale, color, buf);
}

static void text_fit_c(float cx, float y, float scale, u32 color, const char* str, float max_w) {
    char buf[200];
    fit(str, scale, max_w, buf, sizeof(buf));
    text_c(cx, y, scale, color, buf);
}

// Círculos como abanico de triángulos (Azahar no dibuja C2D_DrawCircleSolid)
static void circle(float cx, float cy, float r, u32 color) {
    int segs = r < 5 ? 12 : (r < 14 ? 20 : 36);
    float px = cx + r, py = cy, z = zn();
    for (int i = 1; i <= segs; i++) {
        float a = 6.2832f * i / segs;
        float nx = cx + cosf(a) * r, ny = cy + sinf(a) * r;
        C2D_DrawTriangle(cx, cy, color, px, py, color, nx, ny, color, z);
        px = nx;
        py = ny;
    }
}

// Abanico de triángulos (Azahar no dibuja los círculos ni elipses de Citro2D)
static void ellipse_z(float cx, float cy, float rx, float ry, float z, u32 color) {
    float r = rx > ry ? rx : ry;
    int segs = r < 5 ? 12 : (r < 14 ? 20 : 36);
    float px = cx + rx, py = cy;
    for (int i = 1; i <= segs; i++) {
        float a = 6.2832f * i / segs;
        float nx = cx + cosf(a) * rx, ny = cy + sinf(a) * ry;
        C2D_DrawTriangle(cx, cy, color, px, py, color, nx, ny, color, z);
        px = nx;
        py = ny;
    }
}

// Rectángulo redondeado: todas sus piezas con el mismo z para que el alfa no se sume
static void rrect(float x, float y, float w, float h, float r, u32 color) {
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    float z = zn();
    C2D_DrawRectSolid(x + r, y, z, w - 2 * r, h, color);
    C2D_DrawRectSolid(x, y + r, z, r, h - 2 * r, color);
    C2D_DrawRectSolid(x + w - r, y + r, z, r, h - 2 * r, color);
    // Cuartos de círculo en las esquinas
    const float q[4][3] = { {x + r, y + r, 3.1416f}, {x + w - r, y + r, 4.7124f},
                            {x + w - r, y + h - r, 0.0f}, {x + r, y + h - r, 1.5708f} };
    for (int k = 0; k < 4; k++) {
        float cx = q[k][0], cy = q[k][1], a0 = q[k][2];
        float px = cx + cosf(a0) * r, py = cy + sinf(a0) * r;
        for (int i = 1; i <= 6; i++) {
            float a = a0 + 1.5708f * i / 6;
            float nx = cx + cosf(a) * r, ny = cy + sinf(a) * r;
            C2D_DrawTriangle(cx, cy, color, px, py, color, nx, ny, color, z);
            px = nx;
            py = ny;
        }
    }
}

static void line(float x0, float y0, float x1, float y1, float th, u32 color) {
    C2D_DrawLine(x0, y0, color, x1, y1, color, th, zn());
}

// Arco grueso como banda de triángulos, con degradado de alfa opcional (cola de cometa)
static void arc(float cx, float cy, float r, float th, float a0, float a1, u32 color, bool fade) {
    int segs = (int)(fabsf(a1 - a0) * 8) + 2;
    float ri = r - th / 2, ro = r + th / 2, z = zn();
    for (int i = 0; i < segs; i++) {
        float a = a0 + (a1 - a0) * i / segs, b = a0 + (a1 - a0) * (i + 1) / segs;
        u32 ca = fade ? alpha(color, (float)i / segs) : color;
        u32 cb = fade ? alpha(color, (float)(i + 1) / segs) : color;
        float ax = cosf(a), ay = sinf(a), bx = cosf(b), by = sinf(b);
        C2D_DrawTriangle(cx + ax * ri, cy + ay * ri, ca, cx + ax * ro, cy + ay * ro, ca, cx + bx * ro, cy + by * ro, cb, z);
        C2D_DrawTriangle(cx + ax * ri, cy + ay * ri, ca, cx + bx * ro, cy + by * ro, cb, cx + bx * ri, cy + by * ri, cb, z);
    }
}

static void spinner(float cx, float cy, float r, float th, u32 color) {
    float a0 = now_s() * 6.0f;
    arc(cx, cy, r, th, a0, a0 + 4.2f, color, true);
    float ah = a0 + 4.2f;
    circle(cx + cosf(ah) * r, cy + sinf(ah) * r, th / 2, color);
}

static void icon_check(float cx, float cy, float sz, u32 color) {
    line(cx - sz * 0.42f, cy, cx - sz * 0.12f, cy + sz * 0.3f, sz * 0.2f, color);
    line(cx - sz * 0.12f, cy + sz * 0.3f, cx + sz * 0.45f, cy - sz * 0.35f, sz * 0.2f, color);
}

static void icon_cross(float cx, float cy, float sz, u32 color) {
    float d = sz * 0.34f;
    line(cx - d, cy - d, cx + d, cy + d, sz * 0.2f, color);
    line(cx - d, cy + d, cx + d, cy - d, sz * 0.2f, color);
}

static void icon_note(float x, float y, float sz, u32 color) {
    ellipse_z(x, y, sz * 0.3f, sz * 0.22f, zn(), color);
    line(x + sz * 0.26f, y, x + sz * 0.26f, y - sz * 0.85f, sz * 0.12f, color);
    line(x + sz * 0.26f, y - sz * 0.85f, x + sz * 0.6f, y - sz * 0.55f, sz * 0.12f, color);
}

static void gradient(float x, float y, float w, float h, u32 top, u32 bottom) {
    C2D_DrawRectangle(x, y, zn(), w, h, top, top, bottom, bottom);
}

static void fmt_size(s64 bytes, char* out, size_t sz) {
    if (bytes >= 1024 * 1024) snprintf(out, sz, "%.1f MB", bytes / 1048576.0);
    else snprintf(out, sz, "%lld KB", (long long)(bytes / 1024));
}

// ---------------------------------------------------------------------------
// Pantalla de arriba
// ---------------------------------------------------------------------------

#define VF_X 110
#define VF_Y 26
#define VF_S 180

static void pill_caption(float cy, u32 bg, u32 fg, const char* str) {
    float w = text_width(str, 0.55f) + 28;
    rrect(TOP_W / 2 - w / 2, cy - 12, w, 24, 12, bg);
    text_c(TOP_W / 2, cy - 9, 0.55f, fg, str);
}

static void draw_top_scan(const UiModel* m) {
    float t = now_s();
    bool found = m->found_t >= 0;
    if (m->has_frame && s.cam_tex_ok) {
        static const Tex3DS_SubTexture sub = { CAM_W, CAM_H, 0.0f, 1.0f, CAM_W / 512.0f, 1.0f - CAM_H / 256.0f };
        C2D_Image img = { &s.cam_tex, &sub };
        C2D_DrawImageAt(img, 0, 0, zn(), NULL, 1.0f, 1.0f);
    } else {
        gradient(0, 0, TOP_W, TOP_H, COL_BG2, COL_BG);
        if (!m->cam_failed) {
            spinner(TOP_W / 2, 110, 18, 5, COL_ACCENT);
            text_c(TOP_W / 2, 140, 0.55f, COL_DIM, T("Iniciando la cámara...", "Starting the camera..."));
        } else {
            circle(TOP_W / 2, 100, 26, COL_ERR);
            icon_cross(TOP_W / 2, 100, 28, COL_TEXT);
            text_c(TOP_W / 2, 140, 0.6f, COL_TEXT, T("No se pudo abrir la cámara", "Couldn't open the camera"));
            text_c(TOP_W / 2, 164, 0.48f, COL_DIM, T("Cierra la app y vuelve a abrirla", "Close the app and open it again"));
        }
        return;
    }

    // Oscurece fuera del visor para que se note dónde poner el código
    u32 shade = C2D_Color32(10, 4, 20, 120);
    C2D_DrawRectSolid(0, 0, zn(), TOP_W, VF_Y, shade);
    C2D_DrawRectSolid(0, VF_Y + VF_S, zn(), TOP_W, TOP_H - VF_Y - VF_S, shade);
    C2D_DrawRectSolid(0, VF_Y, zn(), VF_X, VF_S, shade);
    C2D_DrawRectSolid(VF_X + VF_S, VF_Y, zn(), TOP_W - VF_X - VF_S, VF_S, shade);

    // Esquinas: respiran mientras busca; se cierran y se ponen verdes al reconocerlo
    float k = found ? fminf(m->found_t / 0.25f, 1.0f) : 0.0f;
    float inset = found ? 10 * k : 2 + 2 * sinf(t * 3.0f);
    u32 cc = found ? COL_OK : COL_TEXT;
    float x0 = VF_X + inset, y0 = VF_Y + inset, x1 = VF_X + VF_S - inset, y1 = VF_Y + VF_S - inset, L = 28, th = 4;
    line(x0, y0, x0 + L, y0, th, cc); line(x0, y0, x0, y0 + L, th, cc);
    line(x1, y0, x1 - L, y0, th, cc); line(x1, y0, x1, y0 + L, th, cc);
    line(x0, y1, x0 + L, y1, th, cc); line(x0, y1, x0, y1 - L, th, cc);
    line(x1, y1, x1 - L, y1, th, cc); line(x1, y1, x1, y1 - L, th, cc);

    if (!found) {
        // Línea de barrido que sube y baja con una estela
        float ph = (sinf(t * 2.2f) + 1) / 2;
        float ly = VF_Y + 10 + ph * (VF_S - 20);
        bool down = cosf(t * 2.2f) > 0;
        u32 clear = alpha(COL_ACCENT, 0), glow = alpha(COL_ACCENT, 0.45f);
        if (down) C2D_DrawRectangle(VF_X + 8, ly - 18, zn(), VF_S - 16, 18, clear, clear, glow, glow);
        else C2D_DrawRectangle(VF_X + 8, ly, zn(), VF_S - 16, 18, glow, glow, clear, clear);
        C2D_DrawRectSolid(VF_X + 8, ly - 1, zn(), VF_S - 16, 2, COL_ACCENT);
        pill_caption(TOP_H - 18, C2D_Color32(20, 10, 36, 200), COL_TEXT, T("Apunta la cámara al código QR", "Point the camera at the QR code"));
    } else {
        // Destello al reconocerlo
        C2D_DrawRectSolid(0, 0, zn(), TOP_W, TOP_H, alpha(C2D_Color32(255, 255, 255, 255), 0.5f - m->found_t * 1.6f));
        pill_caption(TOP_H - 18, alpha(COL_OK, 0.92f), COL_INK, T("QR detectado", "QR found"));
    }
}

static void draw_top_work(const UiModel* m) {
    const JobStatus* j = &m->job;
    gradient(0, 0, TOP_W, TOP_H, COL_BG2, COL_BG);
    const float cx = TOP_W / 2, cy = 92, r = 58;
    char buf[96];

    if (j->phase == JOB_LIST || j->count == 0) {
        arc(cx, cy, r, 9, 0, 6.2832f, C2D_Color32(255, 255, 255, 24), false);
        spinner(cx, cy, r, 9, COL_ACCENT);
        icon_note(cx - 6, cy + 14, 34, COL_TEXT);
        text_c(cx, 172, 0.62f, COL_TEXT, T("Leyendo la lista del PC...", "Reading the list from the PC..."));
        return;
    }

    int done = j->ok + j->skipped + j->failed;
    float cur = j->total > 0 ? (float)j->now / (float)j->total : 0.0f;
    float frac = (done + (done < j->count ? cur : 0.0f)) / (float)j->count;
    if (frac > 1) frac = 1;
    arc(cx, cy, r, 10, 0, 6.2832f, C2D_Color32(255, 255, 255, 24), false);
    if (frac > 0.005f) {
        arc(cx, cy, r, 10, -1.5708f, -1.5708f + 6.2832f * frac, COL_ACCENT, false);
        circle(cx + cosf(-1.5708f + 6.2832f * frac) * r, cy + sinf(-1.5708f + 6.2832f * frac) * r, 5, COL_ACCENT);
    }
    snprintf(buf, sizeof(buf), "%d/%d", j->current + 1 > j->count ? j->count : j->current + 1, j->count);
    text_c(cx, cy - 22, 0.95f, COL_TEXT, buf);
    text_c(cx, cy + 10, 0.45f, COL_DIM, T("canciones", "songs"));

    const char* name = (m->items && j->current < m->item_count) ? m->items[j->current].name : "";
    text_fit_c(cx, 164, 0.6f, COL_TEXT, name, 370);

    char a[24], b[24];
    fmt_size(j->now, a, sizeof(a));
    if (j->total > 0) {
        fmt_size(j->total, b, sizeof(b));
        snprintf(buf, sizeof(buf), "%s / %s   -   %lu KB/s", a, b, (unsigned long)(j->speed / 1024));
    } else {
        snprintf(buf, sizeof(buf), "%s   -   %lu KB/s", a, (unsigned long)(j->speed / 1024));
    }
    text_c(cx, 190, 0.48f, COL_DIM, m->cancelling ? T("Cancelando...", "Cancelling...") : buf);

    // Avance de la canción actual
    rrect(70, 218, 260, 8, 4, C2D_Color32(255, 255, 255, 30));
    if (cur > 0.02f) rrect(70, 218, 260 * cur, 8, 4, COL_CYAN);
}

static void draw_top_done(const UiModel* m) {
    const JobStatus* j = &m->job;
    float t = now_s();
    gradient(0, 0, TOP_W, TOP_H, COL_BG2, COL_BG);
    const float cx = TOP_W / 2, cy = 84;
    char buf[128];

    bool error = j->error != JERR_NONE || (j->count > 0 && j->failed == j->count);
    bool partial = j->cancelled || j->failed > 0;
    u32 col = error ? COL_ERR : (partial ? COL_WARN : COL_OK);

    // Notas que suben cuando todo salió bien
    if (!error && !partial) {
        for (int i = 0; i < 4; i++) {
            float ph = fmodf(t * 0.35f + i * 0.25f, 1.0f);
            float nx = cx + (i % 2 ? 1 : -1) * (70 + 18 * i) + sinf(ph * 6 + i) * 6;
            icon_note(nx, cy + 40 - ph * 70, 18, alpha(i % 2 ? COL_CYAN : COL_ACCENT, 1.0f - ph));
        }
    }
    circle(cx, cy, 44, alpha(col, 0.25f));
    circle(cx, cy, 36, col);
    if (error) icon_cross(cx, cy, 34, COL_INK);
    else if (partial) {
        C2D_DrawRectSolid(cx - 3.5f, cy - 18, zn(), 7, 23, COL_INK);
        C2D_DrawRectSolid(cx - 3.5f, cy + 10, zn(), 7, 7, COL_INK);
    } else icon_check(cx, cy, 40, COL_INK);

    const char* title = error ? T("Algo salió mal", "Something went wrong")
                      : j->cancelled ? T("Descarga cancelada", "Download cancelled")
                                     : T("Listo", "Done");
    text_c(cx, 138, 0.8f, COL_TEXT, title);

    if (j->error == JERR_LIST) {
        text_c(cx, 172, 0.5f, COL_DIM, T("No se pudo leer la lista del PC.", "Couldn't read the list from the PC."));
        text_c(cx, 192, 0.45f, COL_DIM, T("Revisa que el PC y la consola estén en la misma red.", "Make sure the PC and console share the same network."));
    } else if (j->error == JERR_EMPTY) {
        text_c(cx, 172, 0.5f, COL_DIM, T("El QR no trae canciones .mp3 ni .aac.", "The QR has no .mp3 or .aac songs."));
    } else {
        snprintf(buf, sizeof(buf), T("%d nuevas   -   %d ya estaban   -   %d con error", "%d new   -   %d already there   -   %d failed"),
                 j->ok, j->skipped, j->failed);
        text_c(cx, 174, 0.5f, COL_TEXT, buf);
        if (j->ok + j->skipped > 0)
            text_c(cx, 198, 0.45f, COL_DIM, T("Escúchalas en Sound3DS > Local", "Play them in Sound3DS > Local"));
    }
}

// ---------------------------------------------------------------------------
// Pantalla de abajo
// ---------------------------------------------------------------------------

typedef struct { float x, y, w, h; } Rect;
#define FOOT_Y 206
static const Rect R_SCAN_START = { 196, FOOT_Y, 112, 26 };
static const Rect R_WORK_B     = { 196, FOOT_Y, 112, 26 };
static const Rect R_DONE_A     = { 12, FOOT_Y, 168, 26 };
static const Rect R_DONE_START = { 196, FOOT_Y, 112, 26 };

static bool in_rect(const Rect* r, int x, int y) {
    return x >= r->x - 3 && x <= r->x + r->w + 3 && y >= r->y - 4 && y <= r->y + r->h + 4;
}

UiButton ui_button_at(Screen scr, int x, int y) {
    switch (scr) {
    case SCR_SCAN: return in_rect(&R_SCAN_START, x, y) ? BTN_START : BTN_NONE;
    case SCR_WORK: return in_rect(&R_WORK_B, x, y) ? BTN_B : BTN_NONE;
    case SCR_DONE:
        if (in_rect(&R_DONE_A, x, y)) return BTN_A;
        if (in_rect(&R_DONE_START, x, y)) return BTN_START;
        return BTN_NONE;
    }
    return BTN_NONE;
}

static void button(const Rect* r, UiButton id, const char* label, bool pressed) {
    rrect(r->x, r->y, r->w, r->h, 9, pressed ? COL_PANEL_HI : COL_PANEL);
    float gx = r->x + 15, gy = r->y + r->h / 2;
    if (id == BTN_START) {
        rrect(r->x + 6, gy - 8, 38, 16, 8, COL_DIM);
        text_c(r->x + 25, gy - 6, 0.36f, COL_INK, "START");
        gx = r->x + 50;
    } else {
        circle(gx, gy, 9, id == BTN_A ? COL_ACCENT : COL_DIM);
        text_c(gx, gy - 7.5f, 0.48f, COL_INK, id == BTN_A ? "A" : "B");
        gx += 15;
    }
    text(gx, gy - 8, 0.5f, COL_TEXT, label);
}

static void draw_header(const UiModel* m, float progress) {
    C2D_DrawRectSolid(0, 0, zn(), BOT_W, 34, COL_HEADER);
    icon_note(17, 25, 18, COL_ACCENT);
    text(32, 7, 0.62f, COL_TEXT, "Sound3DS QR");
    // Estado del Wi-Fi
    bool ok = m->net_ok && m->wifi_ok;
    rrect(244, 8, 66, 18, 9, C2D_Color32(0, 0, 0, 60));
    circle(256, 17, 4, ok ? COL_OK : COL_ERR);
    text(264, 9, 0.45f, ok ? COL_TEXT : COL_DIM, "Wi-Fi");
    // Avance general bajo el encabezado
    C2D_DrawRectSolid(0, 34, zn(), BOT_W, 3, C2D_Color32(0, 0, 0, 80));
    if (progress > 0) C2D_DrawRectSolid(0, 34, zn(), BOT_W * progress, 3, COL_ACCENT);
}

static void step(float y, int n, const char* title, const char* sub) {
    char num[4];
    snprintf(num, sizeof(num), "%d", n);
    circle(28, y + 12, 11, COL_ACCENT);
    text_c(28, y + 4, 0.5f, COL_INK, num);
    text(48, y - 1, 0.52f, COL_TEXT, title);
    text(48, y + 16, 0.42f, COL_DIM, sub);
}

static void draw_bottom_scan(const UiModel* m) {
    draw_header(m, 0);
    text(14, 44, 0.6f, COL_TEXT, T("Pasa canciones desde el PC", "Send songs from your PC"));
    rrect(8, 70, 304, 122, 10, COL_PANEL);
    step(80, 1, T("En el PC abre Sound3DS Share", "On the PC open Sound3DS Share"),
         T("elige canciones y toca Generar QR", "pick songs and click Generate QR"));
    step(118, 2, T("Apunta la cámara al QR", "Point the camera at the QR"),
         T("la cámara de afuera, a unos 20 cm", "outer camera, about 20 cm away"));
    step(156, 3, T("Se guardan en sdmc:/music", "Saved to sdmc:/music"),
         T("y aparecen en Sound3DS > Local", "and show up in Sound3DS > Local"));

    if (m->cam_failed || !m->net_ok || !m->wifi_ok) {
        const char* msg = m->cam_failed ? T("No se pudo abrir la cámara", "Couldn't open the camera")
                                        : T("Sin Wi-Fi: actívalo para descargar", "No Wi-Fi: turn it on to download");
        u32 col = m->cam_failed ? COL_ERR : COL_WARN;
        circle(20, FOOT_Y + 13, 6, col);
        text_fit(32, FOOT_Y + 5, 0.44f, col, msg, 160);
    }
    button(&R_SCAN_START, BTN_START, T("Salir", "Exit"), m->pressed == BTN_START);
}

static void item_icon(float cx, float cy, u8 st) {
    switch (st) {
    case IT_ACTIVE: spinner(cx, cy, 6, 2.5f, COL_CYAN); break;
    case IT_OK: icon_check(cx, cy, 13, COL_OK); break;
    case IT_EXISTS: icon_check(cx, cy, 13, COL_DIM); break;
    case IT_FAIL: icon_cross(cx, cy, 13, COL_ERR); break;
    case IT_CANCEL: C2D_DrawRectSolid(cx - 5, cy - 1.5f, zn(), 10, 3, COL_WARN); break;
    default: circle(cx, cy, 2.5f, COL_DIM); break;
    }
}

static void draw_list(const UiModel* m, int first) {
    const float y0 = 44, rh = 22;
    for (int r = 0; r < UI_LIST_ROWS; r++) {
        int i = first + r;
        if (i >= m->item_count) break;
        const JobItem* it = &m->items[i];
        float y = y0 + r * rh;
        if (it->state == IT_ACTIVE) rrect(6, y, 308, rh - 2, 7, COL_PANEL_HI);
        else if (r % 2 == 0) rrect(6, y, 308, rh - 2, 7, alpha(COL_PANEL, 0.6f));
        item_icon(20, y + rh / 2 - 1, it->state);
        u32 col = it->state == IT_PENDING ? COL_DIM : COL_TEXT;
        text_fit(34, y + 3, 0.48f, col, it->name, 220);
        const char* tag = NULL;
        if (it->state == IT_EXISTS) tag = T("ya estaba", "already there");
        else if (it->state == IT_FAIL) tag = T("error", "failed");
        if (tag) text(306 - text_width(tag, 0.4f), y + 4.5f, 0.4f, it->state == IT_FAIL ? COL_ERR : COL_DIM, tag);
    }
    // Barra de desplazamiento si no caben todas
    if (m->item_count > UI_LIST_ROWS) {
        float h = UI_LIST_ROWS * rh - 4;
        float bh = h * UI_LIST_ROWS / m->item_count;
        float by = y0 + (h - bh) * first / (m->item_count - UI_LIST_ROWS);
        C2D_DrawRectSolid(316, by, zn(), 3, bh, alpha(COL_DIM, 0.7f));
    }
}

static float overall(const JobStatus* j) {
    if (j->count == 0) return 0;
    int done = j->ok + j->skipped + j->failed;
    float cur = (j->total > 0 && done < j->count) ? (float)j->now / (float)j->total : 0;
    float f = (done + cur) / j->count;
    return f > 1 ? 1 : f;
}

static void draw_bottom_work(const UiModel* m) {
    const JobStatus* j = &m->job;
    draw_header(m, overall(j));
    if (j->phase == JOB_LIST || m->item_count == 0) {
        spinner(BOT_W / 2, 110, 16, 4, COL_ACCENT);
        text_c(BOT_W / 2, 140, 0.5f, COL_DIM, T("Buscando canciones...", "Looking for songs..."));
    } else {
        int first = j->current - 3;
        if (first > m->item_count - UI_LIST_ROWS) first = m->item_count - UI_LIST_ROWS;
        if (first < 0) first = 0;
        draw_list(m, first);
    }
    text(14, FOOT_Y + 6, 0.44f, COL_DIM, T("HOME funciona mientras baja", "HOME works while downloading"));
    button(&R_WORK_B, BTN_B, m->cancelling ? T("Cancelando", "Cancelling") : T("Cancelar", "Cancel"), m->pressed == BTN_B);
}

static void draw_bottom_done(const UiModel* m) {
    draw_header(m, m->job.count ? 1.0f : 0.0f);
    if (m->item_count > 0) {
        draw_list(m, m->scroll);
    } else {
        rrect(8, 60, 304, 120, 10, COL_PANEL);
        text_c(BOT_W / 2, 78, 0.52f, COL_TEXT, T("Qué revisar:", "What to check:"));
        text_c(BOT_W / 2, 104, 0.45f, COL_DIM, T("Sound3DS Share sigue abierto en el PC", "Sound3DS Share is still open on the PC"));
        text_c(BOT_W / 2, 124, 0.45f, COL_DIM, T("PC y consola en la misma red Wi-Fi", "PC and console on the same Wi-Fi"));
        text_c(BOT_W / 2, 144, 0.45f, COL_DIM, T("El firewall del PC deja pasar el puerto", "The PC firewall allows the port"));
    }
    button(&R_DONE_A, BTN_A, T("Escanear otro", "Scan another"), m->pressed == BTN_A);
    button(&R_DONE_START, BTN_START, T("Salir", "Exit"), m->pressed == BTN_START);
}

void ui_draw(const UiModel* m) {
    s.en = m->en;

    C2D_TargetClear(s.top, COL_BG);
    C2D_SceneBegin(s.top);
    s.z = 0.05f;
    if (m->screen == SCR_SCAN) draw_top_scan(m);
    else if (m->screen == SCR_WORK) draw_top_work(m);
    else draw_top_done(m);

    C2D_TargetClear(s.bot, COL_BG);
    C2D_SceneBegin(s.bot);
    s.z = 0.05f;
    if (m->screen == SCR_SCAN) draw_bottom_scan(m);
    else if (m->screen == SCR_WORK) draw_bottom_work(m);
    else draw_bottom_done(m);
}
