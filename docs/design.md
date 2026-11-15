# Design

Dio File Manager works and looks like GNOME Files (Nautilus 46/47, libadwaita),
painted in Dio's colours so it feels like Dio Tools. Omid's words (2026-11-13):
«دقیقاً کلون همون gnome file manager رو با cui برام بساز ... از ترکیب رنگ و
الگوهای dio استفاده کن که حس و حال dio-tools رو داشته باشه.»

The colour values come from Dio Docs' token master (`E:\w\dio-docs\src\data\tokens.json`,
the Halo palette). CUI's own rules (`docs/design.md` in CUI) hold where this page says nothing.

## Window

```
+------------------+---------------------------------------------------------------+
|              [=] | [<] [>]   [ (home) Home  >  Documents  > ]   [search] [grid|v] |  header rows, 46 px
|------------------|---------------------------------------------------------------|
| (clock) Recent   |                                                               |
| (star) Starred   |   [folder]   [folder]   [folder]   [ pdf ]   [ img ]          |
| (home) Home      |    Music     Projects    Papers   report.pdf  photo.jpg       |
| Desktop          |                                                               |
| Documents        |   [ txt ]    [ zip ]                                          |
| Downloads        |   notes.txt  backup.zip                                       |
| Music            |                                                               |
| Pictures         |                                                               |
| Videos           |                                 ( "report.pdf" selected 2.4 MB ) |  floating status pill
| Trash            |                                                               |
| ---------------- |                                                               |
| Local Disk (C:)  |                                                               |
| Data (E:)     ^  |                                                               |
| + Other Locations|                                                               |
+------------------+---------------------------------------------------------------+
```

- **Two panes.** The sidebar (240 px) and the content pane. Each has its own header row of 46 px,
  as in libadwaita's split view. There is no Windows title bar: the client area starts at the
  window's top edge and the header rows are the title bar, in our colours (`src/chrome_win.c`).
- **Sidebar header:** the main menu button (`menu` icon) at its right end.
- **Content header:** back and forward on the left; the path bar in the middle (CUI `cui_path_bar`,
  a home icon on the first segment when inside Home, a menu on each chevron; Ctrl+L or a click on
  the empty part turns it into a text field); on the right a search toggle and the view-options button.
  The path bar takes all the width between Forward and Search, less about 32 px of empty header for dragging the window
  (there is no grid or list button in the header). The view-options button shows the current view's icon (grid or list)
  and a small down chevron, as GNOME Files 43's view menu button did; it is 66 px wide, not the sliders icon, which looked
  like the main menu's settings. Its menu (sort by name, size, modified or type; reversed order; show hidden files) opens
  with the view choice: two linked toggle buttons side by side, grid and list, the current one highlighted. A click switches
  the view at once and the menu stays open. Next is a continuous view-size slider, value 0 to 5: Details at the left, then List,
  Small icons, Medium icons, Large icons and Extra large icons at the right. The thumb stays wherever it is
  released; it never snaps. The mode name under the slider is the nearest stop, and six ticks only mark the stops.
  The mode (Details, List, icons) switches at the midpoints 0.5, 1.5, 2.5, 3.5 and 4.5, and the new view
  fades in over 120 ms (the old view is not cross-faded). In the icon range the icon edge, the cell and the label
  size follow the value smoothly: 32, 64, 96 and 144 px at the values 2 to 5, a straight line between them;
  16 px in List.
  The view-options surface is opaque, including while opening over file icons.
