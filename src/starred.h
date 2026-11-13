#ifndef DFM_STARRED_H
#define DFM_STARRED_H
/* The starred paths, kept in %APPDATA%\dio-file-manager\starred.txt (UTF-8, one path per line).
   Safe to call from the window and from listing workers. */
#include <stdbool.h>
#include <stddef.h>
void starred_load(void);                          /* read the file; call once at start */
bool starred_is(const char *path);
bool starred_set(const char *path, bool on);     /* saves at once; false when the file could not be written */
int starred_count(void);
bool starred_get(int i, char *out, size_t cap);
#endif
