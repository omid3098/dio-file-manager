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
  as in libadwaita's split view. The Windows title bar stays above them unless CUI can extend the
  client area into it; the header rows are ours either way.
- **Sidebar header:** the main menu button (`menu` icon) at its right end.
- **Content header:** back and forward on the left; the path bar in the middle (CUI `cui_path_bar`,
  a home icon on the first segment when inside Home, a menu on each chevron; Ctrl+L or a click on
  the empty part turns it into a text field); on the right a search toggle, a view switch
  (grid or list) and a view-options menu (sort by name, size, modified or type; reversed order;
  show hidden files). Its first control is a six-stop view slider: Details at the left, then List,
  Small icons, Medium icons, Large icons and Extra large icons at the right. The current stop is
  named under the slider. Six ticks mark its discrete positions; the thumb snaps to each stop.
  The view-options surface is opaque, including while opening over file icons.
- **Sidebar rows:** Recent, Starred, Home, Desktop, Documents, Downloads, Music, Pictures, Videos,
  Trash; a hairline; each drive by its label and letter ("Local Disk (C:)"), with an eject button
  for removable drives; "Other Locations" last (all drives with their used and free space).
  Rows are 36 px, a 16 px Lucide icon and 14 px text, radius 10, 6 px inside the pane edge.
  The current place is filled with the `selected` colour; hover with `hover`.
  Trash shows an item-count badge when nonempty. CUI 1.4.0 has no empty-bin icon,
  so the icon stays the same when the bin is empty or full.
- **Grid view (default):** cells about 112 px wide; a 64 px icon above the name; the name centred,
  up to two lines then cut short with an ellipsis (whole name in a tooltip, and in full while
  selected). Folders are drawn filled in the accent colour (a back flap a step darker), not as
  outline icons. Files are a page with a folded corner, with a small type label ("PDF", "JPG")
  or a Lucide type icon; image files show a thumbnail (aspect kept, rounded corners, soft shadow) once it
  is read, only for the cells in view, with the page icon until then. The icon stops use 32, 64,
  96 and 144 px icons; Medium (64 px) is the default. Small puts the name beside the icon.
  Thumbnail requests match the displayed edge, including the display scale.
- **List stop:** 16 px icons and names in 240 px columns, filled top to bottom then left to right.
  The wheel scrolls sideways. Only visible columns are built. A persistent 6 px horizontal
  scrollbar at the bottom uses the vertical scrollbar's colour and rounded ends, and can be dragged.
  Columns have 16 px top and bottom space. The header's list button is active in List and Details;
  the grid button is active at the four icon stops.
- **View shortcuts:** Ctrl+wheel and Ctrl+plus/minus move one stop; Ctrl+0 returns to Medium.
  Wheel events retain their modifier state even when Ctrl is released before the next frame.
  The grid button (Ctrl+1) returns to the last icon stop; the list button (Ctrl+2) opens Details.
  The current and last icon stops survive restarts in `%APPDATA%\dio-file-manager\view.txt`.
  One background writer saves the latest stop and finishes pending writes on exit, so
  dragging the slider never waits for a settings-file flush.
  Search, Recent, Starred and Trash share this scale.
  Presentation keeps CUI's normal event-driven display pacing. Idle windows wake only for
  input, timers or worker progress. Recycle Bin badge queries run on a worker.
- **Selection:** the icon gets a rounded `selected` fill; the name gets an accent pill with
  `accent-fg` text. Click selects, Ctrl+click toggles, Shift+click selects a range, dragging on
  the empty background draws a rubber band.
  A background drag replaces the selection live with every cell or row the box touches;
  Ctrl or Shift adds to the selection saved at press time. Shrinking the box removes
  items outside it again. The box has an 18% accent fill and a one-device-pixel accent
  border in both themes. Holding it within 32 px of the top or bottom scrolls vertically;
  List uses the left and right edges. Its anchor stays in the document while scrolling.
  A background click clears the selection; item presses and right clicks keep their
  normal actions. The same selection works at every scale, in search and special places.
- **Details stop:** columns Name (16 px icon and name), Size, Modified, Type; sortable headers;
  folders first. Rows 36 px.
- **Floating status pill:** bottom right of the content pane, over the files, only while something
  is selected or loading: "“report.pdf” selected (2.4 MB)", "3 items selected (5.1 MB)",
  "Loading…". It floats, so it has the popover shadow.
- **Empty folder:** a large muted folder icon and "Folder is Empty", centred.
- **Unreadable folder:** a large muted icon, "Unable to access “Name”" and the reason.
- **Narrow window (< 640 px):** the sidebar leaves; a sidebar button appears at the left of the
  content header and opens it as an overlay.

## Behaviour (as in GNOME Files)

- Double click or Enter opens: a folder in place, a file with its default app.
- Alt+Left and Alt+Right go back and forward; Alt+Up goes to the parent; Alt+Home goes Home.
- Ctrl+L types a path; Ctrl+F searches below the current folder; typing a letter in the view
  starts the search as well (only while the file view has the keyboard: not in the path field, the
  rename or new-folder popover, the search field or an open menu); Esc ends it and returns to the folder.
  Opening search again restores keyboard focus to its field, including after visiting special places.
  The search field takes the place of the path bar; the search runs 0.25 s after the last key, below
  the folder, at most 10,000 results. Results list in the grid or the list; the list adds a Location
  column with the folder of the result relative to the searched folder. No match shows "No Results
  Found". Results open and take the item menu, with Open Item Location.
  Moving a result to Trash keeps the query open and removes missing results. Undo restores the
  file and refreshes the same search; Esc returns to a refreshed folder listing.
- Ctrl+1 grid, Ctrl+2 list; Ctrl+H hidden files; F5 reload; Ctrl+A select all.
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
- Other Locations lists drives with a usage bar and free/total space. Clicking one opens it.
- A file that is still being copied into the shown folder is listed under its final name, dimmed, with
  "Copying..." in the size column (list) and no thumbnail. The copy writes to a hidden temporary name
  `.<name>.dfm-copy-<pid>-<n>.tmp`; the listing shows such a file only while the copying process is alive and
  hides it as soon as the real file exists. The folder never reads "Only hidden items are here" for it. Open,
  copy and trash skip it. The folder relists on its own when a copy or move into it starts a file and shortly
  after, keeping selection and scroll.
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
