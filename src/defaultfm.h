#ifndef DFM_DEFAULTFM_H
#define DFM_DEFAULTFM_H
/* Use the app as Windows' default file manager, for the current user only (HKCU, no administrator).
 * defaultfm_enable saves every value it replaces; defaultfm_disable restores them exactly.
 * The registered command runs a launcher copy of this exe; see docs/architecture.md. */
#include <windows.h>
#include <stdbool.h>
#include "fs.h"

enum { REQ_HOME, REQ_FOLDER, REQ_PLACE, REQ_FALLBACK };
enum { DFM_REQ_PATH = 520 };

typedef struct dfm_request {
    int kind;                     /* REQ_* */
    int loc;                      /* LOC_* for REQ_PLACE */
    char path[FS_PATH_MAX];       /* the folder for REQ_FOLDER (UTF-8) */
    char select[FS_PATH_MAX];     /* the name to select in it, or empty */
} dfm_request;

/* Test hooks (only with --test-state): a redirected registry root, a launcher folder and an Explorer stand-in
 * come from DFM_TEST_REGROOT, DFM_TEST_LOCAL and DFM_TEST_EXPLORER. Call once at start, before any other call. */
void defaultfm_test_mode(const wchar_t *state_base);   /* NULL or empty: not a test */

bool defaultfm_enabled(void);
/* extra_args is put between the launcher and the folder in the registered commands (tests only), or NULL. */
bool defaultfm_enable(const wchar_t *extra_args);
bool defaultfm_disable(void);
void defaultfm_sync(void);        /* a normal start: follow an exe that moved, refresh the launcher copy */

/* Turn the command-line arguments that Explorer would get into a request. */
void defaultfm_request(wchar_t **args, int n, dfm_request *out);
/* In a check, write what arrived to the test log. */
void defaultfm_arrival(const dfm_request *r);
/* Open the arguments in the real Explorer. False when it was refused (a loop, or no way to start it). */
bool defaultfm_fallback(wchar_t **args, int n);
/* True when this exe was started under the launcher's name; runs the launcher and returns its exit code. */
bool defaultfm_is_launcher(void);
int defaultfm_launcher_main(void);

#endif
