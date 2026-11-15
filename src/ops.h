#ifndef DFM_OPS_H
#define DFM_OPS_H
/* UTF-8 paths. Each job owns its inputs and a worker thread. Poll from any
   thread; answer a pending conflict once. Free cancels and joins the worker.
   files_done counts completed nodes (including folders); totals include skipped
   nodes. Errors retain the last Win32 code (HRESULT for trash) and path.
   Processing continues after errors. Replacing folders merges their contents;
   replacing a file with a folder or vice versa is refused without deleting it.
   Copy does not traverse junctions/symlinks; it reports ERROR_NOT_SUPPORTED.
   Cancel leaves completed items in place, but removes an incomplete file copy.
   A trash job never deletes for good on its own: an item the Recycle Bin cannot take
   (path of 260+ characters, larger than the bin, bin off, non-fixed drive) is asked
   about with waiting_conflict and cannot_trash set; it stays where it is unless the
   answer is OPS_DELETE_NOW. Cancelling leaves it too.
   States contain large path buffers: allocate one on the heap, not a small stack. */
#include <stddef.h>
#include <stdint.h>
#define OPS_PATH_CAP 131072
typedef struct ops_job ops_job;
typedef enum ops_kind { OPS_COPY, OPS_MOVE, OPS_TRASH, OPS_DELETE, OPS_RENAME, OPS_NEW_FOLDER } ops_kind;
typedef enum ops_answer { OPS_REPLACE, OPS_SKIP, OPS_KEEP_BOTH, OPS_DELETE_NOW } ops_answer;
typedef struct ops_state {
    uint64_t files_done, files_total, bytes_done, bytes_total;
    int finished, cancelled, waiting_conflict;
    int cannot_trash;                 /* with waiting_conflict: conflict_source cannot go to Trash; answer OPS_DELETE_NOW or OPS_SKIP */
    unsigned trashed, deleted;        /* trash job: items now in the Recycle Bin / deleted for good after the user agreed */
    unsigned errors, error_code;
    char current_name[1024], error_path[OPS_PATH_CAP];
    char conflict_source[OPS_PATH_CAP], conflict_target[OPS_PATH_CAP];
} ops_state;
/* destination is a folder for copy/move/new-folder; name is a single component
   for rename/new-folder. Rename requires one source; new-folder requires none.
   Paths must be absolute. A NULL return sets GetLastError(). */
ops_job *ops_start(ops_kind kind, const char *const *sources, size_t count,
                   const char *destination, const char *name);
void ops_poll(ops_job *job, ops_state *state);
void ops_cancel(ops_job *job);
void ops_resolve(ops_job *job, ops_answer answer, int apply_to_all);
void ops_free(ops_job *job);
/* NULL means valid. Otherwise returns a static short English reason. */
const char *ops_name_error(const char *name);
#ifdef DFM_INTERNAL
#include <windows.h>
/* Private shared helpers; allocations use malloc/free. */
wchar_t *dfm_wide(const char *s);
char *dfm_utf8(const wchar_t *s);
wchar_t *dfm_wide_long(const char *s);   /* absolute, with the extended prefix (a job's path); NULL sets GetLastError() */
wchar_t *dfm_plain(const wchar_t *w);    /* the plain form of a path with the extended prefix; free it */
wchar_t *dfm_join(const wchar_t *a, const wchar_t *b);
const wchar_t *dfm_leaf(const wchar_t *s);
#endif
#endif
