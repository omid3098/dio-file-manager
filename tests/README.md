# Operations checks

## Merge queue

`queue_check.py typecheck` builds the candidate in MinSizeRel, including the
800 KB post-build limit. `changed-safe` builds and runs `input_smoke.py`,
`search_smoke.py` and `actions_smoke.py`;
`full-safe` also runs all CTest console checks on the hidden desktop with clipboard
preservation and the shared `Local\DioFileManagerClipboardTest` mutex.
All use checks pass `--test-state`, so default file opens only write `.opened`
instead of starting a real app. The action checks use
explicit stand-ins for file opens, Open With and Terminal; their hidden desktop
job closes the complete process tree after the run, including on failure.
The read-only CUI v1.4.0 tree in the main checkout's `.agent-work/cui-src` is
required; the queue never downloads it. Build products and use-check artifacts
use a unique folder under the main checkout's `.agent-work/merge-queue-checks/`.
Each helper run removes its own folder on success or failure and copies the
hidden checks' output into the queue log before cleanup.
Run the helper's focused checks with
`python -m unittest discover -s tests -p test_queue_check.py`.

The console test uses only files it creates under
`E:\w\dio-file-manager\.agent-work\console\ops\verification` by default.
It refuses to reuse an existing verification folder. The sandbox tests remove
their files on exit, including failure. A cross-volume test additionally creates
and removes a `dio-file-manager-test-<pid>` folder on a second fixed drive.
Clipboard tests snapshot the prior clipboard formats and restore them afterward.
The console checks do not open shell windows. The real-input action checks below
exercise the shell entries on a private desktop. Empty Trash is never run by tests.
The jobs input suite uses an isolated Recent source.

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
Both check gaming and use the private desktop. The input suite has 20 checks; the search
suite has 8, including list-row menus and search Trash/Undo. Clipboard checks preserve all
formats in memory; when wrapping console tests, preserve the clipboard around the whole
process as well so a failed assertion cannot leave test data behind.
Set `TEMP` and `TMP` below the task folder before each runner call.

Each standalone smoke script defaults to `<repo root>/.agent-work/use-checks/<script stem>`
(for example, `use-checks/input_smoke`). `run.py` uses `use-checks/run`.
`DFM_USE_WORK` overrides that folder, including the standalone console sandbox.

## Action checks

`tests/use/actions_smoke.py` imports the input suite's helpers and has 7 checks:
Open With, Open in Terminal, Properties by menu and Alt+Enter, toast Undo,
Replace/Keep Both/Skip, Shift+Delete cancel/confirm, and large-copy progress/cancel.
It uses only its sandbox and preserves the clipboard during copy/paste checks.
Set `DFM_EXE` to the MinSizeRel exe and `DFM_USE_WORK` below the task folder, then
run it through `waitfor.py` with a 22-minute cap. Pillow is used to check that
the painted header arc grows while real CopyFileEx callbacks report progress.

Properties is the real Windows sheet: the check reads its filename and Location,
checks its thread's desktop, then closes it. Windows Terminal can reuse an
instance on the interactive desktop, and Open With can invoke a shell broker.
These checks therefore substitute a native stand-in window using test-only
`DFM_TEST_TERMINAL` and `DFM_TEST_OPEN_WITH` command prefixes. Its recorded command
line, file arguments, working directory, and window desktop are checked; the
stock chooser and actual terminal GUI are deliberately not launched. `DFM_TEST_OPEN`
catches unintended default-handler launches, including an Alt+Enter regression.
These overrides are confined to the check process and its children.

The progress check copies roughly 256 MB of real data. `DFM_TEST_COPY_DELAY_MS=50`
slows each copy callback in that check's app, so a fast disk cannot finish before
the ring and Cancel can be clicked. The normal default is zero; values above
500 ms or too long for the setting buffer are ignored. `jobs_smoke.py` uses 300 ms so that a
128 MiB copy lasts about 19 s and still runs when the machine is busy. JSON/PNG proof and native
window records remain in `DFM_USE_WORK`.

All clipboard-mutating use checks import `preserved_clipboard` from
`input_smoke.py`. It takes the machine-shared Windows named mutex
`Local\DioFileManagerClipboardTest` with CreateMutexW/WaitForSingleObject,
waiting up to 15 minutes, before saving any formats. It releases the mutex only
after restoring them. Use a 22-minute runner cap when other workers may be using
the clipboard. The clipboard roundtrip check also asserts that another thread
cannot acquire the mutex during preservation, including nested guards.

Recycle Bin guard: every suite imports `tests/use/recycle_guard.py` through `input_smoke.py`. At exit,
also after a failure, it finds Recycle Bin items whose original path is under the check's work folder
(`DFM_USE_WORK`) and whose deletion time is after the suite started, and removes their `$I` and `$R`
files. Items from anywhere else are never touched. `recycle_guard_test.py` proves that decoys survive and
a real round trip is removed. To clean up after old runs by hand:
`python tests/use/recycle_guard.py <work folder> --since <seconds>`.

The queue uses the safe mode names so baseline probes refuse old helpers
that could run brokered file-opening checks.

