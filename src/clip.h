#ifndef DFM_CLIP_H
#define DFM_CLIP_H
/* UTF-8 absolute paths, malloc-owned reads. Uses CF_HDROP and Explorer's
   Preferred DropEffect. DWORD-style return: 0 success, Win32 error otherwise.
   Call on the UI thread with its HWND as owner (NULL is allowed for tests).
   A read does not clear a cut clipboard; clear/refresh it after a successful move. */
#include <stddef.h>
#include <windows.h>
typedef struct clip_files { char **paths; size_t count; int cut; } clip_files;
/* OpenClipboard that retries while another program holds it; TRUE when open. */
BOOL clip_open(HWND owner);
/* Test mode only: the base name of two events (<base>-blocked, <base>-released) that a check creates to hold the
   first refused OpenClipboard until its other program has written and closed the clipboard (5 s at most). */
void clip_test_handshake(const char *base);
DWORD clip_write(HWND owner, const char *const *paths, size_t count, int cut);
DWORD clip_read(HWND owner, clip_files *files);
void clip_free(clip_files *files);
#endif
