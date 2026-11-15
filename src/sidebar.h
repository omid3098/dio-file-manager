/* The sidebar's own settings: default rows the user removed, and folders the user added.
 * Kept in %APPDATA%\dio-file-manager\sidebar.txt (UTF-8, one entry per line: "hide <key>" or "add <path>").
 * UI thread only. A key names a default row: "recent", "starred", "place0".."place6", "trash", "drive:C". */
#ifndef DFM_SIDEBAR_H
#define DFM_SIDEBAR_H
#include <stdbool.h>
#include <stddef.h>

void sidebar_load(void);
bool sidebar_is_hidden(const char *key);
bool sidebar_any_hidden(void);
bool sidebar_hide(const char *key);          /* false: the file could not be saved (the row is hidden for this run anyway) */
bool sidebar_show_hidden(void);              /* every hidden default row comes back */
int  sidebar_count(void);                    /* added folders, in the order added */
const char *sidebar_path(int i);
bool sidebar_has(const char *path);
bool sidebar_add(const char *path);          /* false: full or not saved */
bool sidebar_remove(const char *path);
bool sidebar_gone(int i);                    /* the added folder (or its drive) is missing; looked at most once a second */
void sidebar_leaf(const char *path, char *out, size_t cap);   /* the row's label: the folder's name, or "C:\" for a drive root */

#endif
