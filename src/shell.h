#ifndef DFM_SHELL_H
#define DFM_SHELL_H
/* UTF-8 absolute paths. Return HRESULT. Call on the UI's COM-initialized STA.
   These actions may show Windows UI; Terminal hides its console launcher.
   owner is the application window, or NULL. copy_path writes CF_UNICODETEXT. */
#include <windows.h>
void shell_test_log(const char *path);
HRESULT shell_open(HWND owner, const char *path);
HRESULT shell_open_with(HWND owner, const char *path);
HRESULT shell_terminal(const char *folder);
HRESULT shell_properties(HWND owner, const char *path);
HRESULT shell_copy_path(HWND owner, const char *path);
#endif
