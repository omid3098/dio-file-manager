"""Persian typed into the text boxes must join and edit in reading order, on a private desktop.
Run from the repo: python tests/use/persian_input_smoke.py
DFM_EXE selects the executable, DFM_USE_WORK the folder for artifacts and the sandbox. Requires the shared correct/hidden.py.
This is the guard of the CUI fork (docs/architecture.md): an app built on a CUI without pull request #12 types Persian
unjoined and left to right, and these checks then fail: query and rename text, Right stepping back in reading order,
a mixed Persian and English search, a new folder name, the address bar.
It types with WM_CHAR, the message a Unicode key press becomes, and saves the app's own frame of each box
(search.png, search-mixed.png, rename.png, new-folder.png, address.png) and results.json in the work folder.
"""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

HIDDEN = Path(r'C:\Users\Omid\.agents\source\correct')
GAMING = Path(r'C:\Users\Omid\.agents\tools\gaming.py')
SALAM, WANT, MIXED, FOLDER = 'سلام', 'می\u200cخواهم', 'گزارش report ۱۴۰۵', 'پوشه'
ROOT = Path(__file__).resolve().parents[2]
WORK = Path(os.environ.get('DFM_USE_WORK', ROOT / '.agent-work/use-checks' / Path(__file__).stem))
EXE = Path(os.environ.get('DFM_EXE', ROOT / 'build/MinSizeRel/dio-file-manager.exe'))
VK_BACK, VK_RETURN, VK_ESC, VK_HOME, VK_RIGHT, VK_F2, VK_CTRL, VK_SHIFT = 8, 13, 27, 0x24, 0x27, 0x71, 17, 16

U = C.WinDLL('user32', use_last_error=True)
K = C.WinDLL('kernel32', use_last_error=True)
ENUM = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
U.EnumWindows.argtypes = [ENUM, W.LPARAM]
U.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
U.GetWindowThreadProcessId.restype = W.DWORD
U.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
U.SendMessageTimeoutW.restype = C.c_ssize_t
U.AttachThreadInput.argtypes = [W.DWORD, W.DWORD, W.BOOL]
U.SetKeyboardState.argtypes = [C.POINTER(C.c_ubyte)]
U.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
U.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
U.InvalidateRect.argtypes = [W.HWND, C.c_void_p, W.BOOL]


