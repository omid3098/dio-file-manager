"""Twelve action checks through Win32 input, on correct/hidden.py's desktop.

Brokered Open With and Terminal use an explicit stand-in: never activate an
existing shell/terminal on the user's desktop. Properties uses the real sheet.
"""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('DFM_USE_WORK', str(ROOT / '.agent-work/use-checks' / Path(__file__).stem))
from input_smoke import App, ROOT, WORK, SAND, U, K, ENUM, fresh, preserved_clipboard

U.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
U.DestroyWindow.argtypes = [W.HWND]
U.EnumChildWindows.argtypes = [W.HWND, ENUM, W.LPARAM]
U.IsWindowVisible.argtypes = [W.HWND]
U.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
U.GetThreadDesktop.argtypes = [W.DWORD]
U.GetThreadDesktop.restype = W.HANDLE
U.GetUserObjectInformationW.argtypes = [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]
U.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
U.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]


def desktop(tid):
    name = C.create_unicode_buffer(256)
    assert U.GetUserObjectInformationW(U.GetThreadDesktop(tid), 2, name, C.sizeof(name), None)
    return name.value


def windows():
    found = []
    def each(h, _):
        if U.IsWindowVisible(h):
            title, cls = C.create_unicode_buffer(2048), C.create_unicode_buffer(256)
            U.GetWindowTextW(h, title, len(title))
            U.GetClassNameW(h, cls, len(cls))
            found.append((h, title.value, cls.value))
        return True
    U.EnumWindows(ENUM(each), 0)
    return found


STANDIN_WAIT = float(os.environ.get('DFM_STANDIN_WAIT', 45))   # seconds a started stand-in may take to show its window: a busy computer starts a process late
CURRENT = WORK/'current-check.txt'   # the name of the check that is running; empty between checks


def native_window(a, predicate):
    found = []
    def ready():
        found[:] = [row for row in windows() if predicate(row)]
        return bool(found)
    a.until('Expected native window missing', ready, STANDIN_WAIT)
    h, title, cls = found[0]
    actual = desktop(U.GetWindowThreadProcessId(h, None))
    assert actual == desktop(a.tid) and actual.startswith('correct-hidden-'), (title, actual)
    return h, title, cls


def close_native(a, h):
    U.PostMessageW(h, 0x10, 0, 0)
    a.until('Native window did not close', lambda: not U.IsWindow(h))


def menu(a, row, item=True):
    if item:
        a.rclick(*a.cell_xy(0))
    else:
        a.rclick(700, 420)
    assert a.snap()['ctx'], 'Context menu did not open'
    for _ in range(row):
        a.key(40)
    a.key(13)


def click_rect(a, index):
    x, y, w, h = a.snap()['action_rects'][index]
    assert w > 0 and h > 0, ('Button has no hit area', index, x, y, w, h)
    a.click(x+w/2, y+h/2)


def finished(a):
    a.until('Operation did not finish', lambda: not (a.snap()['act'] & 2), 15)


class Probe:
    """Asks the window a question every 10 ms from its own thread and keeps the longest wait for an answer.
    A window whose thread is busy (a process start on the UI thread, say) answers late: that wait is the number."""
    def __init__(self, hwnd):
        self.hwnd, self.longest, self.count, self.stop_flag = hwnd, 0.0, 0, False
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        result = C.c_size_t()
        while not self.stop_flag:
            start = time.monotonic()
            U.SendMessageTimeoutW(self.hwnd, 0, 0, 0, 0, 10000, C.byref(result))   # WM_NULL, no abort: a long block is a number
            self.longest = max(self.longest, time.monotonic()-start)
            self.count += 1
            time.sleep(.01)

    def __enter__(self):
        self.thread.start(); return self

    def __exit__(self, *_):
        self.stop_flag = True; self.thread.join(15)


def burners(n):
    """n processes that each keep one core busy: a computer that is doing other work."""
    return [subprocess.Popen([sys.executable, '-c', 'while True: pass'], creationflags=subprocess.CREATE_NO_WINDOW)
            for _ in range(n)]


