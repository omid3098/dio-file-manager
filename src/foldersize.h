/* Folder sizes: the total logical size of a folder and everything below it, measured on worker threads.
 * No CUI calls; UTF-8 paths. The window asks (fsz_view, fsz_want), reads (fsz_get) and is woken by the notify callback.
 * Every directory is its own task for 4 workers; each finished folder keeps its own files' size and its total in an
 * in-memory cache of at most FSZ_CACHE_MAX entries and FSZ_CACHE_BYTES of keys and records. One ReadDirectoryChangesW
 * per drive keeps it correct: a change re-reads only the changed folder and moves the difference up to its cached
 * ancestors; a full re-walk happens only for data that is missing, and not sooner than max(1 s, 3 x its last walk). */
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
bool fsz_stale(const char *path);              /* a change in it is waiting to be read, or it waits for a walk */
void fsz_test_delay(unsigned ms_per_folder);   /* checks only: slow each folder down */
void fsz_test_cap(unsigned max_entries);       /* checks only: a smaller cache (0: the normal cap) */
unsigned fsz_test_reads(void);                 /* checks only: directory reads so far */
unsigned fsz_test_peak(void);                  /* checks only: the most directory reads that ran at the same time since the last call */
#endif
