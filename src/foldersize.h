/* Folder sizes: the total logical size of a folder and everything below it, measured on worker threads.
 * No CUI calls; UTF-8 paths. The window asks (fsz_view, fsz_want), reads (fsz_get) and is woken by the notify callback.
 * The cache lives in memory only, at most FSZ_CACHE_MAX entries and FSZ_CACHE_BYTES of keys and records; one
 * ReadDirectoryChangesW per drive keeps it correct (a change marks the folder and its parents stale). */
#ifndef DFM_FOLDERSIZE_H
#define DFM_FOLDERSIZE_H
#include <stdbool.h>
#include <stdint.h>

#define FSZ_CACHE_MAX 65536u
#define FSZ_CACHE_BYTES (24u << 20)

enum { FSZ_NONE = 0, FSZ_PENDING = 1, FSZ_DONE = 2 };

typedef void (*fsz_notify_fn)(void);          /* called from a helper thread, at most every 100 ms, when results landed */
void fsz_start(fsz_notify_fn notify);          /* once; the threads start with the first request */
void fsz_stop(void);                           /* cancels the work and ends the threads */
bool fsz_eligible(const char *path);           /* a path on a local fixed or removable drive */
void fsz_view(const char *parent);             /* the shown folder: another one cancels the old folder's queued and running work */
void fsz_want(const char *path);               /* one child folder of the shown folder; call in the order the view shows them */
int fsz_get(const char *path, uint64_t *size); /* FSZ_DONE with the size (maybe about to be recomputed), FSZ_PENDING, or FSZ_NONE */
int fsz_open(void);                            /* wanted folders of the current view whose work has not finished */
unsigned fsz_version(void);                    /* grows whenever a result lands */
bool fsz_stale(const char *path);              /* a change was seen below it and it is not recomputed yet */
void fsz_test_delay(unsigned ms_per_folder);   /* checks only: slow each folder down */
#endif
