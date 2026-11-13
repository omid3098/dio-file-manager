# Operations checks

The console test uses only files it creates under
`E:\w\dio-file-manager\.agent-work\console\ops\verification` by default.
It refuses to reuse an existing verification folder. The sandbox tests remove
their files on exit, including failure. A cross-volume test additionally creates
and removes a `dio-file-manager-test-<pid>` folder on a second fixed drive.
Clipboard tests snapshot the prior clipboard formats and restore them afterward.
No test opens a terminal, an app, Properties, or Open With. Recent files and those
shell actions are compile-checked only. Empty Trash is never run by tests.

The main build adds `dfm-ops-tests` when this directory exists. Run that executable
hidden with a bounded runner. `run.py` builds the same test directly with MSVC
2022 x64 and `/W4 /WX`, then runs it with `CREATE_NO_WINDOW`. It sets `TEMP` and
`TMP` under the task folder, and writes build products there. From the repository:

```
python C:\Users\Omid\.agents\tools\waitfor.py --cap 2 --log .agent-work\ops-v1\checks.log -- python tests\run.py
```

## Connecting the window

Read each header for ownership and threading. Allocate `ops_state` on the heap,
start a job, and poll it. Show a dialog only while `waiting_conflict` is true;
submit one `ops_resolve` answer. Call `ops_free` after completion. Cancelling a
job interrupts copying and wakes a pending conflict. Trash cancellation takes
effect between shell items. `trash_undo` targets the last trash batch made by
this process, including successful items from a partially failed batch.

Drain search results each frame and free every returned path and the array.
Folder replacement merges contents. A file/folder type mismatch reports an
error; it never erases a target tree. Reparse points are not copied or traversed.
Permanent deletion removes a reparse point itself, without following it.

Use a UI STA (COM initialized with `COINIT_APARTMENTTHREADED`) for shell actions.
Run blocking Recycle Bin and Recent functions on a worker. Recycle Bin operations
initialize their own COM apartment. Trash refuses non-fixed drives rather than
falling back to permanent deletion. Check HRESULTs and Win32 errors; the window
owns confirmations for permanent delete and Empty Trash.

Paths use UTF-8. Win32 calls receive UTF-16. Extended `\\?\` paths are accepted;
ordinary long paths depend on the application's Windows long-path manifest.
The complete 800 KB application budget must be checked by the main build once
the window and these modules are linked together.

## Release checks

Build both console tests with `cmake --build build --config MinSizeRel`.
Run `ctest --test-dir build -C MinSizeRel -V` through the bounded runner with hidden
console children. `test_ops.c` has 8 check groups; `test_thumbs.c` has 3.
The thumbnail test creates its own `thumbs` directory below the same console work folder.
Set that folder at configure time with `"-DDFM_TEST_WORK=E:/w/dio-file-manager/.agent-work/deliver-v1/console"`.
Neither console test reuses its leaf sandbox; both remove their files on exit.

Run `tests/use/input_smoke.py` and `tests/use/search_smoke.py` once against the release
executable, with `DFM_EXE` pointing at it and separate `DFM_USE_WORK` folders below the task.
Both check gaming and use the private desktop. The input suite has 18 checks; the search
suite has 8, including list-row menus and search Trash/Undo. Clipboard checks preserve all
formats in memory; when wrapping console tests, preserve the clipboard around the whole
process as well so a failed assertion cannot leave test data behind.
Set `TEMP` and `TMP` below the task folder before each runner call.
