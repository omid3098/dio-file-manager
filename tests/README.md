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
Both check gaming and use the private desktop. The input suite has 21 checks, including the mouse back and forward buttons (`14-mouse-navigation`); the search
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
holds the clipboard and then writes text: the text stays; the order is a handshake, not timing: in test mode the app's first refused `OpenClipboard` sets the named event `<DFM_TEST_CLIP_HANDSHAKE>-blocked` and waits up to 5 s for `-released`, which the other program sets after it wrote and closed the clipboard, and only then do the 300 ms count; if `-released` does not come in 5 s the app sets `-timedout` and the check fails; the first two events must exist, so a release build and other checks are unaffected) and 9 (Ctrl+V in the path field while another program
still holds the clipboard) fail on the code before these changes.

## Header as title bar

`tests/use/chrome_smoke.py` has 14 checks on the private desktop: hit testing (caption, sidebar,
buttons, every header control, all eight resize edges, the top strip), the system Move loop
(keyboard form `SC_MOVE 0xF010`; a mouse drag cannot start on a private desktop, so the caption is
proved by the hit test), double click maximize and restore (maximized client equals the work area),
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