class App:
    def __init__(self, exe, folder, out):
        self.out, self.base, self.mods, self.slowest = out, out / 'state', (), 0.0
        self.proc = subprocess.Popen([str(exe), '--path', str(folder), '--view', 'list', '--test-state', str(self.base), '--theme', 'light'],
                                     env=dict(os.environ, APPDATA=str(out / 'appdata')), creationflags=subprocess.CREATE_NO_WINDOW)
        self.hwnd = None

        def find(h, _):
            pid, title = W.DWORD(), C.create_unicode_buffer(256)
            U.GetWindowThreadProcessId(h, C.byref(pid))
            U.GetWindowTextW(h, title, 256)
            if pid.value == self.proc.pid and title.value == 'Dio File Manager':
                self.hwnd = h
            return True
        deadline = time.monotonic() + 15
        while not self.hwnd and time.monotonic() < deadline:
            U.EnumWindows(ENUM(find), 0)
            time.sleep(.05)
        assert self.hwnd, f'No app window (exit {self.proc.poll()})'
        self.tid, self.our = U.GetWindowThreadProcessId(self.hwnd, None), K.GetCurrentThreadId()
        assert U.AttachThreadInput(self.our, self.tid, True), C.get_last_error()
        self.keys(())
        self.until('The folder never listed', lambda: self.snap()['state'] != 0, 15)

    def msg(self, m, wp=0, lp=0):
        r, t = C.c_size_t(), time.monotonic()
        assert U.SendMessageTimeoutW(self.hwnd, m, wp, lp, 2, 2000, C.byref(r)), f'Window stopped answering (message {m:x})'
        return time.monotonic() - t

    def keys(self, mods):
        state = (C.c_ubyte * 256)()
        for k in mods:
            state[k] = 0x80
        U.SetKeyboardState(state)
        for k in set(self.mods) | set(mods):
            self.msg(0x100 if k in mods else 0x101, k, 1)
        self.mods = mods

    def key(self, k, mods=()):
        self.keys(mods)
        self.msg(0x100, k, 1)
        time.sleep(.05)
        self.msg(0x101, k, 0xC0000001)
        self.keys(())
        time.sleep(.12)

    def type(self, text):
        for ch in text:   # every character of these strings is in the Basic Multilingual Plane: one WM_CHAR each
            self.slowest = max(self.slowest, self.msg(0x102, ord(ch)))
            time.sleep(.03)
        time.sleep(.3)

    def snap(self, label=None):
        req = Path(str(self.base) + '.request')
        req.touch()
        U.InvalidateRect(self.hwnd, None, False)
        deadline = time.monotonic() + 5
        while req.exists() and time.monotonic() < deadline:
            U.PostMessageW(self.hwnd, 0, 0, 0)
            time.sleep(.03)
        assert not req.exists(), 'No state dump'
        s = json.loads(Path(str(self.base) + '.json').read_text(encoding='utf-8'))
        if label:
            shutil.copyfile(str(self.base) + '.png', self.out / f'{label}.png')
        return s

    def until(self, what, cond, seconds=6):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if cond():
                return
            time.sleep(.1)
        raise AssertionError(what)

    def close(self):
        if self.proc.poll() is None:
            U.AttachThreadInput(self.our, self.tid, False)
            U.PostMessageW(self.hwnd, 0x10, 0, 0)
            try:
                self.proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def inside(exe, out):
    box = out / 'sandbox'
    shutil.rmtree(box, ignore_errors=True)
    (box / FOLDER).mkdir(parents=True)
    for name in (SALAM, MIXED, 'notes'):
        (box / f'{name}.txt').write_text('x', encoding='utf-8')
    results, slow, app = [], {}, None

    def check(name, fn):
        nonlocal app
        app = App(exe, box, out)   # a fresh window for each box: no box inherits what the one before left open
        try:
            fn()
            results.append({'check': name, 'result': 'passed'})
        except Exception as e:
            app.snap(name + '-failed')
            results.append({'check': name, 'result': 'FAILED', 'reason': str(e)})
        finally:
            slow[name] = round(app.slowest * 1000, 1)
            app.close()
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)

    def search():
        app.key(ord('F'), (VK_CTRL,))
        app.until('The search box did not take the keyboard', lambda: app.snap()['typing'])
        app.type(SALAM)
        app.until(f'A search for {SALAM} did not find {SALAM}.txt', lambda: app.snap()['count'] == 1)
        s = app.snap('search')
        assert s['query'] == SALAM, f'Query is {s["query"]!r}'
        app.key(VK_BACK)
        assert app.snap()['query'] == SALAM[:-1], 'Backspace did not remove exactly one letter'
        app.type(SALAM[-1])
        app.key(VK_RIGHT)   # right to left: Right steps back one letter in reading order
        app.type('ا')
        q = app.snap()['query']
        assert q == 'سلاام', f'Right did not step back in reading order: {q!r}'
        app.key(VK_ESC)
        app.until('Esc did not leave search', lambda: not app.snap()['search'])

    def search_mixed():
        app.key(ord('F'), (VK_CTRL,))
        app.until('The search box did not take the keyboard', lambda: app.snap()['typing'])
        app.type(MIXED)
        app.until('A mixed search did not find its file', lambda: app.snap()['count'] == 1)
        app.snap('search-mixed')
        app.key(VK_ESC)
        app.until('Esc did not leave search', lambda: not app.snap()['search'])

    def rename():
        for k in (VK_HOME, 0x28):   # Home, then Down
            app.key(k)
            if app.snap()['selection']:
                break
        else:   # click the first row of the list
            sc = app.snap().get('scale', 1)
            lp = (int(103 * sc) << 16) | int(400 * sc)
            app.msg(0x201, 1, lp)
            app.msg(0x202, 0, lp)
        app.until('Nothing is selected to rename', lambda: app.snap()['selection'])
        app.key(VK_F2)
        app.until('Rename did not open', lambda: app.snap()['act'] & 1)
        app.until('The rename box did not take the keyboard', lambda: app.snap()['typing'])
        time.sleep(.3)   # the field selects the name on its second frame
        app.type(SALAM)
        app.snap('rename')
        app.key(VK_ESC)   # cancel: nothing is renamed
        app.until('Esc did not close rename', lambda: not app.snap()['act'] & 1)

    def new_folder():
        app.key(ord('N'), (VK_CTRL, VK_SHIFT))
        app.until('New folder did not open', lambda: app.snap()['act'] & 1)
        app.until('The name box did not take the keyboard', lambda: app.snap()['typing'])
        time.sleep(.3)
        app.type(WANT)
        app.snap('new-folder')
        app.key(VK_RETURN)
        app.until(f'Folder {WANT} was not made', lambda: (box / WANT).is_dir())

    def address():
        app.key(ord('L'), (VK_CTRL,))
        time.sleep(.3)
        app.type(SALAM)
        app.snap('address')
        app.key(ord('A'), (VK_CTRL,))
        app.type(str(box / FOLDER))
        app.snap('address-path')
        app.key(VK_RETURN)
        app.until(f'Enter did not open {FOLDER}', lambda: app.snap()['path'].rstrip('\\').endswith(FOLDER))

    for name, fn in (('search', search), ('search-mixed', search_mixed), ('new-folder', new_folder), ('rename', rename), ('address', address)):
        check(name, fn)
    results.append({'check': 'slowest-key-ms', 'result': slow})   # the longest time the window took to take one typed letter
    (out / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    return int(any(r['result'] == 'FAILED' for r in results))


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--inside':
        return inside(EXE, WORK)
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')   # the results hold Persian; a pipe is not UTF-8 by default
    for d in ('tmp', 'appdata'):
        (WORK / d).mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'), PYTHONIOENCODING='utf-8')
    code = subprocess.run([sys.executable, str(GAMING), 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable, str(GAMING), 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif code:
        return code
    sys.path.insert(0, str(HIDDEN))
    from hidden import popen
    with (WORK / 'persian.log').open('wb') as log, (WORK / 'persian.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], str(ROOT), env, b'', log, err)
        try:
            code = run.wait(timeout=240)
        finally:
            run.close()
    print((WORK / 'persian.log').read_text(encoding='utf-8', errors='replace') + (WORK / 'persian.err').read_text(encoding='utf-8', errors='replace'))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
