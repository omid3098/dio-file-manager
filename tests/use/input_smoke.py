"""Real Win32 input on a private desktop. No default file handlers are invoked.
Run from the repo: python tests/use/input_smoke.py
Artifacts stay in .agent-work/use-v1. Requires the shared correct/hidden.py.
"""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / '.agent-work/use-v1'
EXE = ROOT / 'build/MinSizeRel/dio-file-manager.exe'
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


class App:
    def __init__(self, name, path, view='grid'):
        self.base = WORK / name
        Path(str(self.base)+'.request').unlink(missing_ok=True)
        self.proc = subprocess.Popen([str(EXE), '--path', str(path), '--view', view,
                                      '--test-state', str(self.base), '--theme', 'light'],
                                     cwd=ROOT, creationflags=subprocess.CREATE_NO_WINDOW)
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

    def cell(self, index, mods=()):
        s = self.snap()
        x,y,w,h = s['row']
        col = index % s['cols']
        row = index // s['cols'] - s['first']
        self.click(x + 16 + (col+.5)*(w-32)/s['cols'], y + row*h + 40, mods)

    def snap(self, label=None):
        request = Path(str(self.base)+'.request')
        request.touch()
        deadline = time.monotonic() + 5
        while request.exists() and time.monotonic() < deadline:
            self.msg(0)
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
        self.keys(())
        U.AttachThreadInput(self.our_tid, self.tid, False)
        self.msg(0x10)
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def inside():
    results = []
    def check(name, folder, fn, view='grid'):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        app = None
        try:
            app = App(name, folder, view)
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
        assert after['first'] > before['first'], f'Wheel did not move: {before["first"]} -> {after["first"]}'
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
        a.cell(0); a.cell(0)
        assert a.wait_ready()['path'] == str(WORK/'fixture'/'A'), 'Double click did not open folder'
        a.key(37,(18,)); assert a.wait_ready()['path'] == str(WORK/'fixture'), 'Back'
        a.key(39,(18,)); assert a.wait_ready()['path'].endswith('\\A'), 'Forward'
        a.key(38,(18,)); assert a.wait_ready()['path'] == str(WORK/'fixture'), 'Parent'
        before = a.snap()['path']; a.key(8)
        assert a.snap()['path'] == before, 'Backspace unexpectedly navigates'

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

    check('1-grid-wheel', WORK/'big', wheel)
    check('2-list-wheel', WORK/'big', wheel, 'list')
    check('3-navigation', WORK/'fixture', navigation)
    check('4-selection', WORK/'fixture', selection)
    check('5-shortcuts', WORK/'fixture', shortcuts)
    check('6-sidebar', WORK/'fixture', sidebar)
    check('7-narrow', WORK/'fixture', narrow)
    check('8-empty', WORK/'empty-pristine', empty)
    check('8-unreadable', 'C:\\System Volume Information', denied)
    (WORK/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    WORK.mkdir(parents=True,exist_ok=True)
    (WORK/'tmp').mkdir(exist_ok=True)
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
            return run.wait(timeout=120)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())
