#ifndef DFM_TRASH_H
#define DFM_TRASH_H
/* UTF-8 paths. Blocking shell work: call on a worker, never during a UI frame.
   Functions initialize COM themselves; they accept an existing STA or MTA.
   Return HRESULT. List allocations are freed with trash_list_free.
   IDs are shell parsing names, valid until that bin item is changed.
   Restore refuses an occupied original path. Undo restores only the most recent
   trash_move batch, including partial successes; it never restores older trash.
   Empty deletes ALL Recycle Bin contents: caller must obtain confirmation. */
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
typedef struct trash_item {
    char *id, *name, *original_path;
    uint64_t size;
    FILETIME deleted;
} trash_item;
typedef struct trash_items { trash_item *items; size_t count; } trash_items;
HRESULT trash_move(const char *const *paths, size_t count);
HRESULT trash_list(trash_items *list);
void trash_list_free(trash_items *list);
HRESULT trash_restore(const char *const *ids, size_t count);
HRESULT trash_undo(void);
HRESULT trash_empty(void);
#endif
