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