- **Sidebar rows:** Recent, Starred, Home, Desktop, Documents, Downloads, Music, Pictures, Videos,
  Trash; a hairline; each drive by its label and letter ("Local Disk (C:)"), with an eject button
  for removable drives; "Other Locations" last (all drives with their used and free space).
  Rows are 36 px, a 16 px Lucide icon and 14 px text, radius 10, 6 px inside the pane edge.
  The current place is filled with the `selected` colour; hover with `hover`.
  A drive with a known size shows how full it is: a 3 px bar with rounded ends under its label, from the label's left edge
  to the row's right padding (it stops before the eject button on a removable drive). The fill is the accent colour and turns
  danger red above 90% used, as the meter in Other Locations does; the rest is CUI's muted track. The label and the bar sit in the
  same 32 px row (CUI's nav item keeps its label centred, so the bar fits under the text's descenders). Selected and hovered rows
  keep both. No numbers are written in the row: its hover tooltip reads "199.5 GB of 238.2 GB used (84%)" (the unit by the
  drive's size, as in Other Locations). A drive without a size (an empty card reader, a network location) has no bar and no tooltip.
  The bars change with the drive list, which is read at start and each time Other Locations opens.
  Trash shows an item-count badge when nonempty. CUI has no empty-bin icon,
  so the icon stays the same when the bin is empty or full.
- **Grid view (default):** cells about 112 px wide; a 64 px icon above the name; the name centred,
  up to two lines (GNOME Files 47 has three: lines 3, wrap word-char, ellipsize middle; three lines make the
  reflow motion check fail, see NAME_LINES in ui_grid.c), the ellipsis in the middle of the last line, so a longer name
  keeps its start and its end and the extension stays visible; the whole name is in a tooltip under it (wrapped inside its box). Folders are drawn filled in the accent colour (a back flap a step darker), not as
  outline icons. Files are a page with a folded corner, with a small type label ("PDF", "JPG")
  or a Lucide type icon; image files show a thumbnail (aspect kept, rounded corners, soft shadow) once it
  is read, only for the cells in view, with the page icon until then. The icon stops use 32, 64,
  96 and 144 px icons (values in between interpolate); Medium (64 px, value 3) is the default. Small puts the name beside the icon.
  Thumbnail requests match the displayed edge, including the display scale.
- **Reflow glide:** when the window width or the view size changes the number of columns, every visible
  item moves to its new place in 150 ms (cubic ease-out): quick enough to feel light. An item that stays in its
  row glides, and its width and height glide with it. An item that changes row crossfades: its old copy fades out where it
  was and is gone by the middle of the 150 ms (squared fade), while its new copy fades in where it goes over all of it,
  so duplicates show only in the first third and the grid never dims below 75% of its items' summed opacity. Only items at full
  opacity are kept apart; a fading item may overlap others while it is translucent. Names never print over each other: a fading copy holds its name back where it comes near the name of a more opaque one. A click lands on the most opaque
  item under the pointer, never on a fading ghost.
  A second change during a glide starts from the positions and opacities drawn at that moment. Only the visible items
  (at most 1,024) move, and the scroll position keeps the item at the top of the view at the top. Cells keep their
  real rectangles for clicks and keys; only the drawing is offset. With animations off the columns change at once.
  While the size is still changing, thumbnails of the nearest size are scaled; the sharper size is read 200 ms
  after the value stops.
- **List stop:** 16 px icons and names in 240 px columns, filled top to bottom then left to right.
  The wheel scrolls sideways. Only visible columns are built. A persistent 6 px horizontal
  scrollbar at the bottom uses the vertical scrollbar's colour and rounded ends, and can be dragged.
  Columns have 16 px top and bottom space. The menu's list toggle is lit in List and Details;
  the grid toggle is lit at the four icon stops.
- **View shortcuts:** Ctrl+wheel and Ctrl+plus/minus move one whole value up or down and glide there in 150 ms
  (cubic ease-out; a new press retargets from the current value); Ctrl+0 glides back to Medium.
  With Windows' "Animation effects" off (or `DFM_TEST_ANIMATIONS=off` in test mode) they jump.
  Wheel events retain their modifier state even when Ctrl is released before the next frame.
  The grid toggle (Ctrl+1) returns to the last icon stop; the list toggle (Ctrl+2) opens Details.
  The exact value and the last icon value survive restarts in `%APPDATA%\dio-file-manager\view.txt`
  (`view-v2 <value> <last icon value>`; the older `view-v1` file with whole stops is still read).
  One background writer saves the latest stop and finishes pending writes on exit, so
  dragging the slider never waits for a settings-file flush.
  Search, Recent, Starred and Trash share this scale.
  Presentation keeps CUI's normal event-driven display pacing. Idle windows wake only for
  input, timers or worker progress. Recycle Bin badge queries run on a worker.
- **Selection:** the icon gets a rounded `selected` fill; the name gets an accent pill with
  `accent-fg` text. Click selects, Ctrl+click toggles, Shift+click selects a range. Dragging from
  anywhere but an item's icon or name text draws a rubber band: from the empty background,
  and from an item's blank part too (in Details and List the row beside the name text and the Size,
  Modified and Type cells; in the icon views the padding around the icon and the label). A press on
  the icon or the name text is the item's own and never draws the box. A press on an item's blank
  part is an item press until the pointer has moved 4 px: a click selects that item and a double
  click opens it as on any part of the item, and only a drag turns into the box. In a Details row the
  name's blank part starts where the name text ends; a name that fills its column has none, but its
  Size, Modified and Type cells still do.
  A drag replaces the selection live with every cell or row the box touches;
  Ctrl or Shift adds to the selection saved at press time. Shrinking the box removes
  items outside it again. The box has an 18% accent fill and a one-device-pixel accent
  border in both themes. Holding it within 32 px of the top or bottom scrolls vertically;
  List uses the left and right edges. Its anchor stays in the document while scrolling.
  A background click clears the selection; item presses and right clicks keep their
  normal actions. The same selection works at every scale, in search and special places.
