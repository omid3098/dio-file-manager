# Operations checks

## Merge queue

`queue_check.py typecheck` builds the candidate in MinSizeRel, including the
800 KB post-build limit. `changed-safe` builds and runs `input_smoke.py`,
`search_smoke.py` and `actions_smoke.py`;
`full-safe` also runs `persian_input_smoke.py` (Persian typed into the text boxes; the guard of the CUI fork, see `docs/architecture.md`) and all CTest console checks on the hidden desktop with clipboard
preservation and the shared `Local\DioFileManagerClipboardTest` mutex.
All use checks pass `--test-state`, so default file opens only write `.opened`
instead of starting a real app. The action checks use
explicit stand-ins for file opens, Open With and Terminal; their hidden desktop
job closes the complete process tree after the run, including on failure.
The read-only CUI tree for the commit in the candidate's `cmake/cui-pin.cmake` is required in the main checkout's
`.agent-work/cui-src-<first 7 characters of the commit>`; `tools/update_cui.py` makes it (the only way to move CUI,
see `docs/architecture.md`). A missing folder stops the check with its path, and the queue never downloads it.
`python -m unittest discover -s tests -p test_update_cui.py` tests that script offline (local repositories, no network). Build products and use-check artifacts
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
initialize their own COM apartment. A trash job never deletes for good by itself: an item the bin cannot take
(the item or any child path of 260+ characters, judged on the real path behind a junction or symlink and without
entering links; larger than the bin, bin switched off, non-fixed drive) is asked about
(`waiting_conflict` with `cannot_trash`; answer `OPS_DELETE_NOW` or `OPS_SKIP`, or cancel) and
stays where it is until the answer is Delete; `trash_move` itself refuses such an item. The shell is the last guard:
trash runs without `FOF_NOCONFIRMATION` and with `FOF_WANTNUKEWARNING`, so a case the check misses makes the shell
ask (it never deletes silently; the check closes any such prompt on its private desktop and fails). `state.trashed`
counts the items that really reached the bin, also when a later item failed or the job was cancelled, and the window
offers Undo for them. Check HRESULTs and Win32 errors; the window
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
Both check gaming and use the private desktop. The input suite has 31 checks, including the popup checks `9-new-folder*` and `9-rename*` (Enter, the Create or Rename button, Cancel, and refused names all close or keep the popover as they should), and including the mouse back and forward buttons (`14-mouse-navigation`) and Recent and Other Locations at the default 1080x720 light and 360x280 dark (`12-virtual-navigation*`: the sidebar row is found from the app's own row list, never at a fixed height, because the drive count changes the row's place); the search
suite has 8, including list-row menus and search Trash/Undo. Clipboard checks preserve all
formats in memory; when wrapping console tests, preserve the clipboard around the whole
process as well so a failed assertion cannot leave test data behind.
Set `TEMP` and `TMP` below the task folder before each runner call.

Each standalone smoke script defaults to `<repo root>/.agent-work/use-checks/<script stem>`
(for example, `use-checks/input_smoke`). `run.py` uses `use-checks/run`.
`DFM_USE_WORK` overrides that folder, including the standalone console sandbox.

## Action checks

`tests/use/actions_smoke.py` imports the input suite's helpers and has 13 checks (the first 7 are listed here; `11` and `12` prove that a toast and the selection label never cover each other, with a long Persian name and with 4 items, at 1280x800 light and 360x280 dark):
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
A stand-in that starts late (a busy computer; the wait for its window is 45 s, `DFM_STANDIN_WAIT`) belongs to
the check that started it: when that check has ended or given up, the stand-in exits at once without a window,
so it never holds the sandbox folder as its working folder. `DFM_STANDIN_SLOW=<seconds>` makes every stand-in
start that slowly, to rehearse this: only the checks that use a stand-in then fail, and the others still pass.
`fresh()` also retries for 10 s to empty the sandbox, because a program that just ended may still hold it.

Open in Terminal must never hold the window: a probe thread asks the window (`WM_NULL`) every 10 ms while the menu
item runs, and the longest wait must stay within 150 ms of the window's own longest wait while it only opens and closes
the menu on the same machine, the longest of 8 open-and-close rounds (`<check>-control.json`; `2-terminal`, and `2b-terminal-slow-loaded`, which adds
`DFM_TEST_TERMINAL_DELAY_MS=1500`, a slow process start as on a busy computer, and one busy process per core). The
numbers are written to `<check>-answer.json`. Before the process start moved to a worker, the slow case measured
about 1580 ms and failed with "Window stopped answering" (error 1460), the failure `2-terminal` showed on a busy machine.

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

