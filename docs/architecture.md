# Architecture

One Windows executable in C, built with CMake and MSVC 2022 (Build Tools), CUI v1.4.0 fetched with
FetchContent. All text is UTF-8 inside the app; convert to UTF-16 only at Win32 calls.

## Files and owners

| Files | What |
|---|---|
| `CMakeLists.txt` | Build. Compiles every `src/*.c` (a glob), links CUI and `shell32 ole32 oleaut32 uuid shlwapi propsys comctl32 dwmapi setupapi cfgmgr32`, adds `tests/` when it exists, and fails a Release or MinSizeRel build whose exe is over the size budget (`docs/design.md`). |
| `src/main.c`, `src/ui_*.c`, `src/theme.c` | The window: header rows, sidebar, grid and list views, menus, popovers, toasts. |
| `src/chrome_win.c` | The header as the title bar: subclass messages (`WM_NCCALCSIZE`, `WM_NCHITTEST`, min/max size, activation, the maximize button's non-client hover and click), the three window buttons, inactive dimming, DWM border colour, window icons, and the `"chrome"` part of the test snapshot (window and client rectangles, buttons, header controls, border colour, hover). Header code calls `chrome_nodrag_last()` after each control. |
| `src/dio-file-manager.rc`, `src/dio-file-manager.ico`, `design/icon/` | The embedded icon, its source and the script that builds the frames. |
| `src/fs.c`, `src/fs.h` | Reading folders, the places (known folders) and the drives. |
| `src/ops.c`, `src/ops.h` | File work on worker threads: copy, move, trash, delete, rename, new folder, conflicts. |
| `src/clip.c`, `src/clip.h` | The Windows clipboard for files (CF_HDROP and "Preferred DropEffect"). |
| `src/trash.c`, `src/trash.h` | The Recycle Bin: list, restore, empty, undo of the last trash. |
| `src/search.c`, `src/search.h` | Recursive search below a folder on a worker thread. |
| `src/recent.c`, `src/recent.h` | Recent files from the Windows Recent folder. |
| `src/shell.c`, `src/shell.h` | Open, Open With, Open in Terminal, Properties, Copy Path. |
| `src/defaultfm.c`, `src/defaultfm.h` | Use as Default File Manager: HKCU registration with exact restore, the launcher, Explorer-style requests, the Explorer fallback. |
| `tests/` | Console tests of the non-window parts, on a sandbox under `.agent-work/`. |

The window code calls the other modules; they never call CUI. Work that can take long (copy,
move, trash, search, folder sizes, thumbnails) runs on its own thread, and the window polls
its state each frame (`cui_request_wakeup` while it runs).

`ui_ctxmenu.c` isolates the CUI 1.4.0 internal popup and list-row hit calls. CUI's
data-grid row does not update `last_id`, so `cui_last_rect` cannot identify that row.
The same adapter registers the window's frame callback for CUI's native modal
resize loop. It finishes pending layout before allocating screenshot buffers,
so both growing and shrinking use the current pixel dimensions.
Search results keep their own listing. Trash completion drops missing results and refreshes
the saved folder; reload and Undo rerun the query without closing it.

Alt+Enter opens only Properties; plain Enter opens an item only without modifiers.
The running-job header control is one clickable progress ring; its popover names
the copy and offers Cancel. Real-input action checks and private-desktop shell
substitutions are documented in `tests/README.md`.
`ui_zoom.c` owns the continuous view value `A.zv` (0 to 5; `A.zoom` is the stop it rounds to and selects the mode and
layout style), the 150 ms ease-out glide of the shortcuts, the 120 ms fade-in of a mode switch, the animations setting
(`SPI_GETCLIENTAREAANIMATION`) and the atomic replacement of
`%APPDATA%\dio-file-manager\view.txt`. Its record holds the current and last icon values.
Startup restores it unless `--view grid|list` overrides it. `ui_grid.c` draws the four
icon stops and the column-major List stop; `ui_list.c` keeps Details and its
place-specific columns, including Location for search. `ui_ctxmenu.c` also isolates
CUI's scroll-input consumption and virtual horizontal columns. List maps the ordinary
wheel sideways; Ctrl+wheel is consumed before the file view scrolls. The popover uses
CUI's slider without a step. `ui_grid.c` also runs the reflow glide: before the cells are built `glide_plan` compares
the layout with last frame's, anchors the scroll to the top item and stores per-item offsets, sizes and opacities
(horizontal in document pixels, vertical in rows); while it runs the cells are drawn by an overlay (`ui_overlay_set`
in `ui_ctxmenu.c`) at their offset places and the canvases only take input. An item that stays in its row glides with
its width and height; an item that changes row is drawn twice for the 150 ms, as a ghost at its old place with opacity
`a * (1 - p / 0.5)^2` (gone at half of the glide) and at its new place with `a0 + (1 - a0) * ease` (cubic ease-out over all of it), so the two add up to at least about 82%.
Names never overprint: `mask_labels` holds back the name of a copy that is less opaque where it comes within 12 px of the name of a more opaque copy (all of it at 4 px or less), so a glider's name stays whole and a fading copy yields; the icons and the fades are untouched.
A ghost carried into a second change is rebased on the scroll the anchor moved to, and on the frame of a change the glide draws with the anchored scroll (`force_sy`), because the list clamps the jump to last frame's content height. Input
during the glide tests the drawn places and takes the most opaque item under the pointer. Animation frames are asked for with `cui_request_redraw`, so they run at the display rate,
and nothing is requested once the glide has ended. Thumbnails request the displayed device-pixel edge; while `A.thumb_settled` is false the nearest existing
thumbnail is scaled and no new size is requested.
In `--test-state` mode the app also writes `<base>.frames`, one line per drawn frame (time, frame number, value, icon px,
columns, gliding, probe item, probe x and y in document pixels, build ms; the probe is the last built item that is on
screen) and, while a glide draws, one `D` line per overlay frame with every drawn item's place, size and opacity, and the box and opacity of its name. The JSON gains `zv`, `icon_px`, `content_w`, `cell_h` and `probe`. `--zoom N` starts at a value;
`DFM_TEST_STRIP=<dir>` saves up to 14 pictures of a glide or size change; `DFM_TEST_GLIDE_MS` slows the glides.

`tests/use/view_smoke.py` imports the Win32 driver from `input_smoke.py`, runs on the
private desktop and redirects APPDATA into its sandbox for restart checks. It covers
slider drags, shortcuts, persistence, selection/opening, column order, special places,
search and a 50 ms input-message limit while scrolling 20,000 items at every stop.
Clipboard-preserving checks acquire the shared Windows mutex
`Local\DioFileManagerClipboardTest` before saving any formats and hold it through
restoration; the wait is capped at 15 minutes. The helper lives in `clipboard_guard.py`.

## Build and install

From the repository root, build the app and console checks together:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config MinSizeRel -j 8
```

The post-build guard refuses an executable over 819,200 bytes (800 KB).
Install for the current Windows user. No administrator access is needed:

```powershell
$install = Join-Path $env:LOCALAPPDATA 'Programs\Dio File Manager'
New-Item -ItemType Directory -Force $install | Out-Null
$exe = Join-Path $install 'dio-file-manager.exe'
Copy-Item -LiteralPath 'build\MinSizeRel\dio-file-manager.exe' -Destination $exe -Force
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Dio File Manager.lnk'))
$shortcut.TargetPath = $exe
$shortcut.WorkingDirectory = $install
$shortcut.IconLocation = "$exe,0"
$shortcut.Save()
```

Open **Dio File Manager** from Start. It opens Home in grid view and follows the Windows theme.
Updating preserves an open instance: rename its exe to `dio-file-manager.old.exe`
and copy the new exe to the normal path. Remove an older backup only when it is
not running; never close the user's window. For automated install checks, run the
installed executable on `correct/hidden.py`'s private desktop with isolated `APPDATA` and
`--test-state BASE`; the resulting PNG proves that copy created a working window.

The window has no native caption; the border colour and the system menu follow the app's Light/Dark/System choice. System mode handles
`WM_SETTINGCHANGE` immediately, including while the app is idle. Sidebar highlights
belong only to the current or hovered row; unselected navigation groups never inherit
CUI 1.4.0's retained selection. List rename anchors use the actual visible row rectangle,
including the header and scroll offset, to open below their row.

`tests/use/input_smoke.py` checks three pixel sizes during Windows' modal sizing
loop before mouse-up. On the inactive desktop Windows reads the physical cursor,
so the driver applies mouse positions with `SetWindowPos`; physical edge dragging
is not covered by this automated check. It captures each held-size frame and checks
its dimensions and right-edge fill. Clipboard checks use the shared Windows mutex
`Local\DioFileManagerClipboardTest`, with a 15-minute wait, from saving all formats
until restoration finishes (including nested checks and exception paths).

## Default file manager

`src/defaultfm.c` registers the app as Windows' folder handler for the current user (HKCU only).

- Keys written under `HKCU\Software\Classes`: `Folder\shell\open\command`, `Folder\shell\explore\command` and
  `CLSID\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}\shell\opennewwindow\command` (Win+E). Each gets the default value
  `"<launcher>" "%1"` (Win+E: `"<launcher>"`) and `DelegateExecute` = empty string.
- Saved state: `HKCU\Software\Dio File Manager\DefaultFileManager` with `App`, `Launcher`, `Args` and
  `Saved\<i>` for each key: `Path`, `Existed`, `Top` (the first key the registration created) and `Values`
  (a copy of every value with its type). Turning off restores the values, or removes the keys it created (each
  key from the leaf up to `Top`, and a parent only while it is empty, so another program's entries beside ours stay).
  Only when every key is back does it delete this key, the launcher copy and its folder (when empty); if a key
  cannot be restored, turning off reports failure and keeps both, and the next try starts from the same record.
  Any registry error other than "not found" while turning off (a saved value that cannot be read, a key that
  cannot be written) is such a failure; only a record that is really absent means "remove what is ours". While
  the record is left, or any command is still ours, the switch reads as on, so the next click retries turning
  off and a turn-on never replaces the record.
- The launcher is a copy of the exe named `dio-file-manager-launcher.exe` in `%LOCALAPPDATA%\dio-file-manager`.
  `main()` checks its own file name first: with the app present it starts it with the same arguments and exits
  (no window); with the app gone it restores the keys, hands the request to Explorer and deletes itself.
  A normal start refreshes the launcher copy by size and follows an exe that moved.
- Requests: `defaultfm_request` turns the arguments into a folder (with an optional name to select), a place
  (Recent, Trash, This PC), Home or a fallback. The shell sends `C:\` as `"C:\"`, which arrives as `C:"`; trailing quotes are removed.
- Loop guard: the same Explorer request twice within 2 s is refused (`fallback.txt` in the launcher folder, exit 4).
- Test hooks, only with `--test-state BASE`: `DFM_TEST_REGROOT` (registry root under HKCU with `\Classes` and `\State`),
  `DFM_TEST_LOCAL` (launcher folder), `DFM_TEST_EXPLORER` (command that stands in for Explorer; the real one is
  never started). `BASE.opened` lines: `pid`, `arrive-folder	<path>	<select>`, `arrive-home`, `arrive-place	<loc>`,
  `explorer	<cmd>`, `refused`, `restored`. `--default-fm on|off|status` (status exits 10 when off).

## Jobs, Recent and removable drives

Each paste starts an independent operation. The header ring summarizes active jobs by
bytes (item counts for jobs without a byte total). Its popover lists each job with its
own progress and Cancel action. Conflicts are answered one job at a time; other jobs
continue. A destination created by another job while a copy is in flight also raises
the conflict dialog, before the temporary copy can replace it. Closing the window
cancels and joins active file workers, removing incomplete copies.

Recent's item menu offers **Remove from Recent**. A worker removes every Windows
Recent shortcut pointing at the selected targets, including duplicate shortcuts.
The targets are never deleted. This action has no Undo.

Only `DRIVE_REMOVABLE` sidebar rows have an **Eject Drive** button. A worker rechecks
the drive type, finds its disk device by its Windows storage device number and asks
Plug and Play for safe removal (`CM_Request_Device_EjectW`). Windows can refuse a busy
device; the app leaves its row visible and shows an error. A successful request removes
the row and leaves a folder on that drive. Fixed and network drives are refused by the
backend as well as excluded from the UI. No network disconnect API is used.

The input harness can opt into sandbox sources only with `--test-state`:
`DFM_TEST_RECENT` names a folder of sandbox `.lnk` files; `DFM_TEST_DRIVES` names a
text file with `letter drive-type label` rows (for example `Y 2 Sandbox USB`);
`DFM_TEST_EJECT_LOG` replaces the physical eject with a log of requested drive roots.
A sandbox drive list always disables physical eject, even if its log is missing.
The copy checks use the existing `DFM_TEST_COPY_DELAY_MS` callback delay (at most 100 ms)
for observable concurrent progress on fast disks. Recent and drive overrides require `--test-state`.
