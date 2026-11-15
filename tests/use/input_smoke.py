"""Real Win32 input on a private desktop. No default file handlers are invoked.
Run from the repo: python tests/use/input_smoke.py
DFM_USE_WORK and DFM_EXE select isolated artifacts and the executable.
Requires the shared correct/hidden.py.
"""
import ctypes as C
from contextlib import contextmanager
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
WORK = Path(os.environ.get('DFM_USE_WORK', ROOT / '.agent-work/use-checks' / Path(__file__).stem))
EXE = Path(os.environ.get('DFM_EXE', ROOT / 'build/MinSizeRel/dio-file-manager.exe'))
sys.pycache_prefix = str(WORK/'tmp/pycache')
from clipboard_guard import clipboard_test_lock, preserved_clipboard  # shared by every clipboard check
import recycle_guard
recycle_guard.install(WORK)  # at exit, removes only this check's own items from the real Recycle Bin
U = C.WinDLL('user32', use_last_error=True)
K = C.WinDLL('kernel32', use_last_error=True)
ENUM = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
U.EnumWindows.argtypes = [ENUM, W.LPARAM]
U.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
U.GetWindowThreadProcessId.restype = W.DWORD
U.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
U.AttachThreadInput.argtypes = [W.DWORD, W.DWORD, W.BOOL]
U.SetKeyboardState.argtypes = [C.POINTER(C.c_ubyte)]
U.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
U.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
U.GetDpiForWindow.argtypes = [W.HWND]
U.IsWindow.argtypes = [W.HWND]
U.SendMessageTimeoutW.restype = C.c_ssize_t
U.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
U.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
U.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
U.InvalidateRect.argtypes = [W.HWND, C.c_void_p, W.BOOL]



class App:
    def __init__(self, name, path, view='grid', theme='light', extra=()):
        self.started = time.monotonic()
        self.name, self.folder, self.view = name, path, view
        self.theme = theme
        self.base = WORK / name
        Path(str(self.base)+'.request').unlink(missing_ok=True)
        self.proc = subprocess.Popen([str(EXE), '--path', str(path), '--view', view,
                                      '--test-state', str(self.base), '--theme', theme, *extra],
                                     cwd=ROOT, env=dict(os.environ, APPDATA=str(WORK/'appdata')),
                                     creationflags=subprocess.CREATE_NO_WINDOW)
        self.hwnd = None
        time.sleep(.2)  # Let window creation and font loading finish.
        def find(h, _):
            pid = W.DWORD()
            U.GetWindowThreadProcessId(h, C.byref(pid))
            title = C.create_unicode_buffer(256)
            U.GetWindowTextW(h, title, 256)
            if pid.value == self.proc.pid and title.value == 'Dio File Manager':
                self.hwnd = h
            return True
        deadline = time.monotonic() + 10
        while not self.hwnd and time.monotonic() < deadline:
            U.EnumWindows(ENUM(find), 0)
            time.sleep(.02)
        assert self.hwnd, f'No app window (exit {self.proc.poll()})'
        self.tid = U.GetWindowThreadProcessId(self.hwnd, None)
        self.our_tid = K.GetCurrentThreadId()
        assert U.AttachThreadInput(self.our_tid, self.tid, True), C.get_last_error()
        self.mods = ()
        self.keys(())
        self.wait_ready()

    def header_icon(self, from_right):
        """Centre (x, y) in points of a header control on the right: 0 view options, 1 search (the view options button is wider: icon and chevron).
        Read from the app's own list of header controls, so a moved icon never lets a click fall on a window button."""
        c = self.snap()['chrome']
        icons = sorted((r for r in c['nodrag'] if r[1] < 46 and r[0] > 240 and 30 <= r[2] <= 72), key=lambda r: -r[0])
        assert len(icons) > from_right, f'Header icons: {icons}'
        r = icons[from_right]
        return r[0] + r[2] / 2, r[1] + r[3] / 2

    def msg(self, msg, wp=0, lp=0):
        result = C.c_size_t()
        start = time.monotonic()
        C.set_last_error(0)
        assert U.SendMessageTimeoutW(self.hwnd, msg, wp, lp, 2, 500, C.byref(result)), f'Window stopped answering (message {msg:x}, error {C.get_last_error()}, process {self.proc.poll()})'
        return time.monotonic() - start

    def keys(self, mods):
        state = (C.c_ubyte * 256)()
        for k in mods:
            state[k] = 0x80
        assert U.SetKeyboardState(state)
        for k in set(self.mods) | set(mods):
            self.msg(0x100 if k in mods else 0x101, k, 1)
        self.mods = mods

    def key(self, key, mods=()):
        self.keys(mods)
        self.msg(0x100, key, 1)
        time.sleep(.06)
        self.msg(0x101, key, 0xC0000001)
        self.keys(())
        time.sleep(.1)

    def point(self, x, y):
        # UI coordinates are logical pixels; the backend converts from device pixels.
        dpi = U.GetDpiForWindow(self.hwnd) / 96
        return int(x*dpi) | (int(y*dpi) << 16)

    def click(self, x, y, mods=()):
        self.keys(mods)
        p = self.point(x, y)
        self.msg(0x200, 0, p)
        time.sleep(.05)
        self.msg(0x201, 1, p)
        time.sleep(.05)
        self.msg(0x202, 0, p)
        time.sleep(.08)
        self.keys(())

    def rclick(self, x, y):
        p = self.point(x, y)
        self.msg(0x200, 0, p)
        time.sleep(.05)
        self.msg(0x204, 2, p)
        time.sleep(.05)
        self.msg(0x205, 0, p)
        time.sleep(.3)

    def cell_xy(self, index):
        s = self.snap()
        x,y,w,h = s['row']
        col = index % s['cols']
        row = index // s['cols'] - s['first']
        return x + 16 + (col+.5)*(w-32)/s['cols'], y + row*h + 40

    def until(self, what, cond, seconds=5):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if cond():
                return
            self.msg(0)
            time.sleep(.05)
        raise AssertionError(what)

    def cell(self, index, mods=()):
        s = self.snap()
        x,y,w,h = s['row']
        col = index % s['cols']
        row = index // s['cols'] - s['first']
        self.click(x + 16 + (col+.5)*(w-32)/s['cols'], y + row*h + 40, mods)

    def snap(self, label=None):
        request = Path(str(self.base)+'.request')
        request.touch()
        U.InvalidateRect(self.hwnd, None, False)
        deadline = time.monotonic() + 5
        while request.exists() and time.monotonic() < deadline:
            U.PostMessageW(self.hwnd, 0, 0, 0)
            time.sleep(.03)
        assert not request.exists(), 'No state dump'
        s = json.loads(Path(str(self.base)+'.json').read_text(encoding='utf-8'))
        if label:
            import shutil
            for ext in ('.json', '.png'):
                shutil.copyfile(str(self.base)+ext, WORK / (label+ext))
        return s

    def wait_ready(self):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            s = self.snap()
            if s['state'] != 0:
                return s
        raise AssertionError('Listing never completed')

    def path(self, path):
        self.key(ord('L'), (17,))
        for char in str(path):
            self.msg(0x102, ord(char))
            time.sleep(.002)
        self.key(13)
        return self.wait_ready()

    def close(self):
        if self.proc.poll() is not None:
            return
        self.keys(())
        U.AttachThreadInput(self.our_tid, self.tid, False)
        self.msg(0x10)
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()

    def restart(self):
        self.close()
        self.__init__(self.name, self.folder, self.view, self.theme)


