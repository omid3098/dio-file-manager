#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Watches the open folder (not its subfolders) for changes made by other programs. A worker thread holds one
 * ReadDirectoryChangesW and settles a burst of changes (about 200 ms after the last one, at most every 500 ms during a
 * long burst); it then sets a flag and calls `notify` (from the worker: it must only post a message). With nothing
 * changing in the folder the worker sleeps in a wait with no timeout: no timer, no CPU. The window asks `watch_take`
 * and relists quietly. */
void watch_init(void *hwnd, void (*notify)(void));
void watch_open(const char *dir);        /* watch this folder instead of the one watched now (or none); returns at once */
void watch_close(bool wait);             /* stop watching; wait: return only after the folder handle is closed (ejecting) */
bool watch_take(void);                   /* true once for each settled change since the last call */
bool watch_is_open(void);                /* a watch for the folder exists (it may still be opening) */
/* WM_DEVICECHANGE: the folder's handle is closed when Windows asks to remove its device; returns true when it was ours. */
bool watch_device_event(uintptr_t wparam, intptr_t lparam);
unsigned watch_test_events(void);        /* checks: settled changes so far */
unsigned long long watch_test_t0(void);  /* checks: tick of the first change of the latest burst */