## Selection box from an item's blank part

`tests/use/band_anywhere_smoke.py` (thirteen checks, about 5 minutes, real input on the private desktop, no clipboard) imports
`press`, `move` and `release` from `select_smoke.py`. It checks that a drag from the Size, Modified or Type cell or
from beside a Details name draws the box (5,200 files: row 3's Size cell to row 20 selects exactly 18 rows, in dark and light), that
a Details press on the name text or a tile's icon draws none, that auto-scroll past the bottom extends the box, that
List, Small, Medium and Large icons start from the padding between icons, Ctrl+drag adds, a click with no drag on
a blank part selects the item, a press in the padding above a Small or Medium icon cell (inside the cell, outside the drawn icon and name) draws the box, Persian names (a short one with a blank part, a long one that fills its column), and a
360x280 window in Details and icons, and a double click on a folder's blank part opening it. Pictures of the whole window go to `final/` of `DFM_USE_WORK`.

## Drag and drop

`tests/use/dnd_smoke.py` (twenty-two checks, about 2 minutes, real mouse messages on the private desktop, the clipboard untouched)
checks a drag out and a drop in. Out: a press that barely moves starts nothing; a drag of one file, a folder, a long Persian name,
a selection of two, 300 files and a Details row reports the data the drag would carry (every path, Preferred DropEffect copy). In: test-only
`WM_COPYDATA` commands (`enter`, `over`, `drop`, `leave`) hand a real `IDataObject` of sandbox files to the window's own
`IDropTarget` at client points and keys (checks 19 to 22: in Details the pointer moving from a folder into blank space drops into the open folder; a Shift or Ctrl the source does not allow is refused, with no key the one effect it offers is taken; a drop with a 590-character path hands both files to the job; a file moved out of the open folder by another process leaves the list): the background, a folder item, a sidebar drive row (a `subst` letter), Trash, a same-drive
move, a cross-drive copy, Shift and Ctrl, a folder onto itself and into its own subfolder (refused), the name conflict dialog, a drop
while a copy runs, edge scroll in icons and Details, a 360x280 window in dark and light, and leaving the window. Pictures of the whole window
(`drop-over-*.png`) go to `DFM_USE_WORK`. Needs a free drive letter. Not covered: a real drag between two programs (a browser upload,
Explorer), which needs a real pointer; the first real use checks it.

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
## Folder size checks

`test_foldersize.c` (CTest target `dfm-foldersize-tests`, console, 54 checks) builds a sandbox under the task folder and checks
the engine alone: exact sizes, nested totals, a junction not followed, a walk cancelled when the view moves on, one shown folder
walked by several threads at once (peak >= 3), ten shown folders surviving a cache cap of 4, and 10,000 lookups under 25 ms.
Entering a folder again: the same folder asked again at once reads no directory; a file changed while another folder is shown
gives the new size when the parent is entered again, the old size stays readable and marked as being measured meanwhile; a
folder entered while its walk still runs, and its children entered right after, cost exactly one read per directory (10 for
the test tree), and the same again after the walk ended reads none; no call of the window thread (`fsz_want` for 10,000 folders,
`fsz_get`, `fsz_open`) takes 20 ms or more while the workers are busy (about 0.3 to 1.4 ms measured); and writes while nothing is
measured read no directory (no watcher). Removed with the live updates (their feature is gone): the checks of a changed folder
updating in place, a moved folder, a deep change moving up through the ancestors with at most 3 reads, a new and a removed
subfolder, and missing data walked again after a second. Run it hidden through the bounded runner like the other console tests;
run the exe ten times for the timing checks.

`use/foldersize_budget.py <measure.json>` fails when a measurement of E:\w (the `w-folder` block that the task's `measure.py` writes) is over a budget of `docs/design.md`: all in within 30 s, idle CPU and CPU with a writer below 0.1 % of one core, working set at most 175 MB, window message delay below 50 ms, child sizes equal to an independent scan. The 4-worker run (40.3 s) fails it, the 8-worker run passes. Mutants of the engine (every measured folder counts as fresh, no waiting for a running walk, a walk that ends without a folder it was waited on for, no revert when a view is left, the old value dropped while measuring) each fail 1 to 5 console checks.

`use/foldersize_smoke.py` runs 8 checks on the private desktop, on a fixture it creates (`a` 3,000,000 bytes, `b` 1,000,000 plus
a nested 500,000, `c` 10,000, `d` empty, and a file), with `DFM_TEST_FSZ_DELAY_MS=1200` where the measuring state is read:
(1) every folder is "measuring" (state kind 2, no text) while `fsz_open` is above 0, then the exact texts `3.0 MB`, `1.5 MB`,
`10.0 kB`, `0 bytes` (the picture `final/measuring.png` and `final/sizes.png`); (2) a file is never among the sized folders and
a nested folder counts in its parent; (3) a click on the Size header sorts folders first and by size, and the one re-sort when
the sizes are in changes the order exactly once and keeps the selected folder selected with the cursor on it, then the other
direction; (4) the status pill reads `“a” selected (…)` and `2 items selected (…)` while measuring and `2 items selected (4.5 MB)`
and `“a” selected (3.0 MB)` after; (5) a folder that grows on disk keeps its old size while its parent stays open (1.5 s), and shows the new size within 12 s after the parent is entered again (a folder and back). Each of the five fails on the
build before the folder sizes (the state has no `sizes`): it was run against main's older MinSizeRel exe and failed 5 of 5.
(6) the folder entered again keeps its old size in the cell and the pill reads `“b” selected (…)` (not `(1.5 MB)`) until the new size is in; (7) a search is opened, a folder changes, Esc closes the search, and the folder shows its new size; (8) the same with F5 pressed while the search is open. Checks 6 and 7 fail with their fix removed. Check 8 fails only with both search fixes removed (the Esc fix alone already covers the F5 case).
The console test also covers the folder that cannot be listed (a deny entry on one subfolder: the subfolder is `FSZ_DENIED`, its parent still sums the rest, and it is measured again once it is readable), the longest path the walk buffer takes (`fsz_test_path_fits`: 32,763 wide characters; the check goes through the buffer arithmetic only, since Windows cannot create a folder that deep to read), and that only fixed drives are eligible (`fsz_test_drive_ok`, plus every removable drive of the machine, if there is one).
`DFM_EXE` and `DFM_USE_WORK` apply as for the other suites; `USE_ONLY=<prefix>` runs one check. The window checks do not
cover scroll position after the re-sort (the fixture fits on one screen).

## View slider acceptance

`view_smoke.py` covers all six modes on the continuous slider (a thumb within 0.1 of a stop gives that stop's
mode and, within a few pixels, its row height), folder opening, saved size (the value within 0.12), keyboard and
wheel shortcuts (each waits for the 150 ms glide to reach the whole value), List scrolling, special places and
search, plus a strict 50 ms message limit for each mode over 20,000 files. The selected Details row remains clickable beneath the geometry-only rename
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
turning off, and long Unicode folder and file names. Win+E (checks 16 to 24): the process starts on turn-on and
the Run entry and `DisabledHotkeys` are written and removed (an absent value, letters, an expand
type, and a value that already has E all come back exactly), a posted `WM_HOTKEY` opens a window, the process says
`1409` while another program holds the hotkey and takes it after `TaskbarCreated`, an install made before the
process existed gets it at the first start, an update while the process runs from the launcher (the launcher
becomes the new build, the process restarts from it, a turn-on from a newer build works too), the app gone (check 21: the process stays on and
opens the stand-in Explorer), and, after a turn-off while the process holds the hotkey (check 23), a posted
`WM_HOTKEY` starts the stand-in Explorer while the registry is already restored, a `TaskbarCreated` from the same
stand-in taskbar process changes nothing, ending that process ends the bridge and frees the hotkey with no
`TaskbarCreated` sent, and the sign-in cleanup (the RunOnce command run by the check) deletes the redirected launcher;
check 24 turns it on again while the bridge runs (the cleanup entry goes, a fresh process opens the app, a cleanup
that still fires leaves the launcher alone). The helper `off()` in the checks lets Explorer restart and the sign-in
happen after a turn-off, so the checks before 23 see the end state. The
budgets (working set at most 5 MB, idle CPU 0, a posted Win+E at most 50 ms slower than a plain launcher start; the
numbers are printed). In a check the process holds Win+Ctrl+Alt+Shift+F23, because Win+E belongs to the real
Explorer on every desktop of the session. The real `DisabledHotkeys` and Run value are read-only compared after
each check. `DFM_CPU_BUDGET=1` also runs `cpu_budget.py --max 0` on the process (2.5 minutes). `USE_ONLY` takes
comma-separated name prefixes. 24 checks.

`use/default_fm_real.py` is not a `*_smoke.py`: run it by hand. It runs `gaming.py check`, backs up the three real
key trees, turns the switch on with `--test-state` (so opened places are only logged), calls `ShellExecuteEx`
`open` on a folder and on `C:\` and `opennewwindow` on Win+E's target from the hidden desktop, and in a `finally`
block turns the switch off and compares the keys byte for byte (it imports the backup and retries on a
difference). The keys stay registered for about 5 seconds, at most 10. It never starts the real Explorer:
before each call it reads the class's command and does not call when it is not the launcher.

The app itself retries `OpenClipboard` for up to 1.5 s (`CLIP_WAIT_MS` in `src/clip.c`),
because clipboard history, sync tools and other programs open the clipboard after every change,
mostly for a few milliseconds but sometimes for half a second, and Windows refuses a second opener. The mutex above only
orders the checks; it cannot stop those outside programs. Before this retry, check
`5-name-conflicts` failed ("No name conflict dialog") whenever such a program was busy: a loop
that opens the clipboard for 20 ms every 25 ms failed it 3 of 3; with the retry it passed 6 of 6.

Text fields (the path, rename and search fields) copy and paste through the same retry: `ui_text_clipboard`
in `src/ui_ctxmenu.c` replaces CUI's own clipboard hooks, which try once. When a move ends, the app empties the
clipboard only if its sequence number is still the one the cut had, checked after the clipboard is open (a program
that held it meanwhile may have written new data). `actions_smoke.py` checks 8 (a move ends while another program
holds the clipboard and then writes text: the text stays; the order is a handshake, not timing: in test mode the app's first refused `OpenClipboard` sets the named event `<DFM_TEST_CLIP_HANDSHAKE>-blocked` and waits up to 5 s for `-released`, which the other program sets after it wrote and closed the clipboard, and only then does the 1.5 s wait count; if `-released` does not come in 5 s the app sets `-timedout` and the check fails; the first two events must exist, so a release build and other checks are unaffected) and 9 (Ctrl+V in the path field while another program
still holds the clipboard) fail on the code before these changes.
A program that holds the clipboard open for 450 ms in a loop (a stand-in for a slow clipboard manager) made
`5-name-conflicts` and `14-mouse-navigation` fail ("No name conflict dialog", "Copy not running") 5 of 5 and 3 of 3 times with the
300 ms retry, and they pass 5 of 5 and 5 of 5 with 1.5 s. `actions_smoke.py` check 13 (`13-clipboard-busy`) holds the clipboard open
for 700 ms during Ctrl+C and again during Ctrl+V: the copy lands, the paste starts and the name conflict dialog opens. It posts the
keys and does not wait for the window, because the app is meant to wait; `App.msg` gives up on a window after 500 ms.

## Header as title bar

`tests/use/chrome_smoke.py` has 14 checks on the private desktop: hit testing (caption, sidebar,
buttons, every header control, all eight resize edges, the top strip), the window following moves (`SetWindowPos` steps between `WM_ENTERSIZEMOVE` and `WM_EXITSIZEMOVE`; a mouse drag cannot start on a private desktop, so the caption is
proved by the hit test; the system Move loop is never started because it moves the real pointer, also from a hidden desktop, so these checks no longer prove that Windows' own move loop runs for this window), double click maximize and restore (maximized client equals the work area),
the three buttons (real click, the posted non-client hover, press and click of the maximize button),
close ends the app with code 0, the top three pixel rows in dark and light and active and inactive
(ours, no Windows grey), the app's report that Windows accepted the border colour (DWM cannot read
it back), dimming of all icons and buttons, the main menu opening from a click while inactive's
veil is up, the window icons, a maximized picture, and the width checks (client widths 360 to 1280, both sides of the 640 px
breakpoint, dark and light: the three window buttons inside the window and clear of every other
header control, hit testing agrees with them, the main menu opens where actions moved into it, and
it scrolls in a 280 px high window). `DFM_CHROME_PICTURES=<folder>` copies `dark-maximized.png`,
`<theme>-<width>.png` and the open-menu pictures there. Test mode drops `WM_NCMOUSELEAVE`, like `WM_MOUSELEAVE`, because
the driver's posted hover is the only pointer. Other suites find the right-hand header icons with
`App.header_icon(n)` (0 view options, 1 search; the grid and list toggles are in the open menu, `view_mode` in the snapshot) from the snapshot, not fixed
coordinates; they exist only in windows wide enough to keep those buttons (from about 750 px). `view_smoke.py`'s 50 ms latency cases fail when other work keeps the CPU at 100%;
read the load, rerun, and compare with main.

## Continuous slider and animated reflow

`motion_smoke.py` drives the real slider and window with Win32 input on the private desktop and reads the app's
`<base>.frames` log (one line per drawn frame, see `docs/architecture.md`). While a glide runs the app also writes one
`D` line per drawn frame: frame number, view height, scroll, view width, content width, then every drawn item as
`m<idx>:x:y:alpha:w:h` (an item at its drawn place) or `g<idx>:...` (a ghost, an item fading out at its old place).
Eleven cases: a slow drag changes the icon size in at least twelve different steps of under 12 px with no snap and the
value survives a restart; the modes switch at the midpoints; Ctrl+wheel, Ctrl+plus and Ctrl+0 glide monotonically in
50 to 300 ms; a width change that removes columns glides the last on-screen item through at least three places in 60
to 300 ms and ends on the exact grid place, draws nothing once idle, and builds every frame in 16 ms; a second
resize 60 ms later retargets from the drawn place; with animations off (`DFM_TEST_ANIMATIONS=off`) the columns change
at once and the glide overlay never draws; columns change during a slider drag and the drag ends on the grid; the
sidebar leaving and returning at the narrow breakpoint glides; a click and a right click, each in its own slowed glide
(`DFM_TEST_GLIDE_MS=2500`) on an item drawn 0.5 to 1.5 cells from its final place, read from the newest draw frame right before the click, land on the item drawn under the pointer, not on the item whose final place it is (the earlier version sampled a state dump that had aged and failed 3 of 3 runs); five
resizes while scrolled down move items in and out of the view. Every glide is read from the `D` lines:

- no two items at full opacity (99.9% or more: gliders and settled items) overlap by more than 2 px; a fading ghost
  and a fading arrival may overlap others while they are translucent (the crossfade);
- the grid never empties (`never_empty`, in the wide wrap of cases 4 and 5): in every drawn frame the opacities of
  everything drawn add up to at least 75% of the number of items whose final place is in view (floor lowered from 90% when the old copy
  began to leave in the first half of the glide; the thinnest moment is about 82%). It failed on the earlier
  design, which faded items out first and in last (56% at the emptiest frame), and passes now;
- `crossfade_timing` (case 4): a ghost is gone by 52% of the glide and an item's two copies both show (15% or more) only in the first 36%;
- `no_jumps` (cases 4, 5, 11): no item drawn in the view moves more than its glide distance between two frames, and a carried ghost does not move at all; case 11 (scrolled, resize to 900 then to 760 after 60 ms) failed on 234f70f (ghost jumped 78 px);
- at the end every item whose place is in view is drawn there and no ghost is left.

Each case starts with no saved view. The suite sets `CUI_RENDERER=software`: on the hidden desktop the default OpenGL
path is emulated on the CPU and delivers a frame every 33 to 45 ms (about 115 per second in software), so frame
counts would measure the emulation. After the glide the case waits out the test's own state request (it asks the
window for frames for about 0.4 s) and then expects no frame in 1.2 s.

`motion_measure.py [--strips]` measures message latency and frame build time during a slider drag and during resize
glides over 1,000 files, and with `--strips` saves frame pictures of the whole window (`DFM_TEST_STRIP`, every nth
frame with `DFM_TEST_STRIP_EVERY`) and joins them into strips: a 1.5 s slowed resize glide (`DFM_TEST_GLIDE_MS`), a
slow drag across 2.5 (compact rows turning into tiles) and a slider drag, each in dark and light. Numbers are in
`docs/view-slider-validation.md`.

## Loading placeholders and the unreadable-folder page

`loading_smoke.py` runs on the hidden desktop and writes pictures of the whole window to `shots/`. A slow listing is made with
`DFM_TEST_LIST_DELAY_MS=n` (read only with `--test-state`: every listing waits n ms); the state dump's `skel` is the number of
frames that drew placeholders for the current listing. Checks: placeholders appear after 200 ms in Grid and Details, light and
dark, at 1280, 600 and 360 px wide, and with animations off; a quick folder, a 100 ms listing, an empty folder, a search with no
results and 5,200 files draw none; after loading no frame is drawn while idle; "Unable to access" (`C:\System Volume Information`,
and a long Persian folder name that the check denies List Folder on and resets afterwards) shows Try Again, and a click on it
lists again.

## Drive usage bars in the sidebar

`sidebar_bars_smoke.py` runs on the hidden desktop and writes pictures of the whole window to `shots/`. Drives get sizes from the
sandbox list (`DFM_TEST_DRIVES`, read only with `--test-state`): a line is `C 3 Label|total bytes|free bytes` (a line without sizes
is a drive of unknown size). Checks, light and dark: a bar under each sized drive (3 px, from the label's left edge to the
row's right padding, to the eject button on a removable drive), its fill share equal to the used share, danger red only above 90%,
no bar for the drive without a size; the same with the selected row, the hovered row, Other Locations open, and the 360 px sheet;
the hover tooltip of each drive reads "199.5 GB of 238.2 GB used (84%)", and the drive without a size has none.

## Sidebar checks

`tests/use/sidebar_layout_smoke.py` has 15 checks on the private desktop: the width by dragging the line (below 180, above 400), the saved
width after a restart, the double-click reset, the content minimum, the 150 ms slide across 683 px both ways (timed from the app's own sidebar frames, 150 to 170 ms, with no picture or state dump while it slides), a slowed slide that turns
round part way, a Win+Left style jump, animations off, a 360x280 window, and pictures (whole windows in light and dark, frame strips
of a slowed slide) under `<work>/pictures/`. It writes the saved width into its own `appdata` copy of `view.txt`; it never touches the
clipboard. In `--test-state` mode the app also writes `<base>.sidebar`: one line `time width sliding` per frame while the sidebar slides and for the first frame after it. The
`<base>.frames` glide flag cannot time the slide: it also holds while the file grid reflows, which adds 10 to 50 ms. Set `DFM_EXE` and `DFM_USE_WORK` below the task folder and run it through `waitfor.py --heavy`.

## Sidebar rows added and removed

`sidebar_smoke.py` runs 19 checks on the private desktop with the check's own `APPDATA` (the settings file `sidebar.txt` is read from it, never Omid's): a folder with a long Persian name added from the item menu (row right after Trash, before the drives, settings line written, the row opens the folder; light, dark, and at 683 px); the same folder added twice and by Ctrl+D (one row); 30 added folders (the order added, 30 lines saved, Other Locations pushed below the window); Home removed and Show Hidden Places; a drive row removed, then every row but Other Locations, then all back from Other Locations' menu; a restart that keeps removed and added rows; Ctrl+D in Recent and in a search adds nothing, a search result folder is added from its menu; a deleted folder's row dimmed (`dim` in the state) and its click leaving the window where it is; the 360x280 window with the sidebar sheet open (remove and show inside it); a sidebar.txt with a 5,000-character line (one broken record, the lines after it read); one folder written six ways (trailing backslash, `.\`, `x\..\`, other letter case) is one row, also when added again; a failed save (read-only sidebar.txt) shows the row and the next Add saves it; the same rows in the 360x280 sheet in dark. The state dump has `sbrows` (each row by key with its rectangle) and `sbmenu` (the two menu items). The menu items are picked with keys for the item menu (the Add to Sidebar row is the 9th) and clicks for the row menu. Not covered: scrolling a long sidebar with the wheel (the harness's wheel messages do not move the sidebar on main either).

## Blink Compare checks

`tests/use/compare_smoke.py [--light]` has 14 checks on the private desktop, with a fixture it makes itself (needs Pillow): two 2048x1536 PNGs
that differ in one 200x120 box (the screenshot diff must find exactly that box, about 1 % of the stage), 800x600 against 2000x1500 of one
picture, a JPG against a PNG, transparency, two Persian file names, two 7680x4320 PNGs, and a text file. It opens the compare view
through the real right-click menu (arrow keys: Open, Open With, Compare Pictures), drags the divider, checks the arrow keys, that Esc keeps
the selection, that Delete, Ctrl+A and typed letters do nothing behind the view, that one or three pictures or a picture and a text file
get no menu row, that the Blink button flips, and (13) that opening the search and (14) the mouse's Back button close it. Check 11 measures the 8K budgets of `docs/design.md` (open time, longest message delay while
decoding, working set steady, peak and after closing, Space to B drawn from the app's own `b_qpc`); check 12 reads `<base>.frames`: no frame
in 2 s when nothing moves, and Blink draws only its flips with every frame under 8 ms. Pictures of each state are in the work folder
(`DFM_USE_WORK`, default `use-checks/compare_smoke`); `--light` runs the same checks in the light theme. Set `DFM_EXE`.

## The checks never move the pointer

The pointer belongs to the whole window station, not to one desktop. Windows' own move and size loops (`WM_SYSCOMMAND` with
`SC_MOVE` or `SC_SIZE`, `WM_NCLBUTTONDOWN` on the caption or a frame edge) move it for real, also from the hidden desktop, as do
`SetCursorPos`, `SendInput`, `mouse_event` and `ClipCursor`. `tests/test_pointer_safe.py` (console, no window; also a CTest entry)
fails if any file in `tests/use/` uses one of them. `input_smoke.py` `live_resize` and `chrome_smoke.py` `drag` therefore test
live sizing and moving with `WM_ENTERSIZEMOVE`, `SetWindowPos` steps and `WM_EXITSIZEMOVE`, and fail if `GUI_INMOVESIZE` is ever
set. Not provable that way: that Windows' modal loop itself runs for this window.

The same test refuses `AttachThreadInput`, `SetKeyboardState` and `BlockInput` in every file under `tests/`, except that
`input_smoke.py` and `persian_input_smoke.py` may use the first two. The app reads Ctrl, Shift and Alt with `GetKeyState`, which
reads the key table of its own thread; posted key messages do not change it and `SetKeyboardState` writes only the caller's table.
So those two checks can hold a modifier for the app only by joining their input queue to the app's thread. Both threads are on the
private desktop, which no hardware input reaches, so Omid's input is not held. A test hook in the app would remove the join.

## Disk tree

`tests/test_treemap.c` (CTest target `dfm-treemap-tests`) checks the layout alone: areas by size, the rectangles cover the area,
no overlap, inside the area, no items, one item, equal sizes, 3,001 items. `tests/use/tree_smoke.py` runs 12 checks on the
private desktop (tiling, nested children, Persian names, select / open / back, Ctrl+1/2/3, arrow keys, rename box, tooltip,
leaving while measuring, empty and denied folders, a path over 260 characters). `tests/use/tree_budget.py PATH...` measures the
budgets of `docs/design.md` "Disk tree" on real folders (message delay, idle CPU, working set, time to the first rectangles);
`DFM_TB_VIEW=list` measures the Details view of the same folder as the baseline.

## Agent names

`test_agent.c` (CTest `dfm-agent`, a console program on `agent.c` alone, its choice file in a sandbox APPDATA): what an answer may do to a name (illegal characters, a path, device names, direction controls, an overlong name, the extension kept, the same name refused, Persian kept), which names count as machine-made, reading answers (fence, chatter, cut off, `\u` escapes, the room given), the Persian start of a text file in the prompt, and the runner with `findstr` as the stand-in agent (600 KB in and out without a stall, the timeout, Cancel, a missing program).

`use/ghost_smoke.py` runs 14 checks on the private desktop with `use/stub_agent.py` as the stand-in (modes ok, broken, slow, collide, illegal; the real agents stay hidden; the choice file is written before the app starts): none installed, first use picks once, Accept all with Tab and Undo, Esc, name collisions, illegal characters, broken JSON, timeout, Cancel (the process is gone), F2 on a machine-made name, F2 on a normal name, F2 never opens the picker, List view, and a click on Accept all. `DFM_BUDGET=1` (or `USE_ONLY=15`) adds check 15, about 5 minutes: idle CPU with suggestions on screen and while a run goes (`cpu_budget.py`, at most 0.5 %), the window's answer time while a run goes (95th percentile under 50 ms), and the working set, written to `ghost-budgets.json`. `DFM_USE_WORK` must be an absolute path without `..`. Not covered by the stand-in: a real agent's answer; run Claude Code by hand once on a folder of machine-named files.
