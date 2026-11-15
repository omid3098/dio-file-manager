/* Reads where a Windows shortcut (.lnk) points, and whether that place exists, on a worker thread: a target on an
 * offline network path can take many seconds to answer, and the window must never wait for it. The shortcut file is
 * only read; nothing is resolved or written back. A job is owned by the window thread that started it: poll it with
 * shortcut_done, read its result, then shortcut_release it (the worker may still run; the last one to let go frees it). */
#ifndef DFM_SHORTCUT_H
#define DFM_SHORTCUT_H
#include <stdbool.h>
#include <windows.h>

typedef struct shortcut_job shortcut_job;

/* Read the target path stored in the shortcut `lnk` (UTF-8). NULL when no worker could start. */
shortcut_job *shortcut_read_start(const char *lnk);
/* Ask whether `target` (UTF-8) exists, and whether it is a folder. */
shortcut_job *shortcut_check_start(const char *target);

/* Wait up to `ms` milliseconds; true once the worker has finished. ms 0 only polls. */
bool shortcut_wait(shortcut_job *j, DWORD ms);
bool shortcut_done(shortcut_job *j);

/* Results, valid once done. The target is "" for a shortcut with no file path (a Store app, a Control Panel item). */
const char *shortcut_input(const shortcut_job *j);    /* what the job was started with */
const char *shortcut_target(const shortcut_job *j);   /* read job: the stored path, without a trailing backslash unless a drive root */
bool shortcut_exists(const shortcut_job *j);          /* check job */
bool shortcut_is_dir(const shortcut_job *j);          /* check job */

void shortcut_release(shortcut_job *j);                /* NULL is fine */

/* The folder to open for a target and the name to select in it. A drive root or a share root has no parent folder:
 * the root itself opens and `leaf` is empty. */
void shortcut_place(const char *target, char *folder, size_t fcap, char *leaf, size_t lcap);
#endif
