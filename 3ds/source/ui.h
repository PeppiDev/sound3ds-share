#ifndef UI_H
#define UI_H

// Interfaz de Sound3DS QR dibujada con Citro2D: vista de la cámara con visor
// arriba, y abajo los pasos, la lista de descargas y el resumen.

#include <3ds.h>
#include <stdbool.h>
#include "job.h"

typedef enum { SCR_SCAN, SCR_WORK, SCR_DONE } Screen;
typedef enum { BTN_NONE, BTN_A, BTN_B, BTN_START } UiButton;

typedef struct {
    Screen screen;
    bool en;              // textos en inglés
    bool has_frame;       // ya llegó imagen de la cámara
    bool cam_failed;
    bool net_ok, wifi_ok;
    float found_t;        // segundos desde que se reconoció el QR (<0: aún no)
    bool cancelling;
    JobStatus job;
    const JobItem* items;
    int item_count;
    int scroll;           // primera fila visible en el resumen
    UiButton pressed;     // botón táctil presionado
} UiModel;

#define UI_LIST_ROWS 7

bool ui_init(void);
void ui_exit(void);

void ui_begin(void);
// Sube el cuadro de la cámara (400x240 RGB565) a la textura; llamar entre ui_begin y ui_draw
void ui_upload_camera(const u16* frame);
void ui_draw(const UiModel* m);
void ui_end(void);

UiButton ui_button_at(Screen s, int x, int y);

#endif