def standin():
    """A real native window recording exactly what the shell child received."""
    kind, *args = sys.argv[sys.argv.index('--standin')+1:]
    time.sleep(float(os.environ.get('DFM_STANDIN_SLOW', 0)))   # rehearses a process that starts slowly, as on a busy computer
    mine = os.environ.get('DFM_STANDIN_CHECK', '')
    def stale():
        """A stand-in that starts after its check ended or gave up belongs to nobody: it must not hold the sandbox as its
        working folder, nor overwrite the marker or show a window the next check would find."""
        try:
            return CURRENT.read_text(encoding='utf-8') != mine
        except OSError:
            return True
    if stale():
        return 0
    marker = WORK / (kind+'-launch.json')
    K.GetCommandLineW.restype = W.LPWSTR
    record = dict(kind=kind, args=args, cwd=os.getcwd(), command=K.GetCommandLineW(),
                  desktop=desktop(K.GetCurrentThreadId()), pid=os.getpid())
    marker.write_text(json.dumps(record, indent=2), encoding='utf-8')
    U.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_int, C.c_int,
                                C.c_int, C.c_int, W.HWND, W.HMENU, W.HINSTANCE, C.c_void_p]
    U.CreateWindowExW.restype = W.HWND
    h = U.CreateWindowExW(0, 'STATIC', 'DFM shell stand-in '+kind+' '+args[-1],
                          0x10CF0000, 100, 100, 800, 180, None, None, None, None)
    assert h, C.get_last_error()
    msg = W.MSG()
    started = time.monotonic()
    while U.IsWindow(h):
        if time.monotonic()-started > 120 or stale():
            U.DestroyWindow(h)
            break
        if U.PeekMessageW(C.byref(msg), None, 0, 0, 1):
            U.TranslateMessage(C.byref(msg)); U.DispatchMessageW(C.byref(msg))
        else:
            time.sleep(.01)
    return 0


def put_cut(path):
    """Another file manager cuts a file: CF_HDROP plus Preferred DropEffect = move. No window of ours owns the data, so
    the program that replaces it later does not have to wait for our window (Windows asks the owner to let go first)."""
    from clipboard_guard import open_clipboard, U, K
    K.GlobalAlloc.argtypes = [W.UINT, C.c_size_t]
    K.GlobalAlloc.restype = W.HGLOBAL
    U.RegisterClipboardFormatW.argtypes = [W.LPCWSTR]
    def blob(data):
        h = K.GlobalAlloc(2, len(data))
        C.memmove(K.GlobalLock(h), data, len(data)); K.GlobalUnlock(h)
        return h
    files = blob(struct.pack('<IiiII', 20, 0, 0, 0, 1)+(str(path)+'\0\0').encode('utf-16-le'))
    effect = blob(struct.pack('<I', 2))
    open_clipboard(None)
    try:
        assert U.EmptyClipboard() and U.SetClipboardData(15, files) and U.SetClipboardData(U.RegisterClipboardFormatW('Preferred DropEffect'), effect), C.get_last_error()
    finally:
        U.CloseClipboard()


def hold_then_write(base, ready, text):
    """Another program: holds the clipboard open, then replaces its contents with text, in a fixed order with the app.
    It arms the app's test handshake (three named events, DFM_TEST_CLIP_HANDSHAKE) and sets `ready`. It then waits for
    "blocked", which the app sets at its first refused OpenClipboard, so the app is certainly waiting for us. It writes
    and closes the clipboard and sets "released", and the app goes on. If the app's wait for "released" ran out, it sets
    "timedout" and this fails: then the app did not wait for the write, and the check proves nothing. No timing is
    guessed: every wait is for a signal and is bounded."""
    from clipboard_guard import open_clipboard, U, K   # the wrappers whose argument and result types are set
    K.GlobalAlloc.argtypes = [W.UINT, C.c_size_t]
    K.GlobalAlloc.restype = W.HGLOBAL
    K.CreateEventW.argtypes = [C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR]
    K.CreateEventW.restype = W.HANDLE
    K.SetEvent.argtypes = [W.HANDLE]
    K.CloseHandle.argtypes = [W.HANDLE]
    K.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
    K.WaitForSingleObject.restype = W.DWORD
    blocked = released = timedout = None
    try:
        open_clipboard(None)
        try:
            blocked = K.CreateEventW(None, True, False, base+'-blocked')
            released = K.CreateEventW(None, True, False, base+'-released')
            timedout = K.CreateEventW(None, True, False, base+'-timedout')
            assert blocked and released and timedout, C.get_last_error()
            ready.set()
            # the click, the move and the window thread reaching the clipboard: a loaded machine can take seconds
            assert K.WaitForSingleObject(blocked, 30000) == 0, 'The app never waited for the clipboard'
            data = (text+chr(0)).encode('utf-16-le')
            h = K.GlobalAlloc(2, len(data))
            C.memmove(K.GlobalLock(h), data, len(data)); K.GlobalUnlock(h)
            assert U.EmptyClipboard() and U.SetClipboardData(13, h), C.get_last_error()
        finally:
            U.CloseClipboard()
            if released: K.SetEvent(released)   # always let the app go on, also when this program failed
        assert K.WaitForSingleObject(timedout, 0) != 0, 'The app gave up waiting for the clipboard write: the order is not proven'
    finally:
        ready.set()
        for e in (blocked, released, timedout):
            if e: K.CloseHandle(e)


