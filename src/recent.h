#ifndef DFM_RECENT_H
#define DFM_RECENT_H
/* Blocking UTF-8 Windows Recent .lnk targets that still exist, newest first,
   deduplicated and capped at 200. Call on a worker. Returns HRESULT; free via
   recent_free. modified is the shortcut's last-write time, not target's time. */
#include <windows.h>
#include <stddef.h>
typedef struct recent_item { char *path; FILETIME modified; } recent_item;
typedef struct recent_items { recent_item *items; size_t count; } recent_items;
HRESULT recent_list(recent_items *list);
/* Deletes all matching shortcuts, never their targets. Blocking; call on a worker. */
HRESULT recent_remove(const char *const *paths, size_t count);
/* Called only with --test-state; redirects listing and removal to sandbox .lnk files. */
void recent_test_source(const char *folder);
void recent_free(recent_items *list);
#endif
