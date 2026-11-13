#ifndef DFM_VIEWS_H
#define DFM_VIEWS_H
/* The Recent, Starred and Trash listings, and Recycle Bin work that blocks (on a worker). */
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include "fs.h"
long views_fill(fs_list *l);     /* on the list worker: fills l for l->vkind, returns FS_DONE or FS_FAILED */

enum { TK_RESTORE = 1, TK_EMPTY, TK_DELETE };
bool views_task_start(int kind, char *const *ids, size_t n);   /* copies the ids; false when busy */
bool views_task_busy(void);
bool views_task_take(int *kind, long *hr);                      /* true once, when a task has finished */
#define VIEWS_TRASH_CHANGED (WM_APP + 1)
bool views_trash_refresh(HWND window);                         /* immediate query; worker also checks every 2.5 s */
long long views_trash_count(void);                             /* cached count only; never starts a query */
#endif
