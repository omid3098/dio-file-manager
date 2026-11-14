/* Folder sizes: the total logical size of a folder and everything below it, measured on worker threads.
 * No CUI calls; UTF-8 paths. The window asks (fsz_view, fsz_want), reads (fsz_get) and is woken by the notify callback.
 * Every directory is its own task for 8 workers; each finished folder keeps its total in an in-memory cache of at most
 * FSZ_CACHE_MAX entries and FSZ_CACHE_BYTES of keys and records. Entering a folder measures its child folders again (the
 * last known value stays readable meanwhile); there is no watcher and no update while a folder stays open. A child
 * whose measurement began after the folder was entered, or inside a walk that is still running, is not walked again. */
#ifndef DFM_FOLDERSIZE_H
#define DFM_FOLDERSIZE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FSZ_CACHE_MAX 65536u
#define FSZ_CACHE_BYTES (24u << 20)

enum { FSZ_NONE = 0, FSZ_PENDING = 1, FSZ_DONE = 2, FSZ_DENIED = 3 /* its listing cannot be read: no size, the Size cell stays empty */ };

typedef void (*fsz_notify_fn)(void);          /* called from a helper thread, at most every 100 ms, when results landed */
void fsz_start(fsz_notify_fn notify);          /* once; the threads start with the first request */
void fsz_stop(void);                           /* cancels the work and ends the threads */
bool fsz_eligible(const char *path);           /* a path on a local fixed drive (removable drives keep an empty Size cell) */
void fsz_view(const char *parent, bool entered); /* the shown folder: another one cancels the old folder's queued and running work; entered: the folder was opened or reloaded (not only listed again), so every child is measured again */
void fsz_want(const char *path);               /* one child folder of the shown folder; call in the order the view shows them */
int fsz_get(const char *path, uint64_t *size); /* FSZ_DONE with the size (maybe about to be recomputed), FSZ_PENDING, FSZ_DENIED or FSZ_NONE */
int fsz_open(void);                            /* wanted folders of the current view whose work has not finished */
unsigned fsz_version(void);                    /* grows whenever a result lands */
bool fsz_stale(const char *path);              /* the value is the last known one: it is being measured again */
bool fsz_test_path_fits(size_t path_len);      /* checks only: does the walk's buffer take \?\ + a path this long + 0? */
bool fsz_test_drive_ok(unsigned drive_type);   /* checks only: is a drive of this GetDriveType kind measured? */
void fsz_test_delay(unsigned ms_per_folder);   /* checks only: slow each folder down */
void fsz_test_cap(unsigned max_entries);       /* checks only: a smaller cache (0: the normal cap) */
unsigned fsz_test_reads(void);                 /* checks only: directory reads so far */
unsigned fsz_test_peak(void);                  /* checks only: the most directory reads that ran at the same time since the last call */
unsigned fsz_test_entries(unsigned long long *bytes); /* checks only: entries held, and the bytes of their keys and records */
#endif