- **Details stop:** columns Name (16 px icon and name), Size, Modified, Type; sortable headers;
  folders first. Rows 36 px. The Size column shows a folder's real size (all files below it, logical bytes, junctions and
  symbolic links not followed), measured in the background, the folders on screen first. While a folder is still being
  measured its cell shows a muted "…" at the right edge; the number replaces it when it is in. Sorting by Size puts folders
  first and sorts them by size among themselves (files by size among themselves); it orders by size once, when every shown
  folder is in, and keeps the selection and the scroll where they were. Local drives only (network and removable drives
  keep the empty cell), folders in a normal listing only (not search, Recent, Starred or Trash), grid view unchanged. On by
  default. The rule, in Omid's words (2026-11-14): «برای اندازه پوشه‌ها نیاز نداریم که ریل تایم سایزشون دیده بشه و آپدیت
  بشه کافیه همون موقعی که وارد هر دایرکتوری می‌شیم ساب دایرکتوری‌های داخلش سایزشون به درستی دیده بشه». So:
  - Every time a folder is entered (opened, back, forward, parent, reload) each folder inside it is measured again. Until
    the new number arrives its cell shows the last known one (the pill says it is being measured), or "…" when there is none.
  - The list of the open folder is live (see "Live folder" below); the sizes are not. A folder's size is measured when its
    parent is entered, never again while the parent stays open: no drive watcher in the size engine, no timer, no re-walk
    after a change on disk. A subfolder that appears while the folder is open is measured once, as it shows up; the other
    subfolders keep the number they have. With nothing being measured the engine does nothing (idle CPU below 0.1 % of one
    core, also while other programs write to the drive). A folder changed on disk shows its new size the next time its parent
    is entered.
  - No waiting time keeps an old number: a folder changed on disk shows its new size as soon as its parent is entered
    again, also at once after it was left, and after a reload (F5, or the list made again when an operation ends). Only a folder
    whose measurement began after the shown folder was entered (a quiet re-listing: a copy running, or another program changing the open folder) is not walked again,
    and a folder inside a walk that is still running waits for that
    walk: entering E:\ and, right after, E:\w costs one walk of E:\w, not two. A folder made after that walk read its
    parent is measured when the walk ends.
  - A file or folder the program may not read counts as 0 bytes in the folder above it, which sums what it can read (the
    same as the folder properties in Explorer), so C:\Users still shows a size. The folder that cannot be listed (for example
    C:\System Volume Information, or a path too long for the walk's buffer) shows no number: its Size cell stays empty, never
    a finished "0 bytes", and the status pill shows no size for it. A junction or symbolic link inside is not followed.
  - Leaving a folder cancels the walks that are not on the way to the new one; a walk of a folder above the new one
    keeps going and brings the sizes the new view asks for. The window's thread never waits for a measurement (it only
    takes a lock held for table work; the longest call in the console check is about 1 ms).
