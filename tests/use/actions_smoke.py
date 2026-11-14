"""Nine action checks through Win32 input, on correct/hidden.py's desktop.

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
U.EnumChildWindows.argtypes = [W.HWND, ENUM, W.LPARAM]
U.IsWindowVisible.argtypes = [W.HWND]
U.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
U.GetThreadDesktop.argtypes = [W.DWORD]
U.GetThreadDesktop.restype = W.HANDLE
U.GetUserObjectInformationW.argtypes = [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]
U.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]


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


def native_window(a, predicate):
    found = []
    def ready():
        found[:] = [row for row in windows() if predicate(row)]
        return bool(found)
    a.until('Expected native window missing', ready, 10)
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


def standin():
    """A real native window recording exactly what the shell child received."""
    kind, *args = sys.argv[sys.argv.index('--standin')+1:]
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
    while U.IsWindow(h):
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


def hold_then_write(hwnd, moved, text):
    """Another program: holds the clipboard open until the move is done and the app is stuck waiting for it,
    then replaces the contents with text and closes it."""
    from clipboard_guard import open_clipboard, U, K   # the wrappers whose argument and result types are set
    K.GlobalAlloc.argtypes = [W.UINT, C.c_size_t]
    K.GlobalAlloc.restype = W.HGLOBAL
    U.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
    U.SendMessageTimeoutW.restype = C.c_ssize_t
    def stuck():  # the app's window thread is inside clip_open's wait and takes no messages
        r = C.c_size_t()
        return not U.SendMessageTimeoutW(hwnd, 0, 0, 0, 2, 30, C.byref(r))  # SMTO_ABORTIFHUNG, 30 ms
    open_clipboard(None)
    try:
        end = time.monotonic()+5
        while not moved():  # the job's worker has moved the file; the window thread now ends the job
            assert time.monotonic() < end, 'The move did not finish'
            time.sleep(.005)
        while not (stuck() and stuck()):
            assert time.monotonic() < end, 'The app never waited for the clipboard'
        time.sleep(.05)
        data = (text+'\0').encode('utf-16-le')
        h = K.GlobalAlloc(2, len(data))
        C.memmove(K.GlobalLock(h), data, len(data)); K.GlobalUnlock(h)
        assert U.EmptyClipboard() and U.SetClipboardData(13, h), C.get_last_error()
    finally:
        U.CloseClipboard()


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


def inside():
    results = []
    command = subprocess.list2cmdline([sys.executable, str(Path(__file__).resolve()), '--standin'])
    os.environ['DFM_TEST_OPEN_WITH'] = command+' open-with'
    os.environ['DFM_TEST_TERMINAL'] = command+' terminal'
    os.environ['DFM_TEST_OPEN'] = command+' unexpected-open'

    def check(name, fn):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        app = None
        try:
            fresh('action target.txt')
            app = App(name, SAND)
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
        print(json.dumps(results[-1]), flush=True)

    def launch(a, kind, row, item):
        marker = WORK/(kind+'-launch.json')
        marker.unlink(missing_ok=True)
        menu(a, row, item)
        h, title, cls = native_window(a, lambda r: r[1].startswith('DFM shell stand-in '+kind))
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
            errors = []
            def other_program():
                try: hold_then_write(a.hwnd, lambda: (dest/name).read_text(encoding='utf-8') == 'incoming', 'copied elsewhere')
                except BaseException as e: errors.append(e)
            hold = threading.Thread(target=other_program)
            hold.start(); time.sleep(.05)  # the other program has the clipboard open
            click_rect(a, 4); finished(a); hold.join()
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
    check('2-terminal', lambda a: launch(a, 'terminal', 4, False))
    check('3-properties', properties)
    check('4-toast-undo', toast)
    check('5-name-conflicts', conflicts)
    check('6-shift-delete', permanent)
    check('7-progress-cancel', progress)
    check('8-cut-keeps-newer-clipboard', newer_clipboard)
    check('9-text-field-paste', text_paste)
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
