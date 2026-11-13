"""The header is the window's title bar: hit-testing, drag, double-click, window buttons, colours, dimming, icon.
Run from the repo: python tests/use/chrome_smoke.py
DFM_USE_WORK and DFM_EXE select isolated artifacts and the executable. Runs on a private desktop.
Real mouse input cannot reach a private desktop, so a drag is proved by the press Windows would send for it
(WM_NCLBUTTONDOWN on HTCAPTION starts the system Move loop) plus the keyboard Move loop that moves the window.
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import input_smoke as base
from input_smoke import App, U, K, WORK, ROOT, EXE

HTCLIENT, HTCAPTION, HTMAXBUTTON = 1, 2, 9
HTLEFT, HTRIGHT, HTTOP, HTTOPLEFT, HTTOPRIGHT, HTBOTTOM, HTBOTTOMLEFT, HTBOTTOMRIGHT = 10, 11, 12, 13, 14, 15, 16, 17
WM_NCHITTEST, WM_NCLBUTTONDOWN, WM_NCLBUTTONUP, WM_NCLBUTTONDBLCLK, WM_NCMOUSEMOVE, WM_GETICON = 0x84, 0xA1, 0xA2, 0xA3, 0xA0, 0x7F
PANE = {'dark': ((6, 8, 16), (16, 19, 28)), 'light': ((237, 240, 248), (247, 248, 252))}   # sidebar, content (docs/design.md)

U.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_int, C.c_int, C.c_int, C.c_int, W.HWND, W.HMENU, W.HINSTANCE, W.LPVOID]
U.CreateWindowExW.restype = W.HWND
U.SetActiveWindow.argtypes = [W.HWND]
U.IsZoomed.argtypes = [W.HWND]
U.IsIconic.argtypes = [W.HWND]
U.ShowWindow.argtypes = [W.HWND, C.c_int]
U.GetClassLongPtrW.argtypes = [W.HWND, C.c_int]
U.GetClassLongPtrW.restype = C.c_void_p
U.MonitorFromWindow.argtypes = [W.HWND, W.DWORD]
U.MonitorFromWindow.restype = W.HANDLE
U.GetMonitorInfoW.argtypes = [W.HANDLE, C.c_void_p]
U.DestroyWindow.argtypes = [W.HWND]
U.GetGUIThreadInfo.argtypes = [W.DWORD, C.c_void_p]


DWM = C.WinDLL('dwmapi')
DWM.DwmGetWindowAttribute.argtypes = [W.HWND, W.DWORD, C.c_void_p, W.DWORD]


def lp(x, y):
    return (int(x) & 0xFFFF) | ((int(y) & 0xFFFF) << 16)


def send(a, msg, wp=0, l=0):
    result = C.c_size_t()
    assert U.SendMessageTimeoutW(a.hwnd, msg, wp, l, 2, 1000, C.byref(result)), f'No answer to {msg:x}'
    return result.value


class Geo:
    """Screen coordinates of the header's parts, from the app's own state dump."""
    def __init__(self, a):
        self.s = a.snap()
        c = self.s['chrome']
        self.c, self.scale = c, c['scale']
        self.ox, self.oy = c['client'][0], c['client'][1]

    def px(self, x, y):   # points in the client -> screen pixels
        return self.ox + x * self.scale, self.oy + y * self.scale

    def hit(self, a, x, y):
        return send(a, WM_NCHITTEST, 0, lp(*self.px(x, y)))

    def mid(self, r):
        return r[0] + r[2] / 2, r[1] + r[3] / 2

    def gaps(self):
        """Empty header spans (x0, x1) in points, on the content header row."""
        rects = sorted([r for r in self.c['nodrag'] + self.c['buttons'] if r[1] < 46 and r[0] >= 240], key=lambda r: r[0])
        out, edge = [], 240
        for r in rects:
            if r[0] - edge > 20:
                out.append((edge, r[0]))
            edge = max(edge, r[0] + r[2])
        return out


def pixel(a, x, y, png=None):
    from PIL import Image
    with Image.open(png or str(a.base) + '.png') as im:
        return im.convert('RGB').getpixel((int(x), int(y)))


def near(c, e, tol=2):
    return all(abs(p - q) <= tol for p, q in zip(c, e))


def activate_other(a):
    other = U.CreateWindowExW(0, 'STATIC', 'other', 0x90000000, 0, 0, 200, 100, None, None, None, None)
    assert other, C.get_last_error()
    U.SetActiveWindow(other)
    a.until('Window did not become inactive', lambda: a.snap()['chrome']['active'] == 0)
    return other


def inside():
    results = []

    def check(name, fn, theme='dark', path=None):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        app = None
        try:
            app = App(name, path or WORK / 'fixture', 'grid', theme)
            fn(app)
            app.snap(name + '-passed')
            results.append({'check': name, 'result': 'passed'})
        except Exception as e:
            if app:
                try:
                    app.snap(name + '-failed')
                except Exception:
                    pass
            results.append({'check': name, 'result': 'FAILED', 'reason': str(e)})
        finally:
            if app:
                app.close()
        print(json.dumps(results[-1]), flush=True)

    def hit_testing(a):
        g = Geo(a)
        c = g.c
        assert c['client'][1] == c['window'][1], f"The client does not start at the window's top edge: {c}"
        assert U.IsZoomed(a.hwnd) == 0
        gaps = g.gaps()
        assert gaps, f'No empty header space on the content header: {c["nodrag"]}'
        gx = max(gaps, key=lambda s: s[1] - s[0])
        x = (gx[0] + gx[1]) / 2
        assert g.hit(a, x, 23) == HTCAPTION, f'Empty content header at {x}: {g.hit(a, x, 23)}'
        assert g.hit(a, 100, 23) == HTCAPTION, 'Empty sidebar header'
        assert g.hit(a, 100, 8) == HTCAPTION, 'Sidebar header below the resize strip'
        assert g.hit(a, 100, 1) == HTTOP and g.hit(a, x, 1) == HTTOP, 'The top strip of the header resizes'
        for i, name in enumerate(('minimize', 'maximize', 'close')):
            want = HTMAXBUTTON if i == 1 else HTCLIENT
            got = g.hit(a, *g.mid(c['buttons'][i]))
            assert got == want, f'{name} button: {got}, want {want}'
        for r in c['nodrag']:
            got = g.hit(a, *g.mid(r))
            assert got == HTCLIENT, f'Header control {r}: {got}'
        assert len([r for r in c['nodrag'] if r[0] < 240]) >= 1, 'Main menu not marked'
        assert g.hit(a, 400, 120) == HTCLIENT, 'Client below the header'
        w = c['window']
        mx, my = (w[0] + w[2]) // 2, (w[1] + w[3]) // 2
        edges = {'left': ((w[0] + 2, my), HTLEFT), 'right': ((w[2] - 3, my), HTRIGHT), 'bottom': ((mx, w[3] - 2), HTBOTTOM),
                 'top': ((mx, w[1] + 1), HTTOP), 'top-left': ((w[0] + 2, w[1] + 2), HTTOPLEFT),
                 'top-right': ((w[2] - 3, w[1] + 2), HTTOPRIGHT), 'bottom-left': ((w[0] + 2, w[3] - 2), HTBOTTOMLEFT),
                 'bottom-right': ((w[2] - 3, w[3] - 2), HTBOTTOMRIGHT)}
        for name, ((px, py), want) in edges.items():
            got = send(a, WM_NCHITTEST, 0, lp(px, py))
            assert got == want, f'Resize edge {name} at {(px, py)}: {got}, want {want}'

    def drag(a):
        g = Geo(a)
        x = (max(g.gaps(), key=lambda s: s[1] - s[0])[0] + max(g.gaps(), key=lambda s: s[1] - s[0])[1]) / 2
        sx, sy = g.px(x, 23)
        info = C.create_string_buffer(72)   # GUITHREADINFO
        def moving():
            C.c_uint.from_buffer(info, 0).value = 72
            U.GetGUIThreadInfo(a.tid, info)
            flags = C.c_uint.from_buffer(info, 4).value
            return bool(flags & 2)
        # Windows starts a mouse move only after the pointer travels past the drag threshold, which a private desktop
        # cannot do. What decides the drag is the hit test; the press must also reach the default handler without harm.
        assert send(a, WM_NCHITTEST, 0, lp(sx, sy)) == HTCAPTION, 'Empty header space is not the caption'
        # The keyboard Move loop is the same system loop: it must move the window.
        r0 = W.RECT(); U.GetWindowRect(a.hwnd, C.byref(r0))
        assert U.PostMessageW(a.hwnd, 0x112, 0xF012, 0)   # SC_MOVE
        a.until('SC_MOVE did not start', moving, 3)
        for _ in range(6):
            U.PostMessageW(a.hwnd, 0x100, 0x27, 1)   # right arrow
            time.sleep(.05)
        U.PostMessageW(a.hwnd, 0x100, 13, 1)
        a.until('Move loop did not end after Enter', lambda: not moving(), 3)
        r1 = W.RECT(); U.GetWindowRect(a.hwnd, C.byref(r1))
        assert r1.left > r0.left, f'The window did not move: {r0.left} -> {r1.left}'

    def double_click(a):
        g = Geo(a)
        gx = max(g.gaps(), key=lambda s: s[1] - s[0])
        sx, sy = g.px((gx[0] + gx[1]) / 2, 23)
        assert not U.IsZoomed(a.hwnd)
        send(a, WM_NCLBUTTONDBLCLK, HTCAPTION, lp(sx, sy))
        a.until('Double click did not maximize', lambda: U.IsZoomed(a.hwnd), 3)
        time.sleep(.4)
        s = a.snap('maximized-dark')
        c = s['chrome']
        class MI(C.Structure):
            _fields_ = [('cb', W.DWORD), ('mon', W.RECT), ('work', W.RECT), ('flags', W.DWORD)]
        mi = MI(); mi.cb = C.sizeof(mi)
        assert U.GetMonitorInfoW(U.MonitorFromWindow(a.hwnd, 2), C.byref(mi))
        w = mi.work
        assert c['maximized'] and c['client'] == [w.left, w.top, w.right, w.bottom], f'Maximized client {c["client"]} is not the work area {(w.left, w.top, w.right, w.bottom)}'
        assert near(pixel(a, 100, 1), PANE['dark'][0], 1) and near(pixel(a, 700, 1), PANE['dark'][1], 1), 'Maximized: top rows are not ours'
        g = Geo(a)
        gx = max(g.gaps(), key=lambda s: s[1] - s[0])
        sx, sy = g.px((gx[0] + gx[1]) / 2, 23)
        send(a, WM_NCLBUTTONDBLCLK, HTCAPTION, lp(sx, sy))
        a.until('Second double click did not restore', lambda: not U.IsZoomed(a.hwnd), 3)

    def buttons(a):
        g = Geo(a)
        cx, cy = g.mid(g.c['buttons'][1])
        before = pixel(a, *[v * g.scale for v in (cx + 8, cy)])
        a.click(cx, cy)
        a.until('Maximize button did not maximize', lambda: U.IsZoomed(a.hwnd), 3)
        time.sleep(.4)
        g = Geo(a)
        cx, cy = g.mid(g.c['buttons'][1])
        a.click(cx, cy)
        a.until('Maximize button did not restore', lambda: not U.IsZoomed(a.hwnd), 3)
        time.sleep(.4)
        # The press Windows sends for HTMAXBUTTON (the real mouse never reaches the client there).
        g = Geo(a)
        cx, cy = g.mid(g.c['buttons'][1])
        sx, sy = g.px(cx, cy)
        send(a, WM_NCMOUSEMOVE, HTMAXBUTTON, lp(sx, sy))
        time.sleep(.3)
        a.snap()
        hover = pixel(a, *[v * g.scale for v in (cx + 8, cy)])
        assert hover != before, f'Hovering maximize changed nothing: {before} {hover}'
        send(a, WM_NCLBUTTONDOWN, HTMAXBUTTON, lp(sx, sy))
        send(a, WM_NCLBUTTONUP, HTMAXBUTTON, lp(sx, sy))
        a.until('NC click on maximize did not maximize', lambda: U.IsZoomed(a.hwnd), 3)
        send(a, 0x112, 0xF120, 0)   # SC_RESTORE
        a.until('Restore failed', lambda: not U.IsZoomed(a.hwnd), 3)
        mx, my = g.mid(g.c['buttons'][0])
        a.click(mx, my)
        a.until('Minimize button did not minimize', lambda: U.IsIconic(a.hwnd), 3)
        U.ShowWindow(a.hwnd, 9)
        a.until('Window did not come back', lambda: not U.IsIconic(a.hwnd), 3)

    def close_button(a):
        g = Geo(a)
        a.click(*g.mid(g.c['buttons'][2]))
        try:
            code = a.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            raise AssertionError('Close button did not end the app')
        assert code == 0, f'Exit code {code}'

    def colours(theme):
        def run(a):
            sidebar, content = PANE[theme]
            for state in ('active', 'inactive'):
                if state == 'inactive':
                    other = activate_other(a)
                time.sleep(.3)
                s = a.snap(f'{theme}-{state}')
                assert s['chrome']['active'] == (state == 'active'), state
                for x, want in ((60, sidebar), (180, sidebar), (700, content), (900, content)):
                    for y in (0, 1, 2):
                        got = pixel(a, x * s['chrome']['scale'], y)
                        assert near(got, want, 1), f'{theme} {state}: pixel ({x},{y}) is {got}, want {want}'
                if state == 'inactive':
                    dimmed = a.snap()
                    # DWM border colour: no Windows grey.
                    border = C.c_uint()
                    DWM.DwmGetWindowAttribute(a.hwnd, 34, C.byref(border), 4)
                    r, g_, b = border.value & 255, (border.value >> 8) & 255, (border.value >> 16) & 255
                    assert near((r, g_, b), (37, 40, 48) if theme == 'dark' else (226, 228, 233), 2), f'Border colour {(r, g_, b)}'
                    U.SetActiveWindow(a.hwnd)
                    U.DestroyWindow(other)
        return run

    def dimming(a):
        def brightest(rect):
            from PIL import Image
            s = a.snap()
            k = s['chrome']['scale']
            with Image.open(str(a.base) + '.png') as im:
                box = tuple(int(v * k) for v in (rect[0], rect[1], rect[0] + rect[2], rect[1] + rect[3]))
                return max(sum(p[:3]) for p in im.convert('RGB').crop(box).getdata())
        g = Geo(a)
        icons = [r for r in g.c['nodrag'] if r[1] < 46 and r[2] <= 40 and r[3] <= 40]
        assert len(icons) >= 6, f'Expected the header icon buttons: {icons}'
        active = [brightest(r) for r in icons] + [brightest(g.c['buttons'][i]) for i in (0, 1, 2)]
        other = activate_other(a)
        time.sleep(.3)
        inactive = [brightest(r) for r in icons] + [brightest(g.c['buttons'][i]) for i in (0, 1, 2)]
        assert all(i < a_ for i, a_ in zip(inactive, active)), f'Icons did not all dim: {active} -> {inactive}'
        # Clicks pass through the veil on the main menu while inactive.
        mr = [r for r in icons if r[0] < 240][0]
        a.click(*g.mid(mr))
        time.sleep(.5)
        assert pixel(a, 90 * g.scale, 120 * g.scale, None) is not None
        a.snap('menu-open-inactive')
        s0 = pixel(a, 120 * g.scale, 110 * g.scale)
        a.key(27)
        U.SetActiveWindow(a.hwnd)
        U.DestroyWindow(other)

    def menu_click_through(a):
        g = Geo(a)
        mr = [r for r in g.c['nodrag'] if r[0] < 240][0]
        closed = pixel(a, 100 * g.scale, 90 * g.scale)
        a.click(*g.mid(mr))
        time.sleep(.6)
        a.snap()
        opened = pixel(a, 100 * g.scale, 90 * g.scale)
        assert opened != closed, 'The main menu did not open from a click on its icon'

    def icon(a):
        big, small = send(a, WM_GETICON, 1, 0), send(a, WM_GETICON, 0, 0)
        assert big and small, f'Window icons: big {big:x} small {small:x}'
        assert U.GetClassLongPtrW(a.hwnd, -14), 'No class icon'
        assert U.GetClassLongPtrW(a.hwnd, -34), 'No small class icon'

    def maximized_picture(a):
        send(a, 0x112, 0xF030, 0)   # SC_MAXIMIZE
        a.until('Not maximized', lambda: U.IsZoomed(a.hwnd), 3)
        time.sleep(.6)
        a.snap('maximized-picture')
        if os.environ.get('DFM_CHROME_PICTURES'):
            shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / 'dark-maximized.png')

    check('1-hit-testing', hit_testing)
    check('2-drag', drag)
    check('3-double-click', double_click)
    check('4-buttons', buttons)
    check('5-close-button', close_button)
    check('6-colours-dark', colours('dark'), 'dark')
    check('6-colours-light', colours('light'), 'light')
    check('7-dimming', dimming, 'dark')
    check('8-menu-click-through', menu_click_through, 'dark')
    check('9-icon', icon)
    check('10-maximized-picture', maximized_picture)
    (WORK / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'tmp').mkdir(exist_ok=True)
    (WORK / 'appdata').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = Path(r'C:\Users\Omid\.agents\tools\gaming.py')
    code = subprocess.run([sys.executable, str(gaming), 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable, str(gaming), 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif code:
        return code
    (WORK / 'fixture').mkdir(exist_ok=True)
    for folder in 'ABCD':
        (WORK / 'fixture' / folder).mkdir(exist_ok=True)
    for name in ('1.txt', '2.txt', '3.txt'):
        (WORK / 'fixture' / name).touch()
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'chrome.log').open('wb') as out, (WORK / 'chrome.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=600)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())