- **Floating status pill:** bottom right of the content pane, over the files, only while something
  is selected or loading: "“report.pdf” selected (2.4 MB)", "3 items selected (5.1 MB)",
  "Loading…". It floats, so it has the popover shadow. A selected folder counts with its measured size; while any selected
  folder is still being measured the pill ends with "…" inside the brackets ("“src” selected (…)", "2 items selected
  (3.0 MB…)": the sum so far, then the dots).
- **Loading:** a listing that has run 200 ms (a quicker one never flashes) shows placeholder shapes in the form of the view
  that will replace them: tiles in Grid, a header with its column titles and rows of bars in Details, short rows in
  columns in List. They are CUI's skeleton shapes in one group, so one shine runs across them; with animations off
  (Windows' setting) they stay still. The real items fade in over them in 120 ms. The pill keeps "Loading…". Frames are
  requested only while the shapes show, so an idle window draws none.
- **Empty folder:** a large muted folder icon and "Folder is Empty", centred.
- **Unreadable folder:** a large muted icon, "Unable to access “Name”" and the reason, and a "Try Again" button under it
  (refresh icon; it asks for the listing again, as F5 does). A name too long for the pane is cut in the middle. No other
  empty state has a button.
- **Narrow window (< 683 px, GNOME Files 47: max-width 682sp):** the sidebar leaves; a sidebar button appears at the left of the
  content header and opens it as an overlay. Back, Forward and view options move from the header into a bottom bar
  (46 px, header colours): history on the left, the view-options button (view icon and chevron) on the right. Back and Forward are never in a menu.

## Behaviour (as in GNOME Files)

- Double click or Enter opens: a folder in place, a file with its default app.
- Alt+Left and Alt+Right go back and forward; Alt+Up goes to the parent; Alt+Home goes Home.
- The mouse's back and forward buttons (and the keyboard's browser keys) do the same, as in Explorer, wherever the
  pointer is in the window. Windows reports a press as `WM_XBUTTONUP` or as `WM_APPCOMMAND`; the window answers
  both with TRUE, and takes the browser keys' `WM_KEYDOWN` itself, so one press moves one step (a held key repeats,
  as in Explorer). With a dialog or the rename popover open the press is dropped; with any menu or popup open it
  closes that popup and goes no further, as in Explorer.
- History is Explorer's: every folder change (double click, sidebar, path bar, typed path, Alt+Up) is one step; the
  same folder again, in any letter case, is none; a new step after Back drops Forward; the first folder of a window
  has no Back.
