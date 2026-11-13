# Architecture

One Windows executable in C, built with CMake and MSVC 2022 (Build Tools), CUI v1.4.0 fetched with
FetchContent. All text is UTF-8 inside the app; convert to UTF-16 only at Win32 calls.

## Files and owners

| Files | What |
|---|---|
| `CMakeLists.txt` | Build. Compiles every `src/*.c` (a glob), links CUI and `shell32 ole32 oleaut32 uuid shlwapi propsys comctl32`, adds `tests/` when it exists, and fails a Release or MinSizeRel build whose exe is over the size budget (`docs/design.md`). |
| `src/main.c`, `src/ui_*.c`, `src/theme.c` | The window: header rows, sidebar, grid and list views, menus, popovers, toasts. |
| `src/fs.c`, `src/fs.h` | Reading folders, the places (known folders) and the drives. |
| `src/ops.c`, `src/ops.h` | File work on worker threads: copy, move, trash, delete, rename, new folder, conflicts. |
| `src/clip.c`, `src/clip.h` | The Windows clipboard for files (CF_HDROP and "Preferred DropEffect"). |
| `src/trash.c`, `src/trash.h` | The Recycle Bin: list, restore, empty, undo of the last trash. |
| `src/search.c`, `src/search.h` | Recursive search below a folder on a worker thread. |
| `src/recent.c`, `src/recent.h` | Recent files from the Windows Recent folder. |
| `src/shell.c`, `src/shell.h` | Open, Open With, Open in Terminal, Properties, Copy Path. |
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

The native caption follows the app's Light/Dark/System choice. System mode handles
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
