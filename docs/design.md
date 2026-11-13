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
  show hidden files).
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
  or a Lucide type icon; image files show a thumbnail once thumbnails exist. Zoom with Ctrl+plus
  and Ctrl+minus: 48, 64, 96 and 128 px icons.
- **Selection:** the icon gets a rounded `selected` fill; the name gets an accent pill with
  `accent-fg` text. Click selects, Ctrl+click toggles, Shift+click selects a range, dragging on
  the empty background draws a rubber band.
- **List view:** columns Name (16 px icon and name), Size, Modified, Type; sortable headers;
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
  starts the search as well; Esc ends it.
- Ctrl+1 grid, Ctrl+2 list; Ctrl+H hidden files; F5 reload; Ctrl+A select all.
- F2 renames in a small popover under the item ("Rename" field and button); Ctrl+Shift+N makes a
  folder through a popover ("New Folder" name field and Create).
- Ctrl+C, Ctrl+X, Ctrl+V copy, cut and paste through the Windows clipboard, so files move between
  this app and Explorer both ways.
- Delete moves to the Recycle Bin and shows a toast "“name” moved to Trash" with Undo.
  Shift+Delete deletes for ever after a confirm dialog.
- Right click on an item: Open, Open With…, Cut, Copy, Rename, Move to Trash, Star, Copy Path,
  Properties. On the background: New Folder, Paste, Select All, Open in Terminal, Properties.
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
| Selected | fg at 10% | fg at 10% | (selected fill) |
| Accent | #5B63E6 | #8AB4FF | ACCENT |
| Text on accent | #FFFFFF | #0A0D1A | ACCENT_FOREGROUND |
| Success, warn, danger, live | #0D9488, #D97706, #E11D48, #0284C7 | #5EEAD4, #FBBF24, #FC909E, #7DD3FC | SUCCESS, WARNING, DANGER, INFO |

The app follows the system's light or dark setting by default; the main menu can pin one.
Radii: controls 10, menus and popovers 14, dialogs 20.

## Type

Dio's interface font is Segoe UI Variable Text. Load it from `C:\Windows\Fonts` at run time
(no bytes in the exe); keep CUI's embedded Inter as the fallback when it cannot be loaded.
Persian and Arabic file names need CUI's `CUI_TEXT_SHAPING` and a font with those letters:
Vazirmatn when it is installed (Dio's Persian font), else Segoe UI. Body text 14 px.

## Size budget

The release exe is at most 800 KB (CUI's own hello app is about 250 KB). The build fails over it.