- Ctrl+L types a path; Ctrl+F searches below the current folder; typing a letter in the view
  starts the search as well (only while the file view has the keyboard: not in the path field, the
  rename or new-folder popover, the search field or an open menu); Esc ends it and returns to the folder.
  Opening search again restores keyboard focus to its field, including after visiting special places.
  The search field takes the place of the path bar; the search runs 0.25 s after the last key, below
  the folder, at most 10,000 results. Results list in the grid or the list; the list adds a Location
  column with the folder of the result relative to the searched folder. No match shows "No Results
  Found". While the search runs, a spinner shows it: CUI's ring, 18 px beside the field, and in the empty view a radar sweep
  (CUI's dither spinner, 32 px, accent colour); a spinner asks for frames only while it is on screen. Results open and take the item menu, with Open Item Location.
  Moving a result to Trash keeps the query open and removes missing results. Undo restores the
  file and refreshes the same search; Esc returns to a refreshed folder listing.
- The File Operations button in the header exists only while a file job runs and for a moment after the last one ends.
  While jobs run it is the progress ring (accent arc over a border track, 36 px, clickable: the popover lists each job with its
  own progress and Cancel). When the last job ends, CUI's status spinner takes its place in the same 36 px: a check in the
  success colour, or a cross in the danger colour when any job failed, holds for CUI's default moment (about 2.4 s) and dissolves
  away; then the button is gone as before. A cancelled job gives no mark of its own: when the jobs that ended include a finished one the check shows, and when every
  one was cancelled nothing shows. The mark shows only when the ring was on screen for at least 1 s; a job that ends sooner (a
  rename, a new folder, a small copy) ends with no mark. Windows animations off show the mark at once, and it goes at once after the same hold. It asks for frames only while it shows.
- Ctrl+1 grid, Ctrl+2 list; Ctrl+H hidden files; F5 reload; Ctrl+A select all.
- **Live folder:** the open folder follows the disk, as in GNOME Files and Explorer: a file or folder another program adds,
  removes, renames or changes shows up without F5. Only the open folder is watched (not its subfolders; not search,
  Recent, Starred or Trash), on local, removable and network drives. A burst of changes is read once: about 200 ms after
  the last change, and at most about every 500 ms while it goes on. The new listing is quiet: no loading pill, no
  placeholders, no fade of the whole view; new items fade in as items do when a copy brings them, removed items go. The
  selection (by name), the keyboard focus, the scroll, an open rename field and an open menu stay as they are. The growth of
  a file this program is copying does not ask for a new listing. With nothing changing in the folder there is no timer
  and no CPU use (below 0.1 % of one core, also while other folders on the drive change). The watch never holds a USB
  drive: it lets go of the folder when Windows asks to remove the drive and when Eject Drive is pressed (a refused eject
  watches the folder again), and when the folder is left. When the open folder is deleted, or its drive goes, the page for
  a folder that cannot be read shows, as after F5. Folder sizes keep their own rule (above).
- F2 renames in a small popover under the item ("Rename" field and button); Ctrl+Shift+N makes a
  folder through a popover ("New Folder" name field and Create).
- Ctrl+C, Ctrl+X, Ctrl+V copy, cut and paste through the Windows clipboard, so files move between
  this app and Explorer both ways.
- Delete moves to the Recycle Bin and shows a toast "“name” moved to Trash" with Undo.
  Shift+Delete deletes for ever after a confirm dialog.
- Right click on an item: Open, Open With…, Cut, Copy, Rename, Move to Trash, Star, Copy Path,
  Properties. On the background: New Folder, Paste, Select All, Open in Terminal, Properties.
  The menu opens at the pointer with its own width (as narrow in the list view and in an empty
  folder as in the grid), from any right click in the content pane, also below the last grid row.
  List rows claim right clicks using their own clipped row rectangle, including clicks on cells.
  While it is open it has Up, Down, Enter and Esc; the file view and the search field do not.
- Ctrl+Z right after a delete undoes it like the toast's Undo button.
  Star/Unstar in the item menu, or the star in a list row, saves the item in Starred
  across app restarts. The list lives under the current user's AppData.
  The rename and new-folder popovers open centred just below the selected item's row.
- Recent lists recently opened items. Recent and Starred show each item's location
  and offer Open Item Location in the item menu.
- Trash shows the Recycle Bin with Original Location and Date Deleted columns.
  Its bar shows an item count, Restore for the selection and Empty Trash.
  Empty Trash and Delete Permanently require confirmation.
- Other Locations lists drives with a CUI pattern meter of the used space (one part, "Used", in the accent, danger
  above 90% used; no legend). Its readout sits on the name line: used/total in the unit of the drive's total (TB from
  1 TB, GB from 1 GB, else MB; on hover the used size and share). The name gives way to it with an ellipsis. A drive
  with a meter is 64 px tall. Clicking a row, the meter included, opens the drive.
- A file that is still being copied into the shown folder is listed under its final name, dimmed, with
  "Copying..." in the size column (list) and no thumbnail. The copy writes to a hidden temporary name
  `.<name>.dfm-copy-<pid>-<n>.tmp`; the listing shows such a file only while the copying process is alive and
  is this program, and hides it as soon as the real file exists. The folder never reads "Only hidden items are
  here" for it. Open, copy and trash skip it. The shown folder (the paste folder or one inside it) relists on
  its own when a copy or move starts a file, shortly after, once more after a conflict question is answered,
  and when the job ends. The selection, cursor and anchor are read when the new listing replaces the old one
  and kept by name (sorted, binary search); scroll stays. A file name longer than 200 UTF-16 units is copied
  under a temp name without it, so such a file is not shown while it copies (a deliberate limit).