State capture invalidates the window and posts a wakeup, rather than synchronously
waiting on a UI thread that may be encoding a PNG. Double-click navigation uses one
coordinate lookup and two clicks, with no screenshot between the clicks.

## Concurrent jobs, Recent and eject

`tests/use/jobs_smoke.py` imports the real-input driver from `input_smoke.py` and
runs five checks on the private desktop: two 128 MiB copies making progress together
with both popover rows visible; cancelling one while the other continues; same-name
concurrent copies resolved with Keep Both and both payloads verified; removing a
sandbox Recent entry (including duplicate shortcuts) while retaining its file and an
unrelated entry; and removable-only eject controls with the right root sent to the
stand-in and its row removed. Clipboard formats are saved and restored in each check.
No real Recent shortcut or physical device is changed. Real safe removal still needs
a USB stick or SD card plugged in.

Use `DFM_EXE` for the MinSizeRel executable and `DFM_USE_WORK` for artifacts below
the current task folder; set `TEMP` and `TMP` to that task's `tmp` folder. The same run
must also pass `input_smoke.py` and `search_smoke.py`, each using its own work folder.

All clipboard checks acquire the Windows named mutex
`Local\DioFileManagerClipboardTest` before saving clipboard formats and release it
only after restoration. `clipboard_guard.clipboard_test_lock` waits up to 15 minutes;
`clipboard_guard.preserved_clipboard` always takes it, including nested checks.
## View slider acceptance

`view_smoke.py` covers all six stops, folder opening, saved size, keyboard/wheel
shortcuts, List scrolling, special places and search, plus a strict 50 ms message
limit for each stop over 20,000 files. The selected Details row remains clickable beneath the geometry-only rename
anchor. The input driver keeps `--test-state` enabled,
so accidental file opens only write the `.opened` log unless a stand-in is selected.

CPU acceptance compares identical 64 px grids on the hidden desktop. Use the shared
`cpu_budget.py` process-tree meter for five 60-second idle windows and five 30-second
scroll windows (20 wheel messages per second) on both main and the branch. The idle
ceiling is 0.5% of one core; every branch scroll window must stay at or below
5.0% of one core in all five samples. Alternate main and branch windows. Do not run builds or use checks during these samples.
The final branch restores normal display pacing. Neither the 120 fps no-vsync
mode, normal display pacing, nor an experimental 30 fps clock cap passed every
scrolling CPU sample. The 30 fps experiment also lost slider drag endpoints and
was reverted. See `docs/view-slider-validation.md` for the incomplete acceptance.

## Default file manager checks

`use/default_fm_smoke.py` runs with the registry root redirected to `HKCU\Software\DioFmTest\<id>`
(`DFM_TEST_REGROOT`, set for every child) on the hidden desktop and fails if the real keys change.
It checks on/off with exact restore of values of several types, clean keys, every command-line form,
the loop guard, the app exe gone, the exe moved, the real click in the main menu, a key that cannot be restored
(denied write: record and launcher stay, turning off fails, a retry restores), a saved value that cannot be read
(denied read: turning off fails and keeps everything), Win+E that cannot be written back while Folder can (the
switch stays on, a turn-on does not replace the record, the retry restores), another program's class that survives
turning off, and long Unicode folder and file names. 12 checks.

`use/default_fm_real.py` is not a `*_smoke.py`: run it by hand. It runs `gaming.py check`, backs up the three real
key trees, turns the switch on with `--test-state` (so opened places are only logged), calls `ShellExecuteEx`
`open` on a folder and on `C:\` and `opennewwindow` on Win+E's target from the hidden desktop, and in a `finally`
block turns the switch off and compares the keys byte for byte (it imports the backup and retries on a
difference). The keys stay registered for about 5 seconds, at most 10. It never starts the real Explorer:
before each call it reads the class's command and does not call when it is not the launcher.

The app itself retries `OpenClipboard` for up to 300 ms (`clip_open` in `src/clip.c`),
because clipboard history, sync tools and other programs open the clipboard for a few
milliseconds after every change and Windows refuses a second opener. The mutex above only
orders the checks; it cannot stop those outside programs. Before this retry, check
`5-name-conflicts` failed ("No name conflict dialog") whenever such a program was busy: a loop
that opens the clipboard for 20 ms every 25 ms failed it 3 of 3; with the retry it passed 6 of 6.

Text fields (the path, rename and search fields) copy and paste through the same retry: `ui_text_clipboard`
in `src/ui_ctxmenu.c` replaces CUI's own clipboard hooks, which try once. When a move ends, the app empties the
clipboard only if its sequence number is still the one the cut had, checked after the clipboard is open (a program
that held it meanwhile may have written new data). `actions_smoke.py` checks 8 (a move ends while another program
holds the clipboard and then writes text: the text stays; the order is a handshake, not timing: in test mode the app's first refused `OpenClipboard` sets the named event `<DFM_TEST_CLIP_HANDSHAKE>-blocked` and waits up to 5 s for `-released`, which the other program sets after it wrote and closed the clipboard, and only then do the 300 ms count; both events must exist, so a release build and other checks are unaffected) and 9 (Ctrl+V in the path field while another program
still holds the clipboard) fail on the code before these changes.