def put_text(text, hold=0):
    """Another program: puts text on the clipboard, and keeps the clipboard open for `hold` seconds before it closes it."""
    from clipboard_guard import open_clipboard, U, K
    K.GlobalAlloc.argtypes = [W.UINT, C.c_size_t]
    K.GlobalAlloc.restype = W.HGLOBAL
    data = (text+'\0').encode('utf-16-le')
    h = K.GlobalAlloc(2, len(data))
    C.memmove(K.GlobalLock(h), data, len(data)); K.GlobalUnlock(h)
    open_clipboard(None)
    try:
        assert U.EmptyClipboard() and U.SetClipboardData(13, h), C.get_last_error()
        time.sleep(hold)
    finally:
        U.CloseClipboard()


def clipboard_state(hwnd):
    """(text or None, a file list is on the clipboard)"""
    from clipboard_guard import open_clipboard, U, K
    U.IsClipboardFormatAvailable.argtypes = [W.UINT]
    open_clipboard(hwnd)
    try:
        h = U.GetClipboardData(13)
        text = None
        if h:
            p = K.GlobalLock(h); text = C.wstring_at(p); K.GlobalUnlock(h)
        return text, bool(U.IsClipboardFormatAvailable(15))
    finally:
        U.CloseClipboard()


TOAST_FILES = ('a-delete-me.txt', 'پروندهٔ بسیار بلند با نامی که جا نمی‌شود در یک خط کوچک و دراز.txt', 'b.txt', 'c.txt', 'd.txt')


