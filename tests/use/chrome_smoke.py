"""The header is the window's title bar: hit-testing, drag, double-click, window buttons, colours, dimming, icon.
Run from the repo: python tests/use/chrome_smoke.py
DFM_USE_WORK and DFM_EXE select isolated artifacts and the executable. Runs on a private desktop.
Real mouse input cannot reach a private desktop, so a drag is proved by the hit test (HTCAPTION decides it) plus the window
following SetWindowPos moves while the app is told a move is on (WM_ENTERSIZEMOVE ... WM_EXITSIZEMOVE).
The check never starts a system move or size loop: the pointer belongs to the whole window station, and that loop moves it
for real, also from a hidden desktop. tests/test_pointer_safe.py fails if any file here does.
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
# The main menu holds the default file manager switch: a stray pick must never reach the real registry or launcher folder.
os.environ.setdefault('DFM_TEST_REGROOT', r'Software\DioFmTest\chrome-%d' % os.getpid())
import input_smoke as base
from input_smoke import App, U, K, WORK, ROOT, EXE

HTCLIENT, HTCAPTION, HTMAXBUTTON = 1, 2, 9
HTLEFT, HTRIGHT, HTTOP, HTTOPLEFT, HTTOPRIGHT, HTBOTTOM, HTBOTTOMLEFT, HT_BOTTOM_RIGHT = 10, 11, 12, 13, 14, 15, 16, 17
WM_NCHITTEST, WM_NCPRESS, WM_NCLBUTTONUP, WM_NCLBUTTONDBLCLK, WM_NCMOUSEMOVE, WM_GETICON = 0x84, 161, 0xA2, 0xA3, 0xA0, 0x7F   # WM_NCPRESS: left button down in the non-client area
WM_ENTERSIZEMOVE, WM_EXITSIZEMOVE = 0x231, 0x232
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
U.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
COPY = WORK / 'copy-sandbox'   # a 128 MiB file the copy check copies slowly (DFM_TEST_COPY_DELAY_MS)


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


helpers = []


def activate_other(a):
    other = U.CreateWindowExW(0, 'STATIC', 'other', 0x90000000, 0, 0, 200, 100, None, None, None, None)
    assert other, C.get_last_error()
    helpers.append(other)
    U.SetActiveWindow(other)
    a.until('Window did not become inactive', lambda: a.snap()['chrome']['active'] == 0)
    return other


def inside():
    results = []

    def check(name, fn, theme='dark', path=None, ends=False):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        app = None
        try:
            app = App(name, path or WORK / 'fixture', 'grid', theme)
            fn(app)
            if not ends:   # a case that ends the app has no state left to dump
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
            while helpers:
                U.DestroyWindow(helpers.pop())
        print(json.dumps(results[-1]), flush=True)

    def bottom_bar(a):
        """The bottom bar's controls left to right: Back, Forward, view options (empty when there is no bar)."""
        c = a.snap()['chrome']
        h = (c['client'][3] - c['client'][1]) / c['scale']
        return sorted((r for r in c['nodrag'] if r[1] >= h - 46 and r[1] > 46 and r[2] <= 72), key=lambda r: r[0])

    def set_client(a, cw, ch):
        """Size the window so its client is cw x ch points (the frame's size is read back and corrected for)."""
        U.SetWindowPos(a.hwnd, None, 40, 40, cw + 16, ch + 38, 0x14)
        for _ in range(40):
            time.sleep(.1)
            s = a.snap()
            c = s['chrome']
            got = ((c['client'][2] - c['client'][0]) / c['scale'], (c['client'][3] - c['client'][1]) / c['scale'])
            if abs(got[0] - cw) <= 1 and abs(got[1] - ch) <= 1:
                return s
            frame_w = (c['window'][2] - c['window'][0]) - (c['client'][2] - c['client'][0])
            frame_h = (c['window'][3] - c['window'][1]) - (c['client'][3] - c['client'][1])
            U.SetWindowPos(a.hwnd, None, 40, 40, int(cw * c['scale']) + frame_w, int(ch * c['scale']) + frame_h, 0x14)
        raise AssertionError(f'Could not size the client to {cw}x{ch}: {got}')

    def ends_with(a, folder):
        return a.snap()['path'].rstrip('\\').endswith(folder)

    def history_clicks(a, tag):
        """Back and Forward in the bottom bar take real clicks: Back leaves folder B for A, Forward returns."""
        g = Geo(a)
        bar = bottom_bar(a)
        assert len(bar) == 3, f'{tag}: bottom bar {bar}'
        back, fwd = bar[0], bar[1]
        assert back[2] >= 28 and fwd[2] >= 28, f'{tag}: Back or Forward has no hit area {back} {fwd}'
        assert ends_with(a, 'B'), a.snap()['path']
        a.click(*g.mid(back))
        a.until(f'{tag}: Back did not leave folder B', lambda: ends_with(a, 'A'))
        a.click(*g.mid(fwd))
        a.until(f'{tag}: Forward did not return to folder B', lambda: ends_with(a, 'B'))

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
        right = [r for r in c['nodrag'] if r[0] > 240 and 30 <= r[2] <= 72]
        assert len(right) >= 4, f'Back, Forward, Search and view-options controls must all be marked: {right}'
        assert g.hit(a, 400, 120) == HTCLIENT, 'Client below the header'
        w = c['window']
        mx, my = (w[0] + w[2]) // 2, (w[1] + w[3]) // 2
        edges = {'left': ((w[0] + 2, my), HTLEFT), 'right': ((w[2] - 3, my), HTRIGHT), 'bottom': ((mx, w[3] - 2), HTBOTTOM),
                 'top': ((mx, w[1] + 1), HTTOP), 'top-left': ((w[0] + 2, w[1] + 2), HTTOPLEFT),
                 'top-right': ((w[2] - 3, w[1] + 2), HTTOPRIGHT), 'bottom-left': ((w[0] + 2, w[3] - 2), HTBOTTOMLEFT),
                 'bottom-right': ((w[2] - 3, w[3] - 2), HT_BOTTOM_RIGHT)}
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
        # cannot do. What decides the drag is the hit test. The system move loop (the system-menu move command, a press on the caption)
        # is never started here: it moves the real pointer, even from a hidden desktop.
        assert send(a, WM_NCHITTEST, 0, lp(sx, sy)) == HTCAPTION, 'Empty header space is not the caption'
        r0 = W.RECT(); U.GetWindowRect(a.hwnd, C.byref(r0))
        before = a.snap()['chrome']['window']
        assert not moving(), 'A system move or size loop is running before the check moved anything'
        send(a, WM_ENTERSIZEMOVE)   # what the loop tells the window; the app only notes it
        try:
            for step in range(1, 7):
                assert U.SetWindowPos(a.hwnd, None, r0.left + 10 * step, r0.top, 0, 0, 0x15), C.get_last_error()   # NOSIZE | NOZORDER | NOACTIVATE
                time.sleep(.05)
                assert not moving(), 'A system move loop started'
        finally:
            send(a, WM_EXITSIZEMOVE)
        r1 = W.RECT(); U.GetWindowRect(a.hwnd, C.byref(r1))
        assert r1.left == r0.left + 60 and r1.top == r0.top, f'The window did not move: {r0.left},{r0.top} -> {r1.left},{r1.top}'
        after = a.snap()['chrome']['window']
        assert after[0] == before[0] + 60 and after[1] == before[1], f'The app did not follow the move: {before} -> {after}'
        assert not moving(), 'A system move loop is running after the move'

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
        # Test mode ignores the automatic leave messages, so the posted move stays until the driver moves it.
        send(a, WM_NCMOUSEMOVE, HTMAXBUTTON, lp(sx, sy))
        sh = a.snap()
        assert sh['chrome']['nc'][0] == 1, f"The app did not take the pointer over the maximize button: {sh['chrome']['nc']}"
        hover = pixel(a, *[v * g.scale for v in (cx + 8, cy)])
        assert hover != before, f'Hovering maximize changed nothing: {before} {hover}'
        send(a, WM_NCPRESS, HTMAXBUTTON, lp(sx, sy))   # the app takes this press itself (chrome_win.c): no system tracking loop
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
        t0 = time.monotonic()
        try:
            code = a.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            raise AssertionError(f'Close button did not end the app within {time.monotonic() - t0:.1f} s')
        print(json.dumps({'close_ms': round((time.monotonic() - t0) * 1000)}), flush=True)
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
                    # DWM cannot read the border colour back (E_INVALIDARG), so the app reports what it handed to Windows
                    # and whether Windows accepted it: no Windows grey.
                    ch = dimmed['chrome']
                    assert ch['border_hr'] == 0, f"Windows refused the border colour: {ch['border_hr'] & 0xFFFFFFFF:08x}"
                    want_b = (33, 36, 45) if theme == 'dark' else (223, 225, 231)
                    assert near(tuple(ch['border']), want_b, 1), f"Border colour {ch['border']}, want {want_b}"
                    U.SetActiveWindow(a.hwnd)
                    (U.DestroyWindow(other), helpers.remove(other))
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
        icons = [r for r in g.c['nodrag'] if r[1] < 46 and r[2] <= 72 and r[3] <= 40]
        assert len(icons) >= 5, f'Expected the header icon buttons: {icons}'
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
        (U.DestroyWindow(other), helpers.remove(other))

    def menu_click_through(a):
        from PIL import Image, ImageChops
        g = Geo(a)
        mr = [r for r in g.c['nodrag'] if r[0] < 240][0]
        a.snap()
        box = tuple(int(v * g.scale) for v in (200, 50, 400, 220))
        with Image.open(str(a.base) + '.png') as im:
            closed = im.convert('RGB').crop(box).copy()
        a.click(*g.mid(mr))
        time.sleep(.6)
        a.snap()
        with Image.open(str(a.base) + '.png') as im:
            opened = im.convert('RGB').crop(box).copy()
        changed = sum(1 for p in ImageChops.difference(closed, opened).convert('L').getdata() if p > 8)
        assert changed > 2000, f'The main menu did not open from a click on its icon ({changed} pixels changed under it)'

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

    def widths(theme):
        """At every supported width the three window buttons stay inside the window, clear of every other header control,
        and the header's hit-testing agrees with where they are. Both sides of GNOME Files' 682 px breakpoint (683 px is the first
        wide window): below it Back and Forward sit in the bottom bar, above it in the header."""
        def run(a):
            counts = {}
            for cw in (360, 390, 480, 640, 682, 683, 800, 1024, 1280):
                s = None
                for _ in range(40):
                    U.SetWindowPos(a.hwnd, None, 40, 40, int(cw * (s['chrome']['scale'] if s else 1)) + 16, 608, 0x14)
                    time.sleep(.1)
                    s = a.snap()
                    c = s['chrome']
                    got = (c['client'][2] - c['client'][0]) / c['scale']
                    if abs(got - cw) <= 1:
                        break
                    U.SetWindowPos(a.hwnd, None, 40, 40, int(cw * c['scale'] + (c['window'][2] - c['window'][0]) - (c['client'][2] - c['client'][0])), 608, 0x14)
                assert abs(got - cw) <= 1, f'Could not size the client to {cw}: {got}'
                g = Geo(a)
                c = g.c
                btn = c['buttons']
                names = ['minimize', 'maximize', 'close']
                for n, r in zip(names, btn):
                    assert r[2] > 0 and r[0] >= 0 and r[0] + r[2] <= cw + 0.5, f'{theme} {cw} px: {n} button {r} is outside the {cw} px window'
                    assert 0 <= r[1] and r[1] + r[3] <= 46, f'{theme} {cw} px: {n} button {r} is outside the header'
                assert btn[0][0] + btn[0][2] <= btn[1][0] and btn[1][0] + btn[1][2] <= btn[2][0], f'{theme} {cw} px: window buttons overlap {btn}'
                assert cw - (btn[2][0] + btn[2][2]) >= 4, f'{theme} {cw} px: close button touches the window edge {btn[2]}'
                others = [r for r in c['nodrag'] if r[1] < 46 and r[2] > 0]
                for r in others:
                    assert r[0] >= 0 and r[0] + r[2] <= btn[0][0] + 0.5, f'{theme} {cw} px: header control {r} reaches the window buttons or leaves the window {btn[0]}'
                others.sort()
                for r, n in zip(others, others[1:]):
                    assert r[0] + r[2] <= n[0] + 0.5, f'{theme} {cw} px: header controls overlap: {r} {n}'
                for i, r in enumerate(btn):
                    h = g.hit(a, *g.mid(r))
                    want = HTMAXBUTTON if i == 1 else HTCLIENT
                    assert h == want, f'{theme} {cw} px: the {names[i]} button answers hit-test {h}, not {want}'
                counts[cw] = len(others)
                bar = bottom_bar(a)
                if cw < 683:
                    assert len(bar) == 3, f'{theme} {cw} px: the bottom bar should hold Back, Forward and view options: {bar}'
                    assert all(r[0] >= 0 and r[0] + r[2] <= cw + 0.5 for r in bar), f'{theme} {cw} px: bottom bar control outside the window {bar}'
                else:
                    assert not bar, f'{theme} {cw} px: a wide window has no bottom bar: {bar}'
                    assert len([r for r in others if 240 <= r[0] < 330 and r[2] <= 40]) >= 2, f'{theme} {cw} px: Back and Forward missing from the header {others}'
                a.msg(0x200, 0, a.point(cw / 2, 300))   # the pointer rests in the window's body, over no header control
                time.sleep(.4)
                a.snap(f'width-{theme}-{cw}')
                if os.environ.get('DFM_CHROME_PICTURES') and cw in (360, 640, 682, 683, 1280):
                    shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'width-{theme}-{cw}.png')
                if cw in (360, 640, 683):   # the collapsed actions are reachable: the main menu opens and grows with what moved into it
                    from PIL import Image, ImageChops
                    mr = [r for r in others if r[0] < 240][0] if cw >= 683 else others[-1]
                    box = tuple(int(v * g.scale) for v in (max(0, mr[0] - 230), 40, min(cw, mr[0] + 130), 400))
                    a.snap()
                    with Image.open(str(a.base) + '.png') as im:
                        closed = im.convert('RGB').crop(box).copy()
                    a.click(*g.mid(mr))
                    time.sleep(.6)
                    a.snap()
                    with Image.open(str(a.base) + '.png') as im:
                        opened = im.convert('RGB').crop(box).copy()
                    changed = sum(1 for p in ImageChops.difference(closed, opened).convert('L').getdata() if p > 8)
                    assert changed > 3000, f'{theme} {cw} px: the main menu did not open ({changed} pixels changed)'
                    if os.environ.get('DFM_CHROME_PICTURES'):
                        shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'menu-{theme}-{cw}.png')
                    a.key(0x1B)
                    a.msg(0x200, 0, a.point(cw / 2, 300))
                    time.sleep(.3)
            assert counts[360] <= counts[640] <= counts[1280] and counts[360] < counts[1280], f'Actions did not collapse in order: {counts}'
            print(json.dumps({'visible_controls': counts}), flush=True)
        return run

    def short_menu(a):
        """The main menu scrolls in the shortest window of the first wide width (it is in the sidebar there), so Theme and Show Hidden Files stay reachable."""
        from PIL import Image, ImageChops
        set_client(a, 683, 280)
        g = Geo(a)
        c = g.c
        mr = min(r for r in c['nodrag'] if r[1] < 46 and r[2] > 0)
        a.click(*g.mid(mr))
        time.sleep(.6)
        box = tuple(int(v * g.scale) for v in (190, 55, 420, 270))
        a.snap()
        with Image.open(str(a.base) + '.png') as im:
            top = im.convert('RGB').crop(box).copy()
        for _ in range(15):
            a.msg(0x200, 0, a.point(300, 150))
            a.msg(0x20A, (0x10000 - 120) << 16)
            time.sleep(.05)
        time.sleep(.5)
        a.snap()
        with Image.open(str(a.base) + '.png') as im:
            bottom = im.convert('RGB').crop(box).copy()
        changed = sum(1 for p in ImageChops.difference(top, bottom).convert('L').getdata() if p > 8)
        assert changed > 3000, f'The menu did not scroll in a 280 px window ({changed} pixels changed)'
        if os.environ.get('DFM_CHROME_PICTURES'):
            shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / 'menu-dark-700x280-scrolled.png')

    def bottom_bar_history(a, theme):
        """S3: at 360 px Back and Forward are directly clickable buttons in the bottom bar (never inside the main menu)."""
        fixture = Path(a.folder)
        set_client(a, 360, 600)
        a.path(fixture / 'A')
        a.path(fixture / 'B')
        history_clicks(a, '360 px')
        if os.environ.get('DFM_CHROME_PICTURES'):
            a.snap()
            shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'{theme}-360-bottom-bar.png')

    def bottom_bar_copying(a, theme):
        """S3: the same while a copy runs (its progress ring takes room in the header, as before)."""
        from clipboard_guard import preserved_clipboard
        src, dst = COPY / 'source', COPY / 'target'
        shutil.rmtree(dst, ignore_errors=True)
        dst.mkdir(parents=True)
        set_client(a, 360, 600)
        a.path(src)
        a.cell(0)
        with preserved_clipboard(a.hwnd):
            a.key(ord('C'), (17,))
            a.path(dst)
            a.key(ord('V'), (17,))
            a.until('The copy did not start', lambda: a.snap()['jobs'], seconds=20)
            fixture = Path(a.folder)
            a.path(fixture / 'A')
            a.path(fixture / 'B')
            s = a.snap('copy-running-360')
            assert s['jobs'], 'The copy ended before Back and Forward were tried'
            assert s['job_button'][2] > 0, f"No job ring while copying: {s['job_button']}"
            if os.environ.get('DFM_CHROME_PICTURES'):
                shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'{theme}-360-with-copy-running.png')
            history_clicks(a, '360 px with a copy running')
            assert a.snap()['jobs'], 'The copy ended before the clicks were done'
            c = a.snap()['chrome']
            btn = c['buttons']
            hdr = sorted((r for r in c['nodrag'] if r[1] < 46 and r[2] > 0), key=lambda r: r[0])
            for r in hdr:
                assert r[0] + r[2] <= btn[0][0] + 0.5, f'Header control {r} reaches the window buttons {btn[0]}'
            for r, n in zip(hdr, hdr[1:]):
                assert r[0] + r[2] <= n[0] + 0.5, f'Header controls overlap while copying: {r} {n}'
            assert len(hdr) == 5, f'The header should hold the sidebar button, path bar, copy ring, Search and the menu while a copy runs: {hdr}'

    def menu_follows_keyboard(a, theme):
        """S2: in a window too short for the whole menu, the row the arrow keys highlight stays on screen, and Enter picks it.
        683 px wide: the menu (in the sidebar) scrolls at 280 px high. 360 px wide: the narrow menu is the shortest that fits."""
        for cw in (683, 360):   # 683 px: the first wide window, where Search has moved into the menu
            set_client(a, cw, 280)
            g = Geo(a)
            header = [r for r in g.c['nodrag'] if r[1] < 46 and r[2] > 0]
            mr = min(header) if cw >= 683 else header[-1]
            a.click(*g.mid(mr))
            time.sleep(.6)
            before = a.snap()['hidden']

            def visible():
                s = a.snap()
                row, view = s['menu_row'], s['menu_view']
                return view[3] > 0 and row[3] > 0 and row[1] >= view[1] - 0.5 and row[1] + row[3] <= view[1] + view[3] + 0.5

            def step(key, what):
                a.key(key)
                a.until(f'{cw} px: the highlighted row is off screen after {what}', visible)
            if cw == 683:
                view = a.snap()['menu_view']
                assert view[3] == 0, 'The menu view is only reported for a highlighted row'
            step(0x26, 'Up from the top (to the last row)')
            a.snap(f'menu-last-row-highlighted-{cw}')
            view = a.snap()['menu_view']
            if cw == 683:
                assert abs(view[3] - 210) < 1, f'The 683 px menu is not held to the window (210 px): it would not need to scroll {view}'
            if os.environ.get('DFM_CHROME_PICTURES') and cw == 360:
                shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'menu-{theme}-360x280-keyboard-last.png')
            for n in range(7):
                step(0x26, f'Up {n + 2}')
            for n in range(12):
                step(0x28, f'Down {n + 1}')   # passes the end and wraps to the top
            a.key(0x1B)   # the reported case: open the menu, press Up, then Enter. The last rows are the default file manager switch
            time.sleep(.4)   # and Match Across Keyboards, so Up three times reaches Show Hidden Files: a check never presses those
            a.click(*g.mid(mr))
            time.sleep(.6)
            step(0x26, 'Up on a freshly opened menu')
            step(0x26, 'a second Up on a freshly opened menu')
            step(0x26, 'a third Up on a freshly opened menu')
            a.key(13)
            a.until(f'{cw} px: Enter did not pick Show Hidden Files', lambda: a.snap()['hidden'] != before)

    def client_h(s):
        c = s['chrome']
        return (c['client'][3] - c['client'][1]) / c['scale']

    def row_visible(s, r):
        """A menu row is on screen: inside the window and, when the menu scrolls, inside its scrolling column."""
        v = s['menu_scroll']
        inside = r[3] > 0 and r[1] >= 0 and r[1] + r[3] <= client_h(s) + 0.5
        return inside and (v[3] <= 0 or (r[1] >= v[1] - 0.5 and r[1] + r[3] <= v[1] + v[3] + 0.5))

    def view_options_reachable(a, theme):
        """S4: at 360x280 every View options row (Name, Size, Modified, Type, Reversed Order, Show Hidden Files) can be reached with
        the mouse wheel and with the keyboard, and the keyboard's highlighted row stays on screen."""
        set_client(a, 360, 280)
        g = Geo(a)
        bar = bottom_bar(a)
        assert len(bar) == 3, f'bottom bar {bar}'

        def open_menu():
            a.click(*g.mid(bar[2]))
            a.until('View options did not open', lambda: a.snap()['popups'] >= 1)
            time.sleep(.5)
        open_menu()
        s = a.snap()
        assert len(s['menu_rows']) == 6, f"View options should list 6 rows, not {s['menu_rows']}"
        if os.environ.get('DFM_CHROME_PICTURES'):
            shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'view-options-{theme}-360x280.png')
        seen = set()

        def collect():
            s = a.snap()
            seen.update(i for i, r in enumerate(s['menu_rows']) if row_visible(s, r))
        collect()
        a.msg(0x200, 0, a.point(200, 120))
        for _ in range(14):
            a.msg(0x20A, (0x10000 - 120) << 16)
            time.sleep(.12)
            collect()
        time.sleep(.4)
        collect()
        assert 5 in seen, f'Wheel down never showed Show Hidden Files (row 5): rows seen {sorted(seen)}'
        if os.environ.get('DFM_CHROME_PICTURES'):
            a.snap()
            shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'view-options-{theme}-360x280-scrolled.png')
        for _ in range(14):
            a.msg(0x20A, 120 << 16)
            time.sleep(.12)
            collect()
        assert seen == set(range(6)), f'Wheel scrolling reached rows {sorted(seen)} of 0-5: {a.snap()["menu_rows"]}'
        a.key(0x1B)
        a.until('View options did not close', lambda: a.snap()['popups'] == 0)
        open_menu()
        want = 5

        def on_screen():
            s = a.snap()
            return s['menu_index'] == want and row_visible(s, s['menu_row'])

        def step(key, what):
            a.key(key)
            a.until(f'keyboard {what}: highlighted row {want} is not on screen: {a.snap()["menu_index"]} {a.snap()["menu_row"]} in {a.snap()["menu_scroll"]}', on_screen)
        step(0x26, 'Up from the top (to the last row)')
        for n in range(5):
            want -= 1
            step(0x26, f'Up {n + 2}')
        for n in range(7):
            want = (want + 1) % 6
            step(0x28, f'Down {n + 1}')   # passes the end and wraps to the top

    def bottom_bar_inside(a, theme):
        """S5: at 360x280 the whole bottom bar is inside the window, in an empty folder and in a full one."""
        fixture = Path(a.folder)
        set_client(a, 360, 280)
        for folder, what in ((fixture / 'A', 'empty'), (fixture, 'full')):
            a.path(folder)
            a.until(f'{what} folder not shown', lambda: ends_with(a, folder.name) and (a.snap()['listed'] == 0) == (what == 'empty'))
            time.sleep(.4)
            s = a.snap()
            h = client_h(s)
            bar = bottom_bar(a)
            assert len(bar) == 3, f'{what}: bottom bar {bar}'
            for r in bar:
                assert r[1] >= 0 and r[1] + r[3] <= h + 0.5, f'{what} folder at 360x280: bottom-bar button {r} reaches below the {h:.1f} px window'
                assert abs(r[1] + r[3] / 2 - (h - 23)) <= 1.5, f'{what} folder: bottom-bar button {r} is not centred in the 46 px bar of a {h:.1f} px window'
            if what == 'empty' and os.environ.get('DFM_CHROME_PICTURES'):
                shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'empty-{theme}-360x280.png')

    def search_in_header(a, theme):
        """GNOME Files keeps Search in the header at every width: at 360 px it is a header button, not a menu row."""
        set_client(a, 360, 600)
        g = Geo(a)
        hdr = sorted((r for r in g.c['nodrag'] if r[1] < 46 and r[2] > 0), key=lambda r: r[0])
        assert len(hdr) == 4, f'360 px header should hold the sidebar button, path bar, Search and the menu: {hdr}'
        search = hdr[2]
        assert 30 <= search[2] <= 40, f'the third header control is not an icon button: {hdr}'
        btn = g.c['buttons']
        assert all(r[0] + r[2] <= btn[0][0] + 0.5 for r in hdr), f'header controls reach the window buttons {hdr} {btn}'
        a.click(*g.mid(search))
        a.until('Search did not open from the header at 360 px', lambda: a.snap()['search'] == 1)
        a.key(0x1B)
        a.click(*g.mid(hdr[3]))
        a.until('The main menu did not open', lambda: a.snap()['popups'] >= 1)
        time.sleep(.4)
        assert len(a.snap()['menu_rows']) == 6, f"The 360 px main menu should hold Theme, Show Hidden Files, Match Across Keyboards and the default file manager switch only: {a.snap()['menu_rows']}"

    def menu_switch(a, theme):
        """The main menu holds the default file manager switch inside the window, in the sidebar (1280 px) and in the narrow header menu (360 px)."""
        for cw in (1280, 360):
            set_client(a, cw, 700)
            g = Geo(a)
            header = [r for r in g.c['nodrag'] if r[1] < 46 and r[2] > 0]
            a.click(*g.mid(min(header) if cw >= 683 else header[-1]))
            a.until(f'{cw} px: the main menu did not open', lambda: a.snap()['popups'] >= 1)
            time.sleep(.5)
            s = a.snap()
            item = s['dfm_item']
            assert item[2] > 0 and item[3] > 0, f'{cw} px: the default file manager switch is not in the menu: {item}'
            assert item[0] >= 0 and item[0] + item[2] <= cw + 0.5 and item[1] >= 0 and item[1] + item[3] <= client_h(s) + 0.5,                 f'{cw} px: the default file manager switch {item} is outside the {cw} px window'
            if os.environ.get('DFM_CHROME_PICTURES'):
                a.snap()
                shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'menu-switch-{theme}-{cw}.png')
            a.key(0x1B)
            time.sleep(.3)

    def view_menu_modes(a, theme):
        """The grid and list buttons are gone from the header; the view options menu holds them, switches at once and stays open;
        the path bar fills the header between Forward and the actions (wider than the old 480 px cap at 1280 px)."""
        set_client(a, 1280, 800)
        g = Geo(a)
        hdr = sorted((r for r in g.c['nodrag'] if r[1] < 46 and r[0] >= 240 and r[2] > 0), key=lambda r: r[0])
        # Back, Forward, the path bar, Search, view options: no grid and no list button (a 36 px icon button each).
        assert len(hdr) == 5, f'1280 px header should hold Back, Forward, the path bar, Search and view options only: {hdr}'
        path = hdr[2]
        assert path[2] > 480, f'the path bar is only {path[2]:.0f} px wide at 1280 px: {hdr}'
        assert hdr[4][2] > 40, f'the view options button should be wider than an icon button (view icon and chevron): {hdr[4]}'
        gap = g.c['buttons'][0][0] - (hdr[4][0] + hdr[4][2])
        assert gap >= 8, f'view options sit {gap:.0f} px from the window buttons: {hdr}'
        s0 = a.snap()
        assert s0['zoom'] >= 2, f"the check starts in the grid: {s0['zoom']}"
        a.snap()
        spot = ((hdr[4][0] + 4) * g.scale, (hdr[4][1] + 18) * g.scale)   # inside the button, left of its icon
        closed = pixel(a, *spot)
        a.click(*g.mid(hdr[4]))
        a.until('View options did not open', lambda: a.snap()['popups'] >= 1)
        time.sleep(.4)
        a.msg(0x200, 0, a.point(600, 500))   # the pointer leaves the button: only the open menu lights it
        time.sleep(.3)
        a.snap()
        assert not near(pixel(a, *spot), closed, 6), f'the view options button is not lit while its menu is open: {closed}'
        x, y, w, h = a.snap()['view_mode']
        assert w > 40 and h > 20, f'no grid/list toggles in the view options menu: {(x, y, w, h)}'
        if os.environ.get('DFM_CHROME_PICTURES'):
            a.snap()
            shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'view-menu-grid-{theme}-1280.png')
        a.click(*g.mid((x + w / 3, y, w / 3, h)))   # the middle toggle: list
        a.until('The list toggle did not switch to the list', lambda: a.snap()['zoom'] <= 1)
        assert a.snap()['popups'] >= 1, 'the menu closed when the list toggle was clicked'
        time.sleep(.4)
        if os.environ.get('DFM_CHROME_PICTURES'):
            a.snap()
            shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'view-menu-list-{theme}-1280.png')
        a.click(*g.mid((x, y, w / 3, h)))   # the left toggle: grid
        a.until('The grid toggle did not switch back to the grid', lambda: a.snap()['zoom'] >= 2)
        assert a.snap()['popups'] >= 1, 'the menu closed when the grid toggle was clicked'
        a.key(0x1B)
        a.until('View options did not close', lambda: a.snap()['popups'] == 0)
        a.key(50, (17,)); a.until('Ctrl+2 did not switch to the list', lambda: a.snap()['zoom'] <= 1)
        a.key(49, (17,)); a.until('Ctrl+1 did not switch to the grid', lambda: a.snap()['zoom'] >= 2)
        a.click(*g.mid(hdr[4]))   # the keyboard: Down highlights a sort row, Tab and Right move to the list toggle, Enter picks only that toggle
        a.until('View options did not open', lambda: a.snap()['popups'] >= 1)
        time.sleep(.4)
        for k in (0x28, 9, 0x27, 13):
            a.key(k)
        a.until('Enter on the list toggle did not switch to the list', lambda: a.snap()['zoom'] <= 1)
        time.sleep(.3)
        assert a.snap()['popups'] >= 1, 'Enter on the list toggle also picked the highlighted sort row and closed the menu'
        a.key(0x1B)
        a.key(49, (17,)); a.until('Ctrl+1 did not switch to the grid', lambda: a.snap()['zoom'] >= 2)
        set_client(a, 600, 700)
        if os.environ.get('DFM_CHROME_PICTURES'):
            a.click(*g.mid(bottom_bar(a)[2]))
            time.sleep(.5)
            a.snap()
            shutil.copyfile(str(a.base) + '.png', Path(os.environ['DFM_CHROME_PICTURES']) / f'view-menu-narrow-{theme}-600.png')
            a.key(0x1B)

    check('1-hit-testing', hit_testing)
    check('2-drag', drag)
    check('3-double-click', double_click)
    check('4-buttons', buttons)
    check('5-close-button', close_button, ends=True)
    check('6-colours-dark', colours('dark'), 'dark')
    check('6-colours-light', colours('light'), 'light')
    check('7-dimming', dimming, 'dark')
    check('8-menu-click-through', menu_click_through, 'dark')
    check('9-icon', icon)
    check('10-maximized-picture', maximized_picture)
    check('11-widths-dark', widths('dark'), 'dark')
    check('11-widths-light', widths('light'), 'light')
    check('12-short-menu', short_menu, 'dark')
    for theme in ('dark', 'light'):
        check(f'13-menu-follows-keyboard-{theme}', lambda a, t=theme: menu_follows_keyboard(a, t), theme)
        check(f'14-bottom-bar-history-{theme}', lambda a, t=theme: bottom_bar_history(a, t), theme)
        check(f'15-bottom-bar-history-copying-{theme}', lambda a, t=theme: bottom_bar_copying(a, t), theme)
        check(f'16-view-options-reachable-{theme}', lambda a, t=theme: view_options_reachable(a, t), theme)
        check(f'17-bottom-bar-inside-{theme}', lambda a, t=theme: bottom_bar_inside(a, t), theme)
        check(f'18-search-in-header-{theme}', lambda a, t=theme: search_in_header(a, t), theme)
        check(f'19-menu-switch-{theme}', lambda a, t=theme: menu_switch(a, t), theme)
        check(f'20-view-menu-modes-{theme}', lambda a, t=theme: view_menu_modes(a, t), theme)
    (WORK / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'tmp').mkdir(exist_ok=True)
    (WORK / 'appdata').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'), DFM_TEST_COPY_DELAY_MS='300')
    if '--inside' in sys.argv:
        return inside()
    gaming = Path(r'C:\Users\Omid\.agents\tools\gaming.py')
    code = subprocess.run([sys.executable, str(gaming), 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable, str(gaming), 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif code:
        return code
    (WORK / 'fixture').mkdir(exist_ok=True)
    (COPY / 'source').mkdir(parents=True, exist_ok=True)
    big = COPY / 'source' / 'a.bin'
    if not big.exists() or big.stat().st_size != 128 << 20:
        with big.open('wb') as f:
            for _ in range(128):
                f.write(b'A' * (1 << 20))
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