- Main menu, **Use as Default File Manager** (check mark when on, like Show Hidden Files): makes this app the file
  manager Windows opens for folders, for the current user only, with no administrator rights. Folders opened
  by other programs and Win+E open here at the right place: a folder, a drive, This PC, Trash, Recent, Home (Win+E),
  a `shell:` place that is a real folder (for example `shell:Downloads`) and `explorer /select,<file>`
  (the folder opens with the file selected). Anything it cannot show (Control Panel, `ms-settings:`, a missing
  folder, an unknown `shell:` or `::{CLSID}` place) goes to Windows Explorer, once per 2 seconds at most so a
  place can never loop. Turning it off puts back every value that was there before, exactly. If the app's file is
  deleted, the next folder open restores Explorer's keys and hands the request to Explorer.
  Win+E and other programs start a copy of the app, the launcher (`%LOCALAPPDATA%\dio-file-manager`). Only the
  registered app, the exe whose full path is the saved App path (compared after normalising the path), keeps it
  current: at each start it compares the launcher byte by byte (not only the size) beside the window, and replaces it
  through a temporary name when it differs, and it also adds a missing open verb. Any other copy (a test build, a
  worktree build, a moved exe) leaves the launcher and the keys alone; turning the switch on from that copy
  registers it. Its start-up cost on the UI thread is about 0.1 ms.
  Win+E is registered on both verbs of Windows' Win+E class, `opennewwindow` and `open` (Windows' own copy sends
  `open`, the verb of a launch without a verb, to Explorer). That is not enough on Windows 11 build 26300:
  Explorer's shell handles Win+E itself and never runs the class's verb, also with window preloading off. So
  the switch also starts a small copy of this exe that runs all the time (below).
  **Win+E process.** Turning the switch on writes three things and starts one process; turning it off puts the
  values back at once, and the process follows at the next Explorer restart (the bridge below).
  - `DisabledHotkeys` (`HKCU\...\Explorer\Advanced`) gains the letter E: Explorer then leaves Win+E alone. The old
    value, its type or its absence is saved once under the record and put back exactly; a record that exists is
    never replaced. Explorer reads this value only when it starts.
  - A Run entry, `Dio File Manager`, starts `"<launcher>" --resident` at every sign-in.
  - The process (`src/resident.c`, same exe, run from the launcher path) has one hidden top-level window, no CUI
    and no COM. It calls `RegisterHotKey(Win+E)`. Explorer holds Win+E until it restarts, so the call is refused
    (error 1409) at first; the process tries again whenever Explorer says it restarted (the `TaskbarCreated`
    message), and at sign-in the first try works. `WM_HOTKEY` starts the registered app with the launcher's own
    arguments, so the window is the one a Win+E verb would have opened.
  - The app never restarts Explorer. Explorer reads `DisabledHotkeys` only when it starts, so after turning the
    switch on Win+E changes owner when Explorer restarts or the user signs in again (the turn-on notice says so).
    The same holds in reverse, and that is why turning off does not end the process.
  - **The bridge (turn-off, or the app gone).** When the process holds Win+E and the switch is turned off, Explorer
    may have started with E in `DisabledHotkeys` and so skips Win+E until its next start. The process therefore
    stays on and only changes its job: `WM_HOTKEY` opens Windows Explorer's own window in Home (each press, no loop
    guard), and when the Explorer that ran at turn-off ends (the process waits on it, so idle CPU stays 0) it
    releases the hotkey and ends, before the new Explorer starts: the new one registers its hotkeys while it
    starts, before it says `TaskbarCreated`, and does not try again, so letting go at `TaskbarCreated` would leave
    Win+E with nobody. `TaskbarCreated` alone proves nothing (it is also sent when the display scale changes); a
    bridge that found no Explorer at turn-off watches the one the first `TaskbarCreated` names. A sign-out ends it
    too, and the Run entry is gone, so nothing starts it again.
    The app tells the process through a window message (`resident_release`); a process that never got the hotkey
    (Explorer still owns Win+E) just ends. The same bridge starts when the process finds the app gone: the keys go
    back, this press opens Explorer's window, and it ends with the Explorer that ran then; when a shell command cannot be
    put back it stays as it is, with E and the Run entry kept, and tries again at the next press. Turning the switch on again
    replaces a bridge by a fresh process (`resident_end_bridge`).
  - **The launcher after a turn-off.** With no process the launcher is deleted at once. A bridge runs from it, so
    the delete waits: a `RunOnce` entry (`Dio File Manager launcher cleanup`) starts the registered app as
    `"<app>" --cleanup-launcher` at the next sign-in. That is a GUI-subsystem exe that returns before any window
    exists, so nothing shows; it deletes the launcher only while the switch is off and no process runs from it
    (a turn-on removes the entry). When the app is gone there is nothing to run it with, so the bridge deletes the
    launcher itself, a few seconds after it ends at the Explorer restart; if the user signs out before any
    Explorer restart, that one 0.5 MB copy stays in `%LOCALAPPDATA%\dio-file-manager` and nothing uses it.
  - An install or launcher sync while the process runs from the launcher: the registered app notices the launcher
    differs, stops the process (a running exe cannot be replaced), copies the new build over the launcher and
    starts the process again, so it runs the new code. An install made before this existed gets the keys and the
    process at the first start behind the same registered-app gate.
  - Budgets, each a check in `tests/use/default_fm_smoke.py` (24): process working set at most 5 MB (measured
    about 2 MB); idle CPU 0 (`cpu_budget.py --max 0`, five 30-second windows all pass); a Win+E (posted
    `WM_HOTKEY`) shows the window at most 50 ms later than a plain launcher start (measured 36 ms against 56 ms:
    it saves the launcher's own start); exe at most 10,240 bytes larger than before (measured +6,656). The
    start-time budget did not exist before this change.
  - In a check the process takes Win+Ctrl+Alt+Shift+F23 instead: Win+E belongs to the user's Explorer on every
    desktop of the session, so a private test desktop cannot hold it.
  Limit: "Show in folder" from other programs (`SHOpenFolderAndSelectItems`) opens the folder here but does not
  select the file, and `explorer.exe /select,...` started directly by a program cannot be redirected without
  administrator rights.
- Paste onto a name that exists asks: Replace, Skip or Keep Both, with "Apply to all".

## Colours (Halo, from `tokens.json`)

| Role | Light | Dark | CUI token |
|---|---|---|---|
| Sidebar (page) `bg` | #EDF0F8 | #060810 | BACKGROUND |
| Content pane `bg-elevated` | #F7F8FC | #10131C | SURFACE |
| Fields, popovers `bg-overlay` | #FFFFFF | #171B27 | SURFACE_RAISED |
| Text `fg` | #0E1424 | #EEF1FA | FOREGROUND |
| Secondary `fg-muted` | #4A5570 | #B1B8CF | TEXT_SECONDARY |
| Hints `fg-subtle` | #7A849C | #767E9C | TEXT_MUTED |
| Hairline `line` | #0F172A at 9% | #FFFFFF at 9% | BORDER_SUBTLE, BORDER |
| Control edge `line-strong` | #0F172A at 16% | #FFFFFF at 16% | BORDER_STRONG |
| Hover | fg at 6% | fg at 6% | HOVER |
| Selected | Accent at 16% over content | Accent at 26% over content | ACTIVE |
| Accent | #5B63E6 | #8AB4FF | ACCENT |
| Text on accent | #FFFFFF | #0A0D1A | ACCENT_FOREGROUND |
| Success, warn, danger, live | #0D9488, #D97706, #E11D48, #0284C7 | #5EEAD4, #FBBF24, #FC909E, #7DD3FC | SUCCESS, WARNING, DANGER, INFO |

The app follows the system's light or dark setting by default; the main menu can pin one.
Radii: controls 10, menus and popovers 14, dialogs 20.

## Icon contrast

File-type icons in the list and Details views (below 40 px) are drawn as a 1.25 px outline plus a solid fold,
in an ink mixed from the icon's colour toward the text colour until it reaches 4.5:1 against the page, row,
raised and selected backgrounds (`ui_icon_ink`). `view_smoke.py` case 8 measures it in both themes.

## Type

Dio's interface font is Segoe UI Variable Text. Load it from `C:\Windows\Fonts` at run time
(no bytes in the exe); keep CUI's embedded Inter as the fallback when it cannot be loaded.
Persian and Arabic file names need CUI's `CUI_TEXT_SHAPING` and a font with those letters:
Vazirmatn when it is installed (Dio's Persian font), else Segoe UI. Body text 14 px.

## Size budget

The release exe is at most 800 KB (CUI's own hello app is about 250 KB). The build fails over it.

## Memory budget

Folder sizes keep an in-memory cache (`src/foldersize.c`, nothing is written to disk): at most 65,536 entries and 24 MB for
their path keys and records (an entry is about 80 bytes plus its path), about 1 MB for the hash table, and 8 walk buffers of
128 KB, one per worker. The oldest entries leave first, never one that is shown or being measured, so a shown folder may keep
the cache above its cap until it is left. Budget, Details view, 1280x800, all sizes in: working set at most 120 MB for a
small folder and at most 175 MB for E:\w (539,000 folders; the cache sits at its cap, the rest is the walk and the window).
Measured: 106 to 110 MB for E:\w\dio-file-manager, 164 MB for E:\w.

Time and CPU budgets, each a check that fails over it: entering E:\w, every child size is in within 30 s (9.4 s with 8 workers
on this 16-core machine, 40 s with 4; a walk is below-normal priority and costs about 55 CPU-seconds once); idle CPU below
0.1 % of one core when no measurement runs (0.0 %, also with a writer of 20 writes a second on the drive); the longest
message delay of the window below 50 ms.

## Window: the header is the title bar

- **Drag and resize.** Empty header space is the caption: drag moves the window, double click
  maximizes or restores, right click opens the system menu, Alt+Space too. Every header control
  (menu, back, forward, path bar or search field, search, view options, the job ring)
  is marked and never drags. The top 5 px and the corners resize; the other edges resize through
  Windows' own frame, which is only 8 px wide, drawn in the hairline colour (no Windows grey).
- **Narrow windows.** The window buttons never leave the window (`plan_header` in `src/ui_chrome.c`
  budgets the content header's width). The path bar takes the free width and shrinks first, down to 144 px,
  then search moves into the main menu (Search Current Folder). View options stay in the header (the grid and list choice
  is inside its menu, so nothing else leaves); Back and Forward never
  leave it in a wide window. Below 683 px the sidebar is a sheet, Back, Forward and view options sit in
  the bottom bar, and the main menu
  button sits in the content header, left of the window buttons. The menu scrolls when the window is
  too short for it. About 32 px of empty header stay for dragging while the path bar is above 144 px.
- **Window buttons** sit at the right end of the content header, 12 px after the view-options
  button: minimize, maximize or restore, close. Each is a 24 px circle with a 16 px glyph, as in
  libadwaita. The circle is always there: the foreground colour at 10% at rest, 15% on hover, 20%
  pressed, in both themes. All three glyphs have the same stroke width (the diagonals of the close
  glyph are drawn a little thinner so it does not look heavier). The maximize button answers
  Windows' hit test (`HTMAXBUTTON`), so Windows 11's snap layouts open from it.
- **Inactive window.** The header icons, the glyphs and the circles dim; the content does not.
  The main menu icon is dimmed by a click-through veil because CUI gives that icon no colour option.
- **Maximized.** The client area is the monitor's work area; the frame Windows overhangs is cut off,
  so our header sits at the screen's top edge.
- **Border.** `DWMWA_BORDER_COLOR` is the hairline colour for the theme; the immersive dark mode
  flag follows the theme so the system menu matches.

## Icon

A folder tile in the accent colour, drawn by `design/icon/build_icon.py` from `design/icon/*.svg`
into `src/dio-file-manager.ico` (16, 20, 24, 32, 48, 64, 128 and 256 px). The small frames have a
1.5 px margin around the tile and a folder that fills 80% of its width so they stay readable.
The .ico is embedded through `src/dio-file-manager.rc`; `chrome_win.c` also sends it as the
window's big and small icon (again after a DPI change). It is a temporary icon until Omid supplies one.