def inside():
    results = []
    command = subprocess.list2cmdline([sys.executable, str(Path(__file__).resolve()), '--standin'])
    os.environ['DFM_TEST_OPEN_WITH'] = command+' open-with'
    os.environ['DFM_TEST_TERMINAL'] = command+' terminal'
    os.environ['DFM_TEST_OPEN'] = command+' unexpected-open'
    os.environ['DFM_TEST_CLIP_HANDSHAKE'] = 'Local' + chr(92) + 'DfmClipHandshake-%d' % os.getpid()  # the apps read it in test mode

    def check(name, fn, files=('action target.txt',), **app_args):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        app = None
        CURRENT.write_text(name, encoding='utf-8')
        os.environ['DFM_STANDIN_CHECK'] = name   # the app and the stand-in it starts inherit it
        try:
            fresh(*files)
            app = App(name, SAND, **app_args)
            fn(app)
            app.snap(name+'-passed')
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if app:
                try:
                    app.snap(name+'-failed')
                except Exception:
                    pass
            results.append(dict(check=name, result='FAILED', reason=repr(e)))
        finally:
            if app:
                app.close()
            CURRENT.write_text('', encoding='utf-8')   # any stand-in still starting leaves at once
        print(json.dumps(results[-1]), flush=True)

    def launch(a, kind, row, item, answer=None):
        """`answer`: (label, limit) the window must answer within `limit` seconds the whole time the menu item runs."""
        marker = WORK/(kind+'-launch.json')
        marker.unlink(missing_ok=True)
        probe = Probe(a.hwnd) if answer else None
        if probe: probe.__enter__()
        try:
            menu(a, row, item)
            h, title, cls = native_window(a, lambda r: r[1].startswith('DFM shell stand-in '+kind))
        finally:
            if probe:
                probe.__exit__()
                (WORK/(answer[0]+'-answer.json')).write_text(json.dumps(
                    dict(longest_ms=round(probe.longest*1000, 1), asks=probe.count, limit_ms=answer[1]*1000)), encoding='utf-8')
        if probe:
            assert probe.longest < answer[1], f'The window did not answer for {probe.longest*1000:.0f} ms while the item ran (limit {answer[1]*1000:.0f})'
        try:
            record = json.loads(marker.read_text(encoding='utf-8'))
            target = SAND/'action target.txt'
            assert record['desktop'] == desktop(a.tid), record
            if kind == 'terminal':
                assert record['args'] == ['-d', str(SAND)+'\\.'], record
                assert Path(record['cwd']) == SAND, record
            else:
                assert record['args'] == [str(target)], record
            assert str(target if item else SAND) in title, title
        finally:
            close_native(a, h)

    def control(a, label):
        """The window's own longest wait while it only opens and closes the menu: what frames and a busy computer cost
        without any terminal. Open in Terminal gets that plus 150 ms."""
        with Probe(a.hwnd) as probe:
            for _ in range(8):
                a.rclick(700, 420)
                assert a.snap()['ctx'], 'Context menu did not open'
                a.key(27)
        (WORK/(label+'-control.json')).write_text(json.dumps(dict(longest_ms=round(probe.longest*1000, 1))), encoding='utf-8')
        return probe.longest + .15

    def quiet(a):
        launch(a, 'terminal', 4, False, ('2-terminal', control(a, '2-terminal')))

    def slow_loaded(a):
        """A slow process start (1.5 s, as on a busy computer) on one busy process per core: the window must still
        answer at once, because the app never starts the terminal on its window's thread."""
        busy = burners(os.cpu_count() or 4)
        try:
            launch(a, 'terminal', 4, False, ('2b-terminal-slow-loaded', control(a, '2b-terminal-slow-loaded')))
        finally:
            for p in busy: p.kill()
            for p in busy: p.wait()

    def terminal_fails(a):
        """A terminal that cannot start, slowly (2 s), in a folder with a long Persian name, in a small dark window.
        The window answers while the item runs. Then nobody touches the window: the failure comes back from the worker
        into an idle window, and its toast must still be there (pink pixels of its error icon appear), and no terminal opens."""
        folder = SAND/'-'.join(['پوشهٔ بسیار طولانی برای آزمایش گشودن پایانه']*3)
        folder.mkdir()
        a.theme = 'dark'; a.restart()
        assert a.path(folder)['state'] != 0
        dpi = U.GetDpiForWindow(a.hwnd)/96
        U.SetWindowPos(a.hwnd, None, 0, 0, int(360*dpi), int(280*dpi), 0x16)
        def pink(label):
            from PIL import Image
            a.snap(label)
            with Image.open(WORK/(label+'.png')) as image:
                rgb = image.convert('RGB')
                return sum(1 for r, g, b in rgb.getdata() if r > 200 and r-g > 80 and r-b > 70)   # the pink of the error icon and its title
        before = pink('2c-terminal-fails-before')
        a.rclick(180, 150)
        with Probe(a.hwnd) as probe:
            assert a.snap()['ctx'], 'Context menu did not open'
            for _ in range(4):
                a.key(40)
            a.key(13)
        assert probe.longest < .25, f'The window did not answer for {probe.longest*1000:.0f} ms'
        time.sleep(3.5)   # idle: the worker fails after 2 s, and nothing else wakes the window
        after = pink('2c-terminal-fails-toast')
        assert after > before + 15, f'No error toast after the failed start (pink pixels {before} -> {after})'
        assert not [r for r in windows() if r[1].startswith('DFM shell stand-in')], 'A terminal opened'

    def properties(a):
        unexpected = WORK/'unexpected-open-launch.json'
        unexpected.unlink(missing_ok=True)
        for use_menu in (True, False):
            if use_menu:
                menu(a, 9)
            else:
                a.cell(0); a.key(13, (18,))
            h, title, cls = native_window(a, lambda r: r[2] == '#32770' and 'action target' in r[1])
            try:
                texts = []
                def child(h, _):
                    text = C.create_unicode_buffer(2048)
                    result = C.c_size_t()
                    U.SendMessageTimeoutW(h, 0xD, len(text), C.addressof(text), 2, 500, C.byref(result))
                    texts.append(text.value)
                    return True
                def populated():
                    texts.clear()
                    U.EnumChildWindows(h, ENUM(child), 0)
                    return str(SAND) in '\n'.join(texts)
                a.until('Properties did not show the target folder', populated)
                assert 'Properties' in title or 'Eigenschaften' in title, title
                (WORK/('properties-menu.json' if use_menu else 'properties-key.json')).write_text(
                    json.dumps(dict(title=title, cls=cls, children=texts, desktop=desktop(a.tid)), indent=2), encoding='utf-8')
            finally:
                close_native(a, h)
            assert not unexpected.exists(), 'Properties also launched the default file handler'

    def toast(a):
        target = SAND/'action target.txt'
        a.cell(0); a.key(46)
        a.until('Delete kept the file', lambda: not target.exists())
        finished(a)
        time.sleep(.5)
        a.snap('undo-toast')
        rect = W.RECT(); U.GetClientRect(a.hwnd, C.byref(rect))
        scale = U.GetDpiForWindow(a.hwnd)/96
        a.click(rect.right/scale-110, rect.bottom/scale-45)
        a.until('Toast Undo did not restore the file', target.exists, 10)
        assert target.read_text(encoding='utf-8') == 'x'

    def toast_label(a, tag):
        """A notice and the selection label never cover each other (CUI keeps toasts in the bottom-right corner; the label rises above them).
        Hard cases: one file with a long Persian name selected under an Undo toast, and "4 items selected" under it."""
        before = bool(os.environ.get('DFM_TOAST_BEFORE'))   # an old exe has no rectangles: only the pictures are taken
        target = SAND/TOAST_FILES[0]
        a.cell(0); a.key(46)
        a.until('Delete kept the file', lambda: not target.exists())
        finished(a)
        def state(label, pill, hover=False):
            def settled():
                s = a.snap()
                return s['pill'].startswith(pill) and (before or s['pill_rect'][2] > 0)
            a.until(f'The label never showed {pill!r}', settled, 5)
            if hover and not before:   # the pointer on the toast opens CUI's stack: the label must still be clear of it
                tx, ty, tw, th = a.snap()['toast_rect']
                a.msg(0x200, 0, a.point(tx + tw / 2, ty + th / 2))
            time.sleep(.6)   # the label's rise and the toast's own entrance have ended
            s = a.snap(f'toast-label-{tag}-{label}')
            if before:
                return
            tx, ty, tw, th = s['toast_rect']; px, py, pw, ph = s['pill_rect']
            assert th > 0, f'{label}: the Undo toast is gone, the check proves nothing ({s["toast_rect"]})'
            assert pw > 0 and s['pill'].startswith(pill), f'{label}: label {s["pill"]!r}'
            assert px >= 0 and px + pw <= s['vw'] and py >= 0, f'{label}: the label leaves the window {s["pill_rect"]} in {s["vw"]}x{s["vh"]}'
            apart = py + ph <= ty or px + pw <= tx or tx + tw <= px or ty + th <= py
            assert apart, f'{label}: the notice {s["toast_rect"]} covers the label {s["pill_rect"]}'
            assert not (px < tx + tw and tx < px + pw) or py + ph <= ty, f'{label}: label and notice share a column without a gap'
        names = a.snap()['names']
        assert names[-1] == TOAST_FILES[1], names
        a.key(35)   # End: the long Persian name
        state('one', f'“{TOAST_FILES[1][:8]}')   # the label shortens a long name at a whole character
        a.key(ord('A'), (17,))
        state('four', '4 items selected')
        state('four-hover', '4 items selected', hover=True)

    def conflicts(a):
        source, dest = SAND/'source', SAND/'destination'
        source.mkdir(); dest.mkdir()
        name = 'same name.txt'
        (source/name).write_text('incoming', encoding='utf-8')
        with preserved_clipboard(a.hwnd):
            a.path(source); a.cell(0); a.key(ord('C'), (17,))
            for button, action in ((4,'replace'), (3,'both'), (2,'skip')):
                (dest/name).write_text('original', encoding='utf-8')
                a.path(dest); a.key(ord('V'), (17,))
                a.until('No name conflict dialog', lambda: a.snap()['act'] & 8)
                a.snap('conflict-'+action)
                click_rect(a, button); finished(a)
                assert (source/name).read_text(encoding='utf-8') == 'incoming'
                assert (dest/name).read_text(encoding='utf-8') == ('incoming' if action == 'replace' else 'original'), action
                copy = dest/'same name (copy).txt'
                if action == 'both':
                    assert copy.read_text(encoding='utf-8') == 'incoming'
                elif action == 'skip':
                    assert sorted(p.name for p in dest.iterdir()) == [copy.name, name]
                else:
                    assert not copy.exists()

    def newer_clipboard(a):
        """A move ends while another program holds the clipboard and then puts its own text there: that text stays."""
        source, dest = SAND/'source', SAND/'destination'
        source.mkdir(); dest.mkdir()
        name = 'same name.txt'
        (source/name).write_text('incoming', encoding='utf-8')
        with preserved_clipboard(a.hwnd):
            put_cut(source/name)
            assert clipboard_state(a.hwnd)[1], 'The cut did not put the file on the clipboard'
            (dest/name).write_text('original', encoding='utf-8')
            a.path(dest); a.key(ord('V'), (17,))
            a.until('No name conflict dialog', lambda: a.snap()['act'] & 8)  # the move waits here
            errors, ready = [], threading.Event()
            def other_program():
                try: hold_then_write(os.environ['DFM_TEST_CLIP_HANDSHAKE'], ready, 'copied elsewhere')
                except BaseException as e: errors.append(e)
            hold = threading.Thread(target=other_program)
            hold.start()
            assert ready.wait(20), 'The other program did not get the clipboard'
            assert not errors, ('The other program failed', errors)
            click_rect(a, 4)
            try: finished(a)
            except Exception:
                hold.join()   # when the other program failed, that is the cause: report it, not the stuck window
                assert not errors, ('The other program failed', errors)
                raise
            hold.join()
            assert not errors, ('The other program failed', errors)
            assert (dest/name).read_text(encoding='utf-8') == 'incoming' and not (source/name).exists(), 'Move did not happen'
            state = clipboard_state(a.hwnd)
            assert state == ('copied elsewhere', False), ('The finished move erased the newer clipboard', state)

    def text_paste(a):
        """Ctrl+V in the path field while another program has just written to the clipboard and still holds it."""
        source = SAND/'source'
        source.mkdir()
        with preserved_clipboard(a.hwnd):
            a.key(ord('L'), (17,))
            other = threading.Thread(target=put_text, args=(str(source), .15))
            other.start(); time.sleep(.05)
            a.key(ord('V'), (17,)); other.join()
            a.key(13)
            s = a.wait_ready()
            assert s['path'].lower() == str(source).lower(), ('Pasted text did not reach the path field', s['path'])

    def partial_trash(a):
        # Two files are trashed together; the second is locked against deletion. The first still reaches the Recycle Bin,
        # and the Undo toast must still be offered for it, next to the error.
        first, locked = SAND/'action target.txt', SAND/'z locked file.txt'
        locked.write_text('locked', encoding='utf-8')
        K.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
        K.CreateFileW.restype = W.HANDLE
        handle = K.CreateFileW(str(locked), 0x80000000, 1, None, 3, 0, None)   # read, share read only: delete is refused
        assert handle != C.c_void_p(-1).value, 'could not lock the file'
        try:
            a.key(ord('A'), (17,)); time.sleep(.3)
            a.key(46)
            a.until('The first file did not reach the Trash', lambda: not first.exists())
            finished(a)
            time.sleep(.5)
            assert locked.exists(), 'The locked file vanished'
            a.snap('partial-trash-toast')
        finally:
            K.CloseHandle(handle)
        rect = W.RECT(); U.GetClientRect(a.hwnd, C.byref(rect))
        scale = U.GetDpiForWindow(a.hwnd)/96
        a.click(rect.right/scale-110, rect.bottom/scale-45)
        a.until('Undo was not offered after the partial failure', first.exists, 10)
        assert first.read_text(encoding='utf-8') == 'x'

    def permanent(a):
        target = SAND/'action target.txt'
        a.cell(0); a.key(46, (16,))
        a.until('Shift+Delete did not ask', lambda: a.snap()['act'] & 4)
        assert target.exists(), 'Deleted before confirmation'
        a.snap('permanent-cancel'); click_rect(a, 5)
        assert target.exists(), 'Cancel deleted the file'
        a.until('Cancel left the dialog open', lambda: not a.snap()['act'] & 4)
        a.key(46, (16,))
        a.until('Second Shift+Delete did not ask', lambda: a.snap()['act'] & 4)
        click_rect(a, 6); finished(a)
        assert not target.exists(), 'Confirm kept the file'
        a.key(ord('Z'), (17,)); time.sleep(.3)
        assert not target.exists(), 'Permanent delete was only Trash'

    def progress(a):
        source, dest = SAND/'source', SAND/'destination'
        source.mkdir(); dest.mkdir()
        target = source/'large-copy.bin'
        # Real bytes and real CopyFileEx; bounded callback delay makes cancellation
        # reproducible on fast disks without making multi-gigabyte fixtures.
        with target.open('wb') as f:
            block = b'copy progress check\0' * (1024*1024//20)
            for _ in range(256):
                f.write(block)
        os.environ['DFM_TEST_COPY_DELAY_MS'] = '50'
        a.restart()  # only this app inherits the slow-copy setting
        try:
            with preserved_clipboard(a.hwnd):
                a.path(source); a.cell(0); a.key(ord('C'), (17,))
                a.path(dest); a.key(ord('V'), (17,))
                a.until('Copy not running', lambda: a.snap()['job']['active'])
                a.until('Copy made no byte progress', lambda: 0 < a.snap()['job']['done'] < a.snap()['job']['total'])
                before = a.snap('copy-ring-before')['job']['done']
                click_rect(a, 0)
                a.until('Job popover missing', lambda: a.snap()['job']['popover'])
                a.until('Copy stalled', lambda: a.snap()['job']['done'] > before + target.stat().st_size*.1)
                after = a.snap('copy-popover')
                from PIL import Image
                x,y,w,h = after['action_rects'][0]
                scale = U.GetDpiForWindow(a.hwnd)/96
                box = tuple(round(v*scale) for v in (x,y,x+w,y+h))
                def ring_pixels(label):
                    with Image.open(WORK/(label+'.png')) as image:
                        crop = image.crop(box).convert('RGB')
                        colors = crop.getcolors(crop.width*crop.height)
                        return sum(count for count,(r,g,b) in colors if b > r+40 and b > g+40)
                assert ring_pixels('copy-ring-before') > 5, 'Header progress arc not painted'
                assert ring_pixels('copy-popover') > ring_pixels('copy-ring-before'), 'Header arc did not advance'
                click_rect(a, 1); finished(a)
                assert not (dest/target.name).exists(), 'Cancelled copy committed its destination'
                assert not list(dest.iterdir()), 'Cancelled copy left a partial file'
                assert target.stat().st_size > 200*1024*1024, 'Source damaged'
        finally:
            os.environ.pop('DFM_TEST_COPY_DELAY_MS', None)

    check('1-open-with', lambda a: launch(a, 'open-with', 2, True))
    check('2-terminal', quiet)
    os.environ['DFM_TEST_TERMINAL_DELAY_MS'] = '1500'   # the app reads it at its start
    try:
        check('2b-terminal-slow-loaded', slow_loaded)
    finally:
        os.environ.pop('DFM_TEST_TERMINAL_DELAY_MS', None)
    working = os.environ['DFM_TEST_TERMINAL']
    os.environ['DFM_TEST_TERMINAL'] = 'C:/no-such-folder/no-terminal.exe'   # the process cannot start
    os.environ['DFM_TEST_TERMINAL_DELAY_MS'] = '2000'
    try:
        check('2c-terminal-fails', terminal_fails)
    finally:
        os.environ['DFM_TEST_TERMINAL'] = working
        os.environ.pop('DFM_TEST_TERMINAL_DELAY_MS', None)
    check('3-properties', properties)
    check('4-toast-undo', toast)
    check('11-toast-clear-of-label-1280-light', lambda a: toast_label(a, '1280-light'), TOAST_FILES, theme='light', extra=('--size', '1280x800'))
    check('12-toast-clear-of-label-360-dark', lambda a: toast_label(a, '360-dark'), TOAST_FILES, theme='dark', extra=('--size', '360x280'))
    check('5-name-conflicts', conflicts)
    check('6-shift-delete', permanent)
    check('7-progress-cancel', progress)
    check('8-cut-keeps-newer-clipboard', newer_clipboard)
    check('9-text-field-paste', text_paste)
    check('10-partial-trash-undo', partial_trash)
    (WORK/'actions-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK/'tmp').mkdir(exist_ok=True); (WORK/'appdata').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK/'tmp'), TMP=str(WORK/'tmp'))
    if '--standin' in sys.argv:
        return standin()
    if '--inside' in sys.argv:
        return inside()
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    code = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif code:
        return code
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK/'actions.log').open('wb') as out, (WORK/'actions.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=20*60)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())
