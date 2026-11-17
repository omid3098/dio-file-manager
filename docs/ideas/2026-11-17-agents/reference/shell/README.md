# Dio File Manager mock shell

Open `index.html` directly. No build, package install, or server.
Copy `index.html`, `shell.css`, and `shell.js`, then replace the demo script.
Or start in `round/S01/index.html` and link these shared files:
```html
<link rel="stylesheet" href="../reference/shell/shell.css">
<div class="dio-stage"><div id="shell"></div></div>
<script src="../reference/shell/shell.js"></script>
<script>
const shell = DioShell.create('#shell', {
  path: 'Home\\Downloads',
  files: [{name:'قرارداد اجاره.pdf',kind:'file',size:524288,date:'Yesterday'}]
});
</script>
```

## Helpers
- `DioShell.create(elementOrSelector, options)` mounts a window and try strip.
- Options: `path`, `files`, `addedFolders` (names), `drives` (below), `trashCount`.
- `onOpen(file, shell)` handles opening a non-folder; otherwise a mock dialog opens.
- `shell.setFiles(files)` replaces the current array; names must be unique strings.
- File: `{name, kind:'file'|'folder', size, date, files?, thumbnail?}`.
- Size is bytes (number), a preformatted string, or omitted. Date is display text.
- `files` on a folder supplies its children. `thumbnail` is an image URL/data URL.
- `shell.registerFolder(path, files)` supplies another sidebar/path destination.
- `shell.setFolder(path, files?)` navigates, recording Back/Forward history.
- `shell.select(nameOrNames)` selects by name; `[]` clears selection.
- `shell.setView('grid'|'details'|'list')` changes layout without losing selection.
- `shell.openMenu(clientX, clientY, items)` positions and clamps a menu at the pointer.
- Menu item: `{label, icon?, shortcut?, disabled?, action?}`; separator: `'separator'`.
- `shell.toast(text, {action:'Undo', onAction, duration:6000})` shows a toast.
- Use `action:null` for no action, `duration:0` to keep it until dismissed.
- `shell.popover(anchorElementOrPoint, {title, content})` shows a popover.
- Content is a DOM Node or plain text; a point is `{x,y}` in client coordinates.
- `shell.dialog({title,message,actions:[{label,action?}]})` shows a modal dialog.
- `shell.closePopup()` dismisses the menu, popover, or dialog. Escape does too.
- `shell.setHint('روی یه فایل کلیک کن.', 1, 3)` sets the Persian try strip/counter.
- `shell.setTheme('light'|'dark'|'system')` sets `data-theme` on the root.
- `shell.files`, `.selected`, `.folder`, `.element` expose the current mock state.
- `shell.destroy()` removes the shell and its document listeners/timers.
- `DioShell.icon(name, size=16)` returns inline Lucide SVG; `formatSize(bytes)` formats bytes.
- Drives: `{name,used:84,removable:false}`; `used` is percent; omit it for no meter.
- Mount emits `dio:folder` and `dio:select` DOM events on the host, with state in `detail`.

## Rules
- Page: 1440 × 900. Window: 1180 × 740, sidebar 240, header 46, Details rows 36 px.
- App interface stays English. Persian filenames keep their own direction/font.
- Try strip stays outside the window, in spoken Persian, RTL, with Vazirmatn.
- Default theme follows the system. All transient surfaces use 150 ms motion.
- Controls 10 px radius; menus/popovers 14; dialogs 20; icon stroke 1.75 px.
- Fonts come only from Google Fonts: Inter and Vazirmatn; no icon CDN dependency.
- Operations only change mock arrays. There is no disk, clipboard, or native app access.
- Demo links expose Grid, Details, Menu, Popover, and Dialog for isolated captures.
- `checks.md` records capture results and the limits versus the native app.
