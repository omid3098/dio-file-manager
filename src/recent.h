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
void recent_free(recent_items *list);
#endif
