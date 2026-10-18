#ifndef JOB_H
#define JOB_H

// Descarga en segundo plano de lo que trae un QR: la URL de una canción, varias
// URLs o la URL de una lista (texto, una URL por línea, como la que sirve
// Sound3DS Share en el PC). Corre en su propio hilo para que la interfaz siga
// respondiendo (HOME, cancelar, animaciones) mientras baja todo.

#include <3ds.h>
#include <stdbool.h>

#define JOB_MAX 200
#define MUSIC_DIR "sdmc:/music"

typedef enum { IT_PENDING, IT_ACTIVE, IT_OK, IT_EXISTS, IT_FAIL, IT_CANCEL } ItemState;
typedef enum { JOB_IDLE, JOB_LIST, JOB_DOWNLOAD, JOB_DONE } JobPhase;
typedef enum { JERR_NONE, JERR_LIST, JERR_EMPTY, JERR_SD } JobError;

typedef struct {
    char name[96];
    u8 state;          // ItemState
} JobItem;

typedef struct {
    JobPhase phase;
    int count;          // canciones encontradas
    int current;        // índice de la que se descarga
    s64 now, total;     // bytes de la canción actual (total 0 si no se conoce)
    u32 speed;          // bytes por segundo
    int ok, skipped, failed;
    bool cancelled;
    JobError error;
} JobStatus;

bool job_net_init(void);
void job_net_exit(void);

bool job_start(const char* qr_text);
void job_cancel(void);
// Espera al hilo y deja el trabajo en JOB_IDLE (llamar con el trabajo terminado o cancelado)
void job_reset(void);

void job_status(JobStatus* out);
// Copia la lista de canciones; devuelve cuántas hay
int job_items(JobItem* out, int max);

#endif
