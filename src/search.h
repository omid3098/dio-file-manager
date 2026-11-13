#ifndef DFM_SEARCH_H
#define DFM_SEARCH_H
/* Recursive UTF-8 name search. CompareStringEx uses invariant linguistic,
   case-insensitive matching for Unicode. Never follows reparse points.
   Worker queues results; drain transfers owned paths, so results stream without
   executing UI code on the worker. Free each path, then the array with free().
   Free cancels and joins. Hidden directories are pruned unless show_hidden.
   errors counts inaccessible paths; last_error retains the last Win32 error. */
#include <stddef.h>
typedef struct search_job search_job;
typedef struct search_state { int finished, cancelled; unsigned errors, last_error; } search_state;
search_job *search_start(const char *folder, const char *query, int show_hidden);
char **search_drain(search_job *job, size_t *count, search_state *state);
void search_cancel(search_job *job);
void search_free(search_job *job);
#endif
