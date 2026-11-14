#ifndef DFM_RESIDENT_H
#define DFM_RESIDENT_H
/* Win+E for the default file manager. Windows' shell handles Win+E itself and never runs the registered verb, so a
 * small copy of this exe keeps running with no window and takes the hotkey. See docs/design.md, "Win+E". */
#include <windows.h>
#include <stdbool.h>

/* main(): true when this start is the resident process; it has run to its end and *rc is the exit code. */
bool resident_main(int *rc);

/* Make Win+E ready: Explorer's DisabledHotkeys gains E (the old value is saved once, never replaced) and the Run
 * entry starts the resident process at sign-in. Safe to call again. */
bool resident_keys_set(const wchar_t *launcher, const wchar_t *extra);
/* Put DisabledHotkeys back as it was and remove the Run entry. False when the old value could not be written. */
bool resident_keys_restore(void);

bool resident_running(void);
void resident_start(const wchar_t *launcher, const wchar_t *extra);   /* does nothing while one runs */
void resident_stop(void);                                            /* returns when the process and its file are free */

#endif