SAND = WORK/'sandbox'


def fresh(*names):
    # Only this folder is ever emptied: it holds the files the action checks create.
    import shutil
    deadline = time.monotonic() + 10
    while True:   # a program that just ended (a stand-in window's process) may still hold the folder for a moment
        shutil.rmtree(SAND, ignore_errors=True)
        if not SAND.exists():
            break
        assert time.monotonic() < deadline, f'{SAND} cannot be emptied: a program still uses it'
        time.sleep(.1)
    SAND.mkdir(parents=True)
    for n in names:
        (SAND/n).write_text('x', encoding='utf-8')


def inside():
    results = []
    # The real Recycle Bin is shared with other runs of this check and lists same-named items in no fixed order, so
    # the file gets a name no other run uses: the Trash selection then can only be this run's own item.
    restore_name = f'dio-views-v2-restore-{uuid.uuid4().hex[:12]}.txt'
    def check(name, folder, fn, view='grid', prep=None, theme='light', extra=()):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        app = None
        try:
            if prep:
                prep()
            app = App(name, folder, view, theme, extra)
            fn(app)
            app.snap(name+'-passed')
            results.append({'check':name,'result':'passed'})
        except Exception as e:
            if app:
                app.snap(name+'-failed')
            results.append({'check':name,'result':'FAILED','reason':str(e)})
        finally:
            if app:
                app.close()
        print(json.dumps(results[-1]), flush=True)

    def wheel(a):
        before = a.snap('wheel-'+str(a.snap()['view'])+'-before')
        for _ in range(12):
            a.msg(0x200, 0, a.point(700,400))
            a.msg(0x20A, (0x10000-120) << 16)
            time.sleep(.03)
        time.sleep(.5)
        after = a.snap('wheel-'+str(before['view'])+'-after')
        # A tall icon row can remain in the virtual list's overscan after scrolling.
        assert after['first'] > before['first'] or after['row'][1] < before['row'][1], f'Wheel did not move: {before["row"]} -> {after["row"]}'
        # Reload and probe the queue while the worker lists 20,000 files.
        a.msg(0x100,116,1)
        a.msg(0x101,116,0xC0000001)
        latencies = [a.msg(0) for _ in range(50)]
        print(json.dumps({'view':before['view'],'wheel_first':after['first'],
                          'max_message_ms':round(max(latencies)*1000,3)}),flush=True)
        assert max(latencies) < .5, 'Listing froze message handling'
        assert a.wait_ready()['count'] == 20000
        # Abandon in-flight listings, as when the user rapidly goes back/forward.
        for _ in range(12):
            a.key(37,(18,))
            a.key(39,(18,))
        assert a.wait_ready()['count'] == 20000

    def navigation(a):
        x, y = a.cell_xy(0)
        a.click(x,y); a.click(x,y)
        assert a.wait_ready()['path'] == str(WORK/'fixture'/'A'), 'Double click did not open folder'
        a.key(37,(18,)); assert a.wait_ready()['path'] == str(WORK/'fixture'), 'Back'
        a.key(39,(18,)); assert a.wait_ready()['path'].endswith('\\A'), 'Forward'
        a.key(38,(18,)); assert a.wait_ready()['path'] == str(WORK/'fixture'), 'Parent'
        before = a.snap()['path']; a.key(8)
        assert a.snap()['path'] == before, 'Backspace unexpectedly navigates'

    def mouse_navigation(a):
        # The side buttons as Windows reports them: WM_XBUTTONDOWN/UP (XBUTTON1 back, XBUTTON2 forward) and
        # WM_APPCOMMAND (keyboard browser keys, drivers). One press must move exactly one step.
        fixture = WORK/'fixture'
        def at(expected, what=''):
            time.sleep(.4)  # long enough for a second, wrong step to show
            got = a.wait_ready()['path']
            assert got == str(expected), f'{what}: at {got}, expected {expected}'
        def press(button, x, y):
            p = a.point(x, y)
            a.msg(0x20B, button << 16, p)   # WM_XBUTTONDOWN
            a.msg(0x20C, button << 16, p)   # WM_XBUTTONUP
        def command(cmd):
            a.msg(0x319, int(a.hwnd), (cmd << 16) | 0x8000)   # WM_APPCOMMAND from the mouse
        back = lambda x=700, y=300: press(1, x, y)
        forward = lambda x=700, y=300: press(2, x, y)
        at(fixture, 'step')
        back(); at(fixture, 'Back at the first folder of a new window does nothing')
        a.key(37, (18,)); at(fixture, 'Alt+Left at the first folder of a new window does nothing')
        x, y = a.cell_xy(0)
        a.click(x, y); a.click(x, y)
        assert a.wait_ready()['path'] == str(fixture/'A'), 'Double click did not open folder'
        a.path(fixture/'B')
        back(); at(fixture/'A', 'Back button over the content: one step to A')
        forward(); at(fixture/'B', 'Forward button over the content')
        back(90, 120); at(fixture/'A', 'Back button over the sidebar')
        forward(600, 23); at(fixture/'B', 'Forward button over the header')
        back(300, 700); at(fixture/'A', 'Back button over the bottom')
        command(1); at(fixture, 'WM_APPCOMMAND browser backward')
        command(2); at(fixture/'A', 'WM_APPCOMMAND browser forward')
        a.msg(0x319, int(a.hwnd), 1 << 16); at(fixture, 'WM_APPCOMMAND without the mouse flag')
        a.msg(0x319, int(a.hwnd), 2 << 16); at(fixture/'A', 'WM_APPCOMMAND forward without the mouse flag')
        # The keyboard's Browser Back/Forward keys arrive as real key messages, which CUI would otherwise consume.
        def browser_key(vk):
            a.msg(0x100, vk, 1); time.sleep(.05); a.msg(0x101, vk, 0xC0000001)
        browser_key(0xA6); at(fixture, 'VK_BROWSER_BACK key: one step')
        browser_key(0xA7); at(fixture/'A', 'VK_BROWSER_FORWARD key: one step')
        # While a menu is open the press closes it and goes no further, as in Explorer.
        a.path(fixture/'B'); back(); at(fixture/'A', 'step')
        a.click(212, 23)
        assert a.snap()['popups'], 'Main menu did not open'
        back(); at(fixture/'A', 'Back button with the main menu open must not change folder')
        a.until('Back button did not close the open main menu', lambda: not a.snap()['popups'])
        a.click(212, 23); assert a.snap()['popups'], 'Main menu did not open'
        browser_key(0xA6); at(fixture/'A', 'Browser Back key with the main menu open must not change folder')
        a.until('Browser Back key did not close the open main menu', lambda: not a.snap()['popups'])
        forward(); at(fixture/'B', 'Forward button after the menu closed')
        back(); at(fixture/'A', 'step')
        # Explorer's history: every way to change folder is a step, a new step after Back drops Forward.
        a.click(90, 46+4*34+16)                                       # sidebar place
        s = a.wait_ready(); docs = s['documents']; assert s['path'] == docs, 'Sidebar'
        back(); at(fixture/'A', 'Back after a sidebar click')
        a.path(fixture/'C'); at(fixture/'C', 'Typed path')
        a.key(38, (18,)); at(fixture, 'Alt+Up')
        back(); at(fixture/'C', 'Back after Alt+Up')
        a.path(str(fixture/'C').swapcase())   # the same folder typed in other letters is no new step
        back(); at(fixture/'A', 'Back after the same folder again')
        a.path(fixture/'D')
        forward(); at(fixture/'D', 'A new folder after Back drops Forward')
        back(); at(fixture/'A', 'step')
        back(); at(fixture, 'step')
        back(); at(fixture, 'Back at the first folder stays')
        # The File Operations popup is a popup too: the press closes it for good (its own flag used to reopen it).
        src, dst = WORK/'copy-src', WORK/'copy-dst'
        with preserved_clipboard(a.hwnd):
            a.path(src); a.cell(0); a.key(ord('C'), (17,))
            a.path(dst); a.key(ord('V'), (17,))
            a.until('Copy not running', lambda: a.snap()['job']['active'])
            def job_button(index):
                x, y, w, h = a.snap()['action_rects'][index]
                assert w > 0 and h > 0, ('Button has no hit area', index)
                a.click(x+w/2, y+h/2)
            job_button(0)
            a.until('File Operations popup did not open', lambda: a.snap()['job']['popover'])
            back(); at(dst, 'Back button with File Operations open must not change folder')
            time.sleep(.3)
            s = a.snap()
            assert not s['job']['popover'] and not s['popups'], 'Back button did not close the File Operations popup'
            job_button(0); a.until('File Operations popup did not open again', lambda: a.snap()['job']['popover'])
            job_button(1); a.until('Copy did not end', lambda: not a.snap()['job']['active'], 15)

    def selection(a):
        a.cell(0); assert a.snap()['selected'] == 1, 'Click'
        a.cell(1,(17,)); assert a.snap()['selected'] == 2, 'Ctrl click add'
        a.cell(1,(17,)); assert a.snap()['selected'] == 1, 'Ctrl click remove'
        a.cell(3,(16,)); assert a.snap()['selected'] == 3, 'Shift range from anchor 1'
        a.key(39); assert a.snap()['cursor'] == 4 and a.snap()['selected'] == 1, 'Right arrow'
        a.key(37); assert a.snap()['cursor'] == 3, 'Left arrow'
        a.key(36)
        cols = a.snap()['cols']
        a.key(40); assert a.snap()['cursor'] == cols, 'Down arrow'
        a.key(38); assert a.snap()['cursor'] == 0, 'Up arrow'
        a.key(13)
        assert a.wait_ready()['path'].endswith('\\A'), 'Enter'

    def shortcuts(a):
        a.key(50,(17,)); assert a.snap()['view'] == 1, 'Ctrl 2'
        a.key(49,(17,)); assert a.snap()['view'] == 0, 'Ctrl 1'
        n=a.snap()['count']; a.key(72,(17,))
        assert a.snap()['count'] == n+1, 'Ctrl H'
        s=a.path(WORK/'empty'); assert s['path'] == str(WORK/'empty'), 'Ctrl L path submission'
        (WORK/'empty'/'reload.txt').touch()
        a.key(116); assert a.wait_ready()['count'] == 1, 'F5'

    def sidebar(a):
        a.click(90, 46+4*34+16)
        s=a.wait_ready(); assert s['path'] == s['documents'] and s['place'], 'Documents sidebar/current mark'
        a.click(90,420)
        s=a.wait_ready(); assert s['path'] == s['drive'], 'Drive sidebar'

    def narrow(a):
        dpi=U.GetDpiForWindow(a.hwnd)/96
        U.SetWindowPos(a.hwnd,None,0,0,int(560*dpi),int(720*dpi),0x16)
        time.sleep(.4)
        assert a.snap()['narrow'], 'Sidebar not hidden'
        a.click(24,23); time.sleep(.6)
        assert a.snap('overlay')['overlay'], 'Overlay did not open'
        a.click(80,230)
        s=a.wait_ready(); assert not s['overlay'] and s['place'], 'Overlay navigation'

    def empty(a):
        assert a.snap()['count']==0 and a.snap()['state']==1, 'Empty folder state'
    def denied(a):
        assert a.snap()['state']==2, 'Unreadable folder state'

    def live_resize(a):
        # Enter the native edge-sizing loop with mouse input. An inactive
        # desktop cannot move the physical cursor, so apply its positions with
        # SetWindowPos while the button is held and the modal loop stays open.
        r = W.RECT()
        U.GetWindowRect(a.hwnd, C.byref(r))
        x, y = r.right-2, r.bottom-2
        assert U.PostMessageW(a.hwnd, 0xA1, 17, x | (y << 16))  # HTBOTTOMRIGHT
        time.sleep(.15)
        class GUI(C.Structure):
            _fields_ = [('size',W.DWORD),('flags',W.DWORD),('active',W.HWND),('focus',W.HWND),('capture',W.HWND),('menu',W.HWND),('move',W.HWND),('caret',W.HWND),('rect',W.RECT)]
        info = GUI(); info.size = C.sizeof(info)
        U.GetGUIThreadInfo(a.tid,C.byref(info))
        assert info.flags & 2 and info.move == a.hwnd, 'Native sizing loop did not start'
        frames = []
        try:
            for i, (dx, dy) in enumerate(((70,40),(140,80),(-120,-80))):
                assert U.PostMessageW(a.hwnd, 0x200, 1, (x+dx) | ((y+dy) << 16))
                assert U.SetWindowPos(a.hwnd,None,0,0,r.right-r.left+dx,r.bottom-r.top+dy,0x16)
                time.sleep(.12)
                client = W.RECT()
                U.GetClientRect(a.hwnd, C.byref(client))
                s = a.snap(f'live-resize-held-{i}')
                from PIL import Image
                with Image.open(str(a.base)+'.png') as im:
                    assert im.size == (client.right,client.bottom), f'Stale frame {im.size}, client {(client.right,client.bottom)}'
                    assert im.getpixel((im.width-12, im.height//2))[:3] != (0,0,0), 'Black resize area'
                frames.append((client.right,client.bottom))
            assert len(set(frames)) == 3, f'Live sizing did not change sizes: {frames}'
        finally:
            U.PostMessageW(a.hwnd, 0x202, 0, 0)
            U.PostMessageW(a.hwnd, 0x100, 27, 1)
            time.sleep(.15)
        print(json.dumps({'held_resize_frames':frames}),flush=True)

    def popup(a, key, mods, text):
        a.key(key, mods)
        a.until('Popover did not open', lambda: a.snap()['act'] & 1)
        time.sleep(.5)  # the field takes focus and selects its text on its second frame
        for ch in text:
            a.msg(0x102, ord(ch))
            time.sleep(.01)
        a.key(13)

    def rename(a):
        a.cell(0)
        assert a.snap()['selection'] == ['a.txt'], 'Select a.txt'
        popup(a, 113, (), 'zz')
        a.until('Rename did not happen', lambda: (SAND/'zz.txt').exists())
        assert not (SAND/'a.txt').exists() and (SAND/'b.txt').exists(), 'Only a.txt is renamed, extension kept'
        a.until('New name not selected', lambda: a.snap()['selection'] == ['zz.txt'])
        a.snap('rename-done')

    def new_folder(a):
        popup(a, ord('N'), (17,16), 'Docs')
        a.until('Folder not made', lambda: (SAND/'Docs').is_dir())
        a.until('New folder not selected', lambda: a.snap()['selection'] == ['Docs'])

    def copy_paste(a):
        with preserved_clipboard(a.hwnd):
            a.cell(0); assert a.snap()['selection'] == ['a.txt']
            a.key(ord('C'), (17,))
            a.key(ord('V'), (17,))
            a.until('No copy made', lambda: (SAND/'a (copy).txt').exists())
            assert (SAND/'a.txt').exists(), 'Original kept'
            a.key(ord('V'), (17,))
            a.until('Second paste gave no "(copy 2)"', lambda: (SAND/'a (copy 2).txt').exists())

    def clipboard_restore(a):
        from clipboard_guard_test import roundtrip
        roundtrip(a.hwnd)

    def trash_undo(a):
        a.cell(0); assert a.snap()['selection'] == ['a.txt']
        a.key(46)
        a.until('Delete did not remove the file', lambda: not (SAND/'a.txt').exists())
        time.sleep(.6)
        a.snap('trash-toast')
        a.key(ord('Z'), (17,))
        a.until('Undo did not bring the file back', lambda: (SAND/'a.txt').exists())

    def context(a):
        x,y = a.cell_xy(1)
        a.rclick(x,y)
        a.snap('menu-item')
        a.key(27)
        a.rclick(700,420)
        a.snap('menu-bg')

    def field_keys(a):
        # Keys typed into a field stay there. The app records what it would open (test mode starts no app).
        opened = Path(str(a.base)+'.opened')
        opened.unlink(missing_ok=True)
        a.cell(0); assert a.snap()['selection'] == ['a.txt'], 'Select a.txt'
        a.path(SAND)
        time.sleep(.3)
        assert not opened.exists(), 'Enter in the path field opened the selected file: ' + opened.read_text(encoding='utf-8')
        a.cell(0); assert a.snap()['selection'] == ['a.txt'], 'Select a.txt again'
        a.key(ord('L'), (17,))
        a.until('Path field did not take the keyboard', lambda: a.snap()['typing'])
        a.key(46)
        time.sleep(.3)
        assert (SAND/'a.txt').exists(), 'Delete in the path field trashed the selected file'
        a.key(27)
        a.cell(0)
        a.key(13)
        a.until('Enter in the view did not open the file', lambda: opened.exists() and str(SAND/'a.txt') in opened.read_text(encoding='utf-8'))

    def starred(a):
        saved = WORK/'appdata/dio-file-manager/starred.txt'
        assert not saved.exists(), 'Star test requires an empty sandbox starred list'
        a.cell(0)
        x,y = a.cell_xy(0)
        a.rclick(x,y)
        a.snap('star-menu')
        # Choose Star by row, independent of the popup's clamped screen position.
        for _ in range(7):
            a.key(40)
        a.key(13)
        a.until('Star menu did not persist the sandbox path', lambda: saved.exists() and str(SAND/'starred.txt') in saved.read_text(encoding='utf-8'))
        a.restart()
        a.click(90, 46+34+16)
        s = a.wait_ready()
        assert s['loc'] == 2 and s['count'] == 1, 'Starred after restart'
        a.cell(0)
        assert a.snap('starred-restarted')['selection'] == ['starred.txt'], 'Starred item missing'

    def trash_restore(a):
        target = SAND/restore_name
        a.cell(0)
        assert a.snap()['selection'] == [target.name]
        a.key(46)
        a.until('Delete did not trash the sandbox file', lambda: not target.exists())
        a.until('Trash action still running', lambda: not a.snap()['act'] & 2)
        a.restart()
        initial = a.snap('trash-badge-startup')
        assert initial['trash'] > 0, 'Trash count missing on startup'
        elapsed = time.monotonic() - a.started
        assert elapsed < 2, f'Startup badge check arrived too late ({elapsed:.2f}s)'
        print(json.dumps({'startup_trash': initial['trash'], 'seconds': round(elapsed, 3)}), flush=True)
        a.click(90, 46+9*34+16)
        s = a.wait_ready()
        assert s['loc'] == 3, 'Trash sidebar'
        a.key(36)
        for _ in range(s['count']):
            if a.snap()['selection'] == [target.name]:
                break
            a.key(39)
        else:
            raise AssertionError('Sandbox file not listed in Trash')
        a.snap('trash-selected')
        # The bar restores only the selection. Never select all or empty the bin.
        a.click(925, 69)
        a.until('Restore did not return the sandbox file', target.exists, seconds=10)
        assert target.read_text(encoding='utf-8') == 'x', 'Restored content changed'
        a.key(37, (18,))
        assert a.wait_ready()['path'] == str(SAND), 'Back to original folder'
        a.cell(0)
        assert a.snap('trash-restored')['selection'] == [target.name], 'Restored file not in its folder'

    def prepare_starred():
        fresh('starred.txt')
        (WORK/'appdata/dio-file-manager/starred.txt').unlink(missing_ok=True)

    def sidebar_row(a, key):
        """Centre of a sidebar row, read from the app: its place depends on how many drives the PC lists (another window test may add one
        with subst while this runs). A row below the visible part of the sidebar (the 360 px sheet ends at y=248) is wheeled into view."""
        for _ in range(40):
            s = a.snap()
            rows = [r for r in s['sbrows'] if r['key'] == key]
            assert rows, f'No {key} row in the sidebar'
            x, y, w, h = rows[0]['r']
            if y + h / 2 < (240 if s['narrow'] else 700):
                return x + w / 2, y + h / 2
            a.msg(0x200, 0, a.point(100, 150))
            a.msg(0x20A, (0x10000-120) << 16)
            time.sleep(.1)
        raise AssertionError(f'The {key} row never came into view')

    def virtual_navigation(a, narrow=False):
        def open_sheet():
            if narrow:
                a.click(26, 23)   # the sidebar button opens the sheet
                a.until('The sidebar sheet did not open', lambda: a.snap()['overlay'])
        open_sheet()
        a.click(*sidebar_row(a, 'recent'))
        s = a.wait_ready()
        assert s['loc'] == 1 and s['state'] == 1, 'Recent did not load'
        a.snap('recent-opened')
        open_sheet()
        a.click(*sidebar_row(a, 'other'))
        assert a.snap('other-opened')['loc'] == 4, 'Other Locations did not open'
        if not narrow:   # the drive cards sit at fixed places in the wide window only
            a.click(600, 112)
            s = a.wait_ready()
            assert s['loc'] == 0 and s['path'] == s['drive'], 'Other Locations drive did not open'

    check('10-starred-restart', SAND, starred, prep=prepare_starred)
    check('11-trash-restore', SAND, trash_restore, prep=lambda: fresh(restore_name))
    check('12-virtual-navigation', WORK/'fixture', virtual_navigation)
    check('12-virtual-navigation-360-dark', WORK/'fixture', lambda a: virtual_navigation(a, True), theme='dark', extra=('--size', '360x280'))

    check('9-rename', SAND, rename, prep=lambda: fresh('a.txt','b.txt'))
    check('9-new-folder', SAND, new_folder, prep=lambda: fresh('a.txt'))
    check('9-copy-paste', SAND, copy_paste, prep=lambda: fresh('a.txt'))
    check('9-clipboard-restore', SAND, clipboard_restore, prep=lambda: fresh('a.txt'))
    check('9-trash-undo', SAND, trash_undo, prep=lambda: fresh('a.txt','b.txt'))
    check('9-context', SAND, context, prep=lambda: fresh('a.txt','b.txt','c.pdf'))
    check('9-field-keys', SAND, field_keys, prep=lambda: fresh('a.txt'))
    check('1-grid-wheel', WORK/'big', wheel)
    check('2-list-wheel', WORK/'big', wheel, 'list')
    check('3-navigation', WORK/'fixture', navigation)
    check('4-selection', WORK/'fixture', selection)
    # A folder whose own path is over 400 characters (Persian names, so the UTF-8 form is about twice as long): it must
    # open, list, follow the disk, go back and forward and take a typed path like any other folder.
    BS, LP = chr(92), chr(92) * 2 + '?' + chr(92)   # a backslash; the extended prefix
    long_made = {}
    def long_chain():
        import shutil
        root = WORK/'longroot'
        shutil.rmtree(LP + str(root), ignore_errors=True)
        levels, path, i = [], str(root), 0
        while len(path) <= 420:
            i += 1
            path += BS + f'پوشهٔ-خیلی-بلند-شمارهٔ-{i:02d}'
            levels.append(path)
        deep = LP + path
        os.makedirs(deep + BS + 'زیرپوشه')
        Path(deep + BS + 'فایل-یک.txt').write_text('x' * 2000, encoding='utf-8')
        Path(deep + BS + 'زیرپوشه' + BS + 'داخل.txt').write_text('y' * 5000, encoding='utf-8')
        assert len(path) > 400 and len(path.encode('utf-8')) < 1000, (len(path), len(path.encode('utf-8')))
        long_made['v'] = root, levels
        return root, levels
    def long_at(a, path, count=None):
        s = a.wait_ready()
        assert s['path'] == path and s['state'] == 1, f"{s['path']!r} state {s['state']} wanted {path!r} listed"
        assert count is None or s['count'] == count, f"{s['count']} items"
        return s
    def long_folder(a):
        root, levels = long_made['v']
        for k, level in enumerate(levels):   # open each from its parent: double click its only entry
            x, y = a.cell_xy(0)
            a.click(x, y); a.key(13)
            long_at(a, level, 1 if k < len(levels) - 1 else 2)
        deep = levels[-1]
        s = a.snap('long-deep')
        assert set(s['names']) == {'زیرپوشه', 'فایل-یک.txt'}, s['names']
        if a.view == 'list':   # Details: the folder size of a folder in it
            a.until('folder size', lambda: any(r[0] == 'زیرپوشه' and r[1] == 1 for r in a.snap()['sizes']))
        assert a.snap()['watch'], 'The folder watch did not start'
        Path(LP + deep + BS + 'از-بیرون.txt').write_text('z', encoding='utf-8')   # another program adds a file
        a.until('a file added from outside shows', lambda: 'از-بیرون.txt' in a.snap()['names'], 8)
        a.key(37, (18,)); long_at(a, levels[-2], 1)
        a.key(39, (18,)); long_at(a, deep, 3)
        a.key(116); long_at(a, deep, 3)
        a.key(38, (18,)); long_at(a, levels[-2], 1)
        a.path(root); long_at(a, str(root))
        a.path(deep); long_at(a, deep, 3)   # the typed path
        assert not a.snap()['path'].startswith(LP), 'The path shows the extended prefix'
        a.key(ord('F'), (17,))   # search inside it: the match is in the folder under it
        a.until('Search field did not take the keyboard', lambda: a.snap()['typing'])
        for char in 'داخل':
            a.msg(0x102, ord(char))
        a.until('Search finds the file in the long folder', lambda: a.snap()['searching'] and a.snap()['names'] == ['داخل.txt'], 8)
        a.key(27)
        a.until('Esc leaves the search', lambda: not a.snap()['search'])
    def long_limit_chain():   # the longest path the app keeps: 1036 characters, so the extended form needs the headroom
        import shutil
        root = WORK/'longlimit'
        shutil.rmtree(LP + str(root), ignore_errors=True)
        path = str(root)
        while len(path) < 1036:
            room = 1036 - len(path) - 1   # what the next folder name may take
            path += BS + 'd' * (room if room <= 60 else 59 if room == 61 else 60)   # never leave a gap of 0 for the last name
        os.makedirs(LP + path)
        Path(LP + path + BS + 'x.txt').write_text('x', encoding='utf-8')
        assert len(path) == 1036, len(path)
        return path
    def long_limit(a):
        s = a.wait_ready()
        assert s['state'] == 1 and s['count'] == 1 and len(s['path']) == 1036, (s['state'], s['count'], s['path'][-40:])
    def long_small_dark(a):
        dpi = U.GetDpiForWindow(a.hwnd) / 96
        U.SetWindowPos(a.hwnd, None, 0, 0, int(360 * dpi), int(280 * dpi), 0x16)
        time.sleep(.5)
        a.until('long folder listed', lambda: a.snap()['state'] == 1 and a.snap()['count'] == 2)
        a.snap('long-small-dark')
    def prepare_copy():
        import shutil
        for d in ('copy-src', 'copy-dst'):
            shutil.rmtree(WORK/d, ignore_errors=True); (WORK/d).mkdir(parents=True)
        block = b'copy popup check' + bytes(1) + b'x' * (1024*1024 - 17)
        with (WORK/'copy-src'/'big.bin').open('wb') as f:
            for _ in range(64): f.write(block)
        os.environ['DFM_TEST_COPY_DELAY_MS'] = '50'   # slow enough that the copy outlives the checks
    check('14-mouse-navigation', WORK/'fixture', mouse_navigation, prep=prepare_copy)
    check('15-long-folder-grid', WORK/'longroot', long_folder, prep=long_chain)
    check('15-long-folder-list', WORK/'longroot', long_folder, 'list', prep=long_chain)
    check('15-long-folder-limit', long_limit_chain(), long_limit)
    check('15-long-folder-small-dark', long_chain()[1][-1], long_small_dark, theme='dark')
    check('5-shortcuts', WORK/'fixture', shortcuts)
    check('6-sidebar', WORK/'fixture', sidebar)
    check('7-narrow', WORK/'fixture', narrow)
    check('8-empty', WORK/'empty-pristine', empty)
    check('8-unreadable', 'C:\\System Volume Information', denied)
    check('13-live-resize', WORK/'fixture', live_resize)
    (WORK/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    WORK.mkdir(parents=True,exist_ok=True)
    (WORK/'tmp').mkdir(exist_ok=True)
    (WORK/'appdata').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK/'tmp'),TMP=str(WORK/'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = Path(r'C:\Users\Omid\.agents\tools\gaming.py')
    code=subprocess.run([sys.executable,str(gaming),'check'],creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable,str(gaming),'wait','--cap','60'],check=True,creationflags=subprocess.CREATE_NO_WINDOW)
    elif code:
        return code
    for folder in ('big','fixture','empty','empty-pristine'):
        (WORK/folder).mkdir(exist_ok=True)
    for i in range(20000):
        (WORK/'big'/f'file-{i:05}.txt').touch(exist_ok=True)
    for folder in 'ABCD':
        (WORK/'fixture'/folder).mkdir(exist_ok=True)
    for name in ('.hidden','1.txt','2.txt','3.txt','4.txt','5.txt'):
        (WORK/'fixture'/name).touch()
    (WORK/'empty'/'reload.txt').unlink(missing_ok=True)
    sys.path.insert(0,r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK/'input.log').open('wb') as out, (WORK/'input.err').open('wb') as err:
        run=popen([sys.executable,str(Path(__file__).resolve()),'--inside'],ROOT,dict(os.environ),b'',out,err)
        try:
            return run.wait(timeout=1950)  # two clipboard lock waits of up to 15 minutes

        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())
