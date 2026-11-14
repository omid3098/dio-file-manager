#ifndef DFM_DEFAULTFM_INT_H
#define DFM_DEFAULTFM_INT_H
/* What defaultfm.c shares with resident.c: the registry bases (redirected in checks), the launcher path and a few
 * registry helpers. Nothing outside the two files includes this. */
#include <windows.h>
#include <stdbool.h>

bool dfm_test_on(void);
const wchar_t *dfm_state_base(void);         /* HKCU\...\DefaultFileManager */
const wchar_t *dfm_adv_base(void);           /* HKCU\...\Explorer\Advanced */
const wchar_t *dfm_run_base(void);           /* HKCU\...\Run */
const wchar_t *dfm_test_resident_log(void);  /* in a check: the file the resident process logs to, else NULL */
void dfm_log_text(const wchar_t *file, const char *line);   /* append one line to a check's log, shared with its readers */
bool dfm_explorer_open(void);                /* Explorer's own window in Home, no loop guard */
bool dfm_launcher_path(wchar_t *out, size_t cap);
const wchar_t *dfm_command_tail(void);       /* the command line after the exe's own name */
bool dfm_get_string(HKEY root, const wchar_t *path, const wchar_t *name, wchar_t *out, DWORD cap);
bool dfm_set_string(HKEY k, const wchar_t *name, const wchar_t *v);
bool dfm_is_missing(LONG r);

#endif
